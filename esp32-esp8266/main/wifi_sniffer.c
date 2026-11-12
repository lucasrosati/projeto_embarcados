#include "wifi_sniffer.h"

#include <stdatomic.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "frame_parser.h"
#include "ids_types.h"

static const char *TAG = "sniffer";
static atomic_uint s_drops;
static bool s_started;

// Roda na task do driver Wi-Fi (não é ISR): só extrai metadados e enfileira sem bloquear.
// Nada de log, JSON ou MQTT aqui (regra 4 do CLAUDE.md).
static void sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT) {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = buf;
    frame_meta_t meta;
    if (!frame_parse_mgmt(pkt->payload, pkt->rx_ctrl.sig_len, &meta.subtype, meta.bssid)) {
        return;
    }
    meta.rssi = pkt->rx_ctrl.rssi;
    meta.synthetic = false;
    meta.t_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (xQueueSend(g_capture_queue, &meta, 0) != pdTRUE) {
        atomic_fetch_add(&s_drops, 1);
    }
}

esp_err_t wifi_sniffer_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    const wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(sniffer_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    s_started = true;
    xEventGroupSetBits(g_net_events, SNIFFER_ACTIVE_BIT);
    ESP_LOGI(TAG, "promiscuous ligado (gerenciamento), segue o canal do AP");
    return ESP_OK;
}

uint32_t wifi_sniffer_drops(void)
{
    return atomic_load(&s_drops);
}
