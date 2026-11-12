#include "telemetry.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "config.h"
#include "ids_types.h"
#include "local_ui.h"
#include "wifi_manager.h"

static const char *TAG = "telemetry";

static char s_boot_id[16];  // identifica este boot; compõe o event_id para deduplicação no back-end

static const char *rule_name(rule_id_t r)
{
    return r == RULE_DEAUTH_BURST ? "DEAUTH_BURST" : "DISASSOC_BURST";
}

static const char *rule_severity(rule_id_t r)
{
    return r == RULE_DEAUTH_BURST ? "high" : "medium";
}

static void handle_stats(const stats_snapshot_t *s)
{
    char json[448];
    snprintf(json, sizeof(json),
             "{\"schema_version\":2,\"type\":\"stats\",\"device_id\":\"%s\",\"boot_id\":\"%s\",\"seq\":%" PRIu32
             ",\"uptime_ms\":%" PRIu32 ",\"channel\":%u,\"rssi\":%d,\"window_s\":%u,"
             "\"counts\":{\"beacon\":%" PRIu32 ",\"probe_req\":%" PRIu32 ",\"deauth\":%" PRIu32
             ",\"disassoc\":%" PRIu32 "},\"capture_queue_drops\":%" PRIu32 ",\"alert_queue_drops\":%" PRIu32
             ",\"alerts_suppressed\":%" PRIu32 ",\"wifi_reconnects\":%" PRIu32 ",\"synthetic_injected\":%" PRIu32
             ",\"source\":\"live\"}",
             IDS_DEVICE_ID, s_boot_id, s->seq, s->uptime_ms, s->channel, s->rssi, s->window_s, s->beacon,
             s->probe_req, s->deauth, s->disassoc, s->capture_queue_drops, s->alert_queue_drops,
             s->alerts_suppressed, s->wifi_reconnects, s->synthetic_injected);
    ESP_LOGI(TAG, "stats %s", json);  // parte 2: tópico ids/<id>/stats, QoS 0
    local_ui_show_stats(s);
}

static void handle_alert(const alert_t *a)
{
    char json[384];
    snprintf(json, sizeof(json),
             "{\"schema_version\":2,\"type\":\"alert\",\"device_id\":\"%s\",\"event_id\":\"%s-evt-%03" PRIu32
             "\",\"boot_id\":\"%s\",\"uptime_ms\":%" PRIu32 ",\"channel\":%u,\"rule\":\"%s\",\"severity\":\"%s\","
             "\"bssid\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"count\":%u,\"window_s\":%u,\"source\":\"%s\"}",
             IDS_DEVICE_ID, s_boot_id, a->seq, s_boot_id, a->uptime_ms, a->channel, rule_name(a->rule),
             rule_severity(a->rule), a->bssid[0], a->bssid[1], a->bssid[2], a->bssid[3], a->bssid[4], a->bssid[5],
             a->count, a->window_s, a->synthetic ? "synthetic" : "live");
    ESP_LOGW(TAG, "alert %s", json);  // parte 2: tópico ids/<id>/alerts, QoS 1
    local_ui_show_alert(a);
}

static void publish_status(void)
{
    EventBits_t bits = xEventGroupGetBits(g_net_events);
    uint8_t ch;
    int8_t rssi;
    wifi_manager_ap_info(&ch, &rssi);
    char json[256];
    snprintf(json, sizeof(json),
             "{\"schema_version\":2,\"type\":\"status\",\"device_id\":\"%s\",\"boot_id\":\"%s\",\"state\":\"online\","
             "\"uptime_ms\":%" PRIu32 ",\"wifi_connected\":%s,\"sniffer_active\":%s,\"channel\":%u,\"rssi\":%d}",
             IDS_DEVICE_ID, s_boot_id, (uint32_t)(esp_timer_get_time() / 1000),
             (bits & WIFI_CONNECTED_BIT) ? "true" : "false", (bits & SNIFFER_ACTIVE_BIT) ? "true" : "false", ch,
             rssi);
    ESP_LOGI(TAG, "status %s", json);  // parte 2: tópico ids/<id>/status, QoS 1, retido, Last Will "offline"
}

static void telemetry_task(void *arg)
{
    const TickType_t status_period = pdMS_TO_TICKS(IDS_STATUS_PERIOD_S * 1000);
    TickType_t next_status = xTaskGetTickCount();

    for (;;) {
        int32_t until_status = (int32_t)(next_status - xTaskGetTickCount());
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, until_status > 0 ? (TickType_t)until_status : 0);

        if (bits & NOTIFY_ALERT) {  // alertas primeiro: têm prioridade sobre estatísticas (spec §7)
            alert_t a;
            while (xQueueReceive(g_alert_queue, &a, 0) == pdTRUE) {
                handle_alert(&a);
            }
        }
        if (bits & NOTIFY_STATS) {
            stats_snapshot_t s;
            if (xQueuePeek(g_stats_mailbox, &s, 0) == pdTRUE) {
                handle_stats(&s);
            }
        }
        if ((int32_t)(xTaskGetTickCount() - next_status) >= 0) {
            publish_status();
            next_status += status_period;
        }
    }
}

void telemetry_start(void)
{
    snprintf(s_boot_id, sizeof(s_boot_id), "%08" PRIx32, esp_random());
    ESP_LOGI(TAG, "device_id=%s boot_id=%s", IDS_DEVICE_ID, s_boot_id);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, &g_telemetry_task);
}
