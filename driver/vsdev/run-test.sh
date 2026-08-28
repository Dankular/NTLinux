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

# Windows-host detection, same pattern and same real reason as
# driver/cell/launcher/ntcell (see that script's own comment): no
# socket.AF_UNIX on this host's Python, and no mtools binary reachable
# without admin rights to fix this host's broken chocolatey lock state
# (confirmed live, not assumed - see ROADMAP.md Phase 5's Windows-host
# re-verification). Linux behavior below is completely unchanged.
IS_WINDOWS_HOST=0
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) IS_WINDOWS_HOST=1 ;;
esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGES_DIR="$(cd "$HERE/../cell/images" && pwd)"
LAUNCHER_DIR="$(cd "$HERE/../cell/launcher" && pwd)"
QMP="$LAUNCHER_DIR/qmp_console.py"

WORKDIR="$(mktemp -d /tmp/vsdev-test.XXXXXX)"
FLOPPY="$WORKDIR/vsdev-floppy.img"
QMPSOCK="$WORKDIR/qmp.sock"
SERIAL="$WORKDIR/serial.log"
if [ "$IS_WINDOWS_HOST" = "1" ]; then
    QMPSOCK="tcp:127.0.0.1:45021"
    # Real bug found live: QEMU (a native Windows exe) needs a real
    # Windows path here. Bash/MSYS auto-translates a *bare* POSIX path
    # argument when invoking a native exe, but not one embedded after a
    # prefix like "file:" - "-serial file:/tmp/x" reached QEMU as a
    # literal string, which Windows resolves relative to the current
    # drive root, not $WORKDIR. cygpath -m sidesteps the whole
    # ambiguity by resolving it explicitly, once, up front.
    SERIAL="$(cygpath -m "$SERIAL")"
    FLOPPY_WIN="$(cygpath -m "$FLOPPY")"
fi

cleanup() {
    [ -n "${QEMU_PID:-}" ] && kill -9 "$QEMU_PID" 2>/dev/null
}
trap cleanup EXIT

echo "=== run-test.sh: building vsdev.sys + vsdev_test.exe ==="
if [ "$IS_WINDOWS_HOST" = "1" ]; then
    # No `make` on this host either (real, checked live) - same two
    # commands the Makefile runs, spelled out directly.
    DDK_INC="${NTLINUX_MINGW_DDK_INC:-}"
    x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -I"$DDK_INC" -c -o "$HERE/vsdev.o" "$HERE/vsdev.c" || exit 1
    x86_64-w64-mingw32-gcc -Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared \
        -o "$HERE/vsdev.sys" "$HERE/vsdev.o" -lntoskrnl -lhal || exit 1
else
    make -C "$HERE" || exit 1
fi
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -o "$HERE/vsdev_test.exe" "$HERE/vsdev_test.c" || exit 1

echo "=== run-test.sh: locating a ReactOS x64 nightly LiveCD ISO ==="
ISO="$("$IMAGES_DIR/fetch-reactos-x64-nightly.sh")" || exit 1
echo "using $ISO"

echo "=== run-test.sh: mastering floppy image ==="
if [ "$IS_WINDOWS_HOST" = "1" ]; then
    # No mtools on this host (real, checked live - see the
    # IS_WINDOWS_HOST comment above). write-fat12-floppy.py is a
    # narrow, documented, real-FAT12 replacement for exactly the two
    # mtools operations needed here - see that script's own header.
    "$HERE/write-fat12-floppy.py" "$FLOPPY" "$HERE/vsdev.sys" "$HERE/vsdev_test.exe" || exit 1
else
    dd if=/dev/zero of="$FLOPPY" bs=1024 count=1440 status=none
    mformat -i "$FLOPPY" -f 1440 ::
    mcopy -i "$FLOPPY" "$HERE/vsdev.sys" ::VSDEV.SYS
    mcopy -i "$FLOPPY" "$HERE/vsdev_test.exe" ::VSDTEST.EXE
fi

echo "=== run-test.sh: booting ReactOS under QEMU ==="
QMP_ARG="unix:$QMPSOCK,server,nowait"
[ "$IS_WINDOWS_HOST" = "1" ] && QMP_ARG="$QMPSOCK,server,nowait"
FLOPPY_ARG="$FLOPPY"
[ "$IS_WINDOWS_HOST" = "1" ] && FLOPPY_ARG="$FLOPPY_WIN"
qemu-system-x86_64 \
    -M pc -m 1024 -accel tcg \
    -cdrom "$ISO" \
    -drive file="$FLOPPY_ARG",if=floppy,format=raw \
    -boot d \
    -display none -vga std \
    -usb -device usb-tablet \
    -serial file:"$SERIAL" \
    -qmp "$QMP_ARG" \
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
# --click-x/--click-y: real fix for a real bug found live - blind `ret`
# presses can land while the dialog's language combobox (not the Next
# button) has focus, cycling its selection instead of accepting the
# dialog (observed: it silently changed the guest's UI language
# mid-run - see driver/vsdev/README.md's "Known gaps"). Clicking the
# Next button's actual screen coordinate directly can't misfire onto
# the wrong control the same way.
"$QMP" "$QMPSOCK" dismiss-dialog 0.3125 0.5 58 110 165 --timeout 200 --interval 5 --click-x 0.634 --click-y 0.738 || {
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
# Real, reproducible flake found and fixed live on two independent
# hosts (not this project's original sandbox): the previous approach
# here - click empty desktop, sendkey 'c' to jump keyboard focus to the
# "Command Prompt" icon by its first letter, sendkey 'ret' to launch it
# - stopped reliably opening a window against current ReactOS
# nightlies (0.4.16-amd64-dev build 2669): a post-launch screendump
# showed the icon not even selected, three attempts running, timing
# bumps made no difference (see driver/vsdev/README.md's "Known gaps"
# for the full diagnosis this replaces). Win+R -> "cmd" -> Enter is the
# fix: verified live, repeatably, opening a real
# X:\reactos\System32\cmd.exe window every time tried. meta_l is QEMU's
# qcode for the left Windows/Super key; send_keys sends the whole list
# as one chord (all pressed together, then released together).
"$QMP" "$QMPSOCK" sendkey meta_l,r
sleep 2
"$QMP" "$QMPSOCK" type "cmd"
"$QMP" "$QMPSOCK" sendkey ret
sleep 4

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
if [ "$IS_WINDOWS_HOST" = "1" ]; then
    RESULT="$("$HERE/read-fat12-file.py" "$FLOPPY" RESULT.TXT 2>/dev/null | tr -d '\r\n')"
else
    RESULT="$(mtype -i "$FLOPPY" ::RESULT.TXT 2>/dev/null | tr -d '\r\n')"
fi
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
