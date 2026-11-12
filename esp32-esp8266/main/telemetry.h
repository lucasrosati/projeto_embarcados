// TaskTelemetry (spec §4 e §7): acorda por Task Notification (alerta imediato ou janela fechada) e a cada
// 30 s para o status. Serializa os payloads JSON do contrato (schema_version 2).
// Parte 1: os JSON saem na serial (só depuração). Parte 2: os mesmos JSON vão por esp_mqtt_client_enqueue().
#pragma once

void telemetry_start(void);
