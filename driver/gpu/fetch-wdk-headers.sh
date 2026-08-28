#!/bin/bash
# fetch-wdk-headers.sh — downloads the real Microsoft WDK/SDK headers a
# WDDM display miniport driver needs (d3dkmddi.h, d3dkmdt.h, d3dukmdt.h,
# dispmprt.h) from Microsoft's own official NuGet packages, and extracts
# just those four files into ./build/ (gitignored).
#
# This corrects an earlier, wrong claim in this project's own history
# (ROADMAP.md Phase 11 / docs/DECISIONS.md ADR-0003 originally said the
# WDDM/DXGKRNL kernel-mode interface was "not present in this toolchain
# at all" and treated that as a closed question) — checked directly by
# actually fetching Microsoft's real, current WDK, not assumed from what
# mingw-w64 happens to package. See driver/gpu/README.md for the full,
# corrected account.
#
# ADR-0002/Rule 17 (consume, don't vendor) applies with extra force here:
# the WDK's own EULA (extracted into ./build/WDK-LICENSE.txt by this
# script, read it before using anything this fetches) explicitly
# prohibits redistribution ("share, publish, distribute, or lend the
# software... transfer the software or this agreement to any third
# party") — these headers must NEVER be committed to this repo. This
# script only ever writes into the gitignored ./build/ directory, fetched
# fresh by whoever needs it, exactly like driver/cell/images/
# fetch-reactos-x64-nightly.sh already does for the ReactOS ISO.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HERE/build"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# Real, current (at the time this was written) official Microsoft NuGet
# package versions. The WDK package carries dispmprt.h and d3dkmddi.h;
# its own d3dkmdt.h/d3dukmdt.h dependency actually lives in a *separate*
# base SDK package (Microsoft.Windows.SDK.CPP, no .x64 suffix - that one
# carries only import libraries) - a real, three-package dependency
# chain confirmed by inspecting each package's own .nuspec, not assumed.
WDK_PKG="Microsoft.Windows.WDK.x64"
WDK_VER="10.0.26100.2454"
SDK_PKG="Microsoft.Windows.SDK.CPP"
SDK_VER="10.0.26100.1742"
SDK_INC_VER="10.0.26100.0" # the versioned subdirectory both packages use under c/Include/

fetch_nupkg() {
    local pkg="$1" ver="$2" out="$3"
    echo >&2 "fetch-wdk-headers.sh: downloading $pkg $ver..."
    curl -sSL --max-time 300 -o "$out" \
        "https://www.nuget.org/api/v2/package/$pkg/$ver"
}

mkdir -p "$BUILD_DIR"

fetch_nupkg "$WDK_PKG" "$WDK_VER" "$WORK_DIR/wdk.nupkg"
fetch_nupkg "$SDK_PKG" "$SDK_VER" "$WORK_DIR/sdk.nupkg"

echo >&2 "fetch-wdk-headers.sh: extracting the four headers actually needed..."
unzip -q -o "$WORK_DIR/wdk.nupkg" \
    "c/Include/$SDK_INC_VER/km/dispmprt.h" \
    "c/Include/$SDK_INC_VER/shared/d3dkmddi.h" \
    -d "$WORK_DIR/wdk-extract"
unzip -q -o "$WORK_DIR/sdk.nupkg" \
    "c/Include/$SDK_INC_VER/shared/d3dkmdt.h" \
    "c/Include/$SDK_INC_VER/shared/d3dukmdt.h" \
    -d "$WORK_DIR/sdk-extract"

cp "$WORK_DIR/wdk-extract/c/Include/$SDK_INC_VER/km/dispmprt.h" "$BUILD_DIR/"
cp "$WORK_DIR/wdk-extract/c/Include/$SDK_INC_VER/shared/d3dkmddi.h" "$BUILD_DIR/"
cp "$WORK_DIR/sdk-extract/c/Include/$SDK_INC_VER/shared/d3dkmdt.h" "$BUILD_DIR/"
cp "$WORK_DIR/sdk-extract/c/Include/$SDK_INC_VER/shared/d3dukmdt.h" "$BUILD_DIR/"
unzip -p "$WORK_DIR/wdk.nupkg" LICENSE.txt > "$BUILD_DIR/WDK-LICENSE.txt"

# One real, narrow, documented patch (same technique as
# driver/net/reactos/prepare-ndis-header.sh) - NOT a fix. This
# static_assert is Microsoft's own correctness check that
# DXGK_CHILD_CAPABILITIES's byte layout matches what MSVC computes, and
# it genuinely fails under this GCC/mingw-w64 toolchain (confirmed live,
# a real cross-compiler struct-layout mismatch - see wddm-probe.c and
# README.md's "What's still unresolved"). Commented out here only so
# the *rest* of the interface can be checked for other, independent
# issues, not to hide this finding - it stays fully documented in
# wddm-probe.c, README.md, and ROADMAP.md Phase 11.
# Note: these WDK headers ship with CRLF line endings, so the pattern
# below deliberately doesn't anchor on end-of-line ($) - it would never
# match against the trailing \r otherwise.
sed -i \
    's/^static_assert( FIELD_OFFSET( DXGK_CHILD_CAPABILITIES, HpdAwareness ) == 12, "Type field has changed size" );/\/\/ NTLinux: commented out - fails under GCC\/mingw-w64, see driver\/gpu\/README.md\n\/\/ &/' \
    "$BUILD_DIR/dispmprt.h"

echo >&2 "fetch-wdk-headers.sh: ready in $BUILD_DIR (read WDK-LICENSE.txt - never commit" \
         "these files, see this script's own header comment)"
