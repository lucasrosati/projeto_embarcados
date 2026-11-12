#include "wifi_manager.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "ids_types.h"
#include "sdkconfig.h"
#include "wifi_sniffer.h"

static const char *TAG = "wifi";
static atomic_uint s_reconnects;
static bool s_connected_once;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        // reason 201 = AP não encontrado; 15/204 = falha no handshake (senha errada ou sinal ruim).
        ESP_LOGW(TAG, "desconectado, reason=%d rssi=%d; tentando de novo", ev->reason, ev->rssi);
        xEventGroupClearBits(g_net_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        uint8_t ch = 0;
        int8_t rssi = 0;
        wifi_manager_ap_info(&ch, &rssi);
        ESP_LOGI(TAG, "conectado, ip=" IPSTR " canal=%u rssi=%d", IP2STR(&ev->ip_info.ip), ch, rssi);
        if (s_connected_once) {
            atomic_fetch_add(&s_reconnects, 1);
        }
        s_connected_once = true;
        xEventGroupSetBits(g_net_events, WIFI_CONNECTED_BIT);
        // Sniffer só depois de associar, e nunca mais desligado (spec §2).
        wifi_sniffer_start();
    }
}

void wifi_manager_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, CONFIG_IDS_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, CONFIG_IDS_WIFI_PASSWORD, sizeof(cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    // Sem modem sleep: com economia de energia o rádio dorme entre beacons e o sniffer perde frames.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "procurando o AP \"%s\"", CONFIG_IDS_WIFI_SSID);
}

bool wifi_manager_ap_info(uint8_t *channel, int8_t *rssi)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        *channel = 0;
        *rssi = 0;
        return false;
    }
    *channel = ap.primary;
    *rssi = ap.rssi;
    return true;
}

uint32_t wifi_manager_reconnects(void)
{
    return atomic_load(&s_reconnects);
}
