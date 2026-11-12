#include "local_ui.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_timer.h"

#include "config.h"
#include "ids_mqtt.h"
#include "lcd1602.h"

// Até quando (uptime em ms) o alerta fica visível. Escrito pelo TaskTelemetry, lido pela task dos LEDs.
static atomic_uint_least32_t s_alert_until_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool alert_active(void)
{
    return (int32_t)(atomic_load(&s_alert_until_ms) - now_ms()) > 0;
}

// LEDs: verde aceso = Wi-Fi conectado, sniffer ligado e (se configurado) broker MQTT conectado;
// piscando = falta alguma dessas conexões.
// Vermelho pisca rápido enquanto há alerta recente.
static void led_task(void *arg)
{
    uint32_t tick = 0;
    for (;;) {
        EventBits_t bits = xEventGroupGetBits(g_net_events);
        bool online = (bits & WIFI_CONNECTED_BIT) && (bits & SNIFFER_ACTIVE_BIT) &&
                      (!ids_mqtt_enabled() || (bits & MQTT_CONNECTED_BIT));
        gpio_set_level(PIN_LED_STATUS, online ? 1 : ((tick / 5) % 2));
        gpio_set_level(PIN_LED_ALERT, alert_active() ? (tick % 2) : 0);
        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void local_ui_init(void)
{
    const gpio_config_t leds = {
        .pin_bit_mask = (1ULL << PIN_LED_ALERT) | (1ULL << PIN_LED_STATUS),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&leds));
    gpio_set_level(PIN_LED_ALERT, 0);
    gpio_set_level(PIN_LED_STATUS, 0);

    if (lcd1602_init(PIN_I2C_SDA, PIN_I2C_SCL)) {
        lcd1602_write_line(0, "ESP32 Wi-Fi IDS");
        lcd1602_write_line(1, "conectando...");
    }
    xTaskCreate(led_task, "leds", 2048, NULL, 2, NULL);
}

void local_ui_show_stats(const stats_snapshot_t *s)
{
    if (alert_active()) {
        return;  // mantém a tela de alerta até expirar
    }
    char l1[32], l2[32];  // o LCD corta em 16 colunas
    if (s->associated) {
        const char *net = "ON";  // ON = Wi-Fi ok (MQTT desligado); MQ = broker ok; NB = sem broker
        if (ids_mqtt_enabled()) {
            net = (xEventGroupGetBits(g_net_events) & MQTT_CONNECTED_BIT) ? "MQ" : "NB";
        }
        snprintf(l1, sizeof(l1), "CH%02u %4ddBm  %s", s->channel, s->rssi, net);
        snprintf(l2, sizeof(l2), "B%" PRIu32 " P%" PRIu32 " D%" PRIu32 " X%" PRIu32, s->beacon, s->probe_req,
                 s->deauth, s->disassoc);
    } else {
        snprintf(l1, sizeof(l1), "Wi-Fi: buscando");
        snprintf(l2, sizeof(l2), "IDS aguardando");
    }
    lcd1602_write_line(0, l1);
    lcd1602_write_line(1, l2);
}

void local_ui_show_alert(const alert_t *a)
{
    atomic_store(&s_alert_until_ms, now_ms() + IDS_ALERT_DISPLAY_S * 1000u);
    char l1[32], l2[32];  // o LCD corta em 16 colunas
    snprintf(l1, sizeof(l1), "ALERTA %s", a->rule == RULE_DEAUTH_BURST ? "DEAUTH" : "DISASSOC");
    snprintf(l2, sizeof(l2), "n=%u ..%02x:%02x %s", a->count, a->bssid[4], a->bssid[5],
             a->synthetic ? "SIM" : "LIVE");
    lcd1602_write_line(0, l1);
    lcd1602_write_line(1, l2);
}
