#!/usr/bin/env bash
# Wrapper around mkarchiso that stages ntloader's source into the profile
# before building, so airootfs/root/customize_airootfs.sh can compile and
# install it inside the chroot. Direct `mkarchiso -v -w work -o out .`
# invocations (as documented in Phase 0 / distro/image/README.md before
# ntloader existed) will fail at the customize_airootfs.sh step without
# this staging - use this script instead of calling mkarchiso directly.
#
# Usage: distro/image/build.sh [work-dir] [out-dir]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WORK_DIR="${1:-/tmp/ntlinux-build/work}"
OUT_DIR="${2:-/tmp/ntlinux-build/out}"

STAGE_DIR="$SCRIPT_DIR/airootfs/root/ntloader-src"
APP_TOOL_STAGE="$SCRIPT_DIR/airootfs/root/ntlinux-app"

cleanup() {
    rm -rf "$STAGE_DIR" "$APP_TOOL_STAGE"
}
trap cleanup EXIT

rm -rf "$STAGE_DIR" "$APP_TOOL_STAGE"
cp -a "$REPO_ROOT/ntloader" "$STAGE_DIR"
# Don't ship any local build artifacts that happen to exist in the repo
# checkout (e.g. from `make check` during development) - customize_airootfs.sh
# does its own build inside the chroot.
rm -f "$STAGE_DIR/ntloader"
cp -a "$REPO_ROOT/tooling/installer/ntlinux-app" "$APP_TOOL_STAGE"

mkdir -p "$WORK_DIR" "$OUT_DIR"
mkarchiso -v -w "$WORK_DIR" -o "$OUT_DIR" "$SCRIPT_DIR"
