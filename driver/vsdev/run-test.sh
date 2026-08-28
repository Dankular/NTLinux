#!/bin/bash
# run-test.sh — automated, unattended verification for vsdev.sys (Rule 11).
#
# Boots a real ReactOS x64 kernel under QEMU, drives its console via
# synthetic keyboard input (driver/cell/launcher/qmp_console.py) exactly
# as this driver was first verified live (see ROADMAP.md Phase 5 for the
# manual run and the two real bugs that run found), and reads a real
# pass/fail verdict back from the guest via a file dropped on the floppy
# (A:\RESULT.TXT) - no guest networking, no OCR on a screendump needed.
#
# What this proves, precisely, same as the manual run it automates: a
# real driver loads via the legacy `sc start` path and performs real
# I/O. It does NOT exercise the PnP/AddDevice path (vsdev.inf) or
# ntbridge routing - see driver/vsdev/README.md for that accounting.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGES_DIR="$(cd "$HERE/../cell/images" && pwd)"
LAUNCHER_DIR="$(cd "$HERE/../cell/launcher" && pwd)"
QMP="$LAUNCHER_DIR/qmp_console.py"

WORKDIR="$(mktemp -d /tmp/vsdev-test.XXXXXX)"
FLOPPY="$WORKDIR/vsdev-floppy.img"
QMPSOCK="$WORKDIR/qmp.sock"
SERIAL="$WORKDIR/serial.log"

cleanup() {
    [ -n "${QEMU_PID:-}" ] && kill -9 "$QEMU_PID" 2>/dev/null
}
trap cleanup EXIT

echo "=== run-test.sh: building vsdev.sys + vsdev_test.exe ==="
make -C "$HERE" || exit 1
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -o "$HERE/vsdev_test.exe" "$HERE/vsdev_test.c" || exit 1

echo "=== run-test.sh: locating a ReactOS x64 nightly LiveCD ISO ==="
ISO="$("$IMAGES_DIR/fetch-reactos-x64-nightly.sh")" || exit 1
echo "using $ISO"

echo "=== run-test.sh: mastering floppy image ==="
dd if=/dev/zero of="$FLOPPY" bs=1024 count=1440 status=none
mformat -i "$FLOPPY" -f 1440 ::
mcopy -i "$FLOPPY" "$HERE/vsdev.sys" ::VSDEV.SYS
mcopy -i "$FLOPPY" "$HERE/vsdev_test.exe" ::VSDTEST.EXE

echo "=== run-test.sh: booting ReactOS under QEMU ==="
qemu-system-x86_64 \
    -M pc -m 1024 -accel tcg \
    -cdrom "$ISO" \
    -drive file="$FLOPPY",if=floppy,format=raw \
    -boot d \
    -display none -vga std \
    -serial file:"$SERIAL" \
    -qmp unix:"$QMPSOCK",server,nowait \
    -no-reboot \
    > "$WORKDIR/qemu.log" 2>&1 &
QEMU_PID=$!

# Timings below are generous multiples of what the manual verification
# run actually took (ROADMAP.md Phase 5) - this is a debug/dev-build x64
# LiveCD under TCG software emulation, genuinely slow to reach a desktop.
echo "=== run-test.sh: waiting for boot to reach the LiveCD language dialog (~90s) ==="
sleep 90
"$QMP" "$QMPSOCK" sendkey ret   # accept language dialog defaults
sleep 15
"$QMP" "$QMPSOCK" sendkey ret   # "Run ReactOS Live CD" (if this build shows the chooser)

echo "=== run-test.sh: waiting for desktop (~20s) ==="
sleep 20
"$QMP" "$QMPSOCK" sendkey c     # jump to "Command Prompt" desktop icon
sleep 2
"$QMP" "$QMPSOCK" sendkey ret   # launch it
sleep 8

echo "=== run-test.sh: installing and running vsdev ==="
"$QMP" "$QMPSOCK" type "sc create vsdev type= kernel binPath= A:\VSDEV.SYS"
"$QMP" "$QMPSOCK" sendkey ret
sleep 3
"$QMP" "$QMPSOCK" type "sc start vsdev"
"$QMP" "$QMPSOCK" sendkey ret
sleep 3
"$QMP" "$QMPSOCK" type "A:\VSDTEST.EXE"
"$QMP" "$QMPSOCK" sendkey ret
sleep 5

echo "=== run-test.sh: capturing final screen state ==="
"$QMP" "$QMPSOCK" screendump "$WORKDIR/final.ppm"

kill -9 "$QEMU_PID" 2>/dev/null
wait "$QEMU_PID" 2>/dev/null
QEMU_PID=""

echo "=== run-test.sh: reading A:\\RESULT.TXT from the floppy ==="
RESULT="$(mtype -i "$FLOPPY" ::RESULT.TXT 2>/dev/null | tr -d '\r\n')"
echo "guest-reported result: '$RESULT'"
echo "screendump saved at $WORKDIR/final.ppm (kept for inspection)"

if [ "$RESULT" = "PASS" ]; then
    echo "PASS: vsdev.sys loaded and a real CreateFile/WriteFile/ReadFile round-trip succeeded."
    exit 0
else
    echo "FAIL: expected 'PASS' in A:\\RESULT.TXT, got '$RESULT' (empty means vsdev_test.exe" \
         "never ran or wrote it - check $WORKDIR/final.ppm and $SERIAL)"
    exit 1
fi
