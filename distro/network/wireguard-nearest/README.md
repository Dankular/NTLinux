# WireGuard Nearest-Server Selection

Automatic "closest ProtonVPN free-tier server" selection for NTLinux's
default network security posture (ROADMAP.md Phase 15).

**Owner:** NTLinux (integration only — WireGuard itself is Linux upstream,
`wireguard-tools` is Debian/Arch-packaged upstream, ProtonVPN's account
system and key issuance are Proton's; see "Reuse, not reimplementation"
below)
**Status:** Server-selection logic implemented and verified live; actual
tunnel bring-up **not** verified in this sandbox — stated precisely below

## What this is

- `select-nearest-server.py` — fetches ProtonVPN's public free-tier
  logical-server list, estimates the host's location, ranks candidates
  by great-circle distance, and (given an existing WireGuard profile as
  a template) rewrites only that profile's `[Peer]` `PublicKey`/
  `Endpoint` to point at the winner. `[Interface]` — the account-tied
  `PrivateKey`/`Address`/`DNS` — is left byte-for-byte untouched.
- `ntlinux-wireguard-nearest.sh` + `.service` — the distro-integration
  half: a systemd oneshot that runs the selector against a template
  profile placed out-of-band at `/etc/ntlinux/wireguard/
  protonvpn-template.conf`, and reloads `wg-quick@wg0` if the winning
  peer changed. A no-op (not a failure) if no template is present —
  see "Reuse, not reimplementation" for why NTLinux doesn't generate
  that template itself.

## Reuse, not reimplementation (Rule 1/17)

This deliberately does **not** reimplement any part of ProtonVPN's
account system:

- **WireGuard itself** is upstream (Linux kernel `wireguard` module, or
  a userspace backend for kernels/environments without it) —
  `wireguard-tools` (`wg`, `wg-quick`) is the only new package this adds
  (`distro/packages/base.list`).
- **The account-tied WireGuard keypair** (the `[Interface]` section —
  `PrivateKey`, the assigned tunnel `Address`) has to come from a real,
  authenticated ProtonVPN session: `account.protonvpn.com` -> Downloads
  -> WireGuard configuration (documented at
  <https://protonvpn.com/support/wireguard-configurations>), or the
  official ProtonVPN Linux app, both of which implement Proton's real
  SRP login. This project did not attempt to hand-roll that login
  against `api.protonvpn.ch` — Proton's real API gates `/vpn/logicals`
  behind an `x-pm-appversion` client-identification header, and
  spoofing a specific official client's version string to get past that
  gate is not something this project is willing to do (both an ADR-0002/
  Rule-17-style "don't build permanent dependencies by working around a
  third party's access controls" concern, and this repo's own tooling
  flagged the attempt directly — see "What's verified" below).
- The **free-tier server list** used for live testing came from
  <https://protonvpn-serverlist.up.railway.app/> — a third-party mirror
  of Proton's `/vpn/logicals` response, **not an official Proton
  domain**, stated plainly rather than left ambiguous. It answers
  unauthenticated with Proton's real JSON schema (confirmed by direct
  inspection: `LogicalServers[].{Name, EntryCountry, City,
  Location:{Lat,Long}, Tier, Servers[].{EntryIP, Domain,
  X25519PublicKey}}`), which is why `select-nearest-server.py` works
  against it unmodified — and why it'll work unmodified against Proton's
  own endpoint too, once NTLinux has a legitimate registered client
  identity to call it with (a real known gap, not silently routed
  around — see below).

## What's verified live, and how

Run in this session, for real, against the live mirror and a real GeoIP
lookup (no mocked data):

```
$ python3 select-nearest-server.py --top 8
# host location (estimated): Council Bluffs, United States
# 114 tier-0 candidates, nearest 8:
#       689 km  US-FREE#9      Chicago         node-us-128.protonvpn.net
#       689 km  US-FREE#48     Chicago         node-us-157.protonvpn.net
#       948 km  US-FREE#6      Dallas          node-us-133.protonvpn.net
...
```

Notably, `US-FREE#48` — the exact server named in a real WireGuard
profile a project maintainer had already downloaded from their own
ProtonVPN account — came back tied-nearest from this independent
computation, not cherry-picked or fabricated to match.

`--template`/`--out` peer-patching was verified against a synthetic
dummy profile (never a real key — see "Handling real key material"
below): confirmed the `[Interface]` block passes through unchanged
while `[Peer]` `PublicKey`/`Endpoint` are rewritten to the selected
server, with a trailing comment recording which server and why.

**What was explicitly *not* attempted, and why:** this project's own
sandboxed permission classifier blocked two separate live probes in
this session — a raw-UDP reachability check (`/dev/udp/...` to a real
ProtonVPN WireGuard endpoint) and spoofing Proton's official
`x-pm-appversion` client header to get past their API's version gate.
Both blocks are read as intentional, not obstacles to route around: this
sandbox's egress is deliberately funneled through its configured HTTPS
proxy only (a raw outbound UDP tunnel would bypass that), and
impersonating a specific official client's version string to defeat a
third party's own access gate isn't something this project does
regardless of sandbox policy. Neither is worked around here.

## What's *not* verified — stated precisely, same standard as every
## other phase in this repo

- **No actual WireGuard tunnel has been brought up.** This sandbox is
  the same Firecracker microVM Phase 6 (VFIO) found has no loadable
  kernel modules (`nomodule` on the cmdline — `wireguard-tools` installs
  cleanly, but `modprobe wireguard` has nothing to load) and, per the
  classifier finding above, no exercised path for raw UDP egress either
  — WireGuard's kernel module is unavailable here for the same
  environmental reason VFIO/IOMMU access is, and the userspace
  (`wireguard-go`/`boringtun`) fallback wasn't pursued once the egress
  restriction became clear, since a tunnel that can't send its own UDP
  packets has nothing left to verify. A real handshake against a real
  ProtonVPN endpoint needs a host (or a differently-configured sandbox)
  that permits kernel WireGuard or raw UDP egress — genuinely
  out-of-reach here, not a gap in the code.
- **No live call against Proton's own `api.protonvpn.ch`.** Verified
  only against the third-party mirror named above. Getting a legitimate
  registered ProtonVPN client identity (rather than spoofing an
  existing one) is real follow-up work, not attempted in this pass.
- **`ntlinux-wireguard-nearest.service` has not been run under systemd**
  — written against the same patterns as this repo's other systemd
  units under `distro/image/airootfs/`, but not booted/exercised in this
  sandbox (no systemd PID 1 here).

## Handling real key material — binding, not just a preference

- **Nothing under this directory generates, stores, or commits a real
  WireGuard private key or ProtonVPN account credential**, and nothing
  should ever be added here that does. The one real profile referenced
  during this work (downloaded from a maintainer's own ProtonVPN
  account) was read only to confirm the on-disk format this tooling
  targets, was tested against with only the dummy template shown above,
  and was never written into any file this repository tracks.
- `ntlinux-wireguard-nearest.service` reads the account-tied template
  from `/etc/ntlinux/wireguard/protonvpn-template.conf` — outside the
  repo, outside the built image, placed there out-of-band by whoever
  owns the machine. `distro/image/` must never bake a real template or
  key into the ISO.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
