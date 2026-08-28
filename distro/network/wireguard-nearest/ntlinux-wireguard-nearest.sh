#!/bin/bash
# ntlinux-wireguard-nearest.sh — re-pin the NTLinux WireGuard peer to
# whichever ProtonVPN free-tier server select-nearest-server.py currently
# ranks closest, then (re)load it with wg-quick.
#
# Run by ntlinux-wireguard-nearest.service (systemd oneshot) — see this
# directory's README.md and ROADMAP.md Phase 15 for what's actually been
# verified end-to-end vs. what's designed but unexercised in this sandbox.
#
# Deliberately does NOT generate or hold any account-tied key material.
# TEMPLATE_CONF has to already exist — the real WireGuard profile
# downloaded from the user's own ProtonVPN account (account.protonvpn.com
# -> Downloads -> WireGuard configuration, or the official Linux app,
# which does the real SRP login this project didn't try to reimplement)
# placed there out-of-band, e.g. during first-boot setup. If it isn't
# there, this unit is a documented no-op rather than a hard failure —
# WireGuard security is opt-in, not silently assumed.

set -euo pipefail

TEMPLATE_CONF="${NTLINUX_WG_TEMPLATE:-/etc/ntlinux/wireguard/protonvpn-template.conf}"
OUT_CONF="${NTLINUX_WG_OUT:-/etc/wireguard/wg0.conf}"
SELECTOR="$(dirname "$(readlink -f "$0")")/select-nearest-server.py"
IFACE="${NTLINUX_WG_IFACE:-wg0}"

if [ ! -f "$TEMPLATE_CONF" ]; then
    echo "ntlinux-wireguard-nearest: no template profile at $TEMPLATE_CONF" \
         "— nothing to do (place a ProtonVPN-issued WireGuard profile" \
         "there to enable this unit)" >&2
    exit 0
fi

TMP_OUT="$(mktemp)"
trap 'rm -f "$TMP_OUT"' EXIT

python3 "$SELECTOR" --template "$TEMPLATE_CONF" --out "$TMP_OUT"

if [ -f "$OUT_CONF" ] && cmp -s "$TMP_OUT" "$OUT_CONF"; then
    echo "ntlinux-wireguard-nearest: nearest server unchanged, leaving $IFACE alone" >&2
    exit 0
fi

install -D -m 600 "$TMP_OUT" "$OUT_CONF"
echo "ntlinux-wireguard-nearest: wrote $OUT_CONF, reloading $IFACE" >&2

if command -v wg-quick >/dev/null 2>&1; then
    wg-quick down "$IFACE" 2>/dev/null || true
    wg-quick up "$IFACE"
else
    echo "ntlinux-wireguard-nearest: wg-quick not installed, wrote config only" >&2
fi
