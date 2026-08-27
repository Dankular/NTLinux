#!/usr/bin/env python3
"""
qmp_screendump.py — tiny QMP client used by ntcell to prove a booted
ReactOS driver cell is genuinely producing framebuffer output (not just
that the QEMU process didn't exit), the same way earlier phases of this
project verified a real rendered window rather than trusting a process
exit code alone.

Usage: qmp_screendump.py <qmp-unix-socket> <output-ppm-path>
"""
import json
import socket
import sys


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


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1
    sock_path, out_path = sys.argv[1], sys.argv[2]

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(sock_path)

    greeting = read_json(s)  # QMP sends a greeting with capabilities first
    if "QMP" not in greeting:
        raise RuntimeError(f"unexpected QMP greeting: {greeting}")

    resp = qmp_call(s, "qmp_capabilities")
    if "return" not in resp:
        raise RuntimeError(f"qmp_capabilities failed: {resp}")

    resp = qmp_call(s, "screendump", {"filename": out_path})
    if "return" not in resp:
        raise RuntimeError(f"screendump failed: {resp}")

    print(f"qmp_screendump.py: wrote {out_path}")
    s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
