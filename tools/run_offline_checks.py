"""Run source checks and original tests with Python socket access denied."""

from __future__ import annotations

import importlib.metadata
import json
import os
import shutil
import subprocess
import sys
import tempfile
from html.parser import HTMLParser
from pathlib import Path

from check_public_package import check, public_files

ROOT = Path(__file__).resolve().parents[1]


class Scripts(HTMLParser):
    def __init__(self):
        super().__init__()
        self.inside = False
        self.parts: list[str] = []

    def handle_starttag(self, tag, attrs):
        if tag == "script":
            self.inside = not dict(attrs).get("src")

    def handle_endtag(self, tag):
        if tag == "script":
            self.inside = False
            self.parts.append("\n")

    def handle_data(self, data):
        if self.inside:
            self.parts.append(data)


def deny_network(event, args):
    if event in {"socket.connect", "socket.bind", "socket.getaddrinfo", "socket.sendto"}:
        raise RuntimeError("Offline verification denied socket access")


def main() -> int:
    os.chdir(ROOT)
    sys.dont_write_bytecode = True
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    os.environ["PYTEST_DISABLE_PLUGIN_AUTOLOAD"] = "1"
    for name in list(os.environ):
        if name.startswith(("FISH_MQTT_", "FISH_SIMULATOR_")):
            os.environ.pop(name)
    os.environ["FISH_SIMULATOR_NO_BROWSER"] = "1"
    os.environ["FISH_MQTT_HOST"] = "127.0.0.1"
    sys.addaudithook(deny_network)

    errors, counts = check()
    for error in errors:
        print("FAIL", error)
    print("STATIC_CHECKS", json.dumps(counts, sort_keys=True), flush=True)
    if errors:
        return 1

    node = shutil.which("node")
    if not node:
        print("FAIL Node.js is required for JavaScript syntax checks")
        return 1
    js_count = 0
    inline_count = 0
    for path in public_files():
        if path.suffix == ".js":
            result = subprocess.run(
                [node, "--check", str(path)], capture_output=True,
                text=True, encoding="utf-8", cwd=ROOT
            )
            js_count += 1
        elif path.suffix == ".html":
            parser = Scripts()
            parser.feed(path.read_text(encoding="utf-8-sig"))
            source = "".join(parser.parts).strip()
            if not source:
                continue
            result = subprocess.run(
                [node, "--check", "-"], input=source,
                capture_output=True, text=True, encoding="utf-8", cwd=ROOT
            )
            inline_count += 1
        else:
            continue
        if result.returncode:
            print("FAIL JavaScript syntax:", path.relative_to(ROOT).as_posix())
            return 1
    print(f"JAVASCRIPT_SYNTAX files={js_count} html_inline={inline_count}", flush=True)

    try:
        import pytest
    except ImportError:
        print("FAIL Install requirements-test.txt in a local virtual environment")
        return 1
    for name in ("pytest", "Flask", "paho-mqtt"):
        print("DEPENDENCY", name, importlib.metadata.version(name), flush=True)
    scratch = ROOT / ".verification"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="offline-", dir=scratch) as directory:
        for name in ("TMP", "TEMP", "TMPDIR"):
            os.environ[name] = directory
        tempfile.tempdir = directory
        result = pytest.main([
            "-q", "-p", "no:cacheprovider", "--basetemp", str(Path(directory) / "pytest"),
            "tests", "FishWaterSimulator/tests",
        ])
    print("OFFLINE_TEST_EXIT", int(result))
    return int(result)


if __name__ == "__main__":
    sys.exit(main())
