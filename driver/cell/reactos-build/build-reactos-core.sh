#!/bin/bash
# build-reactos-core.sh — builds the real ReactOS kernel (ntoskrnl.exe),
# HAL (hal.dll), and bootloader (freeldr.sys) from a real, current
# ReactOS source checkout, using this project's existing mingw-w64
# toolchain — no RosBE needed (ROADMAP.md Phase 13).
#
# Real, live-verified finding this script encodes: Phase 13 originally
# assumed RosBE's own custom-built cross-toolchain was required just to
# start. Checked directly instead (Rule 1) - it isn't. The stock
# gcc-mingw-w64-x86-64 package (same one driver/gpu/, driver/kmdf/,
# driver/net/reactos/ already build against) configures and links
# ReactOS's own kernel/HAL/bootloader clean, once one real GCC-14
# compatibility gap is worked around (see CMAKE_C_FLAGS below).
#
# Deliberately targets only ntoskrnl/hal/freeldr, not `ninja bootcd`
# (the full desktop-shell ISO target) - ROS-NTCELL's own scope is
# HAL/ntoskrnl/registry/PnP/IoMgr/ObMgr/MM/driver-loader/bridge-
# transport only, no Explorer/Winlogon/GDI shell, so there is no reason
# to build (or debug) unrelated desktop components like
# dll/shellext/shellbtrfs, which genuinely fails to compile under this
# newer GCC/libstdc++ pairing for reasons that have nothing to do with
# what this phase actually needs.
#
# Does NOT vendor ReactOS source into this repo (ADR-0002/Rule 17) -
# clones fresh into a directory you choose, outside this repo, or into
# a gitignored path if run from within it.
set -euo pipefail

REACTOS_SRC="${1:-./reactos-src}"

if [ ! -d "$REACTOS_SRC/.git" ]; then
    echo >&2 "build-reactos-core.sh: cloning ReactOS into $REACTOS_SRC..."
    git clone --depth 1 https://github.com/reactos/reactos.git "$REACTOS_SRC"
fi

cd "$REACTOS_SRC"

echo >&2 "build-reactos-core.sh: configuring for amd64 (stock mingw-w64, no RosBE)..."
ROS_ARCH=amd64 ./configure.sh

OUTDIR="output-MinGW-amd64"

echo >&2 "build-reactos-core.sh: relaxing GCC 14's new-by-default pointer/implicit-declaration errors back to warnings (real, documented reason above)..."
cmake -DCMAKE_C_FLAGS='-Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=int-conversion' "$OUTDIR" 2>/dev/null || \
    (cd "$OUTDIR" && cmake -DCMAKE_C_FLAGS='-Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=int-conversion' .)

echo >&2 "build-reactos-core.sh: building ntoskrnl, hal, freeldr..."
cd "$OUTDIR"
ninja ntoskrnl hal boot/freeldr/freeldr/freeldr.sys

echo >&2 "build-reactos-core.sh: done. Real outputs:"
file ntoskrnl/ntoskrnl.exe hal/halx86/hal.dll
ls -la boot/freeldr/freeldr/freeldr.sys
