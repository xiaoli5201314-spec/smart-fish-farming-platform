"""Pure protocol and state logic for the ESP32-S3 water gateway simulator."""

from __future__ import annotations

import json
import random
import uuid
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any


PONDS = ("pond_1", "pond_2", "pond_3")
SUPPORTED_PUBLISH_INTERVAL_SECONDS = (2, 60, 300, 1800, 3600, 21600, 86400)
DEFAULT_PUBLISH_INTERVAL_SECONDS = 3600
DEFAULT_SCHEDULE_START = "08:00"
DEFAULT_SCHEDULE_END = "18:00"


@dataclass(frozen=True)
class TopicSet:
    latest: str
    status: str
    command: str
    control_status: str
    config: str


def topics_for(pond_id: str) -> TopicSet:
    if pond_id not in PONDS:
        raise ValueError(f"unsupported pond: {pond_id}")
    prefix = f"fish/pond/{pond_id}/water"
    return TopicSet(
        latest=f"{prefix}/latest",
        status=f"{prefix}/status",
        command=f"{prefix}/cmd",
        control_status=f"{prefix}/control/status",
        config=f"{prefix}/config",
    )


@dataclass
class SimulatorState:
    pond_id: str
    mode: str = "run"
    collecting: bool = True
    schedule_start: str = DEFAULT_SCHEDULE_START
    schedule_end: str = DEFAULT_SCHEDULE_END
    time_synced: bool = True
    time_text: str = ""
    publish_interval_seconds: int = DEFAULT_PUBLISH_INTERVAL_SECONDS
    sensor_mode: str = "fixed"
    ph: float = 7.80
    do_mg_l: float = 5.06
    do_saturation: float = 67.7
    water_temp: float = 30.00
    chip_temp_c: float = 47.7
    ph_online: bool = True
    do_online: bool = True
    poll_cycles: int = 0
    published: int = 0
    failed: int = 0
    last_publish_at: str = ""
    client_id: str = field(default="", init=False)
    gateway_id: str = field(default="", init=False)

    def __post_init__(self) -> None:
        if self.pond_id not in PONDS:
            raise ValueError(f"unsupported pond: {self.pond_id}")
        self.gateway_id = f"water_gateway_{self.pond_id}"
        self.client_id = f"fish-simulator-{self.pond_id}-{uuid.uuid4().hex[:10]}"
        self.sensor_mode = normalize_sensor_mode(self.sensor_mode)


def normalize_sensor_mode(value: Any) -> str:
    value = str(value or "fixed").strip().lower()
    aliases = {"normal": "fixed", "random": "random", "zero": "zero", "offline": "offline"}
    return aliases.get(value, "fixed")


def _online_values(state: SimulatorState) -> tuple[float, float, float, float, float, bool, bool]:
    mode = normalize_sensor_mode(state.sensor_mode)
    if mode == "offline":
        return 0.0, 0.0, 0.0, 0.0, state.chip_temp_c, False, False
    if mode == "zero":
        return 0.0, 0.0, 0.0, 0.0, 0.0, True, True
    return (
        state.ph if state.ph_online else 0.0,
        state.do_mg_l if state.do_online else 0.0,
        state.do_saturation if state.do_online else 0.0,
        state.water_temp if state.do_online else 0.0,
        state.chip_temp_c,
        bool(state.ph_online),
        bool(state.do_online),
    )


def next_reading(state: SimulatorState, rng: random.Random | None = None) -> None:
    """Advance the virtual polling counter and optional small sensor drift."""
    state.poll_cycles += 1
    if normalize_sensor_mode(state.sensor_mode) != "random":
        return
    rng = rng or random
    state.ph = round(max(0.0, min(14.0, state.ph + rng.uniform(-0.04, 0.04))), 2)
    state.do_mg_l = round(max(0.0, min(20.0, state.do_mg_l + rng.uniform(-0.12, 0.12))), 2)
    state.do_saturation = round(max(0.0, min(200.0, state.do_saturation + rng.uniform(-1.5, 1.5))), 1)
    state.water_temp = round(max(-5.0, min(50.0, state.water_temp + rng.uniform(-0.08, 0.08))), 2)
    state.chip_temp_c = round(max(20.0, min(85.0, state.chip_temp_c + rng.uniform(-0.15, 0.15))), 1)


def build_latest_payload(state: SimulatorState, now_text: str | None = None) -> dict[str, Any]:
    ph, do_mg_l, saturation, water_temp, chip_temp, ph_online, do_online = _online_values(state)
    online_sensors = int(ph_online) + int(do_online)
    return {
        "pondId": state.pond_id,
        "gatewayId": state.gateway_id,
        "sampledAt": now_text or state.time_text,
        "timeSynced": bool(state.time_synced),
        "ph": round(ph, 2),
        "dissolvedOxygen": round(do_mg_l, 2),
        "doMgL": round(do_mg_l, 2),
        "doSaturation": round(saturation, 1),
        "waterTemp": round(water_temp, 2),
        "chipTempC": round(chip_temp, 1),
        "phOnline": ph_online,
        "doOnline": do_online,
        "onlineSensors": online_sensors,
        "pollCycles": state.poll_cycles,
    }


def build_status_payload(
    state: SimulatorState,
    online: bool,
    broker: str,
    ip: str = "",
) -> dict[str, Any]:
    return {
        "ok": True,
        "online": bool(online),
        "pondId": state.pond_id,
        "gatewayId": state.gateway_id,
        "ip": ip,
        "broker": broker,
        "latestTopic": topics_for(state.pond_id).latest,
        "published": state.published,
        "failed": state.failed,
    }


def build_control_status(state: SimulatorState) -> dict[str, Any]:
    topics = topics_for(state.pond_id)
    return {
        "ok": True,
        "pondId": state.pond_id,
        "gatewayId": state.gateway_id,
        "mode": state.mode,
        "collecting": bool(state.collecting),
        "scheduleStart": state.schedule_start,
        "scheduleEnd": state.schedule_end,
        "timeSynced": bool(state.time_synced),
        "timeText": state.time_text,
        "publishIntervalSeconds": state.publish_interval_seconds,
        "commandTopic": topics.command,
        "statusTopic": topics.control_status,
    }


def _parse_minutes(value: Any) -> int | None:
    try:
        parsed = datetime.strptime(str(value), "%H:%M")
    except (TypeError, ValueError):
        return None
    return parsed.hour * 60 + parsed.minute


def apply_command(state: SimulatorState, command: dict[str, Any]) -> bool:
    mode = str(command.get("mode") or "").strip().lower()
    if not mode:
        collecting = command.get("collecting")
        if collecting is None:
            return False
        mode = "run" if bool(collecting) else "pause"
    if mode not in {"run", "pause", "schedule"}:
        return False

    if mode == "schedule":
        start = str(command.get("scheduleStart") or "")
        end = str(command.get("scheduleEnd") or "")
        if _parse_minutes(start) is None or _parse_minutes(end) is None:
            return False
        state.schedule_start = start
        state.schedule_end = end

    state.mode = mode
    state.collecting = mode != "pause"
    return True


def apply_publish_config(state: SimulatorState, config: dict[str, Any]) -> bool:
    try:
        seconds = int(config.get("publishIntervalSeconds"))
    except (TypeError, ValueError):
        return False
    if seconds not in SUPPORTED_PUBLISH_INTERVAL_SECONDS:
        return False
    state.publish_interval_seconds = seconds
    return True


def should_collect(state: SimulatorState, now_text: str) -> bool:
    if state.mode == "run":
        return True
    if state.mode == "pause":
        return False
    try:
        current = datetime.strptime(now_text[-8:-3], "%H:%M")
    except (TypeError, ValueError):
        return bool(state.collecting)
    current_minutes = current.hour * 60 + current.minute
    start = _parse_minutes(state.schedule_start)
    end = _parse_minutes(state.schedule_end)
    if start is None or end is None:
        return False
    if start == end:
        return True
    if start < end:
        return start <= current_minutes < end
    return current_minutes >= start or current_minutes < end


def json_payload(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
