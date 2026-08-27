#!/bin/bash
# run-test.sh — end-to-end ntbridge verification (Rule 11: every NT
# compatibility implementation requires tests).
#
# What this actually proves, precisely: driver/ntbridge/protocol/
# ntbridge_protocol.h's wire format, driver/ntbridge/host/ntbridge-host,
# and the SPSC ring transport all work for real across a genuine QEMU
# VM boundary — heartbeat, the logging channel, and the device
# enumeration bridge (seed -> arrive -> ack) — using the honest stand-in
# guest (ntbridge-guest-test) in place of the real, not-yet-buildable
# ReactOS driver (driver/ntbridge/reactos/, needs RosBE — see that
# directory's README). It does NOT prove the ReactOS-side driver itself
# is correct; that half is unverified until this repo can actually
# build ReactOS from source. Say so in the summary this script prints,
# not just in a comment nobody reads.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_DIR="$(cd "$HERE/../../driver" && pwd)"
NTCELL="$DRIVER_DIR/cell/launcher/ntcell"
NTBRIDGE_HOST="$DRIVER_DIR/ntbridge/host/ntbridge-host"

DURATION=10
SHM_PATH="/tmp/ntbridge-test-$$.shm"

cleanup() {
    [ -n "${HOST_PID:-}" ] && kill "$HOST_PID" 2>/dev/null
    rm -f "$SHM_PATH"
}
trap cleanup EXIT

echo "=== run-test.sh: building components ==="
make -C "$DRIVER_DIR/ntbridge/host" || exit 1
make -C "$HERE" || exit 1
"$HERE/build-testguest-initramfs.sh" || exit 1

rm -f "$SHM_PATH"

echo "=== run-test.sh: starting ntbridge-host (shm=$SHM_PATH, duration=${DURATION}s) ==="
"$NTBRIDGE_HOST" --shm "$SHM_PATH" --duration "$DURATION" --devices 3 > /tmp/ntbridge-host-test.log 2>&1 &
HOST_PID=$!

# ntbridge-host creates+initializes $SHM_PATH on startup; give it a
# moment before pointing QEMU at the same file.
for i in $(seq 1 20); do
    [ -f "$SHM_PATH" ] && break
    sleep 0.1
done
if [ ! -f "$SHM_PATH" ]; then
    echo "run-test.sh: FAIL — ntbridge-host never created $SHM_PATH"
    exit 1
fi

echo "=== run-test.sh: booting test guest under QEMU via ntcell ==="
NTCELL_SHM_PATH="$SHM_PATH" "$NTCELL" boot-testguest --duration "$((DURATION - 2))" --timeout "$((DURATION + 15))"

echo "=== run-test.sh: waiting for ntbridge-host to finish ==="
wait "$HOST_PID"
HOST_STATUS=$?
HOST_PID=""

echo "=== run-test.sh: ntbridge-host log ==="
cat /tmp/ntbridge-host-test.log

echo "=== run-test.sh: result ==="
if [ "$HOST_STATUS" -eq 0 ]; then
    echo "PASS: guest heartbeat observed, all seeded devices acked — ntbridge protocol/" \
         "transport/host verified end-to-end over a real QEMU VM boundary."
    echo "NOTE: verified against the stand-in guest test client (tests/reactos/), not the" \
         "real ReactOS driver (driver/ntbridge/reactos/ — not yet buildable here, needs RosBE)."
    exit 0
else
    echo "FAIL: ntbridge-host exited with status $HOST_STATUS — see log above."
    exit 1
fi
