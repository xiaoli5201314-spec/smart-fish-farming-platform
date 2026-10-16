from __future__ import annotations

import argparse
import json
import os
import socket
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

try:
    import uvicorn
    from fastapi import FastAPI
    from fastapi.middleware.cors import CORSMiddleware
    from fastapi.responses import JSONResponse
except ModuleNotFoundError as exc:
    raise SystemExit(
        "缺少 FastAPI 运行环境，请先执行：python -m pip install fastapi uvicorn"
    ) from exc


BASE_DIR = Path(__file__).resolve().parent
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8000
DEEPSEEK_URL = os.getenv("DEEPSEEK_API_URL", "https://api.deepseek.com/chat/completions")
DEEPSEEK_MODEL = os.getenv("DEEPSEEK_MODEL", "deepseek-v4-flash")
REQUEST_TIMEOUT = float(os.getenv("DEEPSEEK_TIMEOUT", "40"))
MAX_HISTORY_MESSAGES = 8
MAX_QUESTION_LENGTH = 800


SYSTEM_PROMPT = """
你是“种鱼数智管理平台”的水质智能助手，同时服务微信小程序端和网页端水质监测看板。
你的核心职责是结合实时水质数据、养殖管理经验和系统使用场景，给养殖人员提供简洁、可靠、可执行的判断与建议。

系统场景：
1. 小程序端：偏移动巡检、现场查看、水质问答、快速判断，回答要短、直接、方便现场人员照着做。
2. 网页端：偏大屏看板、数据可视化、视频监控、综合研判，回答可以更像值班分析员，适合解释趋势、指标关系和看板含义。
3. 现场控制由树莓派 QT 上位机和小程序承担；网页端主要用于水质数据展示、AI 研判和视频查看。
4. 当前主要水质指标包括：水温、pH、COD、氨氮、亚硝酸根、硝酸根、电导率、TDS、盐度。

你可以回答的问题范围：
1. 水质专业分析：pH、COD、氨氮、亚硝酸根、硝酸根、水温、电导率、TDS、盐度的状态、风险、相互影响和处理建议。
2. 养殖运维建议：投喂、增氧、换水、循环、底改、清理残饵、巡检频率、传感器复核等。
3. 系统使用说明：小程序怎么看水质，网页看板怎么看雷达图、核心指标、氮盐动态、视频监控，AI 能做什么。
4. 异常排查：网关离线、数据长期不刷新、AI 无响应、视频流打不开、指标显示为 "--"、ESP32-S3 与电脑不在同一热点等。
5. 日常问答：可以回答“你是谁”“当前状态怎么样”“我应该先看哪个指标”等简单问题。

回答要求：
1. 始终使用中文，语气专业、友好、容易理解。
2. 如果用户问水质、养殖或设备运维，优先结合当前水质数据回答，并给出明确操作建议。
3. 如果用户问题来自网页端或提到“看板、网页、雷达图、视频、监控”，回答要适合大屏值班场景，可以解释页面指标和观察重点。
4. 如果用户问题来自小程序或提到“手机、小程序、现场”，回答要更短，更像现场巡检提示。
5. 不要声称自己已经直接控制设备；渔网、照明、增氧等动作只能建议用户确认后执行。
6. 不要声称自己已经看到了视频画面；如果用户问视频画面，只能提示如何检查视频流、摄像头和现场观察重点。
7. 如果数据不足或指标为 "--"，要说明当前缺少实时数据，并给出排查步骤。
8. 普通聊天控制在 80 字以内；专业水质回答控制在 300 字以内，必要时使用 2 到 4 条短建议。
""".strip()


app = FastAPI(
    title="Fish Water AI Assistant API",
    description="Local AI bridge for fish farming water quality assistant.",
    version="2.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["*"],
)


def load_local_env() -> None:
    """Load simple KEY=VALUE lines from after_api/.env if the file exists."""
    env_path = BASE_DIR / ".env"
    if not env_path.exists():
        return

    for raw_line in env_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = value


def get_api_key() -> str:
    load_local_env()
    key = os.getenv("DEEPSEEK_API_KEY", "").strip()
    if key:
        return key

    key_file = BASE_DIR / "deepseek_api_key.txt"
    if key_file.exists():
        return key_file.read_text(encoding="utf-8").strip()

    return ""


def normalize_question(data: dict[str, Any]) -> str:
    question = data.get("question") or data.get("content") or data.get("message") or ""
    question = str(question).strip()
    if not question:
        raise ValueError("question 不能为空")
    if len(question) > MAX_QUESTION_LENGTH:
        question = question[:MAX_QUESTION_LENGTH]
    return question


def normalize_water_data(data: dict[str, Any]) -> dict[str, Any]:
    water_data = data.get("waterData") or data.get("water_data") or data.get("metrics") or {}
    if isinstance(water_data, dict):
        return water_data
    if isinstance(water_data, list):
        return {"items": water_data}
    return {}


def normalize_history(data: dict[str, Any]) -> list[dict[str, str]]:
    history = data.get("messages") or data.get("history") or []
    if not isinstance(history, list):
        return []

    normalized: list[dict[str, str]] = []
    for item in history[-MAX_HISTORY_MESSAGES:]:
        if not isinstance(item, dict):
            continue
        role = item.get("role")
        if role not in ("user", "assistant"):
            continue
        content = item.get("content") or item.get("text") or ""
        content = str(content).strip()
        if content:
            normalized.append({"role": role, "content": content[:1000]})
    return normalized


def make_user_context(question: str, water_data: dict[str, Any]) -> str:
    if not water_data:
        return (
            f"用户问题：{question}\n"
            "当前没有收到结构化水质数据，请基于通用鱼塘水质管理经验回答。"
        )

    water_text = json.dumps(water_data, ensure_ascii=False, indent=2)
    return f"用户问题：{question}\n\n当前水质数据：\n{water_text}"


def safe_json_loads(text: str) -> Any:
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text[:1000]


def call_deepseek(question: str, water_data: dict[str, Any], history: list[dict[str, str]]) -> dict[str, Any]:
    api_key = get_api_key()
    model = os.getenv("DEEPSEEK_MODEL", DEEPSEEK_MODEL)
    if not api_key:
        return {
            "ok": False,
            "code": "missing_api_key",
            "message": "未配置 DEEPSEEK_API_KEY。请设置环境变量，或在后端目录创建 deepseek_api_key.txt。",
        }

    messages: list[dict[str, str]] = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(history)
    messages.append({"role": "user", "content": make_user_context(question, water_data)})

    payload: dict[str, Any] = {
        "model": model,
        "messages": messages,
        "stream": False,
        "temperature": 0.45,
        "max_tokens": 900,
        "thinking": {"type": "disabled"},
    }

    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        DEEPSEEK_URL,
        data=body,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
    )

    try:
        with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT) as response:
            response_body = response.read().decode("utf-8")
            result = json.loads(response_body)
    except urllib.error.HTTPError as exc:
        error_body = exc.read().decode("utf-8", errors="replace")
        detail = safe_json_loads(error_body)
        readable_message = "DeepSeek 接口返回错误"
        error_message = ""
        if isinstance(detail, dict):
            error = detail.get("error")
            if isinstance(error, dict):
                error_message = str(error.get("message") or "")

        if exc.code == 401:
            readable_message = "DeepSeek API Key 无效或已过期，请检查后端配置"
        elif exc.code == 402 or "insufficient balance" in error_message.lower():
            readable_message = "DeepSeek 账户余额不足，请充值后再测试 AI 回答"
        elif exc.code == 429:
            readable_message = "DeepSeek 请求过于频繁，请稍后再试"

        return {
            "ok": False,
            "code": "deepseek_http_error",
            "status": exc.code,
            "message": readable_message,
            "detail": detail,
        }
    except urllib.error.URLError as exc:
        return {
            "ok": False,
            "code": "network_error",
            "message": f"连接 DeepSeek 失败：{exc.reason}",
        }
    except TimeoutError:
        return {
            "ok": False,
            "code": "timeout",
            "message": "请求 DeepSeek 超时",
        }
    except json.JSONDecodeError:
        return {
            "ok": False,
            "code": "bad_deepseek_response",
            "message": "DeepSeek 返回内容不是合法 JSON",
        }

    choices = result.get("choices") or []
    if not choices:
        return {
            "ok": False,
            "code": "empty_answer",
            "message": "DeepSeek 未返回有效回答",
            "raw": result,
        }

    answer = (choices[0].get("message") or {}).get("content") or ""
    answer = str(answer).strip()
    if not answer:
        return {
            "ok": False,
            "code": "empty_answer",
            "message": "DeepSeek 回答为空",
            "raw": result,
        }

    return {
        "ok": True,
        "answer": answer,
        "model": result.get("model", model),
        "usage": result.get("usage", {}),
    }


def get_lan_ip_hint() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("192.0.2.10", 80))
            return sock.getsockname()[0]
    except OSError:
        return "你的电脑局域网IP"


def health_payload() -> dict[str, Any]:
    return {
        "ok": True,
        "service": "fish-water-ai-assistant",
        "framework": "fastapi",
        "model": os.getenv("DEEPSEEK_MODEL", DEEPSEEK_MODEL),
        "time": int(time.time()),
    }


@app.get("/")
def root() -> dict[str, Any]:
    return health_payload()


@app.get("/health")
def health() -> dict[str, Any]:
    return health_payload()


@app.get("/api/health")
def api_health() -> dict[str, Any]:
    return health_payload()


@app.post("/api/ai/chat")
def ai_chat(data: dict[str, Any]) -> JSONResponse:
    try:
        question = normalize_question(data)
        water_data = normalize_water_data(data)
        history = normalize_history(data)
    except ValueError as exc:
        return JSONResponse(
            status_code=400,
            content={
                "ok": False,
                "code": "bad_request",
                "message": str(exc),
            },
        )

    result = call_deepseek(question, water_data, history)
    return JSONResponse(status_code=200 if result.get("ok") else 502, content=result)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fish farming water quality AI assistant backend")
    parser.add_argument("--host", default=DEFAULT_HOST, help="监听地址，真机调试建议使用 0.0.0.0")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="监听端口")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    lan_ip = get_lan_ip_hint()

    print("Fish water AI assistant backend started with FastAPI.")
    print(f"Health check: http://127.0.0.1:{args.port}/health")
    print(f"Mini program URL: http://{lan_ip}:{args.port}/api/ai/chat")
    print("Press Ctrl+C to stop.")

    uvicorn.run(app, host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
