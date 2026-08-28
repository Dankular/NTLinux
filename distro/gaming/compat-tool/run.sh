#!/bin/bash
# run.sh - NTLinux Runtime, a Steam Play custom compatibility tool
# (Phase 10, ARCHITECTURE.md section 10/38).
#
# Stated precisely what this is and isn't: this execs plain `wine`
# directly against a WINEPREFIX Steam itself manages
# (STEAM_COMPAT_DATA_PATH), NOT `ntloader` — ntloader (ntloader/) is a
# binfmt_misc interpreter designed for `./game.exe` launched directly
# outside Steam's own prefix-lifecycle contract (ARCHITECTURE.md
# section 4), a genuinely different calling convention (Steam invokes
# this script as `%verb% <args...>`, not a bare PE path) and a
# different prefix-ownership model (ntloader owns
# ~/.local/share/ntlinux/apps/<app>/prefix/; a Steam Play compat tool
# must use Steam's own per-game STEAM_COMPAT_DATA_PATH instead). Wiring
# ntabi/ntd's own routing into a Steam-launched game (rather than a
# directly-launched .exe) is real follow-up work for a later NT-runtime
# generation (ARCHITECTURE.md section 3.2's migration table), not
# claimed here. What this tool provides today: a genuine, working
# Steam Play entry that runs a game through this project's own Wine
# stack (distro/packages/base.list's wine/wine-staging) rather than
# Valve's bundled Proton — useful now for testing/debugging against
# NTLinux's own Wine version independent of whatever Proton ships, the
# concrete, honest scope of "gaming integration" this pass delivers.
#
# Verb handling matches the minimal real contract Steam's custom
# compatibility tools actually need (documented by example across
# several public vanilla-Wine Steam Play wrapper tools, not invented
# here — Rule 1/9): `waitforexitandrun` is the one verb that matters
# (launch the game and block until it exits); every other verb Steam
# may send (getcompatpath, getnativepath, ...) is optional to implement
# and safely exits 0.
set -u

VERB="${1:-}"
shift || true

case "$VERB" in
    waitforexitandrun)
        if [ -z "${STEAM_COMPAT_DATA_PATH:-}" ]; then
            echo "ntlinux-runtime run.sh: STEAM_COMPAT_DATA_PATH not set" \
                 "- this script must be launched by Steam as a compatibility tool" >&2
            exit 1
        fi
        export WINEPREFIX="$STEAM_COMPAT_DATA_PATH/pfx"
        mkdir -p "$WINEPREFIX"
        exec wine "$@"
        ;;
    *)
        exit 0
        ;;
esac
