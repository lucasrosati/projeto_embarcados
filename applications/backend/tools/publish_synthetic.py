#!/usr/bin/env python3
"""Publica dados SINTÉTICOS no broker para testar back-end e dashboard sem a placa (spec §8).

Usa o device_id "synthetic-01" e source="synthetic", então nunca se mistura com os dados reais do ESP32.
Credenciais do usuário de teste vêm de applications/backend/.env (MQTT_TESTER_USER/PASSWORD).

Uso (de applications/backend):  .venv/bin/python tools/publish_synthetic.py --windows 30 --interval 2
"""
import argparse
import json
import os
import random
import sys
import time
import uuid
from pathlib import Path

import paho.mqtt.client as mqtt

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from app.config import settings  # noqa: E402  (carrega o .env)

DEVICE = "synthetic-01"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--windows", type=int, default=30, help="quantas janelas de stats publicar")
    ap.add_argument("--interval", type=float, default=2.0, help="segundos entre janelas (o ESP32 usa 10)")
    ap.add_argument("--alert-every", type=int, default=10, help="uma rajada de deauth a cada N janelas (0 = nunca)")
    ap.add_argument("--duplicate-alert", action="store_true", help="reenvia cada alerta (testa a deduplicação)")
    args = ap.parse_args()

    user, password = os.environ.get("MQTT_TESTER_USER"), os.environ.get("MQTT_TESTER_PASSWORD")
    if not user:
        sys.exit("Sem MQTT_TESTER_USER no .env: rode applications/broker/setup_credentials.py")

    boot_id = uuid.uuid4().hex[:8]
    base = f"ids/{DEVICE}"
    offline = json.dumps({"schema_version": 2, "type": "status", "device_id": DEVICE, "boot_id": boot_id,
                          "state": "offline"})
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"tester-{boot_id}")
    client.username_pw_set(user, password)
    client.will_set(f"{base}/status", offline, qos=1, retain=True)
    client.connect(settings.mqtt_host, settings.mqtt_port, keepalive=15)
    client.loop_start()

    def pub(sub: str, payload: dict, qos: int, retain: bool = False) -> None:
        client.publish(f"{base}/{sub}", json.dumps(payload, separators=(",", ":")), qos=qos, retain=retain).wait_for_publish(5)

    uptime = 0
    pub("status", {"schema_version": 2, "type": "status", "device_id": DEVICE, "boot_id": boot_id, "state": "online",
                   "uptime_ms": 0, "wifi_connected": True, "sniffer_active": True, "mqtt_connected": True,
                   "channel": 6, "rssi": -50}, qos=1, retain=True)
    print(f"publicando {args.windows} janelas sintéticas em {base}/... (boot {boot_id})")
    event = 0
    for seq in range(1, args.windows + 1):
        uptime += 10_000
        burst = args.alert_every and seq % args.alert_every == 0
        deauth = random.randint(22, 40) if burst else random.choice([0, 0, 0, 0, 1, 2])
        pub("stats", {
            "schema_version": 2, "type": "stats", "device_id": DEVICE, "boot_id": boot_id, "seq": seq,
            "uptime_ms": uptime, "channel": 6, "rssi": random.randint(-60, -40), "window_s": 10,
            "counts": {"beacon": random.randint(560, 640), "probe_req": random.randint(5, 40), "deauth": deauth,
                       "disassoc": random.choice([0, 0, 0, 1])},
            "capture_queue_drops": 0, "alert_queue_drops": 0, "alerts_suppressed": 0, "mqtt_enqueue_failures": 0,
            "wifi_reconnects": 0, "synthetic_injected": 0, "source": "synthetic",
        }, qos=0)
        if burst:
            event += 1
            alert = {"schema_version": 2, "type": "alert", "device_id": DEVICE, "event_id": f"{boot_id}-evt-{event:03d}",
                     "boot_id": boot_id, "uptime_ms": uptime, "channel": 6, "rule": "DEAUTH_BURST",
                     "severity": "high", "bssid": "02:00:00:00:00:01", "count": 20, "window_s": 10,
                     "source": "synthetic"}
            pub("alerts", alert, qos=1)
            if args.duplicate_alert:
                pub("alerts", alert, qos=1)
            print(f"  janela {seq}: alerta {alert['event_id']}" + (" (enviado 2x)" if args.duplicate_alert else ""))
        if seq % 3 == 0:
            pub("status", {"schema_version": 2, "type": "status", "device_id": DEVICE, "boot_id": boot_id,
                           "state": "online", "uptime_ms": uptime, "wifi_connected": True, "sniffer_active": True,
                           "mqtt_connected": True, "channel": 6, "rssi": -50}, qos=1, retain=True)
        time.sleep(args.interval)

    pub("status", json.loads(offline), qos=1, retain=True)
    client.loop_stop()
    client.disconnect()
    print("fim: status offline publicado")


if __name__ == "__main__":
    main()
