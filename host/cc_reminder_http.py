#!/usr/bin/env python3
"""
cc-reminder (HTTP version) - thay cho test_ble.py

Dùng: cc-reminder IDLE | WORKING | INTERACT | OFF | STATUS

Không cần bleak, chỉ dùng stdlib. Không bao giờ raise ra ngoài
để hook của Claude Code không bị fail khi ESP8266 offline.

Host có thể set qua biến môi trường:
    export CC_REMINDER_HOST=cc-reminder.local     # hoặc IP tĩnh
"""

import os
import sys
import urllib.request
import urllib.error

HOST = os.environ.get("CC_REMINDER_HOST", "cc-reminder.local")
TIMEOUT = float(os.environ.get("CC_REMINDER_TIMEOUT", "1.5"))
VALID = {"IDLE", "WORKING", "INTERACT", "OFF", "STATUS"}


def call(path: str) -> str:
    url = f"http://{HOST}{path}"
    with urllib.request.urlopen(url, timeout=TIMEOUT) as r:
        return r.read().decode().strip()


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: cc-reminder IDLE|WORKING|INTERACT|OFF|STATUS", file=sys.stderr)
        return 2

    state = sys.argv[1].upper()
    if state not in VALID:
        print(f"unknown state: {state}", file=sys.stderr)
        return 2

    path = "/status" if state == "STATUS" else f"/set?state={state}"

    try:
        print(call(path))
    except (urllib.error.URLError, OSError) as e:
        # Im lặng thất bại: LED chỉ là phụ, không được chặn Claude Code
        print(f"cc-reminder: unreachable ({e})", file=sys.stderr)
        return 0

    return 0


if __name__ == "__main__":
    sys.exit(main())
