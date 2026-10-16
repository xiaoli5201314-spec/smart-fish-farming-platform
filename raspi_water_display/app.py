#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
STATIC_DIR = os.path.join(ROOT_DIR, "static")
CONFIG_PATH = os.path.join(ROOT_DIR, "config.json")

DEFAULT_CONFIG = {
    "gatewayUrl": "",
    "pollIntervalMs": 2500,
}


def read_json_body(handler):
    length = int(handler.headers.get("Content-Length") or 0)
    if length <= 0:
        return {}
    raw = handler.rfile.read(length)
    if not raw:
        return {}
    return json.loads(raw.decode("utf-8"))


def load_config():
    if not os.path.exists(CONFIG_PATH):
        save_config(DEFAULT_CONFIG)
        return dict(DEFAULT_CONFIG)

    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            saved = json.load(f)
    except (OSError, ValueError):
        saved = {}

    config = dict(DEFAULT_CONFIG)
    config.update(saved if isinstance(saved, dict) else {})
    config.pop("demoMode", None)
    config["gatewayUrl"] = normalize_gateway_url(config.get("gatewayUrl", ""))
    try:
        config["pollIntervalMs"] = int(config.get("pollIntervalMs", 2500))
    except (TypeError, ValueError):
        config["pollIntervalMs"] = 2500
    config["pollIntervalMs"] = max(1000, min(10000, config["pollIntervalMs"]))
    return config


def save_config(config):
    clean = dict(DEFAULT_CONFIG)
    clean.update(config or {})
    clean.pop("demoMode", None)
    clean["gatewayUrl"] = normalize_gateway_url(clean.get("gatewayUrl", ""))
    try:
        clean["pollIntervalMs"] = int(clean.get("pollIntervalMs", 2500))
    except (TypeError, ValueError):
        clean["pollIntervalMs"] = 2500
    clean["pollIntervalMs"] = max(1000, min(10000, clean["pollIntervalMs"]))

    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(clean, f, ensure_ascii=False, indent=2)
    return clean


def normalize_gateway_url(value):
    url = (value or "").strip()
    if not url:
        return ""

    if "://" not in url:
        url = "http://" + url

    parsed = urllib.parse.urlparse(url)
    if not parsed.netloc:
        return ""

    path = parsed.path or ""
    if path in ("", "/"):
        path = "/api/water/latest"

    normalized = parsed._replace(path=path, params="", query=parsed.query, fragment="")
    return urllib.parse.urlunparse(normalized)


def fetch_json(url, timeout=2.0):
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/json",
            "User-Agent": "raspi-water-display/1.0",
        },
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = response.read(256 * 1024)
        charset = response.headers.get_content_charset() or "utf-8"
        text = body.decode(charset, errors="replace")
        payload = json.loads(text)
        return payload


def now_string():
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())


class WaterDisplayHandler(SimpleHTTPRequestHandler):
    server_version = "RaspiWaterDisplay/1.0"

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/config":
            self.send_json(200, {"ok": True, "config": load_config()})
            return

        if parsed.path == "/api/water/latest":
            self.handle_latest()
            return

        if parsed.path == "/api/water/health":
            config = load_config()
            self.send_json(200, {
                "ok": True,
                "service": "raspi-water-display",
                "gatewayUrl": config["gatewayUrl"],
                "time": now_string(),
            })
            return

        if parsed.path == "/":
            self.path = "/index.html"

        super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/config":
            try:
                body = read_json_body(self)
                config = load_config()
                if "gatewayUrl" in body:
                    config["gatewayUrl"] = body.get("gatewayUrl", "")
                if "pollIntervalMs" in body:
                    config["pollIntervalMs"] = body.get("pollIntervalMs")
                config = save_config(config)
                self.send_json(200, {"ok": True, "config": config})
            except Exception as exc:
                self.send_json(400, {"ok": False, "message": str(exc)})
            return

        if parsed.path == "/api/config/test":
            try:
                body = read_json_body(self)
                url = normalize_gateway_url(body.get("gatewayUrl", ""))
                if not url:
                    self.send_json(400, {"ok": False, "message": "请填写 ESP32-S3 地址"})
                    return
                payload = fetch_json(url, timeout=2.5)
                self.send_json(200, {
                    "ok": True,
                    "gatewayUrl": url,
                    "sample": payload,
                    "time": now_string(),
                })
            except Exception as exc:
                self.send_json(502, {"ok": False, "message": str(exc)})
            return

        self.send_json(404, {"ok": False, "message": "not found"})

    def handle_latest(self):
        config = load_config()
        if not config["gatewayUrl"]:
            self.send_json(200, {
                "ok": True,
                "data": {
                    "connected": False,
                    "updateTime": "--",
                    "gateway": {"onlineSensors": 0},
                    "metrics": {},
                },
                "_display": {
                    "source": "offline",
                    "fetchedAt": now_string(),
                },
            })
            return

        try:
            payload = fetch_json(config["gatewayUrl"], timeout=2.5)
            if isinstance(payload, dict):
                payload.setdefault("_display", {})
                payload["_display"].update({
                    "source": "esp32",
                    "gatewayUrl": config["gatewayUrl"],
                    "fetchedAt": now_string(),
                })
            self.send_json(200, payload)
        except urllib.error.URLError as exc:
            self.send_json(502, {
                "ok": False,
                "message": "无法连接 ESP32-S3 水质网关",
                "detail": str(exc),
                "gatewayUrl": config["gatewayUrl"],
                "time": now_string(),
            })
        except Exception as exc:
            self.send_json(502, {
                "ok": False,
                "message": "水质数据读取失败",
                "detail": str(exc),
                "gatewayUrl": config["gatewayUrl"],
                "time": now_string(),
            })

    def send_json(self, status, payload):
        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def main():
    parser = argparse.ArgumentParser(description="Raspberry Pi water quality display")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8088)
    args = parser.parse_args()

    os.makedirs(STATIC_DIR, exist_ok=True)
    if not os.path.exists(CONFIG_PATH):
        save_config(DEFAULT_CONFIG)

    handler = partial(WaterDisplayHandler, directory=STATIC_DIR)
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print("Raspi water display running at http://%s:%d" % (args.host, args.port))
    print("Open locally: http://127.0.0.1:%d" % args.port)
    server.serve_forever()


if __name__ == "__main__":
    main()
