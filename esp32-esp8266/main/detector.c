#include "detector.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "config.h"
#include "ids_types.h"
#include "synthetic.h"
#include "wifi_manager.h"
#include "wifi_sniffer.h"

static const char *TAG = "detector";

#define WINDOW_MS ((uint32_t)IDS_WINDOW_S * 1000u)
#define COOLDOWN_MS ((uint32_t)IDS_COOLDOWN_S * 1000u)

typedef struct {
    uint32_t win_start_ms;  // janela de cada BSSID começa no primeiro frame dela
    uint16_t count;
    bool fired_this_window;
    bool alerted_once;
    uint32_t last_alert_ms;
} rule_state_t;

typedef struct {
    bool used;
    bool synthetic;
    uint8_t bssid[6];
    uint32_t last_seen_ms;
    rule_state_t rules[RULE_COUNT];
} bssid_entry_t;

static const uint16_t k_threshold[RULE_COUNT] = {IDS_DEAUTH_THRESHOLD, IDS_DISASSOC_THRESHOLD};
static const char *const k_rule_name[RULE_COUNT] = {"DEAUTH_BURST", "DISASSOC_BURST"};

// Estado privado do TaskDetector: só esta task lê e escreve, então não precisa de mutex.
static bssid_entry_t s_table[IDS_MAX_BSSIDS];
static uint32_t s_event_seq;
static uint32_t s_alerts_suppressed;
static uint32_t s_alert_queue_drops;
static uint8_t s_channel;

static bssid_entry_t *find_or_add(const uint8_t bssid[6], bool synthetic)
{
    bssid_entry_t *free_slot = NULL;
    bssid_entry_t *oldest = &s_table[0];
    for (int i = 0; i < IDS_MAX_BSSIDS; i++) {
        bssid_entry_t *e = &s_table[i];
        if (e->used && e->synthetic == synthetic && memcmp(e->bssid, bssid, 6) == 0) {
            return e;
        }
        if (!e->used && free_slot == NULL) {
            free_slot = e;
        }
        if ((int32_t)(e->last_seen_ms - oldest->last_seen_ms) < 0) {
            oldest = e;
        }
    }
    bssid_entry_t *e = free_slot ? free_slot : oldest;  // tabela cheia: reaproveita o mais antigo
    memset(e, 0, sizeof(*e));
    e->used = true;
    e->synthetic = synthetic;
    memcpy(e->bssid, bssid, 6);
    return e;
}

static void emit_alert(rule_id_t rule, const bssid_entry_t *e, uint16_t count, uint32_t now_ms)
{
    alert_t a = {
        .seq = ++s_event_seq,
        .uptime_ms = now_ms,
        .channel = s_channel,
        .rule = rule,
        .count = count,
        .window_s = IDS_WINDOW_S,
        .synthetic = e->synthetic,
    };
    memcpy(a.bssid, e->bssid, 6);
    if (xQueueSend(g_alert_queue, &a, 0) != pdTRUE) {
        s_alert_queue_drops++;
        ESP_LOGE(TAG, "alertQueue cheia, alerta %s descartado", k_rule_name[rule]);
        return;
    }
    xTaskNotify(g_telemetry_task, NOTIFY_ALERT, eSetBits);
}

static void check_burst(rule_id_t rule, const frame_meta_t *m)
{
    bssid_entry_t *e = find_or_add(m->bssid, m->synthetic);
    e->last_seen_ms = m->t_ms;
    rule_state_t *r = &e->rules[rule];

    if (r->count == 0 || (uint32_t)(m->t_ms - r->win_start_ms) >= WINDOW_MS) {
        r->win_start_ms = m->t_ms;
        r->count = 0;
        r->fired_this_window = false;
    }
    if (r->count < UINT16_MAX) {
        r->count++;
    }
    if (r->fired_this_window || r->count < k_threshold[rule]) {
        return;
    }
    r->fired_this_window = true;
    if (r->alerted_once && (uint32_t)(m->t_ms - r->last_alert_ms) < COOLDOWN_MS) {
        s_alerts_suppressed++;
        ESP_LOGI(TAG, "%s suprimido pelo cooldown de %d s", k_rule_name[rule], IDS_COOLDOWN_S);
        return;
    }
    r->alerted_once = true;
    r->last_alert_ms = m->t_ms;
    emit_alert(rule, e, r->count, m->t_ms);
}

static void detector_task(void *arg)
{
    uint32_t beacon = 0, probe_req = 0, deauth = 0, disassoc = 0, seq = 0;
    TickType_t window_end = xTaskGetTickCount() + pdMS_TO_TICKS(WINDOW_MS);

    for (;;) {
        int32_t left = (int32_t)(window_end - xTaskGetTickCount());
        if (left > 0) {
            frame_meta_t m;
            if (xQueueReceive(g_capture_queue, &m, (TickType_t)left) != pdTRUE) {
                continue;
            }
            if (!m.synthetic) {  // estatísticas contam só o que veio do rádio
                switch (m.subtype) {
                case SUB_BEACON: beacon++; break;
                case SUB_PROBE_REQ: probe_req++; break;
                case SUB_DEAUTH: deauth++; break;
                case SUB_DISASSOC: disassoc++; break;
                }
            }
            if (m.subtype == SUB_DEAUTH) {
                check_burst(RULE_DEAUTH_BURST, &m);
            } else if (m.subtype == SUB_DISASSOC) {
                check_burst(RULE_DISASSOC_BURST, &m);
            }
            continue;
        }

        // Fim da janela: monta o retrato e avisa o TaskTelemetry.
        stats_snapshot_t s = {
            .seq = ++seq,
            .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000),
            .window_s = IDS_WINDOW_S,
            .beacon = beacon,
            .probe_req = probe_req,
            .deauth = deauth,
            .disassoc = disassoc,
            .capture_queue_drops = wifi_sniffer_drops(),
            .alert_queue_drops = s_alert_queue_drops,
            .alerts_suppressed = s_alerts_suppressed,
            .wifi_reconnects = wifi_manager_reconnects(),
            .synthetic_injected = synthetic_injected(),
        };
        s.associated = wifi_manager_ap_info(&s.channel, &s.rssi);
        if (s.associated) {
            s_channel = s.channel;  // canal real do AP, usado nos alertas (spec §5)
        }
        xQueueOverwrite(g_stats_mailbox, &s);
        xTaskNotify(g_telemetry_task, NOTIFY_STATS, eSetBits);

        beacon = probe_req = deauth = disassoc = 0;
        window_end += pdMS_TO_TICKS(WINDOW_MS);
    }
}

void detector_start(void)
{
    xTaskCreate(detector_task, "detector", 4096, NULL, 5, NULL);
}
