# ESP32 Wi-Fi IDS (MQTT direto) — Sistemas Embarcados, CESAR School 2026.1

IDS embarcado: um ESP32 em Wi-Fi STA + modo promiscuous captura metadados de frames 802.11 de gerenciamento
(beacon, probe req, deauth, disassoc) no canal do AP, detecta rajadas e **publica ele mesmo** stats/alertas por MQTT
num Mosquitto local. Backend Python grava em SQLite e empurra para um dashboard web.

Spec completa (fonte da verdade): `docs/Especificacao_ESP32_WiFi_IDS_v2_MQTT_Direto.md`.
Modelo do relatório: `docs/Relatorio_CheckPoint_Sistemas_Embarcados_CESAR_School_2026.1.pdf`.

## Stack
- Firmware: ESP-IDF (C), FreeRTOS, `esp_wifi` (STA + promiscuous), `esp-mqtt` — em `/esp32-esp8266`
- Broker: Mosquitto no PC, IP da LAN, com autenticação, nunca exposto à internet
- Backend: Python 3, `paho-mqtt`, FastAPI, SQLite, WebSocket — em `/applications/backend`
- Frontend: HTML/CSS/JS + Chart.js — em `/applications/frontend`

## Estrutura do repo (exigida pelo relatório)
`README.md`, `/docs` (architecture, mvp-plan, test-plan, checkpoint-evidence), `/applications`, `/esp32-esp8266`,
`/schematics` (circuit.png).

## Regras de implementação (spec §12)
1. Não existe ponte serial→MQTT. O ESP32 é o cliente MQTT; serial só para flash/debug.
2. Comece pelo PoC mínimo de STA + promiscuous simultâneos na placa real, antes de criar módulos.
3. Mutex/semáforo protegem memória, nunca o rádio. O driver Wi-Fi arbitra o RF.
4. Callback do sniffer roda na task do driver (não é ISR): só extrair metadados e `xQueueSend(..., 0)`.
   Nada de JSON, MQTT, printf por pacote ou análise cara ali.
5. Publicar com `esp_mqtt_client_enqueue()` (não bloqueante) e checar eventos de erro/outbox.
6. Só metadados, nunca payload. Nenhuma função ofensiva; testes só em rede própria/autorizada.
7. Não alternar promiscuous on/off para publicar nem usar `esp_wifi_set_channel` com o STA associado.
8. Confirmar modelo exato do ESP32, toolchain e IP do broker antes de gerar config. Não inventar pinos nem credenciais.
9. Credenciais Wi-Fi/MQTT fora do Git.
10. Não declarar como concluído no relatório nada que não exista e não tenha sido testado.
