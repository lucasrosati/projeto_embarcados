# Firmware (ESP-IDF)

Placa: ESP32 DevKit V1 (30 pinos). Framework: ESP-IDF v5.x (compilado com a v5.5.6).

## Estado atual: PoC de coexistência

`main/app_main.c` conecta em Wi-Fi STA, liga o modo promiscuous (só frames de gerenciamento) e imprime na serial, a
cada 10 s, as contagens de beacon, probe request, deauth e disassoc, o canal do AP e os drops da fila.
Ainda não há detector, MQTT nem atuadores.

O callback do sniffer só extrai o subtipo do frame e faz `xQueueSend(..., 0)`; a contagem e o log ficam na task
`counter`.

## Compilar e gravar

```sh
cd esp32-esp8266
idf.py set-target esp32
idf.py menuconfig          # menu "IDS Wi-Fi": SSID e senha da rede autorizada
idf.py build
idf.py -p <porta> flash monitor
```

SSID e senha ficam no `sdkconfig`, que está no `.gitignore`. Nunca commitar credenciais.

## Teste de aceitação do PoC

1. Na serial aparece `conectado, ip=...`.
2. A cada 10 s sai uma linha como:
   `uptime_ms=... assoc=1 ch=6 rssi=-48 beacon=115 probe_req=12 deauth=0 disassoc=0 queue_drops=0 wifi_disconnects=0`
3. `ch` é o canal configurado no roteador e `beacon` é maior que zero.
4. Rodar 10 min e salvar o log em `docs/checkpoint-evidence/`.

## Módulos previstos

`wifi_manager`, `wifi_sniffer`, `frame_parser`, `detector`, `telemetry`, `mqtt_client`, `config` (spec §5).
