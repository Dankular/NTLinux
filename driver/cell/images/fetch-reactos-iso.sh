#!/bin/bash
# fetch-reactos-iso.sh — downloads a real upstream ReactOS release ISO
# for use as the driver-cell boot image (ARCHITECTURE.md sections 14-15,
# ADR-0002/Rule 17: consume the upstream release, don't vendor ReactOS
# source into this repo just to get a bootable image).
#
# Known gap, stated plainly: this is the *stock* ReactOS release ISO —
# a general-purpose ReactOS install/live image with a full desktop
# (Explorer, Winlogon, GDI shell), not yet the stripped-down
# driver-only "ROS-NTCELL" build profile ARCHITECTURE.md section 15
# describes (HAL/ntoskrnl/registry/PnP/IoMgr/ObMgr/MM/driver-loader/
# bridge-transport only, no shell). Producing that profile means
# building ReactOS from source with a custom bootcd.cmake target, which
# needs the RosBE cross-toolchain — not reachable in this sandbox (same
# category of gap as the Wine ntdll integration in Phase 2/12 and the
# unbuilt driver/ntbridge/reactos/ .sys skeleton). This script exists so
# driver/cell/launcher/ntcell has a real, genuine ReactOS kernel to
# boot today, to prove the launcher itself works; trimming it to
# ROS-NTCELL is separate, tracked follow-up work, not silently assumed
# done.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="0.4.15"
ZIP_URL="https://sourceforge.net/projects/reactos/files/latest/download"
ISO_NAME="ReactOS-${VERSION}-release-1-gdbb43bbaeb2-x86.iso"
ISO_PATH="$HERE/$ISO_NAME"
ZIP_PATH="$HERE/reactos-${VERSION}.zip"

if [ -f "$ISO_PATH" ]; then
    echo "fetch-reactos-iso.sh: $ISO_PATH already present, skipping download"
    exit 0
fi

echo "fetch-reactos-iso.sh: downloading ReactOS $VERSION release ISO from SourceForge..."
curl -sSL --max-time 120 -o "$ZIP_PATH" "$ZIP_URL"
echo "fetch-reactos-iso.sh: extracting..."
unzip -o -d "$HERE" "$ZIP_PATH"
rm -f "$ZIP_PATH"

if [ ! -f "$ISO_PATH" ]; then
    echo "fetch-reactos-iso.sh: expected $ISO_PATH after extraction but it's missing — " \
         "upstream ReactOS may have shipped a new release under a different filename; " \
         "check $HERE and update ISO_NAME in this script and driver/cell/launcher/ntcell" >&2
    exit 1
fi

echo "fetch-reactos-iso.sh: ready at $ISO_PATH"
