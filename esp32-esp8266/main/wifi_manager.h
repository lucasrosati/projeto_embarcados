// Wi-Fi STA: conexão ao AP autorizado, reconexão e leitura do canal atual (spec §5).
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Inicia netif, STA e conexão. Liga o sniffer quando recebe IP.
void wifi_manager_start(void);

// Canal e RSSI do AP associado. Devolve false se não está associado.
bool wifi_manager_ap_info(uint8_t *channel, int8_t *rssi);

// Reconexões bem-sucedidas depois da primeira conexão.
uint32_t wifi_manager_reconnects(void);
