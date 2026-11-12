"""API do IDS: REST para histórico, WebSocket para tempo real e o dashboard estático.

Rodar (de applications/backend):  .venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000
"""
import asyncio
import logging
from contextlib import asynccontextmanager
from datetime import datetime, timezone

from fastapi import FastAPI, Query, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles

from .config import FRONTEND_DIR, settings
from .db import Database, since_iso
from .mqtt_ingest import MqttIngest

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("ids.api")


class Hub:
    """Repassa eventos do MQTT para todos os navegadores conectados."""

    def __init__(self) -> None:
        self.clients: set[WebSocket] = set()
        self.queue: asyncio.Queue[dict] = asyncio.Queue(maxsize=1000)
        self.loop: asyncio.AbstractEventLoop | None = None

    def emit_threadsafe(self, event: dict) -> None:
        if self.loop is None:
            return

        def put() -> None:
            if self.queue.full():  # navegador lento não pode segurar a ingestão
                self.queue.get_nowait()
            self.queue.put_nowait(event)

        self.loop.call_soon_threadsafe(put)

    async def run(self) -> None:
        while True:
            event = await self.queue.get()
            dead = []
            for ws in list(self.clients):
                try:
                    await ws.send_json(event)
                except Exception:
                    dead.append(ws)
            for ws in dead:
                self.clients.discard(ws)


db = Database(settings.db_path)
hub = Hub()
ingest = MqttIngest(settings, db, hub.emit_threadsafe)


@asynccontextmanager
async def lifespan(app: FastAPI):
    hub.loop = asyncio.get_running_loop()
    task = asyncio.create_task(hub.run())
    ingest.start()
    log.info("banco em %s; dashboard em http://127.0.0.1:8000", settings.db_path)
    yield
    ingest.stop()
    task.cancel()


app = FastAPI(title="ESP32 Wi-Fi IDS", lifespan=lifespan)


def _device_view(d: dict) -> dict:
    """Estado efetivo: offline por Last Will ou por falta de contato além do limite."""
    now = datetime.now(timezone.utc)
    age = None
    if d.get("last_seen_at"):
        seen = datetime.fromisoformat(d["last_seen_at"].replace("Z", "+00:00"))
        age = round((now - seen).total_seconds(), 1)
    effective = d["state"]
    if effective == "online" and (age is None or age > settings.offline_after_s):
        effective = "offline"
    return {**d, "seconds_since_seen": age, "effective_state": effective,
            "synthetic": d["device_id"].startswith("synthetic")}


@app.get("/api/health")
def health() -> dict:
    return {
        "mqtt_connected": ingest.connected,
        "mqtt_broker": f"{settings.mqtt_host}:{settings.mqtt_port}",
        "ingest": ingest.stats,
        "last_error": ingest.last_error,
        "db": {"path": str(settings.db_path), **db.counts()},
        "offline_after_s": settings.offline_after_s,
    }


@app.get("/api/devices")
def devices() -> list[dict]:
    return [_device_view(d) for d in db.devices()]


@app.get("/api/stats")
def stats(device_id: str | None = None, minutes: int = Query(60, ge=1, le=60 * 24 * 30),
          until: str | None = None, limit: int = Query(5000, ge=1, le=50000)) -> list[dict]:
    return db.query_stats(device_id, since_iso(minutes), until, limit)


@app.get("/api/alerts")
def alerts(device_id: str | None = None, minutes: int = Query(60, ge=1, le=60 * 24 * 30),
           until: str | None = None, source: str | None = Query(None, pattern="^(live|synthetic)$"),
           limit: int = Query(500, ge=1, le=5000)) -> list[dict]:
    return db.query_alerts(device_id, since_iso(minutes), until, source, limit)


@app.get("/api/status-events")
def status_events(device_id: str | None = None, minutes: int = Query(60 * 24, ge=1, le=60 * 24 * 30),
                  limit: int = Query(200, ge=1, le=2000)) -> list[dict]:
    return db.status_events(device_id, since_iso(minutes), limit)


@app.websocket("/ws")
async def websocket(ws: WebSocket) -> None:
    await ws.accept()
    hub.clients.add(ws)
    await ws.send_json({"kind": "backend", "data": {"mqtt_connected": ingest.connected}})
    try:
        while True:
            await ws.receive_text()  # o navegador não manda nada; só mantém a conexão
    except WebSocketDisconnect:
        pass
    finally:
        hub.clients.discard(ws)


# Dashboard estático por último, para não esconder as rotas /api e /ws.
app.mount("/", StaticFiles(directory=FRONTEND_DIR, html=True), name="frontend")
