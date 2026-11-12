#include "ids_mqtt.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "config.h"
#include "ids_types.h"
#include "telemetry.h"

static const char *TAG = "mqtt";

#define OUTBOX_LIMIT_BYTES (8 * 1024)  // limita a RAM usada por mensagens pendentes (spec §7)
#define KEEPALIVE_S 15                 // broker declara offline (Last Will) após ~1,5x esse tempo

static esp_mqtt_client_handle_t s_client;
static atomic_uint s_failures;
static char s_topic[3][64];
static char s_lwt[160];

static bool connected(void)
{
    return (xEventGroupGetBits(g_net_events) & MQTT_CONNECTED_BIT) != 0;
}

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "conectado ao broker %s", CONFIG_IDS_MQTT_URI);
        xEventGroupSetBits(g_net_events, MQTT_CONNECTED_BIT);
        xTaskNotify(g_telemetry_task, NOTIFY_MQTT_UP, eSetBits);  // publica status "online" já na conexão
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (connected()) {
            ESP_LOGW(TAG, "desconectado do broker; o cliente tenta reconectar sozinho");
        }
        xEventGroupClearBits(g_net_events, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DELETED:
        // Mensagem expirou no outbox sem ser entregue (ex.: broker fora por muito tempo).
        atomic_fetch_add(&s_failures, 1);
        ESP_LOGW(TAG, "mensagem msg_id=%d descartada do outbox", ev->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            // 5 = não autorizado (usuário/senha); 4 = credenciais malformadas.
            ESP_LOGE(TAG, "broker recusou a conexao, codigo=%d (confira usuario/senha)",
                     ev->error_handle->connect_return_code);
        } else if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "falha de rede ate o broker (errno=%d): broker ligado? IP certo?",
                     ev->error_handle->esp_transport_sock_errno);
        }
        break;
    default:
        break;
    }
}

void ids_mqtt_start(void)
{
    if (s_client != NULL || !ids_mqtt_enabled()) {
        return;
    }
    snprintf(s_topic[MQTT_TOPIC_STATS], sizeof(s_topic[0]), "ids/%s/stats", IDS_DEVICE_ID);
    snprintf(s_topic[MQTT_TOPIC_ALERTS], sizeof(s_topic[0]), "ids/%s/alerts", IDS_DEVICE_ID);
    snprintf(s_topic[MQTT_TOPIC_STATUS], sizeof(s_topic[0]), "ids/%s/status", IDS_DEVICE_ID);
    snprintf(s_lwt, sizeof(s_lwt),
             "{\"schema_version\":2,\"type\":\"status\",\"device_id\":\"%s\",\"boot_id\":\"%s\",\"state\":\"offline\"}",
             IDS_DEVICE_ID, telemetry_boot_id());

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_IDS_MQTT_URI,
        .credentials.username = CONFIG_IDS_MQTT_USER,
        .credentials.client_id = IDS_DEVICE_ID,
        .credentials.authentication.password = CONFIG_IDS_MQTT_PASSWORD,
        .session.keepalive = KEEPALIVE_S,
        .session.last_will = {
            .topic = s_topic[MQTT_TOPIC_STATUS],
            .msg = s_lwt,
            .qos = 1,
            .retain = 1,
        },
        .outbox.limit = OUTBOX_LIMIT_BYTES,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "falha ao criar o cliente MQTT");
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
    ESP_LOGI(TAG, "cliente iniciado: %s (topicos ids/%s/...)", CONFIG_IDS_MQTT_URI, IDS_DEVICE_ID);
}

bool ids_mqtt_enabled(void)
{
    return CONFIG_IDS_MQTT_URI[0] != '\0';
}

bool ids_mqtt_publish(ids_mqtt_topic_t topic, const char *json)
{
    if (s_client == NULL) {
        return false;
    }
    int qos = topic == MQTT_TOPIC_STATS ? 0 : 1;
    int retain = topic == MQTT_TOPIC_STATUS ? 1 : 0;
    bool keep_while_offline = topic == MQTT_TOPIC_ALERTS;  // só alertas esperam a reconexão no outbox

    if (!connected() && !keep_while_offline) {
        atomic_fetch_add(&s_failures, 1);
        return false;
    }
    // store=true: sem ele o esp-mqtt só enfileira QoS 1/2 e recusa o QoS 0 dos stats com -1.
    int id = esp_mqtt_client_enqueue(s_client, s_topic[topic], json, 0, qos, retain, true);
    if (id < 0) {  // -1 erro, -2 outbox cheio
        atomic_fetch_add(&s_failures, 1);
        ESP_LOGW(TAG, "enqueue falhou (%d) em %s", id, s_topic[topic]);
        return false;
    }
    return true;
}

uint32_t ids_mqtt_enqueue_failures(void)
{
    return atomic_load(&s_failures);
}
