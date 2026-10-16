from __future__ import annotations

import argparse
import io
import json
import os
import re
import socket
import time
import urllib.error
import urllib.request
import wave
from pathlib import Path
from typing import Any

try:
    import uvicorn
    from fastapi import FastAPI, File, Form, UploadFile
    from fastapi.middleware.cors import CORSMiddleware
    from fastapi.responses import JSONResponse
except ModuleNotFoundError as exc:
    raise SystemExit(
        "缺少 FastAPI 运行环境，请先执行：python -m pip install -r requirements.txt"
    ) from exc

try:
    from vosk import KaldiRecognizer, Model, SetLogLevel

    VOSK_AVAILABLE = True
    VOSK_IMPORT_ERROR = ""
except ModuleNotFoundError as exc:
    KaldiRecognizer = None
    Model = None
    SetLogLevel = None
    VOSK_AVAILABLE = False
    VOSK_IMPORT_ERROR = str(exc)


BASE_DIR = Path(__file__).resolve().parent
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8010
DEFAULT_TIMEOUT = 45.0
DEFAULT_DEEPSEEK_URL = "https://api.deepseek.com/chat/completions"
DEFAULT_DEEPSEEK_MODEL = "deepseek-v4-flash"
DEFAULT_ASSISTANT_NAME = "智能管理助手"
DEFAULT_VOSK_MODEL_PATH = BASE_DIR / "models" / "vosk-model-small-cn-0.22"
MAX_HISTORY_MESSAGES = 10
MAX_QUESTION_LENGTH = 1200
MAX_CONTEXT_LENGTH = 4000

SPEECH_SYSTEM_PROMPT = """
你是当前鱼塘智能管理系统里的智能助手，负责根据用户提问提供说明、建议和排查思路。
你的定位很明确：
1. 你现在只负责“用户提问，你回答”。
2. 你不是自动控制程序，不能假装自己已经执行了开关设备、切换网络、修改设置或其他物理动作。
3. 你优先回答以下几类问题：
   - 当前系统怎么使用
   - 画面、数据、连接或功能异常时该怎么排查
   - 日常养殖、水质和设备维护的基础建议
   - 与当前系统功能相关的说明
   - 普通日常问答
回答要求：
1. 始终使用中文。
2. 站在使用者角度说话，优先使用自然、易懂、适合语音播报的表达。
3. 除非用户明确追问实现方式，否则不要主动提到底层硬件名称、开发框架、通信接口、模型平台或后端服务名称。
4. 当用户问“你是谁”或“你能做什么”这类问题时，请把自己介绍为“这个系统里的智能助手”或“智能管理助手”，不要主动说底层设备或开发框架名称。
5. 默认简洁，优先给结论，再补一句原因或建议。
6. 除非用户明确要求，不要使用表格，不要输出大段代码，不要堆很多术语。
7. 不要使用 Markdown 标题、复杂列表、代码围栏。
8. 如果信息不足，要直接说明“我还不能确定”，并告诉用户还缺什么信息。
9. 如果用户问高风险操作，给建议可以，但不要假装已经执行。
10. 如果用户问当前项目相关问题，优先结合用户提供的上下文信息回答。
""".strip()

app = FastAPI(
    title="Raspberry Pi Voice Assistant API",
    description="Standalone AI backend for the Raspberry Pi voice Q&A assistant.",
    version="2.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["*"],
)

_vosk_model: Model | None = None


def load_local_env() -> None:
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


def env(name: str, default: str) -> str:
    load_local_env()
    return os.getenv(name, default)


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
    question = data.get("question") or data.get("text") or data.get("message") or ""
    question = str(question).strip()
    if not question:
        raise ValueError("question 不能为空")
    if len(question) > MAX_QUESTION_LENGTH:
        question = question[:MAX_QUESTION_LENGTH]
    return question


def normalize_history(data: dict[str, Any]) -> list[dict[str, str]]:
    history = data.get("messages") or data.get("history") or []
    if not isinstance(history, list):
        return []

    normalized: list[dict[str, str]] = []
    for item in history[-MAX_HISTORY_MESSAGES:]:
        if not isinstance(item, dict):
            continue
        role = str(item.get("role") or "").strip().lower()
        if role not in ("user", "assistant"):
            continue
        content = item.get("content") or item.get("text") or item.get("message") or ""
        content = str(content).strip()
        if content:
            normalized.append({"role": role, "content": content[:1000]})
    return normalized


def normalize_context_value(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value.strip()[:MAX_CONTEXT_LENGTH]
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False, indent=2)[:MAX_CONTEXT_LENGTH]
    return str(value).strip()[:MAX_CONTEXT_LENGTH]


def normalize_context(data: dict[str, Any]) -> str:
    context_candidates = [
        data.get("context"),
        data.get("deviceContext"),
        data.get("projectContext"),
        data.get("systemContext"),
    ]
    context_parts = [normalize_context_value(item) for item in context_candidates]
    context_parts = [item for item in context_parts if item]
    return "\n\n".join(context_parts)[:MAX_CONTEXT_LENGTH]


def normalize_user_name(data: dict[str, Any]) -> str:
    user_name = data.get("userName") or data.get("user_name") or ""
    return str(user_name).strip()[:40]


def normalize_response_style(data: dict[str, Any]) -> str:
    style = str(data.get("responseStyle") or data.get("response_style") or "spoken").strip().lower()
    if style not in {"spoken", "brief", "normal"}:
        return "spoken"
    return style


def build_system_prompt(response_style: str, assistant_name: str, user_name: str) -> str:
    style_hint = {
        "spoken": "请用适合语音播报的自然口语回答，控制在 2 到 6 句话。",
        "brief": "请尽量简短，优先一句话给结论，最多 3 句话。",
        "normal": "请清晰自然地回答，必要时分成 2 到 4 个短段。",
    }[response_style]

    name_hint = f"你的名字可以说成“{assistant_name}”。"
    user_hint = f"当前提问人可称为“{user_name}”。" if user_name else ""
    return "\n".join(item for item in [SPEECH_SYSTEM_PROMPT, name_hint, user_hint, style_hint] if item)


def build_user_content(question: str, context: str) -> str:
    if not context:
        return f"用户问题：{question}"
    return f"用户问题：{question}\n\n当前上下文信息：\n{context}"


def safe_json_loads(text: str) -> Any:
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text[:1000]


def sanitize_answer(answer: str) -> str:
    text = str(answer or "").strip()
    text = re.sub(r"\*\*(.*?)\*\*", r"\1", text)
    text = re.sub(r"^#+\s*", "", text, flags=re.MULTILINE)
    text = re.sub(r"^\s*[-*]\s+", "", text, flags=re.MULTILINE)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def make_tts_text(answer: str) -> str:
    text = sanitize_answer(answer)
    text = re.sub(r"^\s*\d+\.\s+", "", text, flags=re.MULTILINE)
    text = text.replace("\r", "\n")
    text = re.sub(r"\n+", "，", text)
    text = re.sub(r"\s{2,}", " ", text)
    text = re.sub(r"，[，]+", "，", text)
    text = re.sub(r"[`*_#>\[\]]", "", text)
    return text.strip("，").strip()


def voice_model_path() -> Path:
    configured = env("SMP_VOSK_MODEL_PATH", str(DEFAULT_VOSK_MODEL_PATH)).strip()
    return Path(configured).expanduser()


def get_voice_status() -> dict[str, Any]:
    model_path = voice_model_path()
    vosk_ready = VOSK_AVAILABLE and model_path.exists()
    reason = ""
    if not VOSK_AVAILABLE:
        reason = f"缺少 vosk Python 依赖：{VOSK_IMPORT_ERROR}"
    elif not model_path.exists():
        reason = f"未找到 Vosk 中文模型目录：{model_path}"

    return {
        "ready": vosk_ready,
        "engine": "vosk",
        "modelPath": str(model_path),
        "reason": reason,
    }


def get_vosk_model() -> Model:
    global _vosk_model

    status = get_voice_status()
    if not status["ready"]:
        raise RuntimeError(status["reason"] or "语音识别模型未就绪")

    if _vosk_model is None:
        if SetLogLevel is not None:
            SetLogLevel(-1)
        _vosk_model = Model(status["modelPath"])
    return _vosk_model


def normalize_transcript(text: str) -> str:
    cleaned = re.sub(r"\s+", "", str(text or "").strip())
    return cleaned[:MAX_QUESTION_LENGTH]


def transcribe_wav_bytes(audio_bytes: bytes) -> dict[str, Any]:
    if not audio_bytes:
        raise ValueError("上传的音频为空")

    try:
        with wave.open(io.BytesIO(audio_bytes), "rb") as wav_file:
            channels = wav_file.getnchannels()
            sample_width = wav_file.getsampwidth()
            sample_rate = wav_file.getframerate()
            frame_count = wav_file.getnframes()
            compression = wav_file.getcomptype()

            if compression != "NONE":
                raise ValueError("当前只支持未压缩 WAV 音频")
            if channels != 1:
                raise ValueError("当前只支持单声道 WAV 音频")
            if sample_width != 2:
                raise ValueError("当前只支持 16-bit WAV 音频")

            recognizer = KaldiRecognizer(get_vosk_model(), float(sample_rate))
            recognizer.SetWords(False)
            pieces: list[str] = []

            while True:
                chunk = wav_file.readframes(4000)
                if not chunk:
                    break
                if recognizer.AcceptWaveform(chunk):
                    result = safe_json_loads(recognizer.Result())
                    if isinstance(result, dict):
                        text = normalize_transcript(result.get("text", ""))
                        if text:
                            pieces.append(text)

            final_result = safe_json_loads(recognizer.FinalResult())
            if isinstance(final_result, dict):
                text = normalize_transcript(final_result.get("text", ""))
                if text:
                    pieces.append(text)
    except wave.Error as exc:
        raise ValueError(f"WAV 解析失败：{exc}") from exc

    transcript = normalize_transcript("".join(pieces))
    if not transcript:
        raise ValueError("没有识别到有效语音，请靠近麦克风后再试")

    duration_ms = int(frame_count * 1000 / sample_rate) if sample_rate > 0 else 0
    return {
        "text": transcript,
        "sampleRate": sample_rate,
        "durationMs": duration_ms,
    }


def call_deepseek(
    question: str,
    history: list[dict[str, str]],
    context: str,
    response_style: str,
    user_name: str,
) -> dict[str, Any]:
    api_key = get_api_key()
    model = env("DEEPSEEK_MODEL", DEFAULT_DEEPSEEK_MODEL)
    api_url = env("DEEPSEEK_API_URL", DEFAULT_DEEPSEEK_URL)
    assistant_name = env("SMP_ASSISTANT_NAME", DEFAULT_ASSISTANT_NAME)
    timeout = float(env("DEEPSEEK_TIMEOUT", str(DEFAULT_TIMEOUT)))

    if not api_key:
        return {
            "ok": False,
            "code": "missing_api_key",
            "message": "未配置 DEEPSEEK_API_KEY，请在 smp_api/deepseek_api_key.txt 中填入 key，或写入 .env。",
        }

    messages: list[dict[str, str]] = [
        {"role": "system", "content": build_system_prompt(response_style, assistant_name, user_name)}
    ]
    messages.extend(history)
    messages.append({"role": "user", "content": build_user_content(question, context)})

    payload: dict[str, Any] = {
        "model": model,
        "messages": messages,
        "stream": False,
        "temperature": 0.5,
        "max_tokens": 900,
        "thinking": {"type": "disabled"},
    }

    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        api_url,
        data=body,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
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
            readable_message = "DeepSeek API Key 无效或已过期，请检查配置"
        elif exc.code == 402 or "insufficient balance" in error_message.lower():
            readable_message = "DeepSeek 账户余额不足，请充值后再试"
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

    answer = str((choices[0].get("message") or {}).get("content") or "").strip()
    if not answer:
        return {
            "ok": False,
            "code": "empty_answer",
            "message": "DeepSeek 回答为空",
            "raw": result,
        }

    clean_answer = sanitize_answer(answer)
    return {
        "ok": True,
        "answer": clean_answer,
        "ttsText": make_tts_text(clean_answer),
        "model": result.get("model", model),
        "usage": result.get("usage", {}),
    }


def get_lan_ip_hint() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("192.0.2.10", 80))
            return sock.getsockname()[0]
    except OSError:
        return "你的当前局域网 IP"


def health_payload() -> dict[str, Any]:
    voice_status = get_voice_status()
    return {
        "ok": True,
        "service": "raspberry-pi-voice-assistant",
        "framework": "fastapi",
        "model": env("DEEPSEEK_MODEL", DEFAULT_DEEPSEEK_MODEL),
        "apiKeyConfigured": bool(get_api_key()),
        "audioPipelineReady": voice_status["ready"],
        "voiceEngine": voice_status,
        "time": int(time.time()),
    }


def capability_payload() -> dict[str, Any]:
    voice_status = get_voice_status()
    return {
        "ok": True,
        "service": "raspberry-pi-voice-assistant",
        "supports": {
            "textChat": True,
            "voiceChat": voice_status["ready"],
            "stt": voice_status["ready"],
            "tts": False,
            "audioUpload": voice_status["ready"],
        },
        "notes": [
            "当前版本保留独立 AI 文本问答接口。",
            "语音问答通过上传 16k 单声道 WAV 到 /api/smp/voice/chat 完成。",
            "语音播报由本地播放器完成，不占用 8000、8080、8090。",
        ],
    }


@app.get("/")
def root() -> dict[str, Any]:
    payload = health_payload()
    payload["recommendedHealthPath"] = "/api/smp/health"
    payload["recommendedChatPath"] = "/api/smp/chat"
    payload["recommendedVoicePath"] = "/api/smp/voice/chat"
    return payload


@app.get("/api/smp/health")
def health() -> dict[str, Any]:
    return health_payload()


@app.get("/api/smp/capabilities")
def api_capabilities() -> dict[str, Any]:
    return capability_payload()


@app.post("/api/smp/chat")
@app.post("/api/smp/assistant/chat")
def api_chat(data: dict[str, Any]) -> JSONResponse:
    try:
        question = normalize_question(data)
        history = normalize_history(data)
        context = normalize_context(data)
        user_name = normalize_user_name(data)
        response_style = normalize_response_style(data)
    except ValueError as exc:
        return JSONResponse(
            status_code=400,
            content={
                "ok": False,
                "code": "bad_request",
                "message": str(exc),
            },
        )

    result = call_deepseek(
        question=question,
        history=history,
        context=context,
        response_style=response_style,
        user_name=user_name,
    )
    return JSONResponse(status_code=200 if result.get("ok") else 502, content=result)


@app.post("/api/smp/voice/chat")
async def api_voice_chat(
    audio: UploadFile = File(...),
    responseStyle: str = Form("spoken"),
    context: str = Form(""),
    userName: str = Form(""),
    messages: str = Form("[]"),
) -> JSONResponse:
    voice_status = get_voice_status()
    if not voice_status["ready"]:
        return JSONResponse(
            status_code=503,
            content={
                "ok": False,
                "code": "voice_pipeline_not_ready",
                "message": voice_status["reason"] or "语音识别未就绪",
                "voiceEngine": voice_status,
            },
        )

    filename = audio.filename or "voice.wav"
    if not filename.lower().endswith(".wav"):
        return JSONResponse(
            status_code=400,
            content={
                "ok": False,
                "code": "unsupported_audio",
                "message": "当前只支持 WAV 格式录音上传",
            },
        )

    try:
        audio_bytes = await audio.read()
        stt_result = transcribe_wav_bytes(audio_bytes)
    except ValueError as exc:
        return JSONResponse(
            status_code=400,
            content={
                "ok": False,
                "code": "bad_audio",
                "message": str(exc),
            },
        )
    except RuntimeError as exc:
        return JSONResponse(
            status_code=503,
            content={
                "ok": False,
                "code": "voice_pipeline_not_ready",
                "message": str(exc),
                "voiceEngine": voice_status,
            },
        )

    form_data = {
        "question": stt_result["text"],
        "responseStyle": responseStyle,
        "context": context,
        "userName": userName,
        "messages": safe_json_loads(messages),
    }

    result = call_deepseek(
        question=normalize_question(form_data),
        history=normalize_history(form_data),
        context=normalize_context(form_data),
        response_style=normalize_response_style(form_data),
        user_name=normalize_user_name(form_data),
    )

    if result.get("ok"):
        result["question"] = stt_result["text"]
        result["transcript"] = stt_result["text"]
        result["stt"] = {
            "engine": voice_status["engine"],
            "sampleRate": stt_result["sampleRate"],
            "durationMs": stt_result["durationMs"],
        }

    return JSONResponse(status_code=200 if result.get("ok") else 502, content=result)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Raspberry Pi voice assistant backend")
    parser.add_argument("--host", default=env("SMP_API_HOST", DEFAULT_HOST), help="监听地址")
    parser.add_argument(
        "--port",
        type=int,
        default=int(env("SMP_API_PORT", str(DEFAULT_PORT))),
        help="监听端口，默认 8010",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    lan_ip = get_lan_ip_hint()
    voice_status = get_voice_status()

    print("Raspberry Pi voice assistant backend started.")
    print(f"Health check: http://127.0.0.1:{args.port}/api/smp/health")
    print(f"Capabilities: http://127.0.0.1:{args.port}/api/smp/capabilities")
    print(f"Chat API: http://{lan_ip}:{args.port}/api/smp/chat")
    print(f"Voice API: http://{lan_ip}:{args.port}/api/smp/voice/chat")
    if voice_status["ready"]:
        print(f"Voice STT ready. Model: {voice_status['modelPath']}")
    else:
        print(f"Voice STT not ready: {voice_status['reason']}")
    print("This backend is isolated from ports 8000, 8080 and 8090.")
    print("Press Ctrl+C to stop.")

    uvicorn.run(app, host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
