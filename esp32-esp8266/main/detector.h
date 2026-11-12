// TaskDetector (spec §4 e §6): dono único dos contadores. Consome a captureQueue, fecha janelas de
// estatística, aplica as regras de rajada por BSSID com cooldown e entrega alertas ao TaskTelemetry.
#pragma once

void detector_start(void);
