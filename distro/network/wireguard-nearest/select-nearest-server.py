#!/usr/bin/env python3
"""select-nearest-server.py — pick the lowest-latency-estimate free-tier
ProtonVPN WireGuard peer for this host, and (optionally) patch it into an
existing WireGuard profile's [Peer] section.

ARCHITECTURE.md's networking section and ROADMAP.md Phase 15 own the
context for this script; see this directory's README.md for the full
account of what's live-verified and what isn't.

Deliberately narrow scope (Rule 2 — don't build more than the problem
calls for):

  - This script does NOT authenticate to ProtonVPN and does NOT mint
    WireGuard keys. The account-tied [Interface] PrivateKey/Address
    block has to come from the user's own authenticated ProtonVPN
    session (Settings -> Downloads -> WireGuard configuration on
    https://account.protonvpn.com, or the official ProtonVPN Linux
    app, which implements Proton's real SRP login) — reuse that flow,
    don't reimplement Proton's auth protocol (Rule 1/17).
  - What this script *does* do, for real: fetch the public logical
    server list, estimate this host's location, rank free-tier servers
    by great-circle distance (a real proxy for "closest", cheap to
    compute without touching the network beyond one GET), and — given
    an existing profile as a template — rewrite only the [Peer] section
    (PublicKey/Endpoint) to point at the winner, leaving [Interface]
    (the account-tied PrivateKey/Address/DNS) untouched.

Server-list source, stated plainly: the default below is a third-party
mirror (https://protonvpn-serverlist.up.railway.app/) of Proton's public
`/vpn/logicals` response, used here because it answers without the
app-version/client headers Proton's own api.protonvpn.ch gates behind —
this project deliberately did not try to spoof those to get past that
gate (not something to hand-roll against a third party's API). The JSON
shape returned is Proton's own schema verbatim (LogicalServers[].{Name,
EntryCountry,City,Location:{Lat,Long},Tier,Servers[].{EntryIP,Domain,
X25519PublicKey}}), confirmed by inspecting the response directly, so
this script works unmodified against api.protonvpn.ch too once NTLinux
has a legitimate way to call it (see the README's "known gaps").
"""

import argparse
import json
import math
import re
import sys
import urllib.request

DEFAULT_SERVERLIST_URL = "https://protonvpn-serverlist.up.railway.app/"
DEFAULT_GEOIP_URL = "http://ip-api.com/json/"


def http_get_json(url, timeout=15):
    req = urllib.request.Request(url, headers={"User-Agent": "ntlinux-wireguard-nearest/0.1"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.load(resp)


def host_location(geoip_url):
    """Best-effort host geolocation via a plain HTTP GeoIP lookup.

    This is an estimate of the host's *egress* location, not a survey-grade
    position — good enough to rank VPN servers by rough proximity, which is
    all "closest server" needs. Callers can pass --lat/--lon to skip this
    entirely (e.g. a distro install step that already knows the machine's
    location from setup, or a privacy-conscious user who'd rather not make
    even a GeoIP call).
    """
    data = http_get_json(geoip_url)
    lat, lon = data.get("lat"), data.get("lon")
    if lat is None or lon is None:
        raise RuntimeError(f"GeoIP response missing lat/lon: {data!r}")
    return float(lat), float(lon), data.get("city"), data.get("country")


def haversine_km(lat1, lon1, lat2, lon2):
    r = 6371.0088  # mean Earth radius, km
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dlambda / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def candidate_servers(serverlist, tier, exclude_down=True):
    out = []
    for logical in serverlist.get("LogicalServers", []):
        if logical.get("Tier") != tier:
            continue
        loc = logical.get("Location") or {}
        if loc.get("Lat") is None or loc.get("Long") is None:
            continue
        servers = logical.get("Servers") or []
        # Prefer the first server entry whose Status indicates it's up
        # (Status == 1 in Proton's schema); fall back to the first entry
        # if none report as up, rather than silently dropping the logical.
        chosen = None
        for s in servers:
            if not exclude_down or s.get("Status") == 1:
                chosen = s
                break
        if chosen is None and servers:
            chosen = servers[0]
        if chosen is None:
            continue
        out.append(
            {
                "name": logical.get("Name"),
                "city": logical.get("City"),
                "country": logical.get("EntryCountry"),
                "lat": loc["Lat"],
                "lon": loc["Long"],
                "domain": chosen.get("Domain"),
                "entry_ip": chosen.get("EntryIP"),
                "public_key": chosen.get("X25519PublicKey"),
                "load": logical.get("Load"),
            }
        )
    return out


def rank_by_distance(candidates, host_lat, host_lon):
    for c in candidates:
        c["distance_km"] = haversine_km(host_lat, host_lon, c["lat"], c["lon"])
    return sorted(candidates, key=lambda c: c["distance_km"])


def patch_peer_section(template_text, server, port=51820):
    """Rewrite only the [Peer] PublicKey/Endpoint lines of an existing
    WireGuard profile, leaving [Interface] (account-tied PrivateKey/
    Address/DNS) untouched. Never called on a profile this repo owns —
    always on a caller-supplied template that already holds real,
    account-issued key material NTLinux never generates or sees at
    build time."""
    lines = template_text.splitlines()
    out = []
    in_peer = False
    replaced_key = replaced_endpoint = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("["):
            in_peer = stripped.lower() == "[peer]"
            out.append(line)
            continue
        if in_peer and re.match(r"(?i)^publickey\s*=", stripped):
            out.append(f"PublicKey = {server['public_key']}")
            replaced_key = True
            continue
        if in_peer and re.match(r"(?i)^endpoint\s*=", stripped):
            out.append(f"Endpoint = {server['entry_ip']}:{port}")
            replaced_endpoint = True
            continue
        out.append(line)
    if not (replaced_key and replaced_endpoint):
        raise ValueError(
            "template is missing a [Peer] PublicKey/Endpoint pair to replace "
            "— expected an existing ProtonVPN-issued profile, not a blank one"
        )
    out.append(f"# NTLinux wireguard-nearest: pinned to {server['name']} "
               f"({server['city']}, {server['country']}), "
               f"~{server['distance_km']:.0f} km away")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--serverlist-url", default=DEFAULT_SERVERLIST_URL)
    ap.add_argument("--geoip-url", default=DEFAULT_GEOIP_URL)
    ap.add_argument("--lat", type=float, help="skip GeoIP, use this latitude")
    ap.add_argument("--lon", type=float, help="skip GeoIP, use this longitude")
    ap.add_argument("--tier", type=int, default=0, help="0 = free tier (default)")
    ap.add_argument("--top", type=int, default=5, help="how many candidates to print")
    ap.add_argument("--template", help="existing WireGuard profile to patch [Peer] into")
    ap.add_argument("--out", help="where to write the patched profile (default: stdout)")
    args = ap.parse_args()

    if args.lat is not None and args.lon is not None:
        host_lat, host_lon, host_city, host_country = args.lat, args.lon, None, None
    else:
        host_lat, host_lon, host_city, host_country = host_location(args.geoip_url)

    serverlist = http_get_json(args.serverlist_url)
    candidates = candidate_servers(serverlist, args.tier)
    if not candidates:
        print("no candidate servers found for the requested tier", file=sys.stderr)
        return 1
    ranked = rank_by_distance(candidates, host_lat, host_lon)

    where = f"{host_city}, {host_country}" if host_city else f"{host_lat:.3f},{host_lon:.3f}"
    print(f"# host location (estimated): {where}", file=sys.stderr)
    print(f"# {len(candidates)} tier-{args.tier} candidates, nearest {args.top}:", file=sys.stderr)
    for c in ranked[: args.top]:
        print(
            f"#   {c['distance_km']:7.0f} km  {c['name']:<14} {c['city'] or '?':<15} "
            f"{c['domain']}",
            file=sys.stderr,
        )

    winner = ranked[0]
    if args.template:
        with open(args.template) as f:
            template_text = f.read()
        patched = patch_peer_section(template_text, winner)
        if args.out:
            with open(args.out, "w") as f:
                f.write(patched)
            print(f"# wrote patched profile to {args.out}", file=sys.stderr)
        else:
            sys.stdout.write(patched)
    else:
        print(json.dumps(winner, indent=2))

    return 0


if __name__ == "__main__":
    sys.exit(main())
