// PoC de coexistência: Wi-Fi STA conectado + promiscuous ligado, contadores na serial a cada 10 s.
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WINDOW_MS 10000
#define CAPTURE_QUEUE_LEN 128
#define MGMT_HDR_LEN 24

// Subtipos de frame de gerenciamento 802.11 (spec §5).
enum { SUB_PROBE_REQ = 4, SUB_BEACON = 8, SUB_DISASSOC = 10, SUB_DEAUTH = 12 };

static const char *TAG = "ids";
static QueueHandle_t capture_queue;
static atomic_uint queue_drops;
static atomic_uint wifi_disconnects;

// Roda na task do driver Wi-Fi (não é ISR): só extrai o subtipo e enfileira sem bloquear.
static void sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = buf;
    if (type != WIFI_PKT_MGMT || pkt->rx_ctrl.sig_len < MGMT_HDR_LEN) {
        return;
    }
    uint8_t fc0 = pkt->payload[0]; // frame control: bits 2-3 tipo, bits 4-7 subtipo
    if ((fc0 & 0x0C) != 0) {
        return;
    }
    uint8_t subtype = fc0 >> 4;
    switch (subtype) {
    case SUB_PROBE_REQ:
    case SUB_BEACON:
    case SUB_DISASSOC:
    case SUB_DEAUTH:
        if (xQueueSend(capture_queue, &subtype, 0) != pdTRUE) {
            atomic_fetch_add(&queue_drops, 1);
        }
        break;
    default:
        break;
    }
}

static void counter_task(void *arg)
{
    uint32_t beacon = 0, probe_req = 0, deauth = 0, disassoc = 0;
    TickType_t window_end = xTaskGetTickCount() + pdMS_TO_TICKS(WINDOW_MS);

    for (;;) {
        int32_t left = (int32_t)(window_end - xTaskGetTickCount());
        if (left > 0) {
            uint8_t subtype;
            if (xQueueReceive(capture_queue, &subtype, left) == pdTRUE) {
                switch (subtype) {
                case SUB_BEACON: beacon++; break;
                case SUB_PROBE_REQ: probe_req++; break;
                case SUB_DEAUTH: deauth++; break;
                case SUB_DISASSOC: disassoc++; break;
                }
            }
            continue;
        }

        // Canal lido do AP associado: o sniffer acompanha o canal do STA (spec §2).
        wifi_ap_record_t ap;
        bool assoc = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        ESP_LOGI(TAG,
                 "uptime_ms=%lld assoc=%d ch=%d rssi=%d beacon=%lu probe_req=%lu deauth=%lu disassoc=%lu "
                 "queue_drops=%u wifi_disconnects=%u",
                 (long long)(esp_timer_get_time() / 1000), assoc, assoc ? ap.primary : 0, assoc ? ap.rssi : 0,
                 (unsigned long)beacon, (unsigned long)probe_req, (unsigned long)deauth, (unsigned long)disassoc,
                 atomic_load(&queue_drops), atomic_load(&wifi_disconnects));

        beacon = probe_req = deauth = disassoc = 0;
        window_end += pdMS_TO_TICKS(WINDOW_MS);
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_fetch_add(&wifi_disconnects, 1);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "conectado, ip=" IPSTR, IP2STR(&event->ip_info.ip));

        // Liga o sniffer depois de associar e nunca mais desliga (spec §2).
        const wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
        ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
        ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(sniffer_cb));
        ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    }
}

void app_main(void)
{
    if (CONFIG_IDS_WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "SSID vazio: rode 'idf.py menuconfig' e preencha o menu 'IDS Wi-Fi'");
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    capture_queue = xQueueCreate(CAPTURE_QUEUE_LEN, sizeof(uint8_t));
    configASSERT(capture_queue);

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {.ssid = CONFIG_IDS_WIFI_SSID, .password = CONFIG_IDS_WIFI_PASSWORD},
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    // Sem modem sleep: com economia de energia o rádio dorme entre beacons e o sniffer perde frames.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(counter_task, "counter", 4096, NULL, 5, NULL);
}
