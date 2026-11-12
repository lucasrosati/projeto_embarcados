#!/bin/sh
# Sobe o Mosquitto com a configuração do projeto (Ctrl+C para parar).
set -e
cd "$(dirname "$0")"
if [ ! -f passwd ]; then
    echo "Falta o arquivo passwd. Rode antes: python3 setup_credentials.py" >&2
    exit 1
fi
chmod 600 acl passwd  # o Mosquitto 2.1 recusa arquivos de senha/ACL legíveis por outros usuários
MOSQUITTO="$(command -v mosquitto || echo /opt/homebrew/sbin/mosquitto)"
echo "IPs deste computador (use um deles no menuconfig do ESP32, mqtt://IP:1883):"
ifconfig 2>/dev/null | awk '/inet / && $2 != "127.0.0.1" {print "  " $2}' || true
exec "$MOSQUITTO" -c mosquitto.conf
