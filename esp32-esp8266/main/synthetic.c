#include "synthetic.h"

#include <stdatomic.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "config.h"
#include "ids_types.h"

static const char *TAG = "synthetic";

// BSSID fictício "localmente administrado" (bit 0x02): não pertence a nenhum fabricante real.
static const uint8_t k_fake_bssid[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static atomic_uint s_injected;

static void inject_burst(uint8_t subtype, uint16_t n)
{
    frame_meta_t m = {.subtype = subtype, .rssi = 0, .synthetic = true};
    for (int i = 0; i < 6; i++) {
        m.bssid[i] = k_fake_bssid[i];
    }
    uint16_t sent = 0;
    for (uint16_t i = 0; i < n; i++) {
        m.t_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (xQueueSend(g_capture_queue, &m, 0) == pdTRUE) {
            sent++;
        }
    }
    atomic_fetch_add(&s_injected, sent);
    ESP_LOGW(TAG, "rajada sintetica: %u/%u frames %s injetados (BSSID 02:00:00:00:00:01)", sent, n,
             subtype == SUB_DEAUTH ? "deauth" : "disassoc");
}

static void synthetic_task(void *arg)
{
    const gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BUTTON_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    int low_samples = 0;
    bool pressed = false;
    bool next_is_deauth = true;
    TickType_t next_auto = xTaskGetTickCount() + pdMS_TO_TICKS(IDS_SYNTHETIC_PERIOD_S * 1000);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));

        // Botão com pull-up: pressionado = nível baixo. Exige 3 leituras seguidas (~60 ms) contra ruído.
        if (gpio_get_level(PIN_BUTTON_BOOT) == 0) {
            if (++low_samples == 3 && !pressed) {
                pressed = true;
                inject_burst(SUB_DEAUTH, IDS_DEAUTH_THRESHOLD + 5);
            }
        } else {
            low_samples = 0;
            pressed = false;
        }

        if (IDS_SYNTHETIC_PERIOD_S > 0 && (int32_t)(xTaskGetTickCount() - next_auto) >= 0) {
            if (next_is_deauth) {
                inject_burst(SUB_DEAUTH, IDS_DEAUTH_THRESHOLD + 5);
            } else {
                inject_burst(SUB_DISASSOC, IDS_DISASSOC_THRESHOLD + 5);
            }
            next_is_deauth = !next_is_deauth;
            next_auto += pdMS_TO_TICKS(IDS_SYNTHETIC_PERIOD_S * 1000);
        }
    }
}

void synthetic_start(void)
{
    xTaskCreate(synthetic_task, "synthetic", 3072, NULL, 3, NULL);
    if (IDS_SYNTHETIC_PERIOD_S > 0) {
        ESP_LOGW(TAG, "injecao periodica ligada: a cada %d s", IDS_SYNTHETIC_PERIOD_S);
    }
}

uint32_t synthetic_injected(void)
{
    return atomic_load(&s_injected);
}
