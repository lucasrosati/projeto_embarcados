// Cliente MQTT do próprio ESP32 (spec §7): publica stats, alerts e status direto pelo Wi-Fi STA.
// Não existe ponte serial->MQTT.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { MQTT_TOPIC_STATS, MQTT_TOPIC_ALERTS, MQTT_TOPIC_STATUS } ids_mqtt_topic_t;

// Inicia o cliente (uma vez). Chamado quando o STA recebe IP. Sem URI no menuconfig, o MQTT fica desligado.
void ids_mqtt_start(void);

bool ids_mqtt_enabled(void);

// Enfileira sem bloquear (esp_mqtt_client_enqueue). Política (spec §7):
// - stats (QoS 0): só com o broker conectado; senão descarta e conta falha;
// - alerts (QoS 1): vão para o outbox mesmo desconectado e saem na reconexão (outbox limitado);
// - status (QoS 1, retido): só com o broker conectado (o Last Will cobre o "offline").
// Devolve true se a mensagem entrou no outbox. Isso não garante entrega.
bool ids_mqtt_publish(ids_mqtt_topic_t topic, const char *json);

// Mensagens que não puderam ser enfileiradas ou foram descartadas do outbox (acumulado).
uint32_t ids_mqtt_enqueue_failures(void);
