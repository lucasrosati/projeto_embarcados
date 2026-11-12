# Firmware (ESP-IDF)

Placa: ESP32 de 38 pinos (pinagem ESP32-DevKitC, módulo ESP-32 WROOM, USB-C). Framework: ESP-IDF, testado com a
v6.0.1 via PlatformIO. Pinos do circuito em [schematics/montagem.md](../schematics/montagem.md).

## Estado atual: parte 1 (sem MQTT)

Testado na placa real em 12/11/2026, conectado ao hotspot do celular (canal 6). Resultados em
[observações de campo](../docs/observacoes-de-campo.md).

- **Rádio:** Wi-Fi STA conectado + promiscuous (só gerenciamento) ligado ao mesmo tempo, sem desligar.
- **Captura:** o callback extrai subtipo, BSSID e RSSI do cabeçalho e faz `xQueueSend(..., 0)` para a `captureQueue`.
- **Detecção:** o `TaskDetector` fecha janelas de 10 s e aplica `DEAUTH_BURST` (≥ 20) e `DISASSOC_BURST` (≥ 15) por
  BSSID, com cooldown de 30 s.
- **Telemetria:** o `TaskTelemetry` acorda por Task Notification e gera os JSON `stats`, `alert` e `status`
  (`schema_version` 2). Por enquanto eles saem **na serial**, só para depuração.
- **Saídas locais:** LCD 16x2 I2C (canal, RSSI, contagens, alertas), LED verde (conectado) e LED vermelho (alerta).
- **Teste sintético:** o botão BOOT injeta 25 deauth com BSSID fictício (alerta `source=synthetic`). Há também uma
  injeção periódica opcional no menuconfig. Nada é transmitido pelo rádio.

**Ainda não existe:** cliente MQTT (parte 2). Os JSON da serial são os payloads que serão publicados.

## Módulos (spec §5)

| Arquivo | Responsabilidade |
|---|---|
| `app_main.c` | Cria filas, Event Group e tasks; ordem de inicialização |
| `wifi_manager.*` | STA, reconexão, canal/RSSI do AP, bits `WIFI_CONNECTED` |
| `wifi_sniffer.*` + `frame_parser.*` | Promiscuous, callback, leitura do cabeçalho 802.11 |
| `detector.*` | `TaskDetector`: janelas, regras, cooldown, snapshot de stats |
| `telemetry.*` | `TaskTelemetry`: JSON de stats/alert/status (vai virar publicação MQTT) |
| `local_ui.*` + `lcd1602.*` | LCD via I2C e LEDs |
| `synthetic.*` | Botão BOOT e injeção periódica para o teste E |
| `config.h`, `ids_types.h` | Parâmetros, pinos, tipos e objetos FreeRTOS compartilhados |

Sincronização: `captureQueue` e `alertQueue`, Task Notification com bits (alerta/stats), Event Group
(`WIFI_CONNECTED`, `SNIFFER_ACTIVE`) e uma caixa de 1 posição (`xQueueOverwrite`) para o snapshot. Não há mutex: cada
contador tem uma task dona.

## Compilar e gravar

### PlatformIO (VS Code)

```sh
cd esp32-esp8266
~/.platformio/penv/bin/pio run -t menuconfig     # menu "IDS Wi-Fi": SSID e senha (rede 2,4 GHz)
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
3. Aperte BOOT: aparece `alert {..."source":"synthetic"}`, o LED vermelho pisca e o LCD mostra `ALERTA DEAUTH`.
4. Aperte de novo em menos de 30 s: `suprimido pelo cooldown`, sem alerta novo.
5. Se o LCD acender sem texto, ajuste o potenciômetro azul atrás do módulo.
