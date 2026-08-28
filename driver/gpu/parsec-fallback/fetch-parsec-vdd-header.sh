#!/bin/bash
# fetch-parsec-vdd-header.sh — downloads the real, public parsec-vdd.h
# (github.com/nomi-san/parsec-vdd, BSD-3-clause) into ./build/
# (gitignored). Same "consume, don't vendor" shape as
# driver/gpu/fetch-wdk-headers.sh (ADR-0002/Rule 17), even though the
# license situation here is simpler: parsec-vdd.h carries its own
# BSD-3-clause notice in its own top comment, which travels with the
# file whenever it's fetched fresh — no separate LICENSE extraction
# step needed the way the WDK's own EULA required.
#
# What this is for: "Parsec Virtual Display Adapter" (ParsecVDA) is a
# real, third-party IddCx virtual-display driver. It ships pre-installed
# on hosts that use Parsec for remote access — this project's own
# Windows-host sandbox is one (see ROADMAP.md Phase 11, "A real GPU
# became available this session" — the same adapter noticed there).
# ntlinux-parsec-fallback.c (this directory) uses this header's real,
# documented API to drive it as a fallback display device. This is a
# separate, smaller capability from Phase 11's actual ReactOS/VFIO
# driver-cell hosting research — see this directory's own README.md for
# the honest scope boundary.
#
# Pinned to a specific commit rather than refs/heads/main, for
# reproducibility (a moving branch ref would make this script's output
# non-deterministic across time, unlike a tagged/pinned fetch).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HERE/build"

PARSEC_VDD_REPO="nomi-san/parsec-vdd"
PARSEC_VDD_SHA="a827c7137659b618d0a65f261ad8b2da1c74f772"
PARSEC_VDD_URL="https://raw.githubusercontent.com/$PARSEC_VDD_REPO/$PARSEC_VDD_SHA/core/parsec-vdd.h"

mkdir -p "$BUILD_DIR"

echo >&2 "fetch-parsec-vdd-header.sh: downloading parsec-vdd.h @ $PARSEC_VDD_SHA..."
curl -sSL --max-time 60 -o "$BUILD_DIR/parsec-vdd.h" "$PARSEC_VDD_URL"

# Sanity check: confirm what actually landed is the real header (its own
# BSD copyright line, and one of the real symbols
# ntlinux-parsec-fallback.c depends on), not e.g. a GitHub error page —
# checked, not assumed.
if ! grep -q "Copyright (c) 2023, Nguyen Duy" "$BUILD_DIR/parsec-vdd.h"; then
    echo >&2 "fetch-parsec-vdd-header.sh: fetched file doesn't look like the real parsec-vdd.h (missing expected copyright line) - aborting"
    exit 1
fi
if ! grep -q "VDD_ADAPTER_GUID" "$BUILD_DIR/parsec-vdd.h"; then
    echo >&2 "fetch-parsec-vdd-header.sh: fetched file doesn't look like the real parsec-vdd.h (missing VDD_ADAPTER_GUID) - aborting"
    exit 1
fi

echo >&2 "fetch-parsec-vdd-header.sh: ready in $BUILD_DIR (BSD-3-clause notice is embedded in the file's own header comment - read it before using anything this fetches; never vendor this file into the repo itself, same as fetch-wdk-headers.sh)"
