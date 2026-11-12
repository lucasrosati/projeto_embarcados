# Plano de testes

> Fonte: [especificação v2](Especificacao_ESP32_WiFi_IDS_v2_MQTT_Direto.md), seção 11. Executados em 12/11/2026 com a placa real (hotspot do celular, canal 6, RSSI de -26 a -53 dBm).

| Teste | O que faz | Resultado esperado | Resultado (12/11/2026) |
|---|---|---|---|
| **A — IDS sozinho** | Conectado ao AP, sem MQTT; contar frames por tipo a cada 10 s por 5–10 min; registrar drops | Linha de base de contagens e `capture_queue_drops` | ✅ ~13 min sem MQTT: 540–690 beacons e 9–38 probe req por janela, `capture_queue_drops` 0, nenhuma reconexão |
| **B — IDS + MQTT** | Mesma rede/canal; stats a cada 10 s, heartbeat a cada 30 s, alertas de teste | Comparar contagens, drops, disponibilidade e retransmissões com A. Diferenças naturais não são taxa exata de perda. | 🟡 Parcial: com MQTT, 480–600 beacons por janela e drops 0. Ainda falta uma comparação formal com A (mesmas condições, tempo igual) |
| **C — Conectividade** | Derrubar e restabelecer o broker | Sniffer segue processando; falhas de publicação indicadas; cliente reconecta | ✅ Broker parado por 30 s: o sniffer seguiu contando, `mqtt_enqueue_failures` subiu para 5, o ESP32 reconectou ~15 s após o retorno, o back-end também reconectou e o dashboard mostra o buraco |
| **D — Dashboard** | Validar tópicos, `received_at`, unicidade de alertas QoS 1, atualização e histórico | Sem alerta duplicado; histórico consultável | ✅ `received_at` UTC gravado, 2 alertas duplicados ignorados pelo `event_id`, 2 payloads inválidos recusados, ACL bloqueou publicação forjada, histórico por período na API e no dashboard |
| **E — Detecção** | Sequência **sintética** reproduzível, sem transmitir ataques | Alerta disparado e cooldown de 30 s respeitado | ✅ Botão BOOT: alerta `DEAUTH_BURST` `synthetic` no LCD, nos LEDs e no dashboard; segundo aperto em menos de 30 s suprimido (`alerts_suppressed`) |

## Critérios da versão final

- [x] O ESP32 é o cliente MQTT e publica pelo Wi-Fi integrado
- [x] O sniffer fica ativo com o STA conectado (limitação de canal documentada)
- [x] Filas, notificações e Event Group usados de forma justificada
- [x] Stats e alertas chegam ao broker e são persistidos
- [x] O dashboard mostra dados reais do ESP32, estado e falhas
- [x] Reconexões e perdas não param a captura
- [ ] Resultados de coexistência e limitações documentados

Evidências vão em [`checkpoint-evidence/`](checkpoint-evidence/).
