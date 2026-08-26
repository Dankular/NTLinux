# NTLinux

> A Linux operating system that treats the NT execution model as a
> first-class personality, reusing Wine for Windows userland, Proton for
> games, ReactOS for NT kernel/driver semantics, and Linux/KVM/VFIO for safe
> hardware execution.

NTLinux does not rewrite Windows. It reuses the strongest existing
implementations of each piece — Linux, Wine, Proton, ReactOS, KVM/VFIO/IOMMU
— and builds the integration layer between them, so Windows applications and
Windows driver binaries run as first-class workloads instead of foreign
programs launched through a compatibility command.

```text
./game.exe          # not: wine game.exe
```

This repo builds two things, not one:

1. **The NT Runtime** (`ntloader/`, `ntabi/`, `ntd/`, `runtime/`) —
   host-agnostic, installable on any reasonably modern Linux distro, the
   same way Wine and Proton already are today.
2. **The NTLinux reference distro** (`distro/`) — one opinionated bundling
   of the runtime plus Steam/Wine/Proton/DXVK/Wayland for a
   best-out-of-the-box experience, not the runtime's only supported home.

Reuse is the default everywhere: ReactOS for real NT kernel semantics (the
one place genuine reimplementation is unavoidable), existing Linux packages
for everything else that already has a solution (Wine, DXVK, vkd3d-proton,
PipeWire, Mesa, ...). NTLinux-only code is the integration layer between
them — see [`docs/DECISIONS.md`](docs/DECISIONS.md) ADR-0001/ADR-0002.

## Start here

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the full architecture:
  design principles, NT user runtime, NT host ABI, ReactOS driver cell,
  device/driver bridging, security model, repository layout, testing
  strategy, and the rules every contributor (human or agent) follows.
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — amendments made after that doc
  was written (host-agnostic runtime, reuse-over-vendoring).
- [`ROADMAP.md`](ROADMAP.md) — build phases, current phase, and Phase 0's
  detailed task breakdown.
- [`CLAUDE.md`](CLAUDE.md) — condensed contributor rules for coding agents.
- Every directory under this repo has its own `README.md` explaining what it
  owns and its current status.

## Status

Pre-alpha. Phase 0 (baseline gaming-Linux distro, no custom NT architecture
yet) is in progress — see [`ROADMAP.md`](ROADMAP.md).

## Repository layout

```text
distro/     base distro image, installer, package manifests, rootfs
kernel/     kernel config/patches (Linux stays the host kernel)
ntabi/      NT <-> Linux host ABI (protocol, libntabi, ntd client side)
ntloader/   PE executable launcher
ntd/        NT userspace service daemon (objects, registry, services, ...)
runtime/    NT user runtime (Wine-derived: ntdll, win32u, wow64)
graphics/   DXVK, vkd3d-proton, DXVK-NVAPI (vendored)
proton/     Proton fork (compatibility/profile layer)
driver/     ReactOS driver-cell hosting, ntbridge, VFIO/IOMMU, PnP bridging
reactos/    vendored ReactOS + NTLinux patches
desktop/    Wayland/desktop integration for NT windows
tooling/    compat database, tracing, debugger, installer tooling
tests/      differential compatibility test corpus
docs/       architecture reference
```

## License

Not yet decided. NTLinux integrates GPL-2.0 (Linux, ReactOS), LGPL (Wine),
and permissively-licensed (DXVK, vkd3d-proton) components under one project;
the licensing approach for NTLinux's own integration code needs an explicit
decision before this says otherwise.
