// Parâmetros do firmware. Os ajustáveis vêm do menuconfig (menu "IDS Wi-Fi"); pinos seguem schematics/montagem.md.
#pragma once

#include "sdkconfig.h"

#define IDS_DEVICE_ID CONFIG_IDS_DEVICE_ID

// Detecção (spec §6): hipóteses de laboratório, ajustáveis no menuconfig.
#define IDS_WINDOW_S CONFIG_IDS_WINDOW_S
#define IDS_DEAUTH_THRESHOLD CONFIG_IDS_DEAUTH_THRESHOLD
#define IDS_DISASSOC_THRESHOLD CONFIG_IDS_DISASSOC_THRESHOLD
#define IDS_COOLDOWN_S CONFIG_IDS_COOLDOWN_S
#define IDS_MAX_BSSIDS 16

// Teste sintético (spec §11, teste E). 0 desliga a injeção periódica.
#define IDS_SYNTHETIC_PERIOD_S CONFIG_IDS_SYNTHETIC_PERIOD_S

// Filas e periodicidade.
#define IDS_CAPTURE_QUEUE_LEN 128
#define IDS_ALERT_QUEUE_LEN 16
#define IDS_STATUS_PERIOD_S 30
#define IDS_ALERT_DISPLAY_S 10

// Hardware (ESP32 38 pinos, ver schematics/montagem.md).
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22
#define PIN_LED_ALERT 23   // vermelho
#define PIN_LED_STATUS 19  // verde
#define PIN_BUTTON_BOOT 0  // botão BOOT da placa
