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
| Especificação e arquitetura | Implementada e testada (`docs/`). Pendente: comparação formal do teste B |
| Firmware (`/esp32-esp8266`) | Testado na placa: STA + promiscuous, detector, LCD/LEDs, teste sintético e publicação MQTT direta |
| Broker (`/applications/broker`) | Mosquitto local com senha e ACL, testado |
| Backend (`/applications/backend`) | Testado: subscriber MQTT, validação, deduplicação, SQLite, REST e WebSocket |
| Frontend (`/applications/frontend`) | Testado com dados reais: estado, gráficos ao vivo, alertas, histórico |
| Esquemático (`/schematics`) | Circuito montado e testado (LCD, LEDs) |

## Como rodar

Pré-requisitos: Mosquitto (`brew install mosquitto`), Python 3, PlatformIO (VS Code).

```sh
# 1. Credenciais (uma vez, ou quando o IP do PC mudar). Grava senhas só em arquivos fora do Git.
python3 applications/broker/setup_credentials.py            # opcional: --broker-ip <IP do PC>

# 2. Broker (terminal 1)
applications/broker/start_broker.sh

# 3. Back-end + dashboard (terminal 2)
cd applications/backend
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt   # uma vez
.venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000          # abra http://127.0.0.1:8000

# 4. Firmware (terminal 3): SSID/senha no menuconfig, depois gravar
cd esp32-esp8266
~/.platformio/penv/bin/pio run -t menuconfig
~/.platformio/penv/bin/pio run -t upload -t monitor --upload-port /dev/cu.usbserial-XXXX
```

Sem a placa: `applications/backend/.venv/bin/python applications/backend/tools/publish_synthetic.py` publica dados
sintéticos. O que cada número significa está no [guia de funcionamento](docs/guia-funcionamento.md).

### Mudou de rede ou o IP do PC mudou

O ESP32 procura o broker pelo IP do PC, que fica gravado no firmware. Quando você troca de rede (ou o hotspot
reconecta e o PC recebe outro IP):

1. Rode `python3 applications/broker/setup_credentials.py`. Ele detecta o IP novo e gera **senhas novas** para o broker,
   o back-end e o firmware.
2. **Reinicie o broker** (`Ctrl+C` no terminal dele e `applications/broker/start_broker.sh` de novo).
3. **Reinicie o back-end** (`Ctrl+C` e o comando `uvicorn` de novo).
4. **Grave o ESP32 de novo** (`pio run -t upload ...`). Se a rede Wi-Fi também mudou, troque o SSID e a senha no
   `menuconfig` antes.

O broker e o back-end só leem as senhas quando ligam. Se pular os passos 2 ou 3, eles continuam com as senhas antigas.

| Sintoma no monitor serial | Causa | O que fazer |
|---|---|---|
| `broker recusou a conexao, codigo=5` | Senha do ESP32 diferente da do broker | Passos 2 e 4 |
| `falha de rede ate o broker` | Broker desligado ou IP errado no firmware | Ligue o broker; se o IP mudou, passos 1 a 4 |
| `desconectado, reason=201` | Rede Wi-Fi não encontrada (fora de alcance ou 5 GHz) | Confira SSID/senha no `menuconfig` e a banda 2,4 GHz |

Sempre que o firmware for gravado, o `menuconfig` precisa ter sido salvo antes: ele só altera um arquivo no PC
(`sdkconfig.esp32dev`), e a placa recebe as mudanças na gravação.

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
- [Guia de funcionamento](docs/guia-funcionamento.md): o que o IDS faz, LCD, LEDs e logs
- [Arquitetura](docs/architecture.md)
- [Plano do MVP](docs/mvp-plan.md)
- [Plano de testes](docs/test-plan.md)
- [Observações de campo](docs/observacoes-de-campo.md): eventos suspeitos que não significam ataque

## Integrantes

| Nº | Nome | GitHub | Responsabilidade |
|---|---|---|---|
| 1 | Vinicius Gomes Beltrao (representante) | [vinigbeltrao](https://github.com/vinigbeltrao) | Firmware do ESP32 (Wi-Fi, captura, detecção e MQTT), montagem e testes do circuito, back-end e documentação |
| 2 | Leticia Lopes Moreira | — | Ideação e brainstorming do projeto; pesquisa sobre ataques de desconexão em Wi-Fi |
| 3 | Icaro Barros Sampaio | [IcaroBarros](https://github.com/IcaroBarros) | Ideação e brainstorming; prototipação; desenho da arquitetura de comunicação |
| 4 | Lucas Rosati | [lucasrosati](https://github.com/lucasrosati) | Dashboard; engenharia de IA (prompts e especificação) para definir a arquitetura |
