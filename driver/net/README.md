# NDIS Bridge

Bridges Windows NDIS miniport drivers to a Linux netdev via shared packet rings (ARCHITECTURE.md section 22).

**Owner:** NTLinux
**Status:** Protocol/transport/host bridge verified end-to-end (Phase 7); real ReactOS driver written but unbuilt-into-live-ReactOS

Same shape as `driver/ntbridge/`'s Phase 4 split, applied to networking:

- `reactos/ntnet.c` — a real NDIS 5.1 miniport driver, builds and links
  into a real `.sys` via the mingw-w64 DDK toolchain (see its README for
  three real upstream header bugs found and fixed along the way — not
  guessed, not worked around with macros, patched narrowly and
  documented). Not yet loaded inside a running ReactOS kernel — NDIS
  adapter installation is a materially bigger live-verification task
  than `driver/vsdev/`'s single `sc start`, out of reach for this pass.
- `driver/ntbridge/protocol/ntbridge_protocol.h`'s `net_tx_ring`/
  `net_rx_ring` (protocol bumped to v2 for this — Rule 12) — the wire
  format both the miniport and the host side speak.
- `driver/ntbridge/host/ntbridge-host`'s new `--tap` mode — bridges
  those rings to a real Linux TAP device.

**Verified for real, both directions, over a genuine QEMU VM boundary
and a real Linux TAP device** (not same-process, not mocked): the honest
stand-in guest test client (`tests/reactos/ntbridge-guest-test.c`, same
role as Phase 4's stand-in for `ntbridge_pnp.c`) pushes a tagged
synthetic Ethernet frame into `net_tx_ring`; `ntbridge-host --tap`
writes it to a real TAP device; a plain Linux raw-socket listener
(`tests/reactos/net-tap-echo.py` — genuinely what any ordinary Linux
network application would see on that interface, no special test hook)
captures it and confirms the content. Same script then sends a
distinctly-tagged frame into that interface; `ntbridge-host` reads it
off the TAP fd, pushes it into `net_rx_ring`; the guest test client
picks it up and confirms the content matches. See `driver/net/reactos/
run-test.sh` and `ROADMAP.md` Phase 7 for the full transcript.

**What this proves precisely:** the ntbridge network transport — the
wire format, the ring protocol, and the host-side TAP bridge — is real
and correct. What it does not yet prove: that `ntnet.c` itself, running
as an actual NDIS miniport inside a real ReactOS kernel, behaves
correctly — same honest boundary Phase 4 drew around
`ntbridge_pnp.c`, and Phase 5 drew (then closed, for the simpler
`vsdev.c` case) around driver loading in general.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
