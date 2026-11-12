// Saídas locais: LCD 16x2 e LEDs (verde = conectado com sniffer ativo, vermelho = alerta).
#pragma once

#include "ids_types.h"

// Configura LEDs, inicia o LCD e a task que controla os LEDs.
void local_ui_init(void);

// Chamadas só pelo TaskTelemetry (dono do LCD).
void local_ui_show_stats(const stats_snapshot_t *s);
void local_ui_show_alert(const alert_t *a);
