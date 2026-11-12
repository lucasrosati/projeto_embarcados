// ESP32 Wi-Fi IDS — parte 1: STA + promiscuous, detector com filas FreeRTOS, LCD/LEDs e JSON na serial.
// Fluxo (spec §4): sniffer_cb -> captureQueue -> TaskDetector -> alertQueue + Task Notification -> TaskTelemetry.
#include "esp_log.h"
#include "nvs_flash.h"

#include "config.h"
#include "detector.h"
#include "ids_types.h"
#include "local_ui.h"
#include "synthetic.h"
#include "telemetry.h"
#include "wifi_manager.h"

static const char *TAG = "ids";

QueueHandle_t g_capture_queue;
QueueHandle_t g_alert_queue;
QueueHandle_t g_stats_mailbox;
EventGroupHandle_t g_net_events;
TaskHandle_t g_telemetry_task;

void app_main(void)
{
    if (CONFIG_IDS_WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "SSID vazio: rode o menuconfig e preencha o menu 'IDS Wi-Fi'");
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    g_net_events = xEventGroupCreate();
    g_capture_queue = xQueueCreate(IDS_CAPTURE_QUEUE_LEN, sizeof(frame_meta_t));
    g_alert_queue = xQueueCreate(IDS_ALERT_QUEUE_LEN, sizeof(alert_t));
    g_stats_mailbox = xQueueCreate(1, sizeof(stats_snapshot_t));
    configASSERT(g_net_events && g_capture_queue && g_alert_queue && g_stats_mailbox);

    ESP_LOGI(TAG, "regras: DEAUTH_BURST>=%d, DISASSOC_BURST>=%d em %d s, cooldown %d s", IDS_DEAUTH_THRESHOLD,
             IDS_DISASSOC_THRESHOLD, IDS_WINDOW_S, IDS_COOLDOWN_S);

    local_ui_init();     // LEDs e LCD
    telemetry_start();   // antes do detector, que notifica esta task
    detector_start();
    synthetic_start();   // botão BOOT e injeção periódica opcional
    wifi_manager_start();  // conecta; o sniffer liga ao receber IP
}
