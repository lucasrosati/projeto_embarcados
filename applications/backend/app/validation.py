"""Validação do contrato de dados schema_version 2 (spec §7). Payload inválido é descartado e contado."""
import re
from typing import Any

MAC_RE = re.compile(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5}$")
TOPIC_RE = re.compile(r"^ids/([A-Za-z0-9_-]{1,32})/(stats|alerts|status)$")
SOURCES = {"live", "synthetic"}
RULES = {"DEAUTH_BURST": "high", "DISASSOC_BURST": "medium"}
COUNT_KEYS = ("beacon", "probe_req", "deauth", "disassoc")
STATS_OPTIONAL_INTS = (
    "rssi",
    "capture_queue_drops",
    "alert_queue_drops",
    "alerts_suppressed",
    "mqtt_enqueue_failures",
    "wifi_reconnects",
    "synthetic_injected",
)
TOPIC_TYPE = {"stats": "stats", "alerts": "alert", "status": "status"}


class InvalidPayload(ValueError):
    pass


def _is_int(v: Any) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def _require(data: dict, key: str, check, what: str) -> Any:
    if key not in data or not check(data[key]):
        raise InvalidPayload(f"campo '{key}' ausente ou inválido (esperado {what})")
    return data[key]


def _str(max_len: int = 64):
    return lambda v: isinstance(v, str) and 0 < len(v) <= max_len


def parse_topic(topic: str) -> tuple[str, str]:
    m = TOPIC_RE.match(topic)
    if not m:
        raise InvalidPayload(f"tópico fora do padrão ids/<device_id>/<stats|alerts|status>: {topic}")
    return m.group(1), m.group(2)


def validate(topic: str, data: Any) -> tuple[str, dict]:
    """Devolve (tipo, payload) se o JSON respeita o contrato; senão levanta InvalidPayload."""
    device_id, kind = parse_topic(topic)
    if not isinstance(data, dict):
        raise InvalidPayload("payload não é um objeto JSON")
    if data.get("schema_version") != 2:
        raise InvalidPayload("schema_version diferente de 2")
    expected_type = TOPIC_TYPE[kind]
    if data.get("type") != expected_type:
        raise InvalidPayload(f"type '{data.get('type')}' não corresponde ao tópico ({expected_type})")
    if data.get("device_id") != device_id:
        raise InvalidPayload("device_id do payload diferente do tópico")
    _require(data, "boot_id", _str(), "texto")

    if expected_type == "stats":
        for key in ("seq", "uptime_ms", "channel", "window_s"):
            _require(data, key, _is_int, "inteiro")
        counts = _require(data, "counts", lambda v: isinstance(v, dict), "objeto")
        for key in COUNT_KEYS:
            _require(counts, key, lambda v: _is_int(v) and v >= 0, "inteiro >= 0")
        for key in STATS_OPTIONAL_INTS:
            if key in data and not _is_int(data[key]):
                raise InvalidPayload(f"campo '{key}' deveria ser inteiro")
        _require(data, "source", lambda v: v in SOURCES, "live|synthetic")
    elif expected_type == "alert":
        _require(data, "event_id", _str(96), "texto")
        for key in ("uptime_ms", "channel", "count", "window_s"):
            _require(data, key, _is_int, "inteiro")
        rule = _require(data, "rule", lambda v: v in RULES, "DEAUTH_BURST|DISASSOC_BURST")
        _require(data, "severity", lambda v: v == RULES[rule], f"severidade da regra ({RULES[rule]})")
        _require(data, "bssid", lambda v: isinstance(v, str) and MAC_RE.match(v), "MAC aa:bb:cc:dd:ee:ff")
        _require(data, "source", lambda v: v in SOURCES, "live|synthetic")
    else:  # status
        _require(data, "state", lambda v: v in ("online", "offline"), "online|offline")
    return expected_type, data
