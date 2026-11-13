#!/usr/bin/env python3
"""Gera as credenciais MQTT do projeto e as grava só em arquivos fora do Git.

- applications/broker/passwd         (hash das senhas, lido pelo Mosquitto)
- applications/backend/.env          (usuário/senha do back-end e da ferramenta de teste)
- esp32-esp8266/sdkconfig.esp32dev   (URI, usuário e senha do ESP32; e sdkconfig, se existir)

Nenhuma senha é impressa. Rodar de novo gera senhas novas: regrave o ESP32 e reinicie broker e back-end depois.

Uso: python3 setup_credentials.py [--broker-ip 172.20.10.3]
"""
import argparse
import re
import secrets
import shutil
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BROKER_DIR = ROOT / "applications" / "broker"
BACKEND_ENV = ROOT / "applications" / "backend" / ".env"
FIRMWARE_DIR = ROOT / "esp32-esp8266"

USERS = {"esp32": "esp32-ids-01", "backend": "ids-backend", "tester": "ids-tester"}


def lan_ip() -> str:
    """IP da interface usada para sair para a rede (não envia nada: UDP sem conexão real)."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]


def find_mosquitto_passwd() -> str:
    exe = shutil.which("mosquitto_passwd") or "/opt/homebrew/bin/mosquitto_passwd"
    if not Path(exe).exists():
        sys.exit("mosquitto_passwd não encontrado. Instale o Mosquitto: brew install mosquitto")
    return exe


def set_kconfig(path: Path, key: str, value: str) -> None:
    text = path.read_text()
    line = f'{key}="{value}"'
    pattern = re.compile(rf"^(# )?{key}(=.*| is not set)$", re.M)
    text = pattern.sub(lambda _: line, text) if pattern.search(text) else text.rstrip("\n") + "\n" + line + "\n"
    path.write_text(text)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--broker-ip", help="IP do PC na rede do ESP32 (padrão: detectado)")
    args = ap.parse_args()

    broker_ip = args.broker_ip or lan_ip()
    passwords = {role: secrets.token_urlsafe(18) for role in USERS}
    exe = find_mosquitto_passwd()

    passwd = BROKER_DIR / "passwd"
    passwd.unlink(missing_ok=True)
    passwd.touch(mode=0o600)
    for role, user in USERS.items():
        subprocess.run([exe, "-b", str(passwd), user, passwords[role]], check=True, capture_output=True)
    passwd.chmod(0o600)

    BACKEND_ENV.write_text(
        "# Gerado por applications/broker/setup_credentials.py. Fora do Git.\n"
        "MQTT_HOST=127.0.0.1\n"
        "MQTT_PORT=1883\n"
        f"MQTT_USER={USERS['backend']}\n"
        f"MQTT_PASSWORD={passwords['backend']}\n"
        f"MQTT_TESTER_USER={USERS['tester']}\n"
        f"MQTT_TESTER_PASSWORD={passwords['tester']}\n"
    )
    BACKEND_ENV.chmod(0o600)

    patched = []
    for name in ("sdkconfig.esp32dev", "sdkconfig"):
        cfg = FIRMWARE_DIR / name
        if cfg.exists():
            set_kconfig(cfg, "CONFIG_IDS_MQTT_URI", f"mqtt://{broker_ip}:1883")
            set_kconfig(cfg, "CONFIG_IDS_MQTT_USER", USERS["esp32"])
            set_kconfig(cfg, "CONFIG_IDS_MQTT_PASSWORD", passwords["esp32"])
            patched.append(name)

    print(f"Broker: mqtt://{broker_ip}:1883")
    print(f"Usuários criados em {passwd.relative_to(ROOT)}: {', '.join(USERS.values())}")
    print(f"Credenciais do back-end em {BACKEND_ENV.relative_to(ROOT)}")
    if patched:
        print(f"Firmware configurado em esp32-esp8266/{', '.join(patched)}.")
    else:
        print("Nenhum sdkconfig encontrado: compile o firmware uma vez e rode este script de novo.")

    print("\nAs senhas mudaram. Agora, nesta ordem:")
    print("  1. Reinicie o broker:   Ctrl+C no terminal dele e applications/broker/start_broker.sh")
    print("  2. Reinicie o back-end: Ctrl+C e .venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000")
    print("  3. Grave o ESP32:       pio run -t upload --upload-port /dev/cu.usbserial-XXXX")


if __name__ == "__main__":
    main()
