"""Persistência em SQLite. Alertas são deduplicados por event_id (QoS 1 pode entregar duas vezes)."""
import json
import sqlite3
import threading
from datetime import datetime, timedelta, timezone
from pathlib import Path

SCHEMA = """
CREATE TABLE IF NOT EXISTS stats (
    id INTEGER PRIMARY KEY,
    device_id TEXT NOT NULL,
    boot_id TEXT NOT NULL,
    seq INTEGER NOT NULL,
    received_at TEXT NOT NULL,
    uptime_ms INTEGER,
    channel INTEGER,
    rssi INTEGER,
    window_s INTEGER,
    beacon INTEGER, probe_req INTEGER, deauth INTEGER, disassoc INTEGER,
    capture_queue_drops INTEGER, alert_queue_drops INTEGER, alerts_suppressed INTEGER,
    mqtt_enqueue_failures INTEGER, wifi_reconnects INTEGER, synthetic_injected INTEGER,
    source TEXT NOT NULL,
    payload TEXT NOT NULL,
    UNIQUE (device_id, boot_id, seq)
);
CREATE INDEX IF NOT EXISTS stats_received ON stats (device_id, received_at);

CREATE TABLE IF NOT EXISTS alerts (
    id INTEGER PRIMARY KEY,
    event_id TEXT NOT NULL UNIQUE,
    device_id TEXT NOT NULL,
    boot_id TEXT NOT NULL,
    received_at TEXT NOT NULL,
    uptime_ms INTEGER,
    channel INTEGER,
    rule TEXT NOT NULL,
    severity TEXT NOT NULL,
    bssid TEXT NOT NULL,
    count INTEGER,
    window_s INTEGER,
    source TEXT NOT NULL,
    payload TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS alerts_received ON alerts (received_at);

-- Última notícia de cada dispositivo (status atual + último contato).
CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    state TEXT NOT NULL,
    boot_id TEXT,
    last_seen_at TEXT,
    last_status_at TEXT,
    last_status TEXT
);

-- Mudanças de estado (online/offline, novo boot), não cada heartbeat.
CREATE TABLE IF NOT EXISTS status_events (
    id INTEGER PRIMARY KEY,
    device_id TEXT NOT NULL,
    boot_id TEXT,
    state TEXT NOT NULL,
    received_at TEXT NOT NULL,
    payload TEXT NOT NULL
);
"""

STATS_COLUMNS = (
    "capture_queue_drops",
    "alert_queue_drops",
    "alerts_suppressed",
    "mqtt_enqueue_failures",
    "wifi_reconnects",
    "synthetic_injected",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def since_iso(minutes: int) -> str:
    t = datetime.now(timezone.utc) - timedelta(minutes=minutes)
    return t.isoformat(timespec="milliseconds").replace("+00:00", "Z")


class Database:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        # Uma conexão compartilhada entre a thread do MQTT e as rotas da API, protegida por lock.
        self._conn = sqlite3.connect(path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.executescript(SCHEMA)
        self._lock = threading.Lock()
        self.path = path

    # ---- escrita (thread do MQTT) ----

    def insert_stats(self, p: dict, received_at: str) -> bool:
        c = p["counts"]
        row = (
            p["device_id"], p["boot_id"], p["seq"], received_at, p["uptime_ms"], p["channel"], p.get("rssi"),
            p["window_s"], c["beacon"], c["probe_req"], c["deauth"], c["disassoc"],
            *(p.get(k) for k in STATS_COLUMNS), p["source"], json.dumps(p, separators=(",", ":")),
        )
        with self._lock, self._conn:
            cur = self._conn.execute(
                "INSERT OR IGNORE INTO stats (device_id, boot_id, seq, received_at, uptime_ms, channel, rssi,"
                " window_s, beacon, probe_req, deauth, disassoc, " + ", ".join(STATS_COLUMNS) +
                ", source, payload) VALUES (" + ",".join("?" * len(row)) + ")",
                row,
            )
            return cur.rowcount == 1

    def insert_alert(self, p: dict, received_at: str) -> bool:
        """False se o event_id já existia (duplicata de QoS 1)."""
        with self._lock, self._conn:
            cur = self._conn.execute(
                "INSERT OR IGNORE INTO alerts (event_id, device_id, boot_id, received_at, uptime_ms, channel, rule,"
                " severity, bssid, count, window_s, source, payload) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (
                    p["event_id"], p["device_id"], p["boot_id"], received_at, p["uptime_ms"], p["channel"],
                    p["rule"], p["severity"], p["bssid"], p["count"], p["window_s"], p["source"],
                    json.dumps(p, separators=(",", ":")),
                ),
            )
            return cur.rowcount == 1

    def touch_device(self, device_id: str, boot_id: str, seen_at: str) -> None:
        with self._lock, self._conn:
            self._conn.execute(
                "INSERT INTO devices (device_id, state, boot_id, last_seen_at) VALUES (?, 'online', ?, ?)"
                " ON CONFLICT(device_id) DO UPDATE SET last_seen_at = excluded.last_seen_at,"
                " boot_id = excluded.boot_id, state = 'online'",
                (device_id, boot_id, seen_at),
            )

    def update_status(self, p: dict, received_at: str, retained: bool) -> bool:
        """Grava o status atual. Devolve True se o estado ou o boot mudou (vira um status_event)."""
        payload = json.dumps(p, separators=(",", ":"))
        with self._lock, self._conn:
            prev = self._conn.execute(
                "SELECT state, boot_id FROM devices WHERE device_id = ?", (p["device_id"],)
            ).fetchone()
            changed = prev is None or prev["state"] != p["state"] or prev["boot_id"] != p["boot_id"]
            # Mensagem retida é antiga: não conta como contato recente.
            seen = None if retained else received_at
            self._conn.execute(
                "INSERT INTO devices (device_id, state, boot_id, last_seen_at, last_status_at, last_status)"
                " VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(device_id) DO UPDATE SET state = excluded.state,"
                " boot_id = excluded.boot_id, last_status = excluded.last_status,"
                " last_status_at = COALESCE(excluded.last_status_at, devices.last_status_at),"
                " last_seen_at = COALESCE(excluded.last_seen_at, devices.last_seen_at)",
                (p["device_id"], p["state"], p["boot_id"], seen, seen, payload),
            )
            if changed:
                self._conn.execute(
                    "INSERT INTO status_events (device_id, boot_id, state, received_at, payload) VALUES (?,?,?,?,?)",
                    (p["device_id"], p["boot_id"], p["state"], received_at, payload),
                )
            return changed

    # ---- leitura (API) ----

    def query_stats(self, device_id: str | None, since: str, until: str | None, limit: int) -> list[dict]:
        sql = "SELECT * FROM stats WHERE received_at >= ?"
        args: list = [since]
        if until:
            sql += " AND received_at <= ?"
            args.append(until)
        if device_id:
            sql += " AND device_id = ?"
            args.append(device_id)
        sql += " ORDER BY received_at DESC LIMIT ?"
        args.append(limit)
        with self._lock:
            rows = self._conn.execute(sql, args).fetchall()
        return [self._stats_row(r) for r in reversed(rows)]

    def query_alerts(self, device_id: str | None, since: str, until: str | None, source: str | None,
                     limit: int) -> list[dict]:
        sql = "SELECT * FROM alerts WHERE received_at >= ?"
        args: list = [since]
        if until:
            sql += " AND received_at <= ?"
            args.append(until)
        if device_id:
            sql += " AND device_id = ?"
            args.append(device_id)
        if source:
            sql += " AND source = ?"
            args.append(source)
        sql += " ORDER BY received_at DESC LIMIT ?"
        args.append(limit)
        with self._lock:
            rows = self._conn.execute(sql, args).fetchall()
        return [{k: r[k] for k in r.keys() if k != "payload"} for r in rows]

    def devices(self) -> list[dict]:
        with self._lock:
            rows = self._conn.execute("SELECT * FROM devices ORDER BY device_id").fetchall()
        out = []
        for r in rows:
            d = {k: r[k] for k in r.keys() if k != "last_status"}
            d["last_status"] = json.loads(r["last_status"]) if r["last_status"] else None
            out.append(d)
        return out

    def status_events(self, device_id: str | None, since: str, limit: int) -> list[dict]:
        sql = "SELECT device_id, boot_id, state, received_at FROM status_events WHERE received_at >= ?"
        args: list = [since]
        if device_id:
            sql += " AND device_id = ?"
            args.append(device_id)
        sql += " ORDER BY received_at DESC LIMIT ?"
        args.append(limit)
        with self._lock:
            return [dict(r) for r in self._conn.execute(sql, args).fetchall()]

    def counts(self) -> dict:
        with self._lock:
            return {
                t: self._conn.execute(f"SELECT COUNT(*) FROM {t}").fetchone()[0]
                for t in ("stats", "alerts", "status_events", "devices")
            }

    @staticmethod
    def _stats_row(r: sqlite3.Row) -> dict:
        d = {k: r[k] for k in r.keys() if k not in ("payload", "beacon", "probe_req", "deauth", "disassoc")}
        d["counts"] = {"beacon": r["beacon"], "probe_req": r["probe_req"], "deauth": r["deauth"],
                       "disassoc": r["disassoc"]}
        return d
