import json

from simulator_core import (
    PONDS,
    SUPPORTED_PUBLISH_INTERVAL_SECONDS,
    SimulatorState,
    apply_command,
    apply_publish_config,
    build_control_status,
    build_latest_payload,
    next_reading,
    should_collect,
    topics_for,
)


def test_topics_and_payload_match_gateway_contract():
    topics = topics_for("pond_2")
    assert topics.latest == "fish/pond/pond_2/water/latest"
    assert topics.status == "fish/pond/pond_2/water/status"
    assert topics.command == "fish/pond/pond_2/water/cmd"
    assert topics.control_status == "fish/pond/pond_2/water/control/status"
    assert topics.config == "fish/pond/pond_2/water/config"

    state = SimulatorState("pond_2")
    payload = build_latest_payload(state, now_text="2026-08-18 10:00:00")
    assert payload == {
        "pondId": "pond_2",
        "gatewayId": "water_gateway_pond_2",
        "sampledAt": "2026-08-18 10:00:00",
        "timeSynced": True,
        "ph": 7.8,
        "dissolvedOxygen": 5.06,
        "doMgL": 5.06,
        "doSaturation": 67.7,
        "waterTemp": 30.0,
        "chipTempC": 47.7,
        "phOnline": True,
        "doOnline": True,
        "onlineSensors": 2,
        "pollCycles": 0,
    }


def test_latest_payload_is_json_serializable_and_poll_cycles_increment():
    state = SimulatorState("pond_1")
    next_reading(state)
    next_reading(state)
    payload = build_latest_payload(state, now_text="2026-08-18 10:00:00")
    assert payload["pollCycles"] == 2
    json.dumps(payload)


def test_control_and_publish_config_update_state():
    state = SimulatorState("pond_1")
    apply_command(state, {"mode": "pause"})
    assert state.collecting is False

    apply_publish_config(state, {"publishIntervalSeconds": 60})
    assert state.publish_interval_seconds == 60

    apply_command(
        state,
        {"mode": "schedule", "scheduleStart": "08:00", "scheduleEnd": "18:00"},
    )
    assert should_collect(state, "2026-08-18 12:00:00") is True
    assert should_collect(state, "2026-08-18 20:00:00") is False
    assert build_control_status(state)["publishIntervalSeconds"] == 60


def test_schedule_supports_a_window_crossing_midnight():
    state = SimulatorState("pond_1")
    apply_command(
        state,
        {"mode": "schedule", "scheduleStart": "22:00", "scheduleEnd": "06:00"},
    )
    assert should_collect(state, "2026-08-18 23:00:00") is True
    assert should_collect(state, "2026-08-18 05:00:00") is True
    assert should_collect(state, "2026-08-18 12:00:00") is False


def test_unsupported_publish_interval_is_rejected():
    state = SimulatorState("pond_1")
    assert apply_publish_config(state, {"publishIntervalSeconds": 7}) is False
    assert state.publish_interval_seconds == 3600
    assert 3600 in SUPPORTED_PUBLISH_INTERVAL_SECONDS


def test_client_ids_are_unique_and_stable_per_pond():
    states = [SimulatorState(pond) for pond in PONDS]
    assert len({state.client_id for state in states}) == 3
    assert SimulatorState("pond_1").client_id != states[0].client_id
