# Front-end (dashboard)

HTML/CSS/JS + Chart.js 4.4.1 (CDN), servido pelo back-end em http://127.0.0.1:8000.

- **Estado:** online/offline com motivo (Last Will ou sem heartbeat) e último contato.
- **Medidas:** canal, RSSI, uptime, boot, alertas no período, frames perdidos, falhas MQTT, reconexões Wi-Fi.
- **Gráficos:** beacons e probe requests; deauth e disassoc. Ficam separados porque as escalas são muito diferentes.
  Têm crosshair, tooltip e rótulo no fim de cada linha.
- **Tabela:** "Ver tabela das janelas", com os mesmos dados dos gráficos.
- **Alertas:** horário, regra, severidade (ícone + texto), BSSID mascarado, contagem, janela, origem `live`/`synthetic`
  e `event_id`.
- **Filtros:** dispositivo, período (15 min / 1 h / 6 h / 24 h) e origem dos alertas.
- **Aviso:** um banner indica quando o dispositivo selecionado é de teste (dados sintéticos).

O histórico vem da API REST, e o tempo real vem do WebSocket `/ws`, que reconecta sozinho. Funciona nos modos claro e
escuro. A paleta das séries foi validada para daltonismo.
