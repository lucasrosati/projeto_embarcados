# Back-end

Python 3: subscriber `paho-mqtt` → validação (schema_version 2) → deduplicação → SQLite → FastAPI (REST + WebSocket).
Também serve o dashboard de `applications/frontend`.

## Rodar

```sh
cd applications/backend
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt     # uma vez
.venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000
```

Abra http://127.0.0.1:8000. É preciso ter o broker rodando e o `.env` gerado por
`applications/broker/setup_credentials.py`.

## Módulos

| Arquivo | Função |
|---|---|
| `app/config.py` | Lê `.env` (credenciais MQTT, caminho do banco, tempo para considerar offline) |
| `app/validation.py` | Confere tópico, `schema_version`, `type`, `device_id` e campos obrigatórios; recusa o resto |
| `app/db.py` | SQLite: `stats`, `alerts` (`event_id` único), `devices`, `status_events` |
| `app/mqtt_ingest.py` | Thread do paho: valida, grava, conta duplicatas/rejeições e repassa ao WebSocket |
| `app/main.py` | API, WebSocket `/ws` e arquivos estáticos |
| `tools/publish_synthetic.py` | Publica dados **sintéticos** (`synthetic-01`, `source=synthetic`) para testar sem a placa |

## API

| Rota | Retorna |
|---|---|
| `GET /api/health` | Conexão com o broker, contadores da ingestão (aceitas/duplicadas/rejeitadas), tamanho do banco |
| `GET /api/devices` | Dispositivos com estado efetivo (offline por Last Will ou por falta de contato por mais de 75 s) |
| `GET /api/stats?device_id=&minutes=60` | Janelas de estatística do período |
| `GET /api/alerts?device_id=&minutes=60&source=live` | Alertas do período, filtráveis por origem |
| `GET /api/status-events?device_id=` | Mudanças de estado (online/offline, novo boot) |
| `WS /ws` | Eventos em tempo real: `{"kind": "stats"|"alert"|"status"|"backend", "received_at", "data"}` |

O back-end adiciona `received_at` (UTC). Alertas repetidos (o QoS 1 pode entregar duas vezes) são ignorados pelo
`event_id`. Mensagens retidas de `status` não contam como contato recente.

## Testar sem a placa

```sh
.venv/bin/python tools/publish_synthetic.py --windows 30 --interval 2 --duplicate-alert
```
