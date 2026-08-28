# Windows Driver Support

Hosted ReactOS NT kernel environment for running real Windows .sys drivers safely under KVM/VFIO (ARCHITECTURE.md sections 12-27).

**Owner:** ReactOS (kernel semantics) + NTLinux (hosting/bridge)
**Status:** Driver-cell prototype verified end-to-end (Phase 4); first real driver loads and performs I/O inside real ReactOS (Phase 5); NDIS and USB bridges verified end-to-end over a QEMU VM boundary (Phases 7-8)

Phase 4 delivered the driver-cell prototype ARCHITECTURE.md sections
14-19 describe, to the extent this sandbox can genuinely verify:

- `cell/` — `ntcell`, a real QEMU launcher (KVM-preferring, TCG
  fallback), boots a real ReactOS 0.4.15 kernel (screenshot-verified —
  see `cell/README.md`).
- `ntbridge/` — the host/cell shared-memory protocol
  (`ntbridge/protocol/`), the Linux-side daemon
  (`ntbridge/host/ntbridge-host`), and the ReactOS-side WDM bus driver
  source (`ntbridge/reactos/ntbridge_pnp.c`, written but not yet built —
  needs the RosBE toolchain this sandbox doesn't have). The protocol +
  host + transport are proven end-to-end over a real QEMU VM boundary
  using an honest stand-in guest client in place of the real ReactOS
  driver — see `tests/reactos/README.md`.
- `pnp-host/` — not started yet (the real sysfs/udev/netlink device
  enumerator; `ntbridge-host` currently seeds a fixed synthetic device
  list instead, see `ntbridge/host/README.md`'s "known gap").
- `vsdev/` (Phase 5) — NTLinux's first real Windows driver: loads and
  performs genuine `CreateFile`/`WriteFile`/`ReadFile` I/O inside a real,
  freshly-booted ReactOS x64 kernel, verified live and reproducibly (an
  automated `run-test.sh` drives the whole sequence unattended). Found
  and fixed a real bug along the way (`sc start`'s legacy load path never
  calls `AddDevice`) and corrected a real architecture assumption
  (ReactOS 0.4.15 stable is x86-only; a ReactOS x64 nightly build is used
  instead). PnP-triggered load and `ntbridge` routing are the next real
  steps — see its own README.
- `net/` (Phase 7) — the NDIS bridge: a real NDIS 5.1 miniport
  (`net/reactos/ntnet.c`, builds and links clean) plus
  `ntbridge/host`'s `--tap` mode bridging to a real Linux TAP device.
  Protocol/transport/host bridge verified live over a real QEMU VM
  boundary; the miniport itself not yet loaded inside ReactOS — see its
  own README and ROADMAP.md Phase 14.
- `usb/` (Phase 8) — the USB bridge: a WDM bus driver
  (`usb/reactos/ntusb.c`, builds and links clean — not a full Host
  Controller Driver, see its own README for why) plus
  `ntbridge/host`'s `--usb-echo` mode. Same verified-transport/
  not-yet-loaded-live split as `net/`.

`iommu/`, `storage/`, `vfio/` are still untouched — later-phase
territory (`vfio/` is Phase 6, explicitly deferred: this sandbox has no
PCI/IOMMU at all, see ROADMAP.md Phase 6).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
