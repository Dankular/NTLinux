#!/bin/bash
# prepare-ndis-header.sh — generates locally-patched copies of three
# mingw-w64-bundled headers (ReactOS's own ddk/ndis.h and ddk/wdm.h,
# repackaged) into ./build/, fixing three real, unconditional bugs found
# only by actually building an NDIS 5.1 miniport against them (not
# guessed, not fixed by any NDIS*_MINIPORT macro choice - checked):
#
# 1. ddk/ndis.h re-typedefs `enum _NDIS_REQUEST_TYPE` in its own body
#    even though it already `#include "ntddndis.h"`, which defines the
#    byte-for-byte identical enum under its own include guard - a
#    genuine duplicate-typedef error in GCC/mingw.
# 2. ddk/ndis.h's NdisMWanIndicateReceiveComplete() prototype is missing
#    a comma between its two parameters - a literal typo, unrelated to
#    NDIS version or this project's usage (this project doesn't even
#    call that WAN-specific function).
# 3. ddk/wdm.h references SYSTEM_POWER_STATE_CONTEXT inside the IRP
#    Power minor-function union under a guard spelled
#    `NTDDI_VERSION >= NTDDI_WINVISTA`, but the struct itself is only
#    *defined* under the correctly-spelled `NTDDI_VERSION >= NTDDI_VISTA`
#    a few thousand lines earlier - `NTDDI_WINVISTA` doesn't exist
#    anywhere in mingw-w64's sdkddkver.h, so the preprocessor silently
#    treats it as 0 and the guard is always true, referencing a type
#    that's genuinely undefined below Vista-level targets (which is
#    exactly this driver's NDIS 5.1/WinXP-level target - see ntnet.c).
# 4. ddk/ndis.h unconditionally #defines NDIS_PROTOCOL_ID_MAX and
#    NDIS_PROTOCOL_ID_MASK (both 0x0F) a second time, byte-for-byte
#    identical to definitions ntddndis.h (which ndis.h itself already
#    #includes near the top of the file) already provides - the same
#    "redundant #include already covers this" shape as bug 1, just
#    #define instead of typedef. Found during a later full-repo rebuild
#    sweep (ROADMAP.md's Phase 9/10 verification pass), not present in
#    the original three found while building ntnet.c the first time -
#    real, reproducible, caught by actually re-running `make`, not by
#    inspection.
#
# Does NOT touch the system-installed headers - copies them, patches the
# copies, and driver/net/reactos/Makefile puts ./build/ first on the
# include path so the patched copies shadow the originals for this
# build only. Idempotent: safe to re-run.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DDK_DIR="/usr/x86_64-w64-mingw32/include/ddk"
BUILD_DIR="$HERE/build"

if [ ! -f "$DDK_DIR/ndis.h" ] || [ ! -f "$DDK_DIR/wdm.h" ]; then
    echo "prepare-ndis-header.sh: $DDK_DIR/{ndis,wdm}.h not found - is gcc-mingw-w64 installed?" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"

# Fix 1 + 2, applied to a copy of ndis.h. The enum removal uses awk's
# /start/,/end/ range pattern (matches and skips every line from the
# opening brace through the closing "} NDIS_REQUEST_TYPE,
# *PNDIS_REQUEST_TYPE;" inclusive); the missing-comma fix is a plain
# text substitution on the one affected declaration.
awk '
    /^typedef enum _NDIS_REQUEST_TYPE \{$/,/^} NDIS_REQUEST_TYPE, \*PNDIS_REQUEST_TYPE;$/ { next }
    { print }
' "$DDK_DIR/ndis.h" \
    | sed 's/IN NDIS_HANDLE  MiniportAdapterHandle$/IN NDIS_HANDLE  MiniportAdapterHandle,/' \
    | sed '/^#define\tNDIS_PROTOCOL_ID_MAX            0x0F$/d; /^#define\tNDIS_PROTOCOL_ID_MASK           0x0F$/d' \
    > "$BUILD_DIR/ndis.h"

# Fix 3, applied to a copy of wdm.h: the misspelled guard only ever
# appears in this one place, so a global substitution is safe and exact.
sed 's/NTDDI_WINVISTA/NTDDI_VISTA/g' "$DDK_DIR/wdm.h" > "$BUILD_DIR/wdm.h"

echo "prepare-ndis-header.sh: wrote patched $BUILD_DIR/ndis.h and $BUILD_DIR/wdm.h"
