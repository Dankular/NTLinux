#!/bin/bash
# run-test.sh — end-to-end verification for Phase 8 (USB bridge,
# ARCHITECTURE.md section 27's "USB device cell").
#
# What this actually proves, precisely: driver/ntbridge/protocol/
# ntbridge_protocol.h's usb_req_ring/usb_resp_ring and driver/ntbridge/
# host/ntbridge-host's new --usb-echo mode work together over a genuine
# QEMU VM boundary - a tagged request the guest test client pushes onto
# usb_req_ring really is drained by the host process running in a
# separate VM, and a distinctly-tagged reply really does make it back
# into the guest via usb_resp_ring.
#
# It does NOT prove driver/usb/reactos/ntusb.c (the real bus driver) is
# correct or even loadable inside real ReactOS - same honest boundary
# tests/reactos/run-test.sh draws around ntbridge_pnp.c and driver/net/
# reactos/run-test.sh draws around ntnet.c. ntusb.c's URB-processing
# core (NtusbProcessUrb) IS exercised live, separately, via
# ntusb_test.exe's test-only IOCTL path once this same test infra can
# boot a ReactOS build carrying it - not yet attempted here (ROADMAP.md
# Phase 14 tracks the live-PnP-load gap for all of Phase 4/5/7/8's
# drivers together).
#
# Also does NOT prove a real physical USB device works through this
# bridge - --usb-echo is an honest software-only stand-in (see
# driver/ntbridge/host/README.md); this sandbox's kernel has no USB
# subsystem at all (CONFIG_USB not set, confirmed directly).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_DIR="$(cd "$HERE/../.." && pwd)"
TESTS_DIR="$(cd "$DRIVER_DIR/../tests/reactos" && pwd)"
NTCELL="$DRIVER_DIR/cell/launcher/ntcell"
NTBRIDGE_HOST="$DRIVER_DIR/ntbridge/host/ntbridge-host"
DURATION=15
SHM_PATH="/tmp/ntbridge-usbtest-$$.shm"
HOST_LOG="/tmp/ntbridge-usbtest-host-$$.log"

cleanup() {
    [ -n "${HOST_PID:-}" ] && kill "$HOST_PID" 2>/dev/null
    rm -f "$SHM_PATH" "$HOST_LOG"
}
trap cleanup EXIT

echo "=== run-test.sh (Phase 8): building components ==="
make -C "$DRIVER_DIR/ntbridge/host" || exit 1
make -C "$TESTS_DIR" || exit 1
"$TESTS_DIR/build-testguest-initramfs.sh" || exit 1
# ntusb.sys itself isn't loaded by this test (see file header) but
# building it here means a real regression in the bridge driver still
# fails this script, not just silently bit-rots unbuilt.
make -C "$HERE" || exit 1

rm -f "$SHM_PATH"

echo "=== run-test.sh: starting ntbridge-host with --usb-echo ==="
"$NTBRIDGE_HOST" --shm "$SHM_PATH" --duration "$DURATION" --devices 0 --usb-echo \
    > "$HOST_LOG" 2>&1 &
HOST_PID=$!

for i in $(seq 1 20); do
    [ -f "$SHM_PATH" ] && break
    sleep 0.2
done
if [ ! -f "$SHM_PATH" ]; then
    echo "run-test.sh: FAIL - ntbridge-host never created $SHM_PATH"
    cat "$HOST_LOG"
    exit 1
fi

echo "=== run-test.sh: booting test guest under QEMU via ntcell ==="
NTCELL_SHM_PATH="$SHM_PATH" "$NTCELL" boot-testguest --duration "$((DURATION - 3))" --timeout "$((DURATION + 15))"

echo "=== run-test.sh: waiting for ntbridge-host ==="
wait "$HOST_PID"
HOST_PID=""

echo "=== run-test.sh: ntbridge-host output ==="
cat "$HOST_LOG"

USB_OK=0
if grep -q "usb: received expected reply on usb_resp_ring - PASS" "$HOST_LOG"; then
    USB_OK=1
fi

echo "=== run-test.sh: result ==="
echo "USB bulk round-trip (guest request -> host echo -> guest, over a real QEMU VM boundary): $([ $USB_OK -eq 1 ] && echo PASS || echo FAIL)"

if [ "$USB_OK" -eq 1 ]; then
    echo "PASS: USB bridge protocol/transport/host-echo verified end-to-end over a real QEMU" \
         "VM boundary."
    echo "NOTE: verified against the stand-in guest test client, not the real bus driver" \
         "(driver/usb/reactos/ntusb.c builds and links but has never been loaded inside real" \
         "ReactOS - see that directory's README and ROADMAP.md Phase 14)."
    exit 0
else
    echo "FAIL: see the captured output above."
    exit 1
fi
