"""Standalone local dashboard and MQTT workers for three simulated gateways."""

from __future__ import annotations

import json
import math
import os
import sys
import threading
import time
import webbrowser
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

from flask import Flask, jsonify, request, send_from_directory
from paho.mqtt import client as mqtt

from simulator_core import (
    DEFAULT_PUBLISH_INTERVAL_SECONDS,
    PONDS,
    SUPPORTED_PUBLISH_INTERVAL_SECONDS,
    SimulatorState,
    apply_command,
    apply_publish_config,
    build_control_status,
    build_latest_payload,
    build_status_payload,
    json_payload,
    next_reading,
    normalize_sensor_mode,
    should_collect,
    topics_for,
)


APP_DIR = (
    Path(sys.executable).resolve().parent
    if getattr(sys, "frozen", False)
    else Path(__file__).resolve().parent
)
RESOURCE_DIR = Path(__file__).resolve().parent
CONFIG_PATH = APP_DIR / "simulator_config.json"


def load_config(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


RUNTIME_CONFIG = load_config(CONFIG_PATH)
MQTT_CONFIG = RUNTIME_CONFIG.get("mqtt") if isinstance(RUNTIME_CONFIG.get("mqtt"), dict) else {}


def config_value(env_name: str, name: str, default: Any) -> Any:
    value = os.environ.get(env_name)
    if value not in (None, ""):
        return value
    return MQTT_CONFIG.get(name, RUNTIME_CONFIG.get(name, default))


def config_int(env_name: str, name: str, default: int) -> int:
    try:
        return int(config_value(env_name, name, default))
    except (TypeError, ValueError):
        return default


MQTT_HOST = str(config_value("FISH_MQTT_HOST", "host", "127.0.0.1"))
MQTT_PORT = config_int("FISH_MQTT_PORT", "port", 1883)
MQTT_USERNAME = str(config_value("FISH_MQTT_USERNAME", "username", ""))
MQTT_PASSWORD = str(config_value("FISH_MQTT_PASSWORD", "password", ""))
MQTT_CLIENT_PREFIX = str(config_value("FISH_MQTT_CLIENT_PREFIX", "clientPrefix", "fish-simulator"))
WEB_PORT = config_int("FISH_SIMULATOR_PORT", "webPort", 5057)
BROKER_TEXT = f"{MQTT_HOST}:{MQTT_PORT}"

app = Flask(__name__, static_folder=str(RESOURCE_DIR / "static"), static_url_path="")


def now_text() -> str:
    return datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S")


def reason_code_success(reason_code: Any) -> bool:
    value = getattr(reason_code, "value", reason_code)
    try:
        return int(value) == 0
    except (TypeError, ValueError):
        return str(reason_code).lower() in {"success", "normal disconnection"}


def publish_succeeded(result: Any) -> bool:
    if isinstance(result, bool):
        return result
    return getattr(result, "rc", mqtt.MQTT_ERR_SUCCESS) == mqtt.MQTT_ERR_SUCCESS


def create_mqtt_client(client_id: str) -> mqtt.Client:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.reconnect_delay_set(min_delay=2, max_delay=30)
    return client


class PondWorker:
    def __init__(self, pond_id: str, client_factory: Callable[[str], Any] = create_mqtt_client):
        self.state = SimulatorState(pond_id)
        self.topics = topics_for(pond_id)
        self.client_factory = client_factory
        self.state_lock = threading.RLock()
        self.stop_event = threading.Event()
        self.force_event = threading.Event()
        self.client: Any = None
        self.thread: threading.Thread | None = None
        self.running = False
        self.connected = False
        self.last_error = ""
        self.last_message_at = ""
        self._last_poll_monotonic = 0.0
        self._last_publish_monotonic = 0.0
        self._last_collecting: bool | None = None

    def start(self) -> None:
        with self.state_lock:
            if self.running:
                return
            self.stop_event.clear()
            self.force_event.clear()
            self.running = True
            self.last_error = ""
            self.client = self.client_factory(self.state.client_id)
            self.client.on_connect = self._on_connect
            self.client.on_disconnect = self._on_disconnect
            self.client.on_message = self._on_message
            offline = json_payload(build_status_payload(self.state, False, BROKER_TEXT))
            self.client.will_set(self.topics.status, offline, qos=0, retain=True)
            self.thread = threading.Thread(target=self._run, name=f"simulator-{self.state.pond_id}", daemon=True)
            self.thread.start()

    def stop(self) -> None:
        with self.state_lock:
            if not self.running:
                return
            client = self.client
            self.running = False
            self.stop_event.set()
        if client is not None:
            try:
                if self.connected:
                    self._publish(self.topics.status, json_payload(build_status_payload(self.state, False, BROKER_TEXT)))
                client.disconnect()
            except Exception as exc:
                self.last_error = str(exc)
            try:
                client.loop_stop()
            except Exception:
                pass
        thread = self.thread
        if thread and thread is not threading.current_thread():
            thread.join(timeout=3)
        with self.state_lock:
            self.connected = False
            self.client = None
            self.thread = None

    def force_publish(self) -> None:
        self.force_event.set()

    def update_settings(self, values: dict[str, Any]) -> None:
        with self.state_lock:
            if "sensorMode" in values:
                self.state.sensor_mode = normalize_sensor_mode(values["sensorMode"])
            for key, low, high in (
                ("ph", 0.0, 14.0),
                ("doMgL", 0.0, 20.0),
                ("doSaturation", 0.0, 200.0),
                ("waterTemp", -5.0, 50.0),
                ("chipTempC", 20.0, 85.0),
            ):
                if key not in values:
                    continue
                try:
                    value = float(values[key])
                except (TypeError, ValueError):
                    raise ValueError(f"invalid {key}") from None
                if not math.isfinite(value) or not low <= value <= high:
                    raise ValueError(f"invalid {key}")
                target = {
                    "ph": "ph",
                    "doMgL": "do_mg_l",
                    "doSaturation": "do_saturation",
                    "waterTemp": "water_temp",
                    "chipTempC": "chip_temp_c",
                }[key]
                setattr(self.state, target, value)
            for key, target in (("phOnline", "ph_online"), ("doOnline", "do_online")):
                if key in values:
                    setattr(self.state, target, bool(values[key]))
            if "publishIntervalSeconds" in values:
                if not apply_publish_config(self.state, values):
                    raise ValueError("unsupported publish interval")
        self.force_event.set()

    def apply_local_command(self, values: dict[str, Any]) -> None:
        with self.state_lock:
            if not apply_command(self.state, values):
                raise ValueError("invalid control command")
        self._publish_control_status()
        self.force_event.set()

    def snapshot(self) -> dict[str, Any]:
        with self.state_lock:
            current = now_text()
            latest = build_latest_payload(self.state, current)
            control = build_control_status(self.state)
            return {
                "pondId": self.state.pond_id,
                "gatewayId": self.state.gateway_id,
                "clientId": self.state.client_id,
                "running": self.running,
                "connected": self.connected,
                "lastError": self.last_error,
                "lastMessageAt": self.last_message_at,
                "lastPublishAt": self.state.last_publish_at,
                "published": self.state.published,
                "failed": self.state.failed,
                "sensorMode": self.state.sensor_mode,
                "publishIntervalSeconds": self.state.publish_interval_seconds,
                "mode": self.state.mode,
                "collecting": self.state.collecting,
                "scheduleStart": self.state.schedule_start,
                "scheduleEnd": self.state.schedule_end,
                "ph": latest["ph"],
                "doMgL": latest["doMgL"],
                "doSaturation": latest["doSaturation"],
                "waterTemp": latest["waterTemp"],
                "chipTempC": latest["chipTempC"],
                "latest": latest,
                "control": control,
            }

    def _run(self) -> None:
        try:
            self.client.connect_async(MQTT_HOST, MQTT_PORT, keepalive=30)
            self.client.loop_start()
        except Exception as exc:
            with self.state_lock:
                self.last_error = str(exc)
                self.connected = False

        while not self.stop_event.wait(0.2):
            current = now_text()
            monotonic_now = time.monotonic()
            with self.state_lock:
                self.state.time_text = current
                collecting = should_collect(self.state, current)
                if collecting != self.state.collecting and self.state.mode == "schedule":
                    self.state.collecting = collecting
                    status_changed = True
                else:
                    status_changed = False
                connected = self.connected
                should_poll = monotonic_now - self._last_poll_monotonic >= 2.0
                if should_poll:
                    self._last_poll_monotonic = monotonic_now
                    next_reading(self.state)
                forced = self.force_event.is_set()
                interval_due = monotonic_now - self._last_publish_monotonic >= self.state.publish_interval_seconds
                can_publish = collecting and (forced or self._last_publish_monotonic == 0.0 or interval_due)
            if status_changed and connected:
                self._publish_control_status()
            if connected and can_publish:
                self._publish_latest(current)
                self.force_event.clear()

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if not reason_code_success(reason_code):
            with self.state_lock:
                self.connected = False
                self.last_error = f"connect failed: {reason_code}"
            return
        for topic in (self.topics.command, self.topics.config):
            client.subscribe(topic, qos=0)
        with self.state_lock:
            self.connected = True
            self.last_error = ""
            self.state.time_text = now_text()
            self._last_publish_monotonic = 0.0
        self._publish_status(True)
        self._publish_control_status()
        self.force_event.set()

    def _on_disconnect(
        self,
        client: Any,
        userdata: Any,
        disconnect_flags: Any = None,
        reason_code: Any = None,
        properties: Any = None,
    ) -> None:
        with self.state_lock:
            self.connected = False
            if reason_code not in (None, 0):
                self.last_error = f"disconnected: {reason_code}"

    def _on_message(self, client: Any, userdata: Any, message: Any) -> None:
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except (AttributeError, UnicodeDecodeError, json.JSONDecodeError):
            return
        if not isinstance(payload, dict):
            return
        handled = False
        if message.topic == self.topics.command:
            with self.state_lock:
                handled = apply_command(self.state, payload)
            if handled:
                self._publish_control_status()
                self.force_event.set()
        elif message.topic == self.topics.config:
            with self.state_lock:
                handled = apply_publish_config(self.state, payload)
            if handled:
                self._publish_control_status()
                self.force_event.set()
        if handled:
            with self.state_lock:
                self.last_message_at = now_text()

    def _publish(self, topic: str, payload: str) -> bool:
        try:
            return publish_succeeded(self.client.publish(topic, payload, qos=0, retain=True))
        except Exception as exc:
            with self.state_lock:
                self.last_error = str(exc)
            return False

    def _publish_status(self, online: bool) -> None:
        with self.state_lock:
            payload = json_payload(build_status_payload(self.state, online, BROKER_TEXT))
        self._publish(self.topics.status, payload)

    def _publish_control_status(self) -> None:
        with self.state_lock:
            payload = json_payload(build_control_status(self.state))
        self._publish(self.topics.control_status, payload)

    def _publish_latest(self, sampled_at: str) -> None:
        with self.state_lock:
            payload = json_payload(build_latest_payload(self.state, sampled_at))
        ok = self._publish(self.topics.latest, payload)
        with self.state_lock:
            if ok:
                self.state.published += 1
                self.state.last_publish_at = sampled_at
                self._last_publish_monotonic = time.monotonic()
            else:
                self.state.failed += 1


class SimulatorManager:
    def __init__(self, client_factory: Callable[[str], Any] = create_mqtt_client):
        self.workers = {pond_id: PondWorker(pond_id, client_factory) for pond_id in PONDS}

    def worker(self, pond_id: str) -> PondWorker:
        if pond_id not in self.workers:
            raise ValueError("invalid pond id")
        return self.workers[pond_id]

    def start(self, pond_id: str) -> None:
        self.worker(pond_id).start()

    def stop(self, pond_id: str) -> None:
        self.worker(pond_id).stop()

    def start_all(self) -> None:
        for worker in self.workers.values():
            worker.start()

    def stop_all(self) -> None:
        for worker in self.workers.values():
            worker.stop()

    def status(self) -> dict[str, Any]:
        ponds = [worker.snapshot() for worker in self.workers.values()]
        return {
            "ok": True,
            "editionLabel": "水质网关模拟器",
            "mqtt": {
                "connected": any(pond["connected"] for pond in ponds),
                "broker": BROKER_TEXT,
                "running": sum(1 for pond in ponds if pond["running"]),
                "lastError": next((pond["lastError"] for pond in ponds if pond["lastError"]), ""),
            },
            "ponds": ponds,
        }


manager = SimulatorManager()


def _pond_response(pond_id: str) -> tuple[Any, int] | Any:
    try:
        return jsonify({"ok": True, "pond": manager.worker(pond_id).snapshot()})
    except ValueError as exc:
        return jsonify({"ok": False, "message": str(exc)}), 400


@app.get("/")
def index():
    return send_from_directory(app.static_folder, "index.html")


@app.get("/api/status")
def api_status():
    return jsonify(manager.status())


@app.post("/api/all/start")
def api_start_all():
    manager.start_all()
    return jsonify(manager.status())


@app.post("/api/all/stop")
def api_stop_all():
    manager.stop_all()
    return jsonify(manager.status())


@app.post("/api/pond/<pond_id>/start")
def api_start(pond_id: str):
    try:
        manager.start(pond_id)
    except ValueError as exc:
        return jsonify({"ok": False, "message": str(exc)}), 400
    return _pond_response(pond_id)


@app.post("/api/pond/<pond_id>/stop")
def api_stop(pond_id: str):
    try:
        manager.stop(pond_id)
    except ValueError as exc:
        return jsonify({"ok": False, "message": str(exc)}), 400
    return _pond_response(pond_id)


@app.post("/api/pond/<pond_id>/settings")
def api_settings(pond_id: str):
    try:
        manager.worker(pond_id).update_settings(request.get_json(silent=True) or {})
    except ValueError as exc:
        return jsonify({"ok": False, "message": str(exc)}), 400
    return _pond_response(pond_id)


@app.post("/api/pond/<pond_id>/control")
def api_control(pond_id: str):
    try:
        manager.worker(pond_id).apply_local_command(request.get_json(silent=True) or {})
    except ValueError as exc:
        return jsonify({"ok": False, "message": str(exc)}), 400
    return _pond_response(pond_id)


@app.post("/api/pond/<pond_id>/publish-now")
def api_publish_now(pond_id: str):
    try:
        worker = manager.worker(pond_id)
    except ValueError as exc:
        return jsonify({"ok": False, "message": str(exc)}), 400
    worker.force_publish()
    return _pond_response(pond_id)


def run() -> None:
    if os.environ.get("FISH_SIMULATOR_NO_BROWSER") != "1":
        threading.Timer(0.8, lambda: webbrowser.open(f"http://127.0.0.1:{WEB_PORT}")).start()
    app.run(host="127.0.0.1", port=WEB_PORT, threaded=True, use_reloader=False)


if __name__ == "__main__":
    run()
