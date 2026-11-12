// Teste sintético de detecção (spec §11, teste E): injeta metadados falsos direto na captureQueue.
// Nada é transmitido pelo rádio. Os alertas gerados saem com source=synthetic.
#pragma once

#include <stdint.h>

// Botão BOOT (GPIO0) injeta uma rajada de deauth; opcionalmente injeta rajadas periódicas (menuconfig).
void synthetic_start(void);

// Frames sintéticos injetados desde o boot.
uint32_t synthetic_injected(void);
