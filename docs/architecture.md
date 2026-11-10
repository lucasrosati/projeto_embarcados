# Arquitetura

> Status: **planejada**. Nada abaixo está implementado ainda. Fonte: [especificação v2](Especificacao_ESP32_WiFi_IDS_v2_MQTT_Direto.md), seções 2–8.

## Fluxo do dado

```text
AP 2,4 GHz (canal fixo, rede autorizada)
   │ frames 802.11                     ▲ Wi-Fi STA (TCP/IP)
   ▼                                   │
ESP32: driver Wi-Fi STA + promiscuous
   callback de captura ──captureQueue──> TaskDetector ──alertQueue + notificação──> TaskTelemetry
                                                                                      │
                                                         esp-mqtt (task interna) <────┘
   │ MQTT por Wi-Fi
   ▼
Mosquitto (IP LAN do PC) → subscriber Python → validação/deduplicação → SQLite → FastAPI/WebSocket → dashboard
```

O ESP32 é o próprio cliente MQTT. **Não há ponte USB/serial→MQTT.** A serial serve só para flash e depuração.

## Rádio

- STA conectado e sniffer ligado **ao mesmo tempo**. Não desligamos a captura para publicar.
- Monitora apenas o canal do AP (roteador de laboratório com canal 2,4 GHz fixo, 20 MHz). Sem `esp_wifi_set_channel` com o STA associado.
- O driver Wi-Fi arbitra o rádio. Mutex e semáforo protegem memória, não o RF.
- Limitação: frames podem se perder enquanto o rádio transmite. Medido nos testes A vs B ([test-plan](test-plan.md)).

## FreeRTOS

| Mecanismo | Uso |
|---|---|
| Queue `captureQueue` | callback → `TaskDetector` (metadados compactos) |
| Queue `alertQueue` | `TaskDetector` → `TaskTelemetry` |
| Task Notification | acordar `TaskTelemetry` para alerta imediato |
| Event Group | bits `WIFI_CONNECTED`, `MQTT_CONNECTED` |
| Mutex | só se houver estado compartilhado de fato |

O callback roda na task do driver Wi-Fi (não é ISR): extrai metadados e faz `xQueueSend(..., 0)`, sem bloquear, sem
JSON, sem MQTT e sem log por pacote. A publicação usa `esp_mqtt_client_enqueue()` (não bloqueante).

## Detecção (limiares iniciais, hipóteses de laboratório)

| Regra | Condição | Severidade |
|---|---|---|
| `DEAUTH_BURST` | ≥ 20 deauth em 10 s, por BSSID | high |
| `DISASSOC_BURST` | ≥ 15 disassoc em 10 s, por BSSID | medium |

Cooldown de 30 s por regra e BSSID. Beacon e probe request entram só como estatística.

## Tópicos MQTT

| Tópico | QoS | Retain | Frequência |
|---|---|---|---|
| `ids/esp32-ids-01/stats` | 0 | não | 10 s |
| `ids/esp32-ids-01/alerts` | 1 | não | sob demanda, com cooldown |
| `ids/esp32-ids-01/status` | 1 | sim | na conexão e a cada 30 s; Last Will `offline` |

Os payloads usam JSON com `schema_version: 2` (exemplos na spec, §7). Alertas são deduplicados por `event_id` no SQLite,
porque QoS 1 pode duplicar. O backend adiciona `received_at` em UTC.

## Segurança

Credenciais Wi-Fi/MQTT ficam fora do Git, o broker exige autenticação e só fica acessível na LAN, nunca na internet, e
nenhum payload 802.11 é enviado ou persistido.
