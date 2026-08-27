# Windows Driver Support

Hosted ReactOS NT kernel environment for running real Windows .sys drivers safely under KVM/VFIO (ARCHITECTURE.md sections 12-27).

**Owner:** ReactOS (kernel semantics) + NTLinux (hosting/bridge)
**Status:** Driver-cell prototype: launcher + ntbridge protocol/host verified end-to-end; ReactOS-side driver written but unbuilt (Phase 4)

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

`iommu/`, `net/`, `storage/`, `usb/`, `vfio/` are all still untouched —
Phase 5+ territory once a real Windows driver needs one of them.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
