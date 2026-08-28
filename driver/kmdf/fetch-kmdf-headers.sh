#!/bin/bash
# fetch-kmdf-headers.sh — downloads the real Microsoft KMDF (Kernel-Mode
# Driver Framework) headers from the same official WDK NuGet package
# driver/gpu/fetch-wdk-headers.sh already uses for WDDM, and extracts
# the whole c/Include/wdf/kmdf/1.11/ directory into ./build/
# (gitignored).
#
# Follow-up to ROADMAP.md Phase 9's finding: this project's original
# gap probe (tooling/compat-db/ddkgap/) said "KMDF: not present in this
# toolchain at all" - true only in the narrow sense mingw-w64 doesn't
# pre-package it. Investigating Phase 11's WDDM headers turned up the
# real KMDF headers live in the same WDK package, not yet
# compile-verified the way wddm-probe.c verified WDDM. This script +
# kmdf-probe.c in this directory close that "found, not solved" gap.
#
# Same ADR-0002/Rule 17 shape as fetch-wdk-headers.sh: the WDK's own
# EULA prohibits redistribution (extracted alongside these headers as
# build/WDK-LICENSE.txt - read it), so this only ever writes into the
# gitignored ./build/ directory, fetched fresh by whoever needs it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HERE/build"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# Same package/version driver/gpu/fetch-wdk-headers.sh pins - KMDF
# headers live inside this one package (Microsoft.Windows.WDK.x64),
# under a version-independent c/Include/wdf/kmdf/1.11/ path (unlike the
# WDDM headers, which live under the versioned SDK_INC_VER subdirectory
# - confirmed directly by listing this package's own contents, not
# assumed from the WDDM header layout).
WDK_PKG="Microsoft.Windows.WDK.x64"
WDK_VER="10.0.26100.2454"

fetch_nupkg() {
    local pkg="$1" ver="$2" out="$3"
    echo >&2 "fetch-kmdf-headers.sh: downloading $pkg $ver..."
    curl -sSL --max-time 300 -o "$out" \
        "https://www.nuget.org/api/v2/package/$pkg/$ver"
}

mkdir -p "$BUILD_DIR"

fetch_nupkg "$WDK_PKG" "$WDK_VER" "$WORK_DIR/wdk.nupkg"

echo >&2 "fetch-kmdf-headers.sh: extracting c/Include/wdf/kmdf/1.11/..."
unzip -q -o "$WORK_DIR/wdk.nupkg" "c/Include/wdf/kmdf/1.11/*" -d "$WORK_DIR/wdk-extract"

rm -rf "$BUILD_DIR/wdf"
mkdir -p "$BUILD_DIR/wdf"
cp -r "$WORK_DIR/wdk-extract/c/Include/wdf/kmdf/1.11" "$BUILD_DIR/wdf/kmdf-1.11"
unzip -p "$WORK_DIR/wdk.nupkg" LICENSE.txt > "$BUILD_DIR/WDK-LICENSE.txt"

# Two real, narrow, documented patches to the extracted copy - never
# touching a system install, same shape as prepare-ndis-header.sh and
# fetch-wdk-headers.sh's own dispmprt.h patch. Found live by actually
# compiling kmdf-probe.c against these headers, not by inspection.
KMDF_DIR="$BUILD_DIR/wdf/kmdf-1.11"

# 1. Case-sensitivity: these headers ship with #include directives
# referencing filenames in a different case than the files actually
# have on disk (e.g. #include "WdfQueryInterface.h" for
# wdfqueryinterface.h) - invisible on Windows' case-insensitive
# filesystem (what these headers are authored/tested against), a hard
# error on Linux. Symlink every case variant actually referenced,
# rather than guessing which ones matter.
( cd "$KMDF_DIR" && for inc in $(grep -ohE '#include "[A-Za-z0-9_]+\.h"' ./*.h | sed -E 's/#include "([^"]+)"/\1/' | sort -u); do
    if [ ! -e "$inc" ]; then
        lower="$(echo "$inc" | tr 'A-Z' 'a-z')"
        [ -e "$lower" ] && ln -sf "$lower" "$inc"
    fi
done )

# 2. A real header-ordering bug in wdf.h itself (the master include):
# wdfdevice.h (included before wdfrequest.h) references
# WDF_REQUEST_TYPE, which wdfrequest.h - included *later* - actually
# defines. MSVC tolerates this (perhaps a lenient incomplete-enum-as-
# parameter rule, not investigated further); GCC correctly rejects it
# ("parameter 3 has incomplete type"). Fix: pull wdfrequest.h's
# #include earlier, right before wdfdevice.h's - wdfrequest.h's own
# include guard makes its second, later inclusion (still present,
# untouched) a no-op, so this changes only ordering, not content.
sed -i '/#include "wdfdevice.h"/i #include "wdfrequest.h" \/* NTLinux: moved earlier - see fetch-kmdf-headers.sh *\/' "$KMDF_DIR/wdf.h"

echo >&2 "fetch-kmdf-headers.sh: ready in $BUILD_DIR (read WDK-LICENSE.txt - never commit" \
         "these files, see this script's own header comment)"
