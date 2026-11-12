"""Subscriber MQTT: valida, deduplica, grava no SQLite e repassa ao WebSocket (spec §8).

O paho roda numa thread própria (loop_start). Cada mensagem aceita vira um evento entregue ao loop asyncio
da API por call_soon_threadsafe.
"""
import json
import logging
import threading
from typing import Callable

import paho.mqtt.client as mqtt

from .config import Settings
from .db import Database, utc_now
from .validation import InvalidPayload, validate

log = logging.getLogger("ids.mqtt")


class MqttIngest:
    def __init__(self, settings: Settings, db: Database, emit: Callable[[dict], None]):
        self.settings = settings
        self.db = db
        self.emit = emit  # thread-safe: agenda o evento no loop da API
        self.connected = False
        self.stats = {"received": 0, "accepted": 0, "duplicates": 0, "rejected": 0}
        self.last_error: str | None = None
        self._lock = threading.Lock()
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ids-backend")
        self._client.username_pw_set(settings.mqtt_user, settings.mqtt_password)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message
        self._client.reconnect_delay_set(min_delay=1, max_delay=10)

    def start(self) -> None:
        if not self.settings.mqtt_user:
            log.error("MQTT_USER vazio: rode applications/broker/setup_credentials.py para gerar o .env")
        self._client.connect_async(self.settings.mqtt_host, self.settings.mqtt_port, keepalive=30)
        self._client.loop_start()

    def stop(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()

    # ---- callbacks (thread do paho) ----

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            self.connected = False
            self.last_error = f"broker recusou: {reason_code}"
            log.error("conexão MQTT recusada: %s (confira usuário/senha no .env)", reason_code)
            return
        self.connected = True
        self.last_error = None
        client.subscribe(self.settings.mqtt_topic, qos=1)
        log.info("conectado a %s:%s, assinando %s", self.settings.mqtt_host, self.settings.mqtt_port,
                 self.settings.mqtt_topic)
        self.emit({"kind": "backend", "data": {"mqtt_connected": True}})

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        self.connected = False
        log.warning("desconectado do broker (%s); tentando de novo", reason_code)
        self.emit({"kind": "backend", "data": {"mqtt_connected": False}})

    def _count(self, key: str) -> None:
        with self._lock:
            self.stats[key] += 1

    def _on_message(self, client, userdata, msg: mqtt.MQTTMessage):
        self._count("received")
        received_at = utc_now()
        try:
            data = json.loads(msg.payload.decode("utf-8"))
            kind, payload = validate(msg.topic, data)
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            self._reject(msg.topic, f"JSON inválido: {e}")
            return
        except InvalidPayload as e:
            self._reject(msg.topic, str(e))
            return

        try:
            if kind == "stats":
                if not self.db.insert_stats(payload, received_at):
                    self._count("duplicates")
                    return
                self.db.touch_device(payload["device_id"], payload["boot_id"], received_at)
            elif kind == "alert":
                if not self.db.insert_alert(payload, received_at):
                    self._count("duplicates")
                    log.info("alerta duplicado ignorado (event_id=%s)", payload["event_id"])
                    return
                self.db.touch_device(payload["device_id"], payload["boot_id"], received_at)
                log.warning("ALERTA %s %s bssid=%s count=%s source=%s", payload["rule"], payload["severity"],
                            payload["bssid"], payload["count"], payload["source"])
            else:
                changed = self.db.update_status(payload, received_at, retained=msg.retain)
                if changed:
                    log.info("%s agora está %s (boot %s)", payload["device_id"], payload["state"], payload["boot_id"])
        except Exception:  # erro de banco não pode derrubar a thread do MQTT
            log.exception("falha ao gravar mensagem de %s", msg.topic)
            return

        self._count("accepted")
        self.emit({"kind": kind, "received_at": received_at, "retained": bool(msg.retain), "data": payload})

    def _reject(self, topic: str, reason: str) -> None:
        self._count("rejected")
        self.last_error = f"{topic}: {reason}"
        log.warning("payload rejeitado em %s: %s", topic, reason)
