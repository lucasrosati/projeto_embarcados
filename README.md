# ESP32 Wi-Fi IDS — MQTT direto

Projeto da disciplina **Sistemas Embarcados — CESAR School 2026.1** (profs. Bella Nunes e Jymmy Barreto).

Um IDS (sistema de detecção de intrusão) embarcado num único ESP32. Conectado ao AP do laboratório (Wi-Fi STA), ele mantém o
modo promiscuous ligado para capturar **metadados** de frames 802.11 de gerenciamento (beacon, probe request,
deauthentication, disassociation). Ele detecta rajadas anormais e **publica, ele mesmo, por MQTT** estatísticas e alertas
para um broker Mosquitto local. Um backend Python persiste os dados em SQLite e atualiza um dashboard web em tempo real.

> Eventos detectados são **indícios**, não prova de ataque nem de autoria. O projeto é só passivo: nenhuma função
> ofensiva, nenhum payload capturado, testes apenas em rede própria/autorizada.

## Status

| Parte | Situação |
|---|---|
| Especificação e arquitetura | Definidas (`docs/`) |
| Firmware (`/esp32-esp8266`) | Parte 1 testada na placa: STA + promiscuous, detector, LCD/LEDs, teste sintético. Falta MQTT |
| Backend (`/applications/backend`) | Não iniciado |
| Frontend (`/applications/frontend`) | Não iniciado |
| Esquemático (`/schematics`) | Circuito montado e testado (LCD, LEDs) |

## Arquitetura

```text
AP 2,4 GHz ──frames 802.11──> ESP32 (STA + sniffer) ──MQTT/Wi-Fi──> Mosquitto (PC na LAN)
                                                                       │
                                     Dashboard <── WebSocket <── FastAPI + SQLite <── subscriber Python
```

Detalhes em [docs/architecture.md](docs/architecture.md).

## Stack

- **Firmware:** ESP-IDF (C), FreeRTOS, `esp_wifi`, `esp-mqtt`
- **Broker:** Mosquitto na LAN, com autenticação
- **Backend:** Python 3, `paho-mqtt`, FastAPI, SQLite, WebSocket
- **Frontend:** HTML/CSS/JavaScript + Chart.js

## Estrutura

```text
README.md
docs/                 architecture.md, mvp-plan.md, test-plan.md, checkpoint-evidence/, especificação v2
applications/
  backend/            subscriber MQTT, API e persistência
  frontend/           dashboard
esp32-esp8266/        firmware ESP-IDF
schematics/           circuito (circuit.png)
```

## Documentação

- [Especificação v2 (fonte da verdade)](docs/Especificacao_ESP32_WiFi_IDS_v2_MQTT_Direto.md)
- [Arquitetura](docs/architecture.md)
- [Plano do MVP](docs/mvp-plan.md)
- [Plano de testes](docs/test-plan.md)
- [Observações de campo](docs/observacoes-de-campo.md): eventos suspeitos que não significam ataque

## Integrantes

| Nº | Nome | GitHub | Responsabilidade |
|---|---|---|---|
| 1 | _a preencher_ | | |
