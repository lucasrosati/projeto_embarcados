// TaskTelemetry (spec §4 e §7): acorda por Task Notification (alerta imediato ou janela fechada) e a cada
// 30 s para o status. Serializa os payloads JSON do contrato (schema_version 2).
// Os JSON vão para o broker por esp_mqtt_client_enqueue() (ids_mqtt) e também para a serial, só para depuração.
#pragma once

void telemetry_start(void);

// Identificador deste boot (8 hex). Válido depois de telemetry_start().
const char *telemetry_boot_id(void);
