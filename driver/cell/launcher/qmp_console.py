#!/usr/bin/env python3
"""
qmp_console.py — QMP client for driving a QEMU guest's console via
synthetic keyboard input, used to automate ReactOS's text-mode
dialogs and cmd.exe console (no guest networking or agent needed).

Grown out of Phase 5's actual, hands-on driver verification
(driver/vsdev/run-test.sh, ROADMAP.md Phase 5) rather than written
speculatively - every primitive here (screendump, sendkey, type) was
exercised live, repeatedly, driving a real ReactOS boot through its
LiveCD language dialog and a real cmd.exe session.

Usage:
  qmp_console.py <qmp-socket> screendump <out.ppm>
  qmp_console.py <qmp-socket> sendkey <qcode>[,<qcode>...]
  qmp_console.py <qmp-socket> type <text>
"""
import json
import socket
import sys
import time


def qmp_call(sock, command, arguments=None):
    payload = {"execute": command}
    if arguments is not None:
        payload["arguments"] = arguments
    sock.sendall((json.dumps(payload) + "\n").encode())
    return read_json(sock)


def read_json(sock):
    buf = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk
        try:
            return json.loads(buf.decode())
        except json.JSONDecodeError:
            continue
    raise RuntimeError("QMP connection closed before a complete response")


def connect(sock_path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(sock_path)
    read_json(s)  # greeting
    qmp_call(s, "qmp_capabilities")
    return s


# US QWERTY character -> (needs_shift, qcode) map. Extend as needed -
# these are exactly the characters Phase 5's verification needed to type
# (paths, `sc create`/`sc start` command lines, a redirection operator).
CHAR_MAP = {
    ':': (True, 'semicolon'),
    '\\': (False, 'backslash'),
    '.': (False, 'dot'),
    ' ': (False, 'spc'),
    '_': (True, 'minus'),
    '-': (False, 'minus'),
    '%': (True, '5'),
    '=': (False, 'equal'),
    '>': (True, 'dot'),
    '<': (True, 'comma'),
}


def char_to_key(c):
    if c.isalpha():
        return (c.isupper(), c.lower())
    if c.isdigit():
        return (False, c)
    if c in CHAR_MAP:
        return CHAR_MAP[c]
    raise ValueError(f"qmp_console: no key mapping for character {c!r} "
                      f"(add one to CHAR_MAP)")


def send_keys(sock, keys):
    """Send a chord: all `keys` (QMP qcode names) pressed together, then
    released together - e.g. ["shift", "a"] for 'A'."""
    qmp_call(sock, "send-key", {"keys": [{"type": "qcode", "data": k} for k in keys]})


def type_string(sock, text, delay_s=0.03):
    for c in text:
        shift, code = char_to_key(c)
        send_keys(sock, ["shift", code] if shift else [code])
        time.sleep(delay_s)


def screendump(sock, out_path):
    r = qmp_call(sock, "screendump", {"filename": out_path})
    if "return" not in r:
        raise RuntimeError(f"screendump failed: {r}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    sock_path, cmd = sys.argv[1], sys.argv[2]
    s = connect(sock_path)

    if cmd == "screendump":
        screendump(s, sys.argv[3])
        print(f"wrote {sys.argv[3]}")
    elif cmd == "sendkey":
        send_keys(s, sys.argv[3].split(","))
    elif cmd == "type":
        type_string(s, sys.argv[3])
    else:
        print(f"qmp_console.py: unknown command {cmd!r}", file=sys.stderr)
        sys.exit(1)
