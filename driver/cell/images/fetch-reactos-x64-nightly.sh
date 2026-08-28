#!/bin/bash
# fetch-reactos-x64-nightly.sh — downloads the latest ReactOS x64 LiveCD
# nightly build (ADR-0002/Rule 17: consume, don't vendor).
#
# Why a nightly, not a stable release: ReactOS's stable releases
# (0.4.15 as of this writing) ship x86 (32-bit) only - confirmed by
# checking both sourceforge.net and reactos.org, neither lists an x64
# variant at that version. x64 support exists only as nightly CI builds
# (iso.reactos.org), built on every commit to the ReactOS repository -
# "bleeding edge," per ReactOS's own download page, with a higher chance
# of regressions than a numbered release. Used here because Phase 5
# needed a real x64 ReactOS kernel to load a real x64-compiled driver
# against (see driver/vsdev/README.md and ROADMAP.md Phase 5 for the
# x86-vs-x64 mismatch this was built to fix).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LISTING_URL="https://iso.reactos.org/livecd/"

echo >&2 "fetch-reactos-x64-nightly.sh: finding the latest x64 nightly LiveCD build..."
LATEST_7Z="$(curl -sSL --max-time 30 "$LISTING_URL" \
    | grep -oE 'href="[^"]*x64-msvc-win-dbg\.7z"' \
    | sed -E 's/href="([^"]*)"/\1/' \
    | tail -1)"

if [ -z "$LATEST_7Z" ]; then
    echo "fetch-reactos-x64-nightly.sh: could not find any x64 nightly build at $LISTING_URL" >&2
    exit 1
fi

ISO_NAME="${LATEST_7Z%.7z}.iso"
ISO_PATH="$HERE/$ISO_NAME"

if [ -f "$ISO_PATH" ]; then
    echo >&2 "fetch-reactos-x64-nightly.sh: $ISO_PATH already present, skipping download"
    echo "$ISO_PATH"
    exit 0
fi

echo >&2 "fetch-reactos-x64-nightly.sh: downloading $LATEST_7Z..."
curl -sSL --max-time 180 -o "$HERE/$LATEST_7Z" "$LISTING_URL$LATEST_7Z"

echo >&2 "fetch-reactos-x64-nightly.sh: extracting..."
7z x -o"$HERE" "$HERE/$LATEST_7Z" > /dev/null
rm -f "$HERE/$LATEST_7Z"

if [ ! -f "$ISO_PATH" ]; then
    echo "fetch-reactos-x64-nightly.sh: expected $ISO_PATH after extraction but it's missing" >&2
    exit 1
fi

echo >&2 "fetch-reactos-x64-nightly.sh: ready at $ISO_PATH"
echo "$ISO_PATH"
