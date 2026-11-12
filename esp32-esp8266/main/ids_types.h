// Tipos e objetos FreeRTOS compartilhados entre os módulos do IDS (spec §4).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Subtipos de frame de gerenciamento 802.11 monitorados (spec §5).
enum { SUB_PROBE_REQ = 4, SUB_BEACON = 8, SUB_DISASSOC = 10, SUB_DEAUTH = 12 };

// Metadados de um frame: é só isso que sai do callback do sniffer. Nunca o payload.
typedef struct {
    uint32_t t_ms;     // uptime na captura
    uint8_t bssid[6];  // addr3 do cabeçalho
    int8_t rssi;
    uint8_t subtype;
    bool synthetic;    // injetado pelo teste sintético, não veio do rádio
} frame_meta_t;

typedef enum { RULE_DEAUTH_BURST = 0, RULE_DISASSOC_BURST = 1, RULE_COUNT } rule_id_t;

typedef struct {
    uint32_t seq;  // número do evento neste boot (compõe o event_id)
    uint32_t uptime_ms;
    uint8_t bssid[6];
    uint8_t channel;
    rule_id_t rule;
    uint16_t count;
    uint16_t window_s;
    bool synthetic;
} alert_t;

// Retrato de uma janela de estatísticas, produzido só pelo TaskDetector.
typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    uint16_t window_s;
    bool associated;
    uint8_t channel;
    int8_t rssi;  // do AP associado
    uint32_t beacon, probe_req, deauth, disassoc;  // só frames reais da janela
    uint32_t capture_queue_drops;                  // acumulado desde o boot
    uint32_t alert_queue_drops;
    uint32_t alerts_suppressed;
    uint32_t wifi_reconnects;
    uint32_t synthetic_injected;
} stats_snapshot_t;

// Bits do Event Group de conectividade.
#define WIFI_CONNECTED_BIT BIT0
#define SNIFFER_ACTIVE_BIT BIT1
// MQTT_CONNECTED_BIT entra na parte 2.

// Bits de Task Notification para o TaskTelemetry.
#define NOTIFY_STATS (1u << 0)
#define NOTIFY_ALERT (1u << 1)

// Criados em app_main antes de qualquer task.
extern QueueHandle_t g_capture_queue;  // callback/teste sintético -> TaskDetector (frame_meta_t)
extern QueueHandle_t g_alert_queue;    // TaskDetector -> TaskTelemetry (alert_t)
extern QueueHandle_t g_stats_mailbox;  // fila de 1 posição sobrescrita (stats_snapshot_t)
extern EventGroupHandle_t g_net_events;
extern TaskHandle_t g_telemetry_task;
