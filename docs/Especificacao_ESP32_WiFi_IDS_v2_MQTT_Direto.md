# Especificação de implementação — ESP32 Wi-Fi IDS com MQTT direto

**Versão:** 2.0 — 10/11/2026  
**Disciplina:** Sistemas Embarcados — CESAR School  
**Relatório de checkpoint (prazo informado):** quinta-feira, **12/11/2026**  
**Data de checkpoint impressa no modelo:** 17/11/2026  
**Status:** documento de planejamento e handoff para implementação; não é relatório preenchido. Não declarar recursos planejados como concluídos.

## 1. Objetivo e mudança da versão anterior

Construir um IDS embarcado com **um ESP32 e seu rádio Wi-Fi de 2,4 GHz integrado**. O dispositivo captura passivamente frames de gerenciamento IEEE 802.11, conta eventos de interesse, identifica padrões de volume anormal (sobretudo *deauthentication* e *disassociation*) e **publica diretamente, por MQTT sobre a sua conexão Wi-Fi, estatísticas e alertas** para um broker local. Um backend recebe os tópicos, persiste dados em SQLite e atualiza um dashboard web.

**Mudança arquitetural obrigatória:** eliminar a ponte Python USB/serial → MQTT do projeto anterior. O **próprio ESP32 é o cliente/publicador MQTT**. A serial USB pode ser utilizada para flash, depuração e diagnóstico, mas **não** para transportar os dados do dashboard na entrega final.

Não há Bluetooth, NFC, Sub-GHz, ataque ativo, descriptografia, jamming, bloqueio de redes ou identificação de ferramentas específicas como Flipper Zero. Eventos detectados representam **indícios**, não comprovação de ataque ou autoria.

## 2. Viabilidade e decisão fundamental de rádio

O ESP32 suporta modo sniffer (`esp_wifi_set_promiscuous(true)`) **enquanto está conectado como estação** (`WIFI_MODE_STA`). Portanto, **não é necessário desligar a captura para cada publicação**. A conexão MQTT utiliza o rádio Wi-Fi já conectado ao roteador. O monitoramento acompanha **o mesmo canal da rede do roteador/AP** (por exemplo, canal 6); não há monitoração simultânea independente de outros canais. Configurar o roteador de laboratório com canal fixo 2,4 GHz e preferencialmente 20 MHz.

**Estratégia principal:** conexão Wi-Fi STA mantida + captura promiscuous continuamente habilitada + telemetria MQTT pequena e esparsa. A arbitragem temporal do rádio é feita internamente pelo driver Wi-Fi; **nenhuma Task Notification, mutex ou semáforo assume controle físico do rádio**.

**Limitação:** o rádio pode perder frames enquanto transmite/recebe seu tráfego, e o modo sniffer pode reduzir throughput/estabilidade da conexão. MQTT esparso diminui carga, mas **não garante ausência de perdas**. Para validar a hipótese, executar teste comparativo com e sem MQTT (seção 11). O Wi-Fi da estação também gera tráfego de manutenção, mesmo entre publicações.

**Não escolher como padrão** alternar `esp_wifi_set_promiscuous(false/true)` em janelas de envio; isso cria lacunas garantidas e não é necessário para publicar. Somente considerar uma alternativa se os testes medidos demonstrarem problemas importantes. Não usar `esp_wifi_set_channel` para trocar de canal enquanto a estação está associada. Preferir ler o canal atual após a associação.

## 3. Arquitetura completa

```text
                              ROTEADOR/AP 2,4 GHz
                         (canal fixo, rede autorizada)
                                 ^            |
                         frames 802.11        | Wi-Fi STA (TCP/IP)
                                 |            |
                        +--------+------------+---------+
                        |        ESP32                  |
                        | Driver Wi-Fi STA+Promiscuous  |
                        |  callback de captura         |
                        |       |                       |
                        |  CaptureQueue                 |
                        |       |                       |
                        |  TaskDetector (FreeRTOS)      |
                        |       |                       |
                        |  EventQueue + StatsSnapshot  |
                        |       |                       |
                        |  TaskTelemetry               |
                        |       |                       |
                        |  ESP-MQTT client ------------+---- MQTT por Wi-Fi
                        +-------------------------------+
                                                             |
                                                             v
                                                  Mosquitto no computador
                                                             |
                                                             v
                                                 Python MQTT Subscriber
                                                             |
                                                 SQLite + FastAPI/WebSocket
                                                             |
                                                             v
                                                       Dashboard local
```

O computador e ESP32 devem estar na mesma rede IP (ou com roteamento permitido). Exemplo: broker `mqtt://192.168.1.10:1883` — **IP ilustrativo** a substituir pelo IP LAN real; `localhost` no ESP32 apontaria para o próprio ESP32 e não para o computador. Mosquitto deve escutar na interface LAN adequada e permitir conexão autenticada do ESP32; não disponibilizar o broker na internet.

## 4. Concorrência e sincronização FreeRTOS

| Mecanismo | Aplicação prevista | Por quê |
|---|---|---|
| **Queue `captureQueue`** | Callback sniffer → `TaskDetector`, registros compactos | Transporta metadados, não só aviso |
| **Queue `alertQueue`** | `TaskDetector` → `TaskTelemetry` | Não perder informação do alerta entre tarefas |
| **Task Notification** | Aviso para `TaskTelemetry` preparar envio imediato; ou despertar na janela periódica | Leve e rápida; não carrega estrutura extensa |
| **Event Group** | Bits `WIFI_CONNECTED` e `MQTT_CONNECTED` | Condições de conectividade para publicação |
| **Mutex** | Exclusão pontual para estado compartilhado, se realmente existir | Protege memória, **não** o rádio |
| **Semáforo binário** | Não necessário no fluxo principal | Task Notification resolve o simples sinal de despertar |

**Regras importantes:**

1. `wifi_promiscuous_cb_t` executa **na tarefa do driver Wi-Fi, não em ISR**. Nesse callback, extrair apenas metadados e usar `xQueueSend(..., 0)` (não `xQueueSendFromISR`), sem bloquear. Não gerar JSON, publicar MQTT, imprimir a cada pacote nem aplicar análise cara no callback.
2. `TaskDetector` consome `captureQueue`, calcula estatísticas/janelas, atualiza regras e escreve eventos em `alertQueue`.
3. `TaskTelemetry` recebe notificações para alertas imediatos e acorda periodicamente para enviar estatísticas resumidas. Deve consultar o Event Group para saber se o MQTT está disponível, respeitando limites da fila.
4. A biblioteca ESP-MQTT possui sua própria tarefa interna. Usar `esp_mqtt_client_enqueue()` quando disponível no stack escolhido, pois encaminha a transmissão à tarefa interna sem bloquear a detecção. **A chamada não significa entrega confirmada**: monitorar eventos de publicação/erro e outbox.
5. Reduzir compartilhamento usando uma única tarefa como proprietária dos contadores; fornecer *snapshots* curtos/consistentes à telemetria. Mutex somente se necessário.
6. Não fixar núcleo/afinidade antecipadamente: isso depende da variante da placa e do ambiente. Se ESP32 dual-core e houver necessidade demonstrada, testar afinidade após a implementação básica.

### Exemplo de fluxo

```text
Callback detecta frame deauthentication
    -> xQueueSend(captureQueue, FrameMeta, 0)
TaskDetector consome FrameMeta
    -> acumula contagem por BSSID em janela de 10 s
    -> limiar excedido: xQueueSend(alertQueue, Alert, 0)
    -> xTaskNotifyGive(taskTelemetryHandle)
TaskTelemetry acorda
    -> verifica WIFI_CONNECTED e MQTT_CONNECTED
    -> serializa alerta JSON
    -> esp_mqtt_client_enqueue(... QoS 1 ...)
Tarefa interna MQTT transmite via Wi-Fi STA
    -> broker Mosquitto recebe
    -> backend grava e dashboard atualiza
```

## 5. Firmware: módulos e responsabilidades

**Framework sugerido para a implementação nova:** ESP-IDF (C/C++) com driver Wi-Fi nativo, FreeRTOS e `esp-mqtt`. Motivo: integração nativa, APIs de conectividade/eventos e envio MQTT por outbox. Se já existir firmware Arduino avançado, **não migrar automaticamente**: verificar se a combinação Arduino + APIs `esp_wifi_*` + biblioteca MQTT utilizada atende à mesma arquitetura; nenhuma chamada MQTT bloqueante pode ocorrer dentro do callback.

Módulos sugeridos:

```text
/esp32-esp8266/                 # Nome de pasta alinhado ao relatório
  CMakeLists.txt
  main/
    app_main.c                  # ou .cpp, conforme linguagem
    wifi_manager.*             # STA, conexão, IP, reconexão, canal atual
    wifi_sniffer.*             # promiscuous callback e filtros
    frame_parser.*             # cabeçalhos 802.11 e metadados
    detector.*                 # contadores, janelas, limiares e cooldown
    telemetry.*                # buffers, schemas e notificações
    mqtt_client.*              # esp-mqtt, enqueue, evento de conexão
    config.*                   # parâmetros e credenciais fora do Git
```

**Sequência de inicialização:** configurar armazenamento/configuração → iniciar Wi-Fi em STA → conectar a AP autorizado → obter IP e canal atual → habilitar filtros e sniffer → iniciar `TaskDetector` e `TaskTelemetry` → iniciar cliente MQTT → informar status. Se ocorrer reconexão com troca de canal, atualizar informação no IDS e registrar eventual lacuna na monitorização.

**Frames de interesse:** beacon (subtipo 8), probe request (4), deauthentication (12), disassociation (10). Capturar canal, tipo/subtipo, BSSID válido quando presente, RSSI e uptime; não persistir payloads, senhas ou tráfego privado. Conferir tamanho de frame e offsets antes de acessar campos; tratar BSSID ausente. Inicialmente acompanhar uma rede/BSSID de laboratório ou eventos do canal, não prometer análise de qualquer rede próxima.

## 6. Detecção: regras iniciais ajustáveis

| Regra | Condição inicial | Saída |
|---|---|---|
| `DEAUTH_BURST` | >= 20 frames deauthentication em 10 s, agrupados por BSSID válido | Alerta `high` |
| `DISASSOC_BURST` | >= 15 frames disassociation em 10 s, agrupados por BSSID válido | Alerta `medium` |
| Observabilidade | Beacon e probe requests por janela | Só estatísticas, sem alerta no MVP |

Cooldown de alertas repetidos: **30 s por regra e BSSID**. **Esses valores são hipóteses de laboratório**, sem alegar precisão universal. Endereços MAC podem ser falsificados; sinais de deauthentication/disassociation também podem ocorrer legitimamente. PMF/802.11w pode tornar determinados ataques menos eficazes, sem impedir que frames suspeitos possam ser observados.

**Limitações:** um rádio, um canal monitorado enquanto associado, alcance variável, perdas de captura possíveis, somente 2,4 GHz (para variante ESP32 2,4 GHz), sem atribuição de autoria.

## 7. Publicação MQTT diretamente pelo ESP32

**Requisitos:** Wi-Fi STA e captura promiscuous ativos, sem desligar a captura em cada envio. O MQTT deve enviar **resumos e alertas**, não cada frame. Configurar limites de fila e política de descarte, priorizando alertas sobre estatísticas.

**Tópicos e parâmetros iniciais:**

| Tópico | Origem | Destino | QoS | Retain | Frequência |
|---|---|---|---|---|---|
| `ids/esp32-ids-01/stats` | ESP32 MQTT | Back-end | 0 | Não | A cada **10 s** |
| `ids/esp32-ids-01/alerts` | ESP32 MQTT | Back-end | 1 | Não | Sob demanda, com cooldown |
| `ids/esp32-ids-01/status` | ESP32 MQTT | Back-end | 1 | Sim (somente estado atual) | Conexão e **30 s** |

**Status e Last Will:** publicar `online` como retido e configurar *Last Will* `offline` retido. Heartbeat com timestamp/uptime e timeout no back-end. Não utilizar retenção para dados históricos em `stats`/`alerts`.

**Exemplo `stats`:**

```json
{"schema_version":2,"type":"stats","device_id":"esp32-ids-01","boot_id":"boot-001","seq":42,"uptime_ms":420000,"channel":6,"window_s":10,"counts":{"beacon":115,"probe_req":12,"deauth":0,"disassoc":0},"capture_queue_drops":0,"mqtt_enqueue_failures":0,"wifi_reconnects":0,"source":"live"}
```

**Exemplo `alert`:**

```json
{"schema_version":2,"type":"alert","device_id":"esp32-ids-01","event_id":"boot-001-evt-004","boot_id":"boot-001","uptime_ms":423000,"channel":6,"rule":"DEAUTH_BURST","severity":"high","bssid":"02:11:22:33:44:55","count":22,"window_s":10,"source":"live"}
```

Os IDs exemplificativos acima são fictícios. Para deduplicação, `boot_id` deve identificar de forma suficientemente única cada inicialização, e `event_id` deve ser único por evento. O backend adiciona `received_at` em UTC no recebimento. QoS 1 pode ocasionar duplicatas; SQLite deve usar unicidade do `event_id`. Mensagens sem conexão não devem bloquear captura. Manter fila limitada de eventos, informar perdas e definir como envelhecem eventos durante longas desconexões. **Não** manter buffer ilimitado na RAM.

**Segurança de rede:** credenciais Wi-Fi/MQTT fora do Git, broker com autenticação na LAN, nenhuma exposição pública; se TLS puder ser habilitado no ambiente final, avaliar impacto de memória/CPU, sem impor a implementação como pré-requisito do MVP de laboratório. Não enviar payloads de pacotes 802.11.

## 8. Dashboard e back-end

**Broker:** Mosquitto no computador, acessível pelo IP LAN do computador. **Backend:** Python 3, `paho-mqtt`, FastAPI, SQLite e WebSocket. **Frontend:** HTML/CSS/JavaScript + Chart.js.

**Pipeline:** ESP32 MQTT → broker → subscriber Python → validação JSON/deduplicação → SQLite → WebSocket → dashboard. **Não** usar leitor serial como entrada primária final.

**Tela principal deve exibir:**
- Estado ESP32/MQTT e último heartbeat;
- Canal monitorado, uptime, reconexões e drops;
- Frames beacon/probe/deauth/disassoc por intervalo;
- Gráfico temporal das estatísticas;
- Alertas com horário, regra, severidade, BSSID mascarado, contagem e janela;
- Histórico consultável e filtros por intervalo;
- Indicador claro de dados `live` versus `synthetic` para teste.

**Modo de desenvolvimento:** back-end pode ser testado antes do firmware publicando JSON **sintético devidamente identificado** em um broker local. O dashboard deverá depois consumir as mensagens reais do ESP32, sem alterar contrato de dados.

## 9. Estrutura GitHub e artefatos do relatório

```text
README.md
/docs/
  architecture.md
  mvp-plan.md
  test-plan.md
  checkpoint-evidence/
/applications/
  backend/
  frontend/
/esp32-esp8266/
  (firmware ESP-IDF)
/schematics/
  circuit.png
```

**Circuito físico:** ESP32 + USB alimentação/programação; LED+buzzer são opcionais e precisam de esquema caso usados. Wi-Fi atua como interface/sensor virtual por radiofrequência, sem módulos externos. Documentar GPIOs conforme hardware efetivamente utilizado. Diagrama de rede deve destacar conexão ao AP e MQTT direto.

## 10. Plano de execução e checkpoint

### Checkpoint: relatório até **quinta 12/11/2026**

**Entregar apenas o que de fato existir:** repositório público e estrutura, README, esquemático básico do circuito, arquitetura final com MQTT direto **marcada como planejada**, protótipo da interface e, se possível, prova inicial do ESP32 conectado ao AP e executando modo promiscuous. Não inventar testes completos nem percentuais. O documento-modelo impresso indica checkpoint em **17/11/2026**, que não substitui o prazo de envio informado pelo grupo (**12/11**).

**Texto sugerido para a justificativa de MQTT ainda não implementado:**
> A integração MQTT está prevista para a entrega final e será executada diretamente pelo ESP32. Neste checkpoint, a prioridade técnica é validar a coexistência do modo Wi-Fi Station conectado ao roteador com o modo promiscuous de captura no mesmo canal de 2,4 GHz. Após confirmar a estabilidade da captura e definir os payloads e filas do FreeRTOS, serão integrados o cliente MQTT e o broker local, permitindo avaliar também o impacto da publicação sobre a detecção. A arquitetura e os tópicos MQTT já estão definidos, mas a funcionalidade não deve ser declarada como concluída antes dos testes.

### Desenvolvimento depois do checkpoint

1. **Prova de conceito de coexistência** (prioridade máxima): Wi-Fi STA conectado + callback promiscuous + contadores na serial; imprimir canal real; executar por 10 minutos.
2. **Detector**: estruturar `captureQueue`, `TaskDetector`, janelas e alertas sintéticos/live identificados.
3. **MQTT direto**: iniciar Mosquitto, configurar IP LAN, conectar cliente ESP32, publicar `stats`/`status` e testar `alerts` com JSON conhecido.
4. **Integração FreeRTOS**: `alertQueue`, Task Notification, Event Groups, reconexão, limites e métricas de drop.
5. **Aplicação**: assinante Python, SQLite, API/WebSocket, dashboard real e histórico.
6. **Validação**: teste de perda de captura, tempo de entrega MQTT, reconexão, persistência e falsos positivos; documentar evidências e preparar demonstração final.

**Estimativa bruta** (partindo de software ainda não desenvolvido, desenvolvedor com familiaridade com ESP32): 3–5 semanas para versão integrada e validada, conforme horas disponíveis e problemas no ambiente; o checkpoint não exige a conclusão desse trabalho.

## 11. Plano mínimo de testes e critérios de aceitação

**Teste A — IDS somente:** conectado ao AP, sem publicação MQTT, medir frames por tipo a cada 10 s durante 5–10 minutos e registrar drops.

**Teste B — IDS + MQTT:** mesma rede/canal, condições similares, publicar stats a cada 10 s, heartbeat a cada 30 s e alertas de teste; comparar contagens, `capture_queue_drops`, disponibilidade e retransmissões. Contagens naturais não são constantes, portanto não interpretar simples diferenças como uma taxa exata de perda; se possível usar fonte de tráfego de laboratório controlada e um monitor de referência.

**Teste C — conectividade:** desligar temporariamente broker e restabelecer; sniffer deve continuar processando, eventos críticos não devem bloquear a captura, indicar falhas de publicação, e cliente deve reconectar.

**Teste D — dashboard:** validar dados recebidos nos tópicos, timestamp backend, unicidade de alertas QoS 1, atualização e histórico.

**Teste E — detecção:** testar a regra com sequência **sintética** reproduzível (sem transmitir ataques na rede) e verificar alertas + cooldown. Se fizer teste com tráfego real de laboratório, limitar a ambiente próprio/autorizado.

**Critérios para a versão final:**
- O próprio ESP32 é o cliente MQTT e publica pela interface Wi-Fi integrada;
- O sniffer permanece ativo enquanto o STA está conectado, com limitação de canal documentada;
- O firmware usa filas FreeRTOS, notificações de tarefa e grupo de eventos de modo justificado;
- Estatísticas/alertas chegam ao broker e são persistidos no back-end;
- Dashboard recebe **dados reais** MQTT do ESP32 e mostra estado e falhas;
- Reconexões e mensagens perdidas não paralisam a captura;
- Resultados dos testes de coexistência e limitações estão documentados.


## 12. Fontes técnicas principais

- Espressif, Wi-Fi Modes (STA + sniffer e callback na tarefa do Wi-Fi): https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi-driver/wifi-modes.html
- Espressif, Wi-Fi Driver e modo sniffer: https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32/api-guides/wifi.html
- Espressif, ESP-MQTT e função `esp_mqtt_client_enqueue`: https://docs.espressif.com/projects/esp-mqtt/en/latest/esp32/
- Espressif, FreeRTOS / Task Notifications: https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/freertos.html
- Referência da disciplina: PDF **Relatorio_CheckPoint_Sistemas_Embarcados_CESAR_School_2026.1.pdf**, seções 1–5.
