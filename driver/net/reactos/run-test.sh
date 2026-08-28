#!/bin/bash
# run-test.sh — end-to-end verification for Phase 7 (NDIS bridge,
# ARCHITECTURE.md section 22).
#
# What this actually proves, precisely: driver/ntbridge/protocol/
# ntbridge_protocol.h's net_tx_ring/net_rx_ring, driver/ntbridge/host/
# ntbridge-host's new --tap bridging, and a real Linux TAP device all
# work together over a genuine QEMU VM boundary - a synthetic frame
# pushed by the guest test client really does appear on a real Linux
# netdev (captured via a raw AF_PACKET socket, tests/reactos/
# net-tap-echo.py - the same interface a normal Linux network
# application would use, no special test hook on that side), and a
# frame sent into that same interface really does get delivered back
# into the guest via net_rx_ring.
#
# It does NOT prove driver/net/reactos/ntnet.c (the real NDIS miniport)
# is correct or even loadable inside real ReactOS - same honest
# boundary as tests/reactos/run-test.sh draws around
# driver/ntbridge/reactos/ntbridge_pnp.c: ntnet.c builds and links
# clean (see this directory's README) but has never been loaded live.
# The guest side of this specific test is the same stand-in Linux
# userspace client Phase 4 already established
# (tests/reactos/ntbridge-guest-test.c), now also exercising the net
# rings.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_DIR="$(cd "$HERE/../.." && pwd)"
TESTS_DIR="$(cd "$DRIVER_DIR/../tests/reactos" && pwd)"
NTCELL="$DRIVER_DIR/cell/launcher/ntcell"
NTBRIDGE_HOST="$DRIVER_DIR/ntbridge/host/ntbridge-host"
TAP_IFACE="ntlx-nettest0"
DURATION=15
SHM_PATH="/tmp/ntbridge-nettest-$$.shm"
HOST_LOG="/tmp/ntbridge-nettest-host-$$.log"

cleanup() {
    [ -n "${HOST_PID:-}" ] && kill "$HOST_PID" 2>/dev/null
    ip link del "$TAP_IFACE" 2>/dev/null
    rm -f "$SHM_PATH"
}
trap cleanup EXIT

echo "=== run-test.sh (Phase 7): building components ==="
make -C "$DRIVER_DIR/ntbridge/host" || exit 1
make -C "$TESTS_DIR" || exit 1
"$TESTS_DIR/build-testguest-initramfs.sh" || exit 1
# ntnet.sys itself isn't loaded by this test (see file header) but
# building it here means a real regression in the miniport still fails
# this script, not just silently bit-rots unbuilt.
make -C "$HERE" || exit 1

rm -f "$SHM_PATH"

echo "=== run-test.sh: starting ntbridge-host with --tap $TAP_IFACE ==="
"$NTBRIDGE_HOST" --shm "$SHM_PATH" --duration "$DURATION" --devices 0 --tap "$TAP_IFACE" \
    > "$HOST_LOG" 2>&1 &
HOST_PID=$!

for i in $(seq 1 20); do
    [ -f "$SHM_PATH" ] && ip link show "$TAP_IFACE" >/dev/null 2>&1 && break
    sleep 0.2
done
if ! ip link show "$TAP_IFACE" >/dev/null 2>&1; then
    echo "run-test.sh: FAIL - ntbridge-host never created TAP device $TAP_IFACE"
    cat "$HOST_LOG"
    exit 1
fi
ip link set "$TAP_IFACE" up

echo "=== run-test.sh: starting net-tap-echo.py ==="
"$TESTS_DIR/net-tap-echo.py" "$TAP_IFACE" "$DURATION" > /tmp/ntbridge-nettest-echo-$$.log 2>&1 &
ECHO_PID=$!

echo "=== run-test.sh: booting test guest under QEMU via ntcell ==="
NTCELL_SHM_PATH="$SHM_PATH" "$NTCELL" boot-testguest --duration "$((DURATION - 3))" --timeout "$((DURATION + 15))"

echo "=== run-test.sh: waiting for net-tap-echo.py and ntbridge-host ==="
wait "$ECHO_PID"
ECHO_STATUS=$?
wait "$HOST_PID"
HOST_PID=""

echo "=== run-test.sh: net-tap-echo.py output ==="
cat /tmp/ntbridge-nettest-echo-$$.log
rm -f /tmp/ntbridge-nettest-echo-$$.log

echo "=== run-test.sh: ntbridge-host output ==="
cat "$HOST_LOG"

HOST_TO_GUEST_OK=0
if grep -q "net: received expected test frame on net_rx_ring - PASS" "$HOST_LOG"; then
    HOST_TO_GUEST_OK=1
fi
rm -f "$HOST_LOG"

echo "=== run-test.sh: result ==="
echo "guest->host (captured on real Linux TAP interface via raw socket): $([ $ECHO_STATUS -eq 0 ] && echo PASS || echo FAIL)"
echo "host->guest (guest confirmed receipt via net_rx_ring, seen in ntbridge-host's log): $([ $HOST_TO_GUEST_OK -eq 1 ] && echo PASS || echo FAIL)"

if [ "$ECHO_STATUS" -eq 0 ] && [ "$HOST_TO_GUEST_OK" -eq 1 ]; then
    echo "PASS: NDIS bridge network round-trip verified both directions over a real QEMU VM" \
         "boundary and a real Linux TAP device."
    echo "NOTE: verified against the stand-in guest test client, not the real NDIS miniport" \
         "(driver/net/reactos/ntnet.c builds and links but has never been loaded inside real" \
         "ReactOS - see that directory's README)."
    exit 0
else
    echo "FAIL: see the captured output above."
    exit 1
fi
