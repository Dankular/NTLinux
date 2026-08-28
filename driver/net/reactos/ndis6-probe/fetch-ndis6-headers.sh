#!/bin/bash
# fetch-ndis6-headers.sh — downloads the real Microsoft NDIS 6.x
# (connectionless miniport model) headers from the same official
# Microsoft WDK/SDK NuGet packages driver/gpu/fetch-wdk-headers.sh and
# driver/kmdf/fetch-kmdf-headers.sh already use, and extracts just the
# files a real NDIS 6.x miniport needs into ./build/ (gitignored).
#
# Follow-up to tooling/compat-db/ddkgap/'s own finding (README.md,
# ROADMAP.md Phase 9): NdisMRegisterMiniportDriver,
# NdisMSetMiniportAttributes, NdisMIndicateReceiveNetBufferLists, and
# NdisAllocateNetBufferListPool are all real import stubs in this
# project's existing mingw-w64 DDK's libndis.a (confirmed live with
# `nm`, again, directly, in this pass — see README.md), but genuinely
# absent from every header mingw-w64 packages (confirmed with a real
# recursive grep across the *entire* installed
# /usr/x86_64-w64-mingw32/include tree, not just ddk/ — zero hits).
#
# Checked in the priority order CLAUDE.md Rule 1 calls for before
# hand-declaring anything: the real, current Microsoft WDK/SDK NuGet
# packages (same ones driver/gpu/fetch-wdk-headers.sh and
# driver/kmdf/fetch-kmdf-headers.sh already fetch from) turn out to
# carry a complete, current km/ndis.h (~529KB, NDIS 6.x connectionless
# miniport model throughout) plus its full km/ndis/ and shared/ndis/
# support-header directories — found live by listing each package's own
# contents, not assumed from the WDDM/KMDF precedent. Musa.Veil
# (ADR-0006) and hand-declaring from learn.microsoft.com were not
# needed — real, current, authoritative Microsoft headers were sitting
# in the same package this project already fetches from for two other
# gaps.
#
# Same ADR-0002/Rule 17 shape as every other fetch script in this
# project: the WDK's own EULA (extracted alongside these headers as
# build/WDK-LICENSE.txt — read it) explicitly prohibits redistribution,
# so this only ever writes into the gitignored ./build/ directory,
# fetched fresh by whoever needs it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HERE/build"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# Same package/version pins driver/gpu/fetch-wdk-headers.sh and
# driver/kmdf/fetch-kmdf-headers.sh already use — km/ndis.h and its
# km/ndis/ support headers live in the WDK package; the shared/ (cross
# usermode/kernel-mode) headers it depends on (ntddndis.h,
# shared/ndis/*.h, netevent.h, driverspecs.h, ntstatus.h, windot11.h,
# qos.h) live in the separate base SDK package — the identical
# three-package split fetch-wdk-headers.sh's own header comment already
# documents for WDDM, confirmed independently true for NDIS by actually
# listing both packages' contents rather than assumed from that
# precedent.
WDK_PKG="Microsoft.Windows.WDK.x64"
WDK_VER="10.0.26100.2454"
SDK_PKG="Microsoft.Windows.SDK.CPP"
SDK_VER="10.0.26100.1742"
SDK_INC_VER="10.0.26100.0" # the versioned subdirectory both packages use under c/Include/

fetch_nupkg() {
    local pkg="$1" ver="$2" out="$3"
    echo >&2 "fetch-ndis6-headers.sh: downloading $pkg $ver..."
    curl -sSL --max-time 300 -o "$out" \
        "https://www.nuget.org/api/v2/package/$pkg/$ver"
}

mkdir -p "$BUILD_DIR"

fetch_nupkg "$WDK_PKG" "$WDK_VER" "$WORK_DIR/wdk.nupkg"
fetch_nupkg "$SDK_PKG" "$SDK_VER" "$WORK_DIR/sdk.nupkg"

echo >&2 "fetch-ndis6-headers.sh: extracting the real NDIS 6.x header set..."

# km/ (WDK package): the top-level ndis.h itself plus the two other
# top-level km/ headers it #includes (netpnp.h, xfilter.h), plus its
# whole km/ndis/ support-header directory (net-buffer-list handling —
# nbl.h/nblapi.h/nblreceive.h/etc — the NDIS 6.x NET_BUFFER_LIST
# machinery NdisMIndicateReceiveNetBufferLists/
# NdisAllocateNetBufferListPool actually operate on).
unzip -q -o "$WORK_DIR/wdk.nupkg" \
    "c/Include/$SDK_INC_VER/km/ndis.h" \
    "c/Include/$SDK_INC_VER/km/netpnp.h" \
    "c/Include/$SDK_INC_VER/km/xfilter.h" \
    "c/Include/$SDK_INC_VER/km/ndis/*" \
    -d "$WORK_DIR/wdk-extract"

# shared/ (base SDK package): the cross usermode/kernel-mode headers
# ndis.h's own #include chain needs (confirmed by actually walking that
# chain, not guessed) — ntddndis.h (the classic NDIS OID/media type
# definitions ndis.h itself #includes near the top, same file
# prepare-ndis-header.sh's NDIS 5.1 patch already touches a copy of,
# here fetched fresh from the real current SDK instead), its
# shared/ndis/ support headers, and four standalone headers
# (netevent.h, driverspecs.h, ntstatus.h — SAL/status-code plumbing —
# and windot11.h/qos.h, pulled in far down ndis.h for 802.11 and QoS
# OID support respectively). driverspecs.h itself turned out to have a
# second-level dependency, found only by actually compiling
# ndis6-probe.c against it (mingw-w64's own driverspecs.h has no such
# dependency, but this is genuinely the newer real WDK one, pulled in
# because it now shadows mingw-w64's copy on the include path) —
# sdv_driverspecs.h (Static Driver Verifier annotations) and
# concurrencysal.h (the SAL 2.0 concurrency-annotation macro set),
# fetched alongside it for the same reason. That, in turn, needs the
# real, modern sal.h/specstrings*.h (SAL 2.0 - __ANNOTATION/__PRIMOP
# and friends) rather than mingw-w64's own older specstrings.h (which
# only defines the older _In_/_Out_-style subset) - fetched here too,
# and (being on the include path ahead of mingw-w64's DDK dir) this
# also becomes what ntddk.h's own wdm.h resolves <specstrings.h> to for
# this whole compile, confirmed compiling clean against mingw-w64's
# existing ntddk.h/wdm.h despite being the newer, real Microsoft
# version rather than mingw-w64's own bundled one.
#
# ntddndis.h (already fetched above) itself #includes <ws2def.h> and
# <ws2ipdef.h> for SOCKADDR_INET and friends (NDIS 6.x's NDK/RDMA OID
# definitions use them) — found live by compiling: mingw-w64 does ship
# its own ws2def.h/ws2ipdef.h, but its ws2ipdef.h assumes a prior
# <winsock2.h> (usermode) include this kernel-mode-header compile never
# does, and fails with "incomplete type"/"unknown type name" for
# SOCKET_ADDRESS, IN_ADDR, SOCKADDR_IN, and friends as a result. The
# real WDK's own shared/ws2def.h + shared/ws2ipdef.h are self-contained
# for this case (no prior winsock2.h assumed) — fetched here (with
# their own small dependency chain: winapifamily.h,
# winpackagefamily.h, in6addr.h, inaddr.h — walked the same "compile,
# see what's missing, fetch the real file, repeat" way as the SAL
# headers above) to replace mingw-w64's copies for this compile only.
#
# ntddndis.h also #includes <ifdef.h> (NDIS_IF_* network-interface
# type/OID definitions) — mingw-w64 ships its own copy of this one too,
# but it's a genuinely older revision that predates
# NET_IF_OBJECT_ID/IF_QUERY_OBJECT/IF_SET_OBJECT/NET_PHYSICAL_LOCATION
# (confirmed absent from mingw-w64's copy with a direct grep, not
# assumed), which ndis.h's own NDIS 6.x interface-object-query surface
# needs. Same fix as ws2def.h/ws2ipdef.h: fetch the real, current
# shared/ifdef.h (plus its own one dependency, shared/ipifcons.h) to
# shadow mingw-w64's outdated copy for this compile.
unzip -q -o "$WORK_DIR/sdk.nupkg" \
    "c/Include/$SDK_INC_VER/shared/ntddndis.h" \
    "c/Include/$SDK_INC_VER/shared/netevent.h" \
    "c/Include/$SDK_INC_VER/shared/driverspecs.h" \
    "c/Include/$SDK_INC_VER/shared/sdv_driverspecs.h" \
    "c/Include/$SDK_INC_VER/shared/concurrencysal.h" \
    "c/Include/$SDK_INC_VER/shared/ntstatus.h" \
    "c/Include/$SDK_INC_VER/shared/windot11.h" \
    "c/Include/$SDK_INC_VER/shared/qos.h" \
    "c/Include/$SDK_INC_VER/shared/sal.h" \
    "c/Include/$SDK_INC_VER/shared/specstrings.h" \
    "c/Include/$SDK_INC_VER/shared/specstrings_strict.h" \
    "c/Include/$SDK_INC_VER/shared/specstrings_undef.h" \
    "c/Include/$SDK_INC_VER/shared/no_sal2.h" \
    "c/Include/$SDK_INC_VER/shared/wlantypes.h" \
    "c/Include/$SDK_INC_VER/shared/ws2def.h" \
    "c/Include/$SDK_INC_VER/shared/ws2ipdef.h" \
    "c/Include/$SDK_INC_VER/shared/in6addr.h" \
    "c/Include/$SDK_INC_VER/shared/inaddr.h" \
    "c/Include/$SDK_INC_VER/shared/winapifamily.h" \
    "c/Include/$SDK_INC_VER/shared/winpackagefamily.h" \
    "c/Include/$SDK_INC_VER/shared/ifdef.h" \
    "c/Include/$SDK_INC_VER/shared/ipifcons.h" \
    "c/Include/$SDK_INC_VER/shared/ndis/*" \
    -d "$WORK_DIR/sdk-extract"

rm -rf "$BUILD_DIR/km" "$BUILD_DIR/shared"
mkdir -p "$BUILD_DIR/km" "$BUILD_DIR/shared"
cp -r "$WORK_DIR/wdk-extract/c/Include/$SDK_INC_VER/km/." "$BUILD_DIR/km/"
cp -r "$WORK_DIR/sdk-extract/c/Include/$SDK_INC_VER/shared/." "$BUILD_DIR/shared/"
unzip -p "$WORK_DIR/wdk.nupkg" LICENSE.txt > "$BUILD_DIR/WDK-LICENSE.txt"

# Real, narrow, documented patches — found live by actually compiling
# ndis6-probe.c against these headers (same "find the real header
# bugs, patch narrowly, document precisely" technique as
# driver/net/reactos/prepare-ndis-header.sh, driver/gpu/
# fetch-wdk-headers.sh's dispmprt.h patch, and driver/kmdf/
# fetch-kmdf-headers.sh's two patches), not guessed in advance.

# Case-sensitivity, the identical class of bug driver/kmdf/
# fetch-kmdf-headers.sh already found and fixed for the KMDF headers:
# these headers' own #include directives sometimes reference a
# filename in a different case than the file actually has on disk
# (e.g. windot11.h's `#include <WlanTypes.h>` for the real
# shared/wlantypes.h) — invisible on Windows' case-insensitive
# filesystem (what these headers are authored/tested against), a hard
# compile error on Linux. Symlink every case variant actually
# referenced anywhere in the fetched set, found by grepping for
# #include lines across both km/ and shared/ rather than guessed at.
# The pattern anchors #include to (optional leading whitespace then)
# the start of a line - real preprocessor directives only, not prose
# that happens to mention "#include <foo.h>" in a comment (ntddndis.h's
# own file-header comment does exactly this, pointing kernel-mode code
# at ndis.h instead - an early, unanchored version of this pattern
# matched that text and symlinked a spurious build/shared/ndis.h,
# caught by checking what the match loop actually produced, not
# assumed correct on the first pass).
#
# Only symlinks when NO exact-case match exists anywhere across both
# km/ and shared/ - this compile already puts both directories on the
# include path (-Ibuild/km -Ibuild/shared, see Makefile), so an
# #include that just lives in the *other* fetched directory already
# resolves correctly with no patch needed; checked live (an earlier,
# less careful version of this loop symlinked over a dozen files that
# turned out to be unnecessary - resolvable via the existing -I search
# order alone) rather than assumed.
for dir in "$BUILD_DIR/km" "$BUILD_DIR/shared"; do
    for inc in $(grep -rohE '^[[:space:]]*#include[[:space:]]*[<"][A-Za-z0-9_./]+\.h[>"]' "$dir" 2>/dev/null | sed -E 's/^[[:space:]]*#include[[:space:]]*[<"]([^>"]+)[>"]/\1/' | sort -u); do
        base="$(basename "$inc")"
        if find "$BUILD_DIR/km" "$BUILD_DIR/shared" -name "$base" 2>/dev/null | grep -q .; then
            continue # exact-case match exists somewhere on the include path already
        fi
        match="$(find "$BUILD_DIR/km" "$BUILD_DIR/shared" -iname "$base" 2>/dev/null | head -1)"
        if [ -n "$match" ]; then
            target="$dir/$inc"
            mkdir -p "$(dirname "$target")"
            ln -sf "$match" "$target"
        fi
    done
done

echo >&2 "fetch-ndis6-headers.sh: ready in $BUILD_DIR (read WDK-LICENSE.txt - never commit" \
         "these files, see this script's own header comment)"
