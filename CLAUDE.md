# CLAUDE.md — NTLinux contributor rules

This file is read automatically by Claude Code (and should be treated as
binding by any coding agent or contributor) working in this repository.

## What NTLinux is

NTLinux is a Linux distribution that treats the NT execution model (Windows
applications and Windows kernel drivers) as a first-class personality on top
of Linux, by **reusing** existing mature implementations instead of rewriting
them:

- **Linux** — host kernel, hardware, scheduling, filesystems, networking,
  DRM/Vulkan, virtualization (KVM/VFIO/IOMMU).
- **Wine** — Win32/Win64 user-mode API implementation.
- **Proton** — Windows gaming compatibility, DXVK, vkd3d-proton, Steam
  integration.
- **ReactOS** — NT kernel semantics: WDM, I/O Manager, Object Manager, PnP,
  IRPs, device stacks, registry/kernel services, driver execution.
- **KVM/VFIO/IOMMUFD** — safe hosting of Windows kernel drivers that need
  ring 0 or direct hardware access.

The full architecture — object model, NT host ABI, ReactOS driver cell,
build phases, testing strategy, ownership boundaries — is canonical in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). Read it before making any
non-trivial architectural decision. [`docs/DECISIONS.md`](docs/DECISIONS.md)
records amendments made after that doc was written — check it too, most
recent entry wins on conflicts. [`ROADMAP.md`](ROADMAP.md) sequences the
work into phases; work the current phase before later ones.

Two things NTLinux is building, not one:

1. **The NT Runtime** (`ntloader/`, `ntabi/`, `ntd/`, `runtime/`) — host-
   agnostic. Installable on any reasonably modern Linux distro, the way
   Wine and Proton already are. See `docs/DECISIONS.md` ADR-0001.
2. **The NTLinux reference distro** (`distro/`) — one particular,
   opinionated bundling of the above plus Steam/Wine/Proton/DXVK/Wayland
   etc. for a best-out-of-the-box experience (à la SteamOS), not the only
   place the runtime is allowed to run.

The ReactOS driver cell (`driver/`, `reactos/`) is the one place genuine
reimplementation is unavoidable — real NT kernel semantics aren't a Linux
package. Reuse ReactOS for that, reuse existing Linux packages for
everything else with a gap to fill (Wine for Win32, DXVK/vkd3d-proton for
D3D, PipeWire for audio, ...); write new NTLinux-only code only for the
integration layer between them.

NTLinux is **not**: a Windows clone, ReactOS with a Linux theme, Wine
preinstalled on a distro, a full-system Windows VM, a new Direct3D
implementation, a replacement Linux kernel, or a project that loads `.sys`
files directly into Linux kernel space.

## Rules (binding)

1. Before implementing a Windows API or NT kernel facility, search upstream
   Wine and ReactOS first. Check in this order: Wine, Proton, ReactOS, Linux
   kernel, Mesa, DXVK, vkd3d-proton, Gamescope, PipeWire, VFIO/IOMMUFD/KVM.
   Default to adapting/exposing/refactoring/upstreaming existing code, not
   duplicating it.
2. Do not create a duplicate NT subsystem merely because adapting upstream
   code appears inconvenient.
3. Prefer patches that can be upstreamed to Wine, ReactOS, Proton or Linux
   over permanent NTLinux-only forks.
4. Keep Linux-specific code outside generic ReactOS NT logic whenever
   possible.
5. Never load untrusted Windows `.sys` code directly into the Linux kernel.
6. Hardware access from a Windows driver must be mediated through VFIO,
   IOMMU, or an equivalent safe host facility — never given raw access.
7. Normal Win32 execution must not require a KVM guest. The driver cell is
   for kernel-driver execution only.
8. The ReactOS driver cell exists for kernel-driver execution, not as a
   hidden Windows desktop VM (no Explorer/Winlogon/shell in it).
9. Do not rewrite DXVK, vkd3d-proton, Mesa, PipeWire, or other mature host
   subsystems.
10. Compatibility regressions are more important than architectural
    elegance — do not break working Windows-app/driver behavior for the
    sake of a cleaner design.
11. Every NT compatibility implementation requires tests (differential
    against real Windows/Wine/ReactOS behavior where feasible — see
    `docs/ARCHITECTURE.md` section 44).
12. Cross-boundary protocols (`ntabi`, `ntbridge`) must be versioned.
13. Prefer shared-memory transports over chatty RPC for hot I/O paths.
14. All foreign driver execution is considered untrusted.
15. Document which upstream project owns every compatibility behavior you
    touch (see the ownership table in `docs/ARCHITECTURE.md` section 51).
16. The NT Runtime (`ntloader/`, `ntabi/`, `ntd/`, `runtime/`) must stay
    host-agnostic — no code there may assume the NTLinux reference distro's
    packaging, paths, or init system. `distro/` is a reference integration
    target, not the runtime's only supported environment (ADR-0001).
17. Prefer consuming an existing distro/upstream package (DXVK,
    vkd3d-proton, DXVK-NVAPI, Proton itself) over vendoring source into this
    repo. Vendor only to carry an NTLinux-specific patch, and only with an
    upstreaming plan (ADR-0002).

## Directory map

Every top-level and leaf directory has a `README.md` stating its purpose,
current owner (which upstream project or NTLinux itself), and status. Start
there before writing code in a new area. The full tree layout is defined in
`docs/ARCHITECTURE.md` section 41.

## Open decisions not yet locked in

These were picked as reasonable Phase 0 defaults but have not been ratified
by the project owner — flag before building heavily on top of them:

- **Base distro foundation:** Arch Linux + `archiso` (rolling release,
  current Mesa/kernel/Wine, AUR available for `dxvk-bin`/`vkd3d-proton`).
- **Default session:** Gamescope ("Game Mode") as the primary session, with
  Plasma (KDE, Wayland) as an optional desktop-mode session — mirrors
  SteamOS 3's split, and lines up with `docs/ARCHITECTURE.md` section 10's
  Gamescope emphasis.
- **Licensing:** not yet decided and not set — the project mixes GPL-2.0
  (Linux, ReactOS), LGPL (Wine), and MIT/BSD-style (DXVK, vkd3d-proton)
  code. This needs an explicit decision by the project owner before any
  code (as opposed to docs/scaffolding) ships; do not add a repo-wide
  `LICENSE` file without that decision.
