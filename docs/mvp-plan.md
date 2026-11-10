# Plano do MVP

> Fonte: [especificação v2](Especificacao_ESP32_WiFi_IDS_v2_MQTT_Direto.md), seção 10.

## Checkpoint — relatório até qui 12/11/2026

Entregar só o que existir de fato:

- [ ] Repositório público com a estrutura exigida
- [ ] README
- [ ] Esquemático básico do circuito (`/schematics`)
- [ ] Arquitetura com MQTT direto, **marcada como planejada**
- [ ] Protótipo da interface
- [ ] (se possível) ESP32 conectado ao AP rodando promiscuous

## Depois do checkpoint

1. **PoC de coexistência** (prioridade máxima): STA conectado + callback promiscuous + contadores na serial; imprimir o canal real; rodar 10 min.
2. **Detector:** `captureQueue`, `TaskDetector`, janelas, alertas sintéticos/live identificados.
3. **MQTT direto:** Mosquitto na LAN, cliente ESP32, publicar `stats`/`status`, testar `alerts` com JSON conhecido.
4. **Integração FreeRTOS:** `alertQueue`, Task Notification, Event Groups, reconexão, limites e métricas de drop.
5. **Aplicação:** subscriber Python, SQLite, API/WebSocket, dashboard real e histórico.
6. **Validação:** testes A–E ([test-plan](test-plan.md)), evidências e demonstração final.

Estimativa bruta: 3–5 semanas até a versão integrada e validada.

## Pendências de informação

- Modelo exato do ESP32 e toolchain
- IP LAN do broker
- Número do grupo e integrantes
