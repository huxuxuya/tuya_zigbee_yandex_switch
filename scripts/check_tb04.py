#!/usr/bin/env python3
"""Проверка подключения Ai-Thinker TB-04 через USB-UART (CH340).

Использование:
    python3 scripts/check_tb04.py
    python3 scripts/check_tb04.py --port /dev/cu.usbserial-21140 --baud 115200
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("нужен pyserial: pip3 install pyserial", file=sys.stderr)
    sys.exit(2)


def cmd(s: "serial.Serial", text: str, wait: float = 0.6) -> bytes:
    s.reset_input_buffer()
    s.write((text + "\r\n").encode())
    time.sleep(wait)
    return s.read_all()


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="/dev/cu.usbserial-21140")
    p.add_argument("--baud", type=int, default=115200)
    a = p.parse_args()

    try:
        s = serial.Serial(a.port, a.baud, timeout=1)
    except Exception as e:
        print(f"FAIL: не могу открыть {a.port} @{a.baud}: {e}")
        return 1

    print(f"OPEN OK {a.port} @{a.baud}")

    # При открытии TB-04-KIT сам шлет boot-лог
    time.sleep(0.8)
    boot = s.read_all().decode(errors="replace")
    if boot.strip():
        print("--- boot log ---")
        print(boot.strip())

    checks = ["AT", "AT+NAME?", "AT+MAC?", "AT+BAUD?"]
    ok = True
    for c in checks:
        resp = cmd(s, c).decode(errors="replace")
        status = "OK" if ("OK" in resp or "ready" in resp.lower() or "TB-04" in resp) else "WARN"
        if status != "OK":
            ok = False
        print(f"> {c} [{status}]: {resp.strip()!r}")

    s.close()
    print("PASS: TB-04 отвечает" if ok else "WARN: порт открылся, но часть AT без OK")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
