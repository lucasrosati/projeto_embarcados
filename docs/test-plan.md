# Plano de testes

> Fonte: [especificação v2](Especificacao_ESP32_WiFi_IDS_v2_MQTT_Direto.md), seção 11. Nenhum teste executado ainda.

| Teste | O que faz | Resultado esperado |
|---|---|---|
| **A — IDS sozinho** | Conectado ao AP, sem MQTT; contar frames por tipo a cada 10 s por 5–10 min; registrar drops | Linha de base de contagens e `capture_queue_drops` |
| **B — IDS + MQTT** | Mesma rede/canal; stats a cada 10 s, heartbeat a cada 30 s, alertas de teste | Comparar contagens, drops, disponibilidade e retransmissões com A. Diferenças naturais não são taxa exata de perda. |
| **C — Conectividade** | Derrubar e restabelecer o broker | Sniffer segue processando; falhas de publicação indicadas; cliente reconecta |
| **D — Dashboard** | Validar tópicos, `received_at`, unicidade de alertas QoS 1, atualização e histórico | Sem alerta duplicado; histórico consultável |
| **E — Detecção** | Sequência **sintética** reproduzível, sem transmitir ataques | Alerta disparado e cooldown de 30 s respeitado |

## Critérios da versão final

- [ ] O ESP32 é o cliente MQTT e publica pelo Wi-Fi integrado
- [ ] O sniffer fica ativo com o STA conectado (limitação de canal documentada)
- [ ] Filas, notificações e Event Group usados de forma justificada
- [ ] Stats e alertas chegam ao broker e são persistidos
- [ ] O dashboard mostra dados reais do ESP32, estado e falhas
- [ ] Reconexões e perdas não param a captura
- [ ] Resultados de coexistência e limitações documentados

Evidências vão em [`checkpoint-evidence/`](checkpoint-evidence/).
