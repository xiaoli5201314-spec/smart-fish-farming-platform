import app as simulator_app
import json
import time
from pathlib import Path


class FakeMessage:
    def __init__(self, topic, payload):
        self.topic = topic
        self.payload = payload


class FakeClient:
    def __init__(self, client_id):
        self.client_id = client_id
        self.on_connect = None
        self.on_disconnect = None
        self.on_message = None
        self.published = []
        self.subscribed = []
        self.will = None

    def username_pw_set(self, username, password):
        pass

    def reconnect_delay_set(self, min_delay, max_delay):
        pass

    def will_set(self, topic, payload, qos, retain):
        self.will = (topic, payload, qos, retain)

    def connect_async(self, host, port, keepalive):
        self.on_connect(self, None, {}, 0, None)

    def loop_start(self):
        pass

    def loop_stop(self):
        pass

    def subscribe(self, topic, qos):
        self.subscribed.append((topic, qos))

    def publish(self, topic, payload, qos, retain):
        self.published.append((topic, json.loads(payload), qos, retain))
        return True

    def disconnect(self):
        if self.on_disconnect:
            self.on_disconnect(self, None, None, 0, None)


def test_status_exposes_three_ponds_without_connecting_to_mqtt():
    client = simulator_app.app.test_client()
    response = client.get("/api/status")
    assert response.status_code == 200
    body = response.get_json()
    assert body["ok"] is True
    assert [pond["pondId"] for pond in body["ponds"]] == ["pond_1", "pond_2", "pond_3"]
    assert all(pond["running"] is False for pond in body["ponds"])


def test_settings_validate_and_update_simulated_values():
    client = simulator_app.app.test_client()
    response = client.post(
        "/api/pond/pond_1/settings",
        json={"sensorMode": "fixed", "ph": 6.5, "publishIntervalSeconds": 60},
    )
    assert response.status_code == 200
    pond = response.get_json()["pond"]
    assert pond["sensorMode"] == "fixed"
    assert pond["ph"] == 6.5
    assert pond["publishIntervalSeconds"] == 60


def test_invalid_pond_is_rejected():
    client = simulator_app.app.test_client()
    response = client.post("/api/pond/pond_9/start")
    assert response.status_code == 400


def test_worker_publishes_gateway_topics_and_applies_mqtt_commands():
    clients = []

    def factory(client_id):
        client = FakeClient(client_id)
        clients.append(client)
        return client

    worker = simulator_app.PondWorker("pond_2", factory)
    worker.start()
    time.sleep(0.35)
    try:
        client = clients[0]
        topics = {item[0] for item in client.published}
        assert "fish/pond/pond_2/water/status" in topics
        assert "fish/pond/pond_2/water/control/status" in topics
        assert "fish/pond/pond_2/water/latest" in topics
        assert {item[0] for item in client.subscribed} == {
            "fish/pond/pond_2/water/cmd",
            "fish/pond/pond_2/water/config",
        }
        latest = next(item[1] for item in client.published if item[0].endswith("water/latest"))
        assert latest["gatewayId"] == "water_gateway_pond_2"
        worker._on_message(client, None, FakeMessage(worker.topics.command, b'{"mode":"pause"}'))
        worker._on_message(client, None, FakeMessage(worker.topics.config, b'{"publishIntervalSeconds":60}'))
        snapshot = worker.snapshot()
        assert snapshot["mode"] == "pause"
        assert snapshot["publishIntervalSeconds"] == 60
    finally:
        worker.stop()


def test_frontend_preserves_unsaved_form_selection_during_status_refresh():
    script = (Path(simulator_app.RESOURCE_DIR) / "static" / "app.js").read_text(encoding="utf-8")
    assert "settingsDirty" in script
    assert "controlDirty" in script
    assert "if (!settingsDirty)" in script
    assert "if (!controlDirty)" in script
