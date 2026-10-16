"""Capture the existing simulator UI with an in-memory protocol fixture."""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "FishWaterSimulator"))

from simulator_core import PONDS, SimulatorState, build_latest_payload


def fixture() -> dict:
    ponds = []
    for index, pond_id in enumerate(PONDS):
        state = SimulatorState(pond_id)
        state.ph = (7.8, 7.6, 8.0)[index]
        state.do_mg_l = (5.06, 5.4, 5.2)[index]
        state.water_temp = (30.0, 29.5, 29.8)[index]
        state.chip_temp_c = (47.7, 46.2, 47.1)[index]
        state.publish_interval_seconds = (60, 300, 3600)[index]
        ponds.append({
            "pondId": pond_id,
            "gatewayId": state.gateway_id,
            "running": False,
            "connected": False,
            "published": 0,
            "lastPublishAt": "",
            "lastError": "",
            "sensorMode": "fixed",
            "publishIntervalSeconds": state.publish_interval_seconds,
            "mode": "run",
            "collecting": True,
            "scheduleStart": "08:00",
            "scheduleEnd": "18:00",
            "ph": state.ph,
            "doMgL": state.do_mg_l,
            "doSaturation": state.do_saturation,
            "waterTemp": state.water_temp,
            "chipTempC": state.chip_temp_c,
            "latest": build_latest_payload(state),
        })
    return {
        "ok": True,
        "mqtt": {"connected": False, "broker": "protocol fixture", "running": 0},
        "ponds": ponds,
    }


def main() -> int:
    from playwright.sync_api import sync_playwright

    static = ROOT / "FishWaterSimulator" / "static"
    output = ROOT / "docs" / "images" / "simulator-protocol-fixture.png"
    output.parent.mkdir(parents=True, exist_ok=True)
    scratch = ROOT / ".verification"
    scratch.mkdir(exist_ok=True)
    errors = []
    with tempfile.TemporaryDirectory(prefix="visual-", dir=scratch) as directory:
        browser_env = dict(os.environ, TMP=directory, TEMP=directory, TMPDIR=directory)
        with sync_playwright() as playwright:
            context = playwright.chromium.launch_persistent_context(
                user_data_dir=str(Path(directory) / "profile"),
                headless=True,
                viewport={"width": 1600, "height": 1200},
                device_scale_factor=1,
                env=browser_env,
                args=[
                    "--disable-background-networking",
                    "--disable-component-update",
                    "--disable-default-apps",
                    "--no-first-run",
                    "--host-resolver-rules=MAP * ~NOTFOUND",
                ],
            )

            def route_fixture(route):
                parsed = urlparse(route.request.url)
                if parsed.hostname != "protocol-fixture.invalid":
                    route.abort()
                elif parsed.path == "/api/status":
                    route.fulfill(content_type="application/json", body=json.dumps(fixture()))
                elif parsed.path in {"/", "/styles.css", "/app.js"}:
                    name, content_type = {
                        "/": ("index.html", "text/html"),
                        "/styles.css": ("styles.css", "text/css"),
                        "/app.js": ("app.js", "application/javascript"),
                    }[parsed.path]
                    route.fulfill(
                        content_type=content_type,
                        body=(static / name).read_text(encoding="utf-8"),
                    )
                else:
                    route.abort()

            context.route("**/*", route_fixture)
            page = context.pages[0]
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto("http://protocol-fixture.invalid/", wait_until="networkidle")
            page.wait_for_function(
                "document.querySelector('[data-value=ph]').textContent === '7.8'"
            )
            page.evaluate("""() => {
                document.querySelector('.eyebrow').textContent =
                    'PROTOCOL FIXTURE / FISH FARMING';
                document.querySelector('.subtitle').textContent =
                    '协议样例演示 · protocol fixture · 三池塘水质状态与采集配置';
                document.querySelector('.notice strong').textContent = '协议样例演示';
                document.querySelector('.notice span').textContent =
                    '固定样例驱动原有页面，展示字段映射、模拟读数与采集配置。';
            }""")
            assert page.locator(".pond-card").count() == 3
            assert not errors, "Browser page reported JavaScript errors"
            page.screenshot(path=str(output), full_page=True)
            print("PROTOCOL_FIXTURE_CARDS", page.locator(".pond-card").count())
            print("PROTOCOL_FIXTURE_PAGE_ERRORS", len(errors))
            print("PROTOCOL_FIXTURE_IMAGE", output.relative_to(ROOT).as_posix())
            context.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
