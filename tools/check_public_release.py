#!/usr/bin/env python3
"""Fail when obvious private or generated material is about to be published."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

FORBIDDEN_PARTS = {
    ".learnings",
    "agent-handoff",
    "artifacts",
    "backups",
    "build",
    "graphify-out",
    "host-evidence",
    "logs",
    "managed_components",
    "reference",
}
FORBIDDEN_NAMES = {
    ".DS_Store",
    "known_hosts",
    "id_rsa",
    "tab5_lock_pin_private.h",
    "tab5-terminal.json",
}
FORBIDDEN_SUFFIXES = {
    ".bin",
    ".elf",
    ".map",
    ".p12",
    ".pem",
    ".pfx",
    ".pyc",
    ".tar.gz",
    ".tgz",
    ".zip",
}
CONTENT_RULES = {
    "personal email address": re.compile(r"stafford\.lumsden@sydney\.edu\.au", re.I),
    "absolute host home path": re.compile(r"/Users/staffordlumsden(?:/|\b)"),
    "private Tailscale hostname": re.compile(r"acdp2d61ypx\.taile24d41\.ts\.net", re.I),
    "live USB serial device": re.compile(r"/dev/(?:cu|tty)\.[^\s`\"']*usbmodem\d{4,}", re.I),
    "previously observed LAN address": re.compile(r"\b192\.168\.1\.(?:106|112)\b"),
    "GitHub token": re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})\b"),
    "cloud API key": re.compile(r"\b(?:AKIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{30,})\b"),
    "OpenAI-style key": re.compile(r"\bsk-(?!(?:ecdsa|ssh)-)[A-Za-z0-9_-]{20,}\b"),
    "Slack token": re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}\b"),
    "Tailscale auth key": re.compile(r"\btskey-(?!auth-x{12}\b)[A-Za-z0-9_-]{10,}\b"),
    "PEM private key": re.compile(
        r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----\s+[A-Za-z0-9+/=\s]{40,}"
    ),
}


def repository_files() -> list[Path]:
    output = subprocess.check_output(
        ["git", "ls-files", "-co", "--exclude-standard", "-z"]
    )
    paths = [Path(raw.decode()) for raw in output.split(b"\0") if raw]
    return [
        path for path in paths
        if path.is_file() and not (path / ".git").exists()
    ]


def main() -> int:
    issues: list[str] = []
    for path in repository_files():
        if path.resolve() == Path(__file__).resolve():
            continue
        text_path = str(path)
        if text_path == "projects/tab5-media-player" or text_path.startswith("projects/tab5-media-player/"):
            # The separate public companion is represented by a submodule gitlink.
            continue
        if any(part in FORBIDDEN_PARTS for part in path.parts):
            issues.append(f"{path}: forbidden generated/private path")
            continue
        if path.name in FORBIDDEN_NAMES or path.name.endswith(tuple(FORBIDDEN_SUFFIXES)):
            issues.append(f"{path}: forbidden private/generated file type")
            continue
        if path.name.startswith("._"):
            issues.append(f"{path}: AppleDouble metadata file")
            continue
        try:
            text = path.read_text()
        except (UnicodeDecodeError, OSError):
            continue
        for label, pattern in CONTENT_RULES.items():
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                issues.append(f"{path}:{line}: {label}")
    if issues:
        print("PUBLIC RELEASE CHECK FAILED", file=sys.stderr)
        for issue in issues:
            print(f"- {issue}", file=sys.stderr)
        return 1
    print("PASS: no forbidden paths or obvious secrets/PII found")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
