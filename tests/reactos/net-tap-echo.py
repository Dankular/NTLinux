#!/usr/bin/env python3
"""
net-tap-echo.py — the "real Linux side" half of the Phase 7 NDIS bridge
network round-trip test (see ntbridge-guest-test.c's net_tx_ring/
net_rx_ring exercise, and driver/net/reactos/run-test.sh).

Opens a raw AF_PACKET socket on the TAP interface ntbridge-host --tap
created — this is genuinely what a normal Linux network application
sees on that interface, not a special test hook. It:

  1. Captures the guest's outbound test frame (tagged
     "NTLXNETTEST-GUEST-TO-HOST") to prove ntbridge-host really wrote
     what net_tx_ring carried onto a real Linux netdev.
  2. Sends a distinctly-tagged frame ("NTLXNETTEST-HOST-TO-GUEST") INTO
     the interface, which the kernel hands to the TAP driver, which
     ntbridge-host reads off the TAP fd and pushes into net_rx_ring for
     the guest to pick up - proving the reverse direction too.

Usage: net-tap-echo.py <ifname> [timeout_seconds]
Exit 0 if the guest's frame was captured (regardless of whether the
host->guest send was also attempted - see the script's own PASS/FAIL
print for exactly what happened).
"""
import socket
import struct
import sys
import time

ETH_P_ALL = 0x0003
TX_MAGIC = b"NTLXNETTEST-GUEST-TO-HOST"
RX_MAGIC = b"NTLXNETTEST-HOST-TO-GUEST"


def build_frame(magic: bytes) -> bytes:
    dst = b"\xff\xff\xff\xff\xff\xff"
    src = b"\x02\x02\x02\x02\x02\x02"
    ethertype = struct.pack("!H", 0x88B5)  # IEEE 802 "local experimental"
    return dst + src + ethertype + magic


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 1
    ifname = sys.argv[1]
    timeout_s = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
    s.bind((ifname, 0))
    s.settimeout(timeout_s)

    print(f"net-tap-echo.py: listening on {ifname} for guest's test frame "
          f"(timeout {timeout_s}s)...")

    captured = False
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            data = s.recv(2048)
        except socket.timeout:
            break
        if TX_MAGIC in data:
            print(f"net-tap-echo.py: captured guest frame ({len(data)} bytes) - "
                  f"contains {TX_MAGIC!r}: PASS")
            captured = True
            break
        # else: some other frame (e.g. our own echo below looped back
        # locally, or unrelated interface noise) - keep listening.

    print(f"net-tap-echo.py: sending host->guest test frame into {ifname}...")
    try:
        s.send(build_frame(RX_MAGIC))
        print("net-tap-echo.py: host->guest frame sent")
    except OSError as e:
        print(f"net-tap-echo.py: send failed: {e}", file=sys.stderr)

    s.close()

    if not captured:
        print("net-tap-echo.py: FAIL - never captured the guest's test frame")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
