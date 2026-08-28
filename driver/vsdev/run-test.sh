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
    -usb -device usb-tablet \
    -serial file:"$SERIAL" \
    -qmp unix:"$QMPSOCK",server,nowait \
    -no-reboot \
    > "$WORKDIR/qemu.log" 2>&1 &
QEMU_PID=$!

# Real, reproducible flake found and fixed by actually re-running this
# script repeatedly, not by inspection: a fixed "sleep 90; ret" to
# dismiss the LiveCD's language-selection dialog assumed one particular
# boot's observed timing. Real TCG software-emulation boot speed varies
# enough between runs (host load-dependent) that a fixed offset
# sometimes fires before the dialog has even rendered (a harmless
# no-op - the dialog is then still open when the rest of this script
# assumes a plain desktop) and sometimes fires too late, after
# something else has focus. Confirmed directly: a screendump taken at
# the old fixed offset, on a run that then failed, showed the language
# dialog still sitting there waiting - the "ret" meant to dismiss it
# had already fired into nothing minutes earlier.
#
# dismiss-dialog (driver/cell/launcher/qmp_console.py) is the adaptive
# fix: polls a screenshot pixel that's reliably part of the dialog body
# (212,208,200 - a color the plain desktop background, 58,110,165,
# never shows) and sends `ret` on every poll where it's still detected,
# only proceeding once the background color reads back for two
# consecutive polls. Idempotent and safe to poll for a while - a `ret`
# sent while the dialog's default "Next" button already has focus just
# advances it once, and subsequent polls see the background and stop.
echo "=== run-test.sh: waiting for and dismissing the LiveCD language dialog (adaptive, up to 200s) ==="
"$QMP" "$QMPSOCK" dismiss-dialog 0.3125 0.5 58 110 165 --timeout 200 --interval 5 || {
    "$QMP" "$QMPSOCK" screendump "$WORKDIR/dialog-timeout.ppm"
    echo "run-test.sh: FAIL - language dialog never cleared; see $WORKDIR/dialog-timeout.ppm"
    exit 1
}

echo "=== run-test.sh: letting the desktop settle (~8s) ==="
sleep 8
# A left-click on an empty patch of desktop (using a real USB tablet
# device for absolute positioning, added to the QEMU invocation above -
# a PS/2 mouse can't be driven this way) forces keyboard focus onto the
# desktop regardless of what has focus after the dialog closes, making
# the icon-search-by-letter step below reliable.
"$QMP" "$QMPSOCK" click 0.85 0.6   # empty desktop area, right of the icon column
sleep 1
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
