# NTLinux Roadmap

Phases mirror `docs/ARCHITECTURE.md` section 43. Work the current phase
before starting later ones — compatibility and a usable system come before
architectural purity (Rule 10).

Legend: ⬜ not started · 🟨 in progress · ✅ done

---

## Phase 0 — Baseline distro 🟨 (current)

**Goal:** a fully usable gaming Linux distro, before any custom NT
architecture exists. If a user booted this image today, they'd have a
working Linux desktop with Wine/Proton/Steam already working the normal way.

This phase builds the **reference distro** (`distro/`) only — a curated
bundle of upstream packages, no NTLinux runtime code yet (Phases 1-3) and no
ReactOS driver cell (Phases 4+). See ADR-0001/ADR-0002 in
`docs/DECISIONS.md`: consume upstream packages, don't vendor; keep KVM/VFIO
out of scope here — it belongs to the driver-cell phases.

**Base decisions (defaults — see "Open decisions" in `CLAUDE.md`):**
- Foundation: Arch Linux, built via `archiso` (`distro/image/`).
- Session: Gamescope (game mode) as primary, Plasma (Wayland) as desktop
  mode.

**Task breakdown:**

- [x] Repo scaffold + canonical docs (`docs/ARCHITECTURE.md`, `ROADMAP.md`,
      `CLAUDE.md`, per-directory `README.md`s).
- [x] `distro/image/` — archiso profile skeleton (`profiledef.sh`,
      `packages.x86_64`) that builds a bootable ISO.
- [x] `distro/packages/base.list` — canonical Phase 0 package manifest,
      annotated by doc section.
- [ ] `kernel/config/` — kernel config fragment: confirm `CONFIG_NTSYNC=y`
      (mainline since Linux 6.14, donated by Valve/CodeWeavers for Wine
      sync — this *is* item 3 of section 53, "NT-oriented Linux
      synchronization support"), `CONFIG_KVM`, `CONFIG_VFIO*`,
      `CONFIG_IOMMUFD`.
- [ ] Wine + Proton packaged and launchable via Steam, with `ntsync` enabled
      and DXVK/vkd3d-proton working through a real GPU driver (Mesa or
      NVIDIA proprietary).
- [ ] Build-test the archiso profile end-to-end on real Arch tooling
      (`mkarchiso`) — **not done in this sandbox**: this container has no
      pacman/archiso toolchain or mirror access, so the profile is
      written and reviewed but not build-verified yet. Needs to happen on
      an Arch host or CI runner before Phase 0 is called done.
- [ ] Boot the built ISO, confirm Steam + a test game run end-to-end.

**Success criterion (from `docs/ARCHITECTURE.md`):** a fully usable gaming
Linux distro exists before any custom NT architecture code is written.

---

## Phase 1 — Native Windows executable UX ⬜

Deliver `ntloader`, PE binary-format registration, per-app environment
management, desktop file generation, a Windows application installer.

**Success criterion:** `./notepad.exe` works without explicitly invoking
`wine`.

---

## Phase 2 — NT ABI prototype ⬜

Deliver `libntabi`, `ntd`, a versioned protocol, and basic handles/events/
mutexes/semaphores/sections/virtual-memory ops. Route selected `ntdll`
functionality through it.

**Success criterion:** Wine test suites continue passing while some core NT
operations no longer use the normal Wine (wineserver) path.

---

## Phase 3 — Object and wait semantics ⬜

Move named objects, handle tables, wait-any/wait-all, sections, shared
memory, process/thread metadata, completion ports, APC support toward
NTLinux-native implementation.

**Success criterion:** measured reduction in wineserver IPC traffic.

---

## Phase 4 — ReactOS driver-cell prototype ⬜

Deliver a minimal bootable ReactOS image, KVM launcher, `ntbridge`, logging
channel, host heartbeat, device enumeration bridge.

**Success criterion:** ReactOS sees synthetic devices supplied by Linux.

---

## Phase 5 — First Windows driver ⬜

Start with a simple virtual/low-risk device (virtual serial, virtual block,
simple USB, virtual network adapter). Avoid GPU drivers initially.

**Success criterion:** a real Windows `.sys` loads, receives PnP IRPs, and
performs I/O through the host bridge.

---

## Phase 6 — VFIO hardware passthrough ⬜

Add PCI BAR mapping, MSI/MSI-X delivery, DMA mappings, device reset, IOMMU
protection, resource descriptors.

**Success criterion:** a selected real Windows PCI driver operates physical
hardware while Linux stays stable if the cell crashes.

---

## Phase 7 — NDIS bridge ⬜

**Success criterion:** a Windows NIC driver exposes a working Linux network
interface.

---

## Phase 8 — USB driver bridge ⬜

**Success criterion:** a vendor Windows USB driver operates a device
unavailable through a native Linux driver.

---

## Phase 9 — Modern ReactOS driver compatibility ⬜

Upstream work on KMDF, newer WDM, newer NT exports, modern NDIS, PnP, power,
memory management, security. Maintain conformance tests against documented
Windows behavior.

---

## Phase 10 — Gaming integration ⬜

Tight Steam/Proton/NTLinux runtime/Gamescope/DXVK/vkd3d-proton integration
plus the driver compatibility database. Windows applications and games
should feel like native Linux packages.

---

## Phase 11 — Native Windows GPU driver hosting (research) ⬜

Not an MVP dependency (`docs/ARCHITECTURE.md` section 30) — the default
gaming path stays DXVK/vkd3d-proton on Mesa/Vulkan (section 47), and this
phase never gets ahead of that. Tracked explicitly because the payoff is
real and the mechanism already exists rather than needing new invention
(ADR-0003 in `docs/DECISIONS.md`): host a Windows WDDM GPU driver in the
same ReactOS driver cell used for any other kernel driver, with VFIO/IOMMU
mediating the actual hardware.

**Depends on:** Phases 4-9 — a GPU driver is one of the hardest classes of
Windows driver, not a good early target (section 55: avoid the primary GPU
in earlier phases). Requires ReactOS WDDM/DXGKRNL support that mostly
doesn't exist yet (section 29) — build it there, upstreamed, per Rule 3.

**Why it's worth it:** covers hardware/workloads Mesa can't — brand-new
GPUs ahead of open-source driver support, vendor features or professional/
compute workloads that only ship a Windows driver, hardware where the
Linux driver is meaningfully behind.

**Success criterion:** a real Windows WDDM GPU driver operates a
non-primary GPU inside the driver cell, rendering output reaches the host
compositor through the existing VFIO/IOMMU-mediated path, and Linux
survives a driver crash inside the cell (section 26).

---

## Immediate next steps (unordered backlog, section 53)

1. ~~Create base distro image (scaffold).~~ ✅ scaffold done, build-untested.
2. ~~Package current Wine + Proton stack.~~ 🟨 package list drafted.
3. Enable NT-oriented Linux synchronization support (`ntsync`). 🟨 config
   fragment drafted, not build-verified.
4. Implement `ntloader`.
5. Make PE binaries directly executable.
6. Build per-application NT environment manager.
7. Implement `libntabi` protocol skeleton.
8. Route one small set of `ntdll` operations through it.
9. Create ReactOS minimal driver-cell build experiment.
10. Boot that build under KVM with no desktop.
11. Create `ntbridge` shared-memory hello/heartbeat protocol.
12. Send synthetic PCI/USB device descriptions from Linux to ReactOS.
13. Confirm ReactOS PnP creates device nodes.
14. Load a simple test `.sys`.
15. Deliver `IRP_MN_START_DEVICE`.
16. Bridge test I/O back to Linux.
