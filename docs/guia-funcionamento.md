# Guia de funcionamento

O que o IDS faz, o que aparece no LCD e nos LEDs e como ler os logs da serial. Vale para o firmware com MQTT,
testado na placa em 12/11/2026.

## Em uma frase

O ESP32 fica **conectado à rede** (Wi-Fi STA) e, ao mesmo tempo, **escuta o canal dessa rede** (modo promiscuous).
Ele conta os frames de controle do Wi-Fi que passam pelo ar e **dispara um alerta** quando aparece uma rajada de
frames de desconexão, a assinatura típica de um ataque de deauth.

## O que é monitorado

O Wi-Fi tem **frames de gerenciamento**: pacotes que os aparelhos trocam para se anunciar, se conectar e se
desconectar. Eles não carregam os dados do usuário (sites, mensagens). O ESP32 lê só o **cabeçalho** desses frames:
o tipo, quem mandou (BSSID) e a força do sinal (RSSI). **Nunca o conteúdo.**

| Frame | O que é | É normal? |
|---|---|---|
| **Beacon** | O roteador anunciando "eu existo, minha rede se chama X", ~10 vezes por segundo | Sim, constante |
| **Probe request** | Um aparelho perguntando "tem alguma rede aí?" | Sim, varia com o uso |
| **Deauth** | "Você está desconectado" | Pouco é normal; uma **rajada** é suspeita |
| **Disassoc** | Parecido: "encerre a associação" | Idem |

O ataque mais comum contra Wi-Fi é forjar dezenas de deauth para derrubar os aparelhos da rede. É isso que o IDS
procura. Um evento detectado é um **indício**, não uma prova. Veja as [observações de campo](observacoes-de-campo.md).

## O caminho de cada frame

```text
rádio ─► callback do sniffer ─► captureQueue ─► TaskDetector ─► alertQueue / stats ─► TaskTelemetry ─► MQTT + serial + LCD
                                                                                         LEDs ◄─ task dos LEDs
MQTT ─► Mosquitto (PC) ─► back-end Python ─► SQLite ─► WebSocket ─► dashboard no navegador
```

1. **Rádio → callback:** o driver Wi-Fi entrega o frame. O callback copia só os metadados (tipo, BSSID, RSSI,
   horário) e os coloca na fila, sem bloquear e sem processar nada.
2. **Fila → `TaskDetector`:** conta os frames por tipo numa janela de 10 s. Para deauth e disassoc, conta **por
   BSSID**:
   - um BSSID com **≥ 20 deauth** (ou **≥ 15 disassoc**) em 10 s gera um alerta;
   - depois de alertar, espera **30 s** antes de alertar de novo pela mesma regra e pelo mesmo BSSID (*cooldown*);
   - a cada 10 s fecha a janela e entrega um retrato com as contagens.
3. **`TaskTelemetry`:** acorda na hora quando chega um alerta, ou a cada 10 s quando a janela fecha. Monta o JSON,
   **publica no broker MQTT** pelo Wi-Fi (`esp_mqtt_client_enqueue`, que não bloqueia), escreve na serial e atualiza
   o LCD. A cada 30 s, e logo que conecta no broker, publica também o `status`.
4. **Task dos LEDs:** a cada 100 ms, acende ou pisca os LEDs conforme o estado.

Os limiares, a janela e o cooldown podem ser ajustados no `menuconfig` (menu "IDS Wi-Fi").

## LCD

### Tela normal (atualiza a cada 10 s)

```text
CH06 -47dBm  MQ
B607 P17 D0 X0
```

| Campo | Significado |
|---|---|
| `CH06` | Canal monitorado, o mesmo da rede conectada |
| `-47dBm` | Força do sinal do AP. Perto de 0 é forte: -30 é excelente, -70 é fraco, -85 é quase caindo |
| `MQ` | Wi-Fi, sniffer e broker MQTT conectados |
| `NB` | Wi-Fi e sniffer ok, mas **sem broker** (Mosquitto desligado ou IP errado). Os stats dessa hora se perdem; os alertas esperam a reconexão |
| `ON` | Wi-Fi e sniffer ok, MQTT desligado no menuconfig (URI vazia) |
| `B607` | 607 **beacons** nos últimos 10 s, contando o AP e as redes vizinhas no mesmo canal |
| `P17` | 17 **probe requests** (aparelhos procurando redes) |
| `D0` | 0 **deauth** |
| `X0` | 0 **disassoc** |

### Tela de alerta (fica 10 s)

```text
ALERTA DEAUTH
n=20 ..00:01 SIM
```

- `n=20`: quantos deauth vieram desse BSSID na janela.
- `..00:01`: os dois últimos bytes do BSSID de origem.
- `SIM`: alerta sintético, gerado pelo botão BOOT. Num evento real aparece `LIVE`.

### Outras telas

| Tela | Quando |
|---|---|
| `ESP32 Wi-Fi IDS` / `conectando...` | No boot |
| `Wi-Fi: buscando` / `IDS aguardando` | A rede caiu ou não foi encontrada |
| Luz acesa sem texto | Contraste: ajuste o potenciômetro azul atrás do módulo |

## LEDs

| LED | Aceso | Piscando | Apagado |
|---|---|---|---|
| **Verde** (GPIO19) | Wi-Fi, sniffer e broker conectados | Falta a rede ou o broker | Placa sem firmware ou travada |
| **Vermelho** (GPIO23) | — | Alerta nos últimos 10 s | Sem alerta |

## Botão BOOT (teste sintético)

Apertar o **BOOT** (o do lado do USB marcado IO0, não o EN) injeta 25 deauth falsos no detector, com um BSSID
fictício (`02:00:00:00:00:01`). **Nada é transmitido pelo rádio.** É o teste E da spec:

1. Primeiro aperto: alerta (LED vermelho e `ALERTA DEAUTH ... SIM`).
2. Novo aperto em menos de 30 s: sem alerta, porque o cooldown bloqueia (`alerts_suppressed` sobe).
3. Aperto depois de 30 s: alerta de novo.

Os frames sintéticos **não entram nas contagens** do LCD nem do `stats`. Eles aparecem só em `synthetic_injected`.

## Logs da serial

Abra o monitor com:

```sh
~/.platformio/penv/bin/pio device monitor -p /dev/cu.usbserial-0001 -b 115200
```

Formato de cada linha: `I (5491) wifi: conectado ...`. A letra é o nível (**I** = informação, **W** = aviso, em
amarelo, **E** = erro). O número entre parênteses é o tempo desde o boot, em ms. Em seguida vem o módulo que escreveu.

### No boot

```text
I (531)  ids: regras: DEAUTH_BURST>=20, DISASSOC_BURST>=15 em 10 s, cooldown 30 s
I (591)  lcd: LCD 16x2 encontrado em 0x27
I (621)  telemetry: device_id=esp32-ids-01 boot_id=61ce24b5
W (1891) wifi: desconectado, reason=201 rssi=-128; tentando de novo
I (5491) wifi: conectado, ip=172.20.10.9 canal=6 rssi=-31
I (5491) sniffer: promiscuous ligado (gerenciamento), segue o canal do AP
```

| Linha | Significado |
|---|---|
| `regras:` | Limiares em uso (vêm do menuconfig) |
| `LCD 16x2 encontrado em 0x27` | O LCD respondeu no I2C. Se aparecer "nao encontrado", confira SDA/SCL/VCC/GND |
| `boot_id` | Identificador desta ligação da placa. Muda a cada reset |
| `desconectado, reason=...` | Uma tentativa de conexão falhou e ele tenta de novo sozinho. Os códigos estão abaixo |
| `conectado, ip=... canal=...` | Entrou na rede. O canal é o que será monitorado |
| `promiscuous ligado` | A partir daqui começa a contar os frames |
| `mqtt: conectado ao broker ...` | O ESP32 conectou no Mosquitto e já publica |
| `mqtt: falha de rede ate o broker` | Broker desligado, IP errado ou Mac em outra rede. Ele tenta de novo sozinho |
| `mqtt: broker recusou a conexao, codigo=5` | Usuário/senha errados: rode `setup_credentials.py` e regrave |

Códigos de `reason` mais comuns:

| reason | Significado | O que fazer |
|---|---|---|
| 201 | AP não encontrado | Rede fora de alcance, em 5 GHz ou desligada |
| 2, 205 | Falha de autenticação na primeira tentativa | Normal com hotspot; ele reconecta sozinho |
| 15, 204 | Falha no handshake | Senha errada ou sinal muito fraco |

### A cada 10 s: `stats`

```json
{"type":"stats","seq":79,"uptime_ms":790151,"channel":6,"rssi":-45,"window_s":10,
 "counts":{"beacon":646,"probe_req":18,"deauth":16,"disassoc":0},
 "capture_queue_drops":0,"alert_queue_drops":0,"alerts_suppressed":1,"mqtt_enqueue_failures":0,
 "wifi_reconnects":0,"synthetic_injected":75,"source":"live"}
```

| Campo | Para que olhar |
|---|---|
| `seq` | Número da janela. Se pular, alguma se perdeu |
| `channel`, `rssi` | Canal e força do sinal do AP (o mesmo do LCD) |
| `counts` | As 4 contagens da janela, **só frames reais** |
| `capture_queue_drops` | Frames perdidos porque a fila encheu (acumulado). **Deve ficar em 0** |
| `alerts_suppressed` | Alertas bloqueados pelo cooldown (acumulado) |
| `mqtt_enqueue_failures` | Mensagens que não puderam ir ao broker (acumulado): sobe quando o broker cai |
| `wifi_reconnects` | Quantas vezes a conexão caiu e voltou |
| `synthetic_injected` | Total de frames falsos injetados pelo botão |

### No alerta

```text
W synthetic: rajada sintetica: 25/25 frames deauth injetados (BSSID 02:00:00:00:00:01)
W telemetry: alert {"type":"alert","event_id":"61ce24b5-evt-002","rule":"DEAUTH_BURST",
                    "severity":"high","bssid":"02:00:00:00:00:01","count":20,"window_s":10,"source":"synthetic"}
I detector: DEAUTH_BURST suprimido pelo cooldown de 30 s
```

| Campo | Significado |
|---|---|
| `event_id` | Único por evento (`boot_id` + número). Na parte 2, o banco usa esse ID para não gravar o mesmo alerta duas vezes |
| `rule`, `severity` | `DEAUTH_BURST` = `high`, `DISASSOC_BURST` = `medium` |
| `bssid` | Quem mandou a rajada. Lembre que MAC pode ser falsificado |
| `count` | Frames na janela quando o limiar foi atingido |
| `source` | `live` = veio do rádio; `synthetic` = teste |
| `suprimido pelo cooldown` | Houve rajada, mas já tinha saído alerta da mesma regra e do mesmo BSSID há menos de 30 s |

### A cada 30 s: `status`

```json
{"type":"status","state":"online","uptime_ms":30141,"wifi_connected":true,"sniffer_active":true,"channel":6,"rssi":-31}
```

É o "estou vivo" do dispositivo. Na parte 2, ele vira o indicador online/offline do dashboard.

## Dashboard

Com broker e back-end rodando, abra http://127.0.0.1:8000 (veja "Como rodar" no README).

| Parte | O que mostra |
|---|---|
| **Estado** | Online/offline e há quanto tempo veio a última mensagem. Fica offline se chegar o Last Will (o broker percebeu que o ESP32 sumiu) ou se passar 75 s sem nada |
| **Canal, RSSI, uptime, boot** | Os mesmos valores do LCD e do `stats` |
| **Alertas no período** | Total, separando `live` e sintéticos |
| **Frames perdidos / Falhas MQTT** | `capture_queue_drops` e `mqtt_enqueue_failures` da última janela |
| **Gráficos** | Beacons e probe requests; deauth e disassoc. Um **buraco** na linha indica janelas que não chegaram (broker fora, placa desligada) |
| **Alertas** | Um por linha, com o BSSID mascarado e a origem. Alerta novo aparece no topo, destacado |
| **Rodapé** | Mensagens aceitas, duplicadas (QoS 1 repetido) e rejeitadas (fora do contrato) pelo back-end |

O selo **"tempo real ativo"** no canto indica que o navegador está recebendo eventos do back-end. Um aviso amarelo
aparece quando o dispositivo selecionado é de teste (`synthetic-01`, gerado por `tools/publish_synthetic.py`).

## Onde isso se encaixa na arquitetura

Os JSON de `stats`, `alert` e `status` são publicados pelo próprio ESP32 nos tópicos `ids/esp32-ids-01/stats`
(QoS 0), `/alerts` (QoS 1) e `/status` (QoS 1, retido, com Last Will). A serial só repete o que foi publicado, para
depuração: **não existe ponte serial→MQTT**. Veja a [arquitetura](architecture.md).
