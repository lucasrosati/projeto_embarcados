// Modo promiscuous (spec §2 e §4): ligado com o STA associado e nunca desligado para publicar.
#pragma once

#include <stdint.h>

#include "esp_err.h"

// Liga o sniffer (só frames de gerenciamento). Chamar depois de obter IP; chamadas repetidas são ignoradas.
esp_err_t wifi_sniffer_start(void);

// Frames descartados porque a captureQueue estava cheia (acumulado desde o boot).
uint32_t wifi_sniffer_drops(void);
