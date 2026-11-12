"""Configuração do back-end, lida do ambiente e de applications/backend/.env (fora do Git)."""
import os
from dataclasses import dataclass
from pathlib import Path

BACKEND_DIR = Path(__file__).resolve().parents[1]
FRONTEND_DIR = BACKEND_DIR.parent / "frontend"


def _load_dotenv(path: Path) -> None:
    """Carrega KEY=VALUE simples sem sobrescrever variáveis já definidas no ambiente."""
    if not path.exists():
        return
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip())


_load_dotenv(BACKEND_DIR / ".env")


@dataclass(frozen=True)
class Settings:
    mqtt_host: str = os.environ.get("MQTT_HOST", "127.0.0.1")
    mqtt_port: int = int(os.environ.get("MQTT_PORT", "1883"))
    mqtt_user: str = os.environ.get("MQTT_USER", "")
    mqtt_password: str = os.environ.get("MQTT_PASSWORD", "")
    mqtt_topic: str = os.environ.get("MQTT_TOPIC", "ids/+/+")
    db_path: Path = Path(os.environ.get("IDS_DB_PATH", str(BACKEND_DIR / "ids.db")))
    # Sem mensagem do dispositivo por este tempo => offline (heartbeat do status a cada 30 s).
    offline_after_s: int = int(os.environ.get("IDS_OFFLINE_AFTER_S", "75"))


settings = Settings()
