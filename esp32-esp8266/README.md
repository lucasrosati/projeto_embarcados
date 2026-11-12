# Firmware (ESP-IDF)

Placa: ESP32 de 38 pinos (pinagem ESP32-DevKitC, módulo ESP-32 WROOM, USB-C). Framework: ESP-IDF, testado com a
v6.0.1 via PlatformIO. Pinos do circuito em [schematics/montagem.md](../schematics/montagem.md).

## Estado atual

Testado na placa real em 12/11/2026, conectado ao hotspot do celular (canal 6), publicando no Mosquitto do PC.
Resultados no [plano de testes](../docs/test-plan.md) e em [observações de campo](../docs/observacoes-de-campo.md).

- **Rádio:** Wi-Fi STA conectado + promiscuous (só gerenciamento) ligado ao mesmo tempo, sem desligar.
- **Captura:** o callback extrai subtipo, BSSID e RSSI do cabeçalho e faz `xQueueSend(..., 0)` para a `captureQueue`.
- **Detecção:** o `TaskDetector` fecha janelas de 10 s e aplica `DEAUTH_BURST` (≥ 20) e `DISASSOC_BURST` (≥ 15) por
  BSSID, com cooldown de 30 s.
- **Telemetria:** o `TaskTelemetry` acorda por Task Notification e gera os JSON `stats`, `alert` e `status`
  (`schema_version` 2).
- **MQTT direto:** o `ids_mqtt` publica com `esp_mqtt_client_enqueue()` (não bloqueia). `stats` vai com QoS 0;
  `alerts` com QoS 1, e fica no outbox (limitado a 8 KB) enquanto o broker estiver fora; `status` com QoS 1 retido.
  O Last Will é `offline`. As falhas são contadas em `mqtt_enqueue_failures`. A serial só repete os JSON, para
  depuração.
- **Saídas locais:** LCD 16x2 I2C (canal, RSSI, `MQ`/`NB`, contagens, alertas), LED verde (Wi-Fi + broker) e LED
  vermelho (alerta).
- **Teste sintético:** o botão BOOT injeta 25 deauth com BSSID fictício (alerta `source=synthetic`). Há também uma
  injeção periódica opcional no menuconfig. Nada é transmitido pelo rádio.

**Dependência:** no ESP-IDF 6.x, o esp-mqtt é um componente gerenciado (`main/idf_component.yml`, baixado no primeiro
build). A partição de app é de 1,5 MB (`platformio.ini` / `sdkconfig.defaults`).

## Módulos (spec §5)

| Arquivo | Responsabilidade |
|---|---|
| `app_main.c` | Cria filas, Event Group e tasks; ordem de inicialização |
| `wifi_manager.*` | STA, reconexão, canal/RSSI do AP, bits `WIFI_CONNECTED` |
| `wifi_sniffer.*` + `frame_parser.*` | Promiscuous, callback, leitura do cabeçalho 802.11 |
| `detector.*` | `TaskDetector`: janelas, regras, cooldown, snapshot de stats |
| `telemetry.*` | `TaskTelemetry`: JSON de stats/alert/status |
| `ids_mqtt.*` | Cliente esp-mqtt: conexão, Last Will, enqueue não bloqueante, contagem de falhas |
| `local_ui.*` + `lcd1602.*` | LCD via I2C e LEDs |
| `synthetic.*` | Botão BOOT e injeção periódica para o teste E |
| `config.h`, `ids_types.h` | Parâmetros, pinos, tipos e objetos FreeRTOS compartilhados |

Sincronização: `captureQueue` e `alertQueue`, Task Notification com bits (alerta/stats), Event Group
(`WIFI_CONNECTED`, `SNIFFER_ACTIVE`, `MQTT_CONNECTED`) e uma caixa de 1 posição (`xQueueOverwrite`) para o snapshot. Não há mutex: cada
contador tem uma task dona.

## Compilar e gravar

### PlatformIO (VS Code)

```sh
cd esp32-esp8266
~/.platformio/penv/bin/pio run -t menuconfig     # menu "IDS Wi-Fi": SSID e senha (rede 2,4 GHz)
python3 ../applications/broker/setup_credentials.py   # preenche URI, usuário e senha do MQTT
~/.platformio/penv/bin/pio run -t upload -t monitor --upload-port /dev/cu.usbserial-XXXX
```

Pela barra lateral do PlatformIO: *esp32dev → Run Menuconfig* e *Upload and Monitor*. Para o IntelliSense do VS Code,
rode `pio run -t compiledb`.

### ESP-IDF puro

```sh
cd esp32-esp8266
idf.py set-target esp32
idf.py menuconfig
idf.py -p <porta> flash monitor
```

SSID e senha ficam no `sdkconfig` / `sdkconfig.esp32dev`, ambos no `.gitignore`. Nunca commitar credenciais.

## Como testar

1. No boot aparecem `LCD 16x2 encontrado em 0x27`, `conectado, ip=...` e `promiscuous ligado`.
2. A cada 10 s sai `telemetry: stats {...}` com `beacon` > 0 e `capture_queue_drops` em 0. O LCD mostra
   `CH06 -47dBm ON` / `B607 P17 D0 X0`.
3. Com o broker ligado: `mqtt: conectado ao broker` e o LCD mostra `MQ`.
4. Aperte BOOT: aparece `alert {..."source":"synthetic"}`, o LED vermelho pisca, o LCD mostra `ALERTA DEAUTH` e o
   alerta aparece no dashboard.
5. Aperte de novo em menos de 30 s: `suprimido pelo cooldown`, sem alerta novo.
6. Se o LCD acender sem texto, ajuste o potenciômetro azul atrás do módulo.
