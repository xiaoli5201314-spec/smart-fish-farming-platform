"""Check publication inputs without loading application modules or networking."""

from __future__ import annotations

import hashlib
import ipaddress
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCAL_DIRS = {".git", ".verification", ".venv", "__pycache__", ".pytest_cache"}
FORBIDDEN_DIRS = {"node_modules", ".pio", "build", "dist", "release", "debug",
                  "models", "data", "logs"}
FORBIDDEN_SUFFIXES = {
    ".zip", ".rar", ".7z", ".pdf", ".docx", ".pptx", ".uasset", ".umap",
    ".exe", ".dll", ".o", ".obj", ".pyc", ".pyo", ".log", ".csv", ".db",
    ".sqlite", ".wav", ".mp3",
}
TEXT_SUFFIXES = {
    ".md", ".py", ".js", ".html", ".css", ".json", ".ini", ".cpp", ".h",
    ".ui", ".pro", ".qrc", ".wxml", ".wxss", ".txt", ".example",
}
EMPTY_CPP_CONFIG = re.compile(
    r'\b(?:kWifiSsid|kWifiPassword|kMqttBrokerHost|kMqttUsername|kMqttPassword)'
    r'\[\]\s*=\s*"([^"]*)"'
)
IPV4 = re.compile(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])")
HIGH_RISK_TOKEN = re.compile(
    r"(?:(?<![A-Za-z0-9])sk-[A-Za-z0-9_-]{16,}|AKIA[A-Z0-9]{16}|wx[0-9a-f]{16}|"
    r"-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----)"
)
LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)")


def public_files() -> list[Path]:
    return sorted(
        p for p in ROOT.rglob("*")
        if p.is_file() and not any(part in LOCAL_DIRS for part in p.relative_to(ROOT).parts)
    )


def check() -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    counts = {"python": 0, "json": 0, "xml": 0, "source_manifest": 0}
    paths = public_files()
    for path in paths:
        relative = path.relative_to(ROOT).as_posix()
        if any(part in FORBIDDEN_DIRS for part in path.relative_to(ROOT).parts):
            errors.append(f"{relative}: excluded generated/runtime directory")
        if path.suffix.lower() in FORBIDDEN_SUFFIXES or ".pro.user" in path.name:
            errors.append(f"{relative}: excluded file type")
        if path.name in {".env", "project.private.config.json", "deepseek_api_key.txt",
                         "simulator_config.json", "config.json", "project.config.json"}:
            errors.append(f"{relative}: local configuration must stay outside publication")
        if path.suffix not in TEXT_SUFFIXES and path.name != ".gitignore":
            continue
        try:
            content = path.read_text(encoding="utf-8-sig")
        except UnicodeError:
            errors.append(f"{relative}: invalid UTF-8 text")
            continue
        for match in EMPTY_CPP_CONFIG.finditer(content):
            if match[1]:
                errors.append(f"{relative}: nonempty firmware connection setting")
        if HIGH_RISK_TOKEN.search(content):
            errors.append(f"{relative}: credential-like or personal application identifier")
        for match in IPV4.finditer(content):
            try:
                address = ipaddress.ip_address(match.group())
            except ValueError:
                continue
            if not (address.is_loopback or address.is_unspecified
                    or address in ipaddress.ip_network("192.0.2.0/24")):
                errors.append(f"{relative}: non-example address at line "
                              f"{content[:match.start()].count(chr(10)) + 1}")
        try:
            if path.suffix == ".py":
                compile(content, relative, "exec")
                counts["python"] += 1
            elif path.suffix == ".json":
                json.loads(content)
                counts["json"] += 1
            elif path.suffix in {".ui", ".qrc"}:
                tree = ET.fromstring(content)
                counts["xml"] += 1
                if path.suffix == ".qrc":
                    for element in tree.iter("file"):
                        if not element.text or not (path.parent / element.text).is_file():
                            errors.append(f"{relative}: resource file cannot be resolved")
        except (SyntaxError, ValueError, ET.ParseError) as exc:
            errors.append(f"{relative}: parse failure ({type(exc).__name__})")
        if path.suffix == ".md":
            for match in LINK.finditer(content):
                target = match[1].split("#", 1)[0]
                if target and not re.match(r"^[a-z]+:", target, re.I):
                    if not (path.parent / target).exists():
                        errors.append(f"{relative}: unresolved local documentation link")

    manifest_path = ROOT / "docs" / "SOURCE_MANIFEST.json"
    if not manifest_path.is_file():
        errors.append("docs/SOURCE_MANIFEST.json: missing source inventory")
    else:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for entry in manifest["files"]:
            path = ROOT / entry["path"]
            if not path.is_file():
                errors.append(f"{entry['path']}: copied source file is missing")
            elif hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                errors.append(f"{entry['path']}: source inventory hash mismatch")
            counts["source_manifest"] += 1
    return errors, counts


def main() -> int:
    errors, counts = check()
    for error in errors:
        print("FAIL", error)
    print("PUBLIC_PACKAGE_CHECK", json.dumps(counts, sort_keys=True))
    print(f"PUBLIC_PACKAGE_ISSUES {len(errors)}")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
