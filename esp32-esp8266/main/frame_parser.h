// Leitura do cabeçalho 802.11 de gerenciamento (spec §5): só campos de cabeçalho, nunca o corpo do frame.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Devolve true se o frame é de gerenciamento, tem cabeçalho completo e é um dos subtipos monitorados.
// Preenche o subtipo e o BSSID (addr3).
bool frame_parse_mgmt(const uint8_t *frame, uint16_t len, uint8_t *subtype, uint8_t bssid[6]);
