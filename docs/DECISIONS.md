# Architecture decisions

Amendments and clarifications to `docs/ARCHITECTURE.md` made after that
document was written, in ADR-lite form. `docs/ARCHITECTURE.md` stays as the
original canonical snapshot; this file is where it gets refined. On any
conflict, the most recent entry here wins.

---

## ADR-0001 — The NT Runtime is host-agnostic

**Date:** 2026-08-26

**Decision:** `ntloader`, `libntabi`, `ntd`, and `runtime/` (the NT User
Runtime and NT Host ABI — everything between a Windows PE binary and the
Linux kernel, *excluding* the ReactOS driver cell) must build and run on any
reasonably modern Linux distribution, not only on the NTLinux reference
image in `distro/`.

**Rationale:** This mirrors how Wine and Proton already work today — they
are not tied to one distro, and that portability is a large part of why
they're the right things to build on (Rule 1). Locking the runtime to
NTLinux's own packaging would recreate exactly the kind of unnecessary,
narrow reimplementation the project's core principles argue against.

**Consequences:**
- No component under `ntloader/`, `ntabi/`, `ntd/`, `runtime/` may assume
  Arch/pacman-specific paths, an NTLinux-only init layout, or any facility
  the reference image happens to provide but a generic distro wouldn't
  (e.g. don't hardcode `/etc/ntlinux/...`; use FHS-conventional, distro-
  neutral locations, matching how Wine/Proton install today).
  `ntd`'s mapping onto systemd (section 33) is an implementation detail
  behind an abstraction, not a hard dependency — the door stays open to a
  non-systemd host.
- `distro/` (the archiso-based image) becomes a **reference integration
  target**, not the runtime's only supported environment: it exists to
  prove the whole stack works end-to-end and to give users a
  best-out-of-the-box experience, the way SteamOS is one particular
  packaging of Linux+Proton rather than the only place Proton runs.
- The ReactOS driver cell (`driver/`, `reactos/`) is *not* covered by this
  ADR in the same way — it genuinely needs KVM/VFIO/IOMMU, which are Linux
  facilities, not distro-specific ones. "Host-agnostic" here means
  "distro-agnostic Linux host," not "hypervisor-agnostic" or
  "kernel-agnostic" (section 1.2 already commits to Linux as the one host
  kernel).

**Status update to Phase 0 (`ROADMAP.md`):** the archiso profile in
`distro/image/` stays as the reference-distro deliverable, but Phase 0's
package list was trimmed of KVM/VFIO/libvirt packages that belong to
Phase 4+ (driver cell), not the baseline gaming-distro milestone.

---

## ADR-0002 — Prefer existing distro packages over vendoring, everywhere the choice exists

**Date:** 2026-08-26

**Decision:** Extends Rule 1/Rule 9 concretely to the `graphics/` and
`proton/` directories: DXVK, vkd3d-proton, DXVK-NVAPI, and Proton itself
should be **consumed as upstream/distro packages** (official repos, AUR, or
Proton's own bundled copies via Steam) by default. Only place source under
`graphics/*` or `reactos/patches` when NTLinux needs a patch that doesn't
exist upstream yet — and even then, the patch should be written with
upstreaming in mind (Rule 3), with the vendored tree treated as staging,
not a permanent fork.

**Rationale:** the ReactOS driver cell is where genuine, hard-to-avoid
reimplementation work lives (real NT kernel semantics don't exist as a
Linux package). Everywhere else — the D3D-on-Vulkan layers most of all —
there is no gap to fill; the upstream projects already solve the problem
and already ship as installable packages on essentially every distro.
Vendoring them here would (a) duplicate maintenance the upstream projects
already do, and (b) work against ADR-0001's host-agnostic goal, since a
vendored build has to be rebuilt/repackaged per distro anyway.

**Consequences:**
- `distro/image/packages.x86_64` installs `dxvk`/`vkd3d-proton` style
  packages from the distro's package manager (or leaves them to Proton's
  own bundling via Steam) rather than building them from source in-tree.
- `graphics/*/README.md` and `reactos/patches/README.md` updated to state
  this explicitly.

---

## ADR-0003 — Native Windows GPU driver hosting is a named milestone, not just a footnote

**Date:** 2026-08-26

**Decision:** `docs/ARCHITECTURE.md` section 30 already declines to make
Windows WDDM GPU drivers an MVP dependency and calls native GPU-driver
execution "a research milestone." That's still correct — DXVK/vkd3d-proton
on Mesa/Vulkan stays the default gaming path (section 47's performance goal:
normal games never cross into the driver cell). This ADR promotes that
research milestone from a footnote to a tracked roadmap phase
(`ROADMAP.md` Phase 11), because the payoff is real and the architecture to
get there already exists rather than needing new invention:

- Same driver-cell mechanism as any other Windows kernel driver
  (`docs/ARCHITECTURE.md` sections 14-27): ReactOS hosts the driver, VFIO/
  IOMMU mediates hardware access, Linux stays the trust boundary.
- Same reuse-first principle: build out WDDM/DXGKRNL support in ReactOS
  (upstreamed, per Rule 3) rather than an NTLinux-only shim.
- The value isn't "replace Mesa" — Mesa/DXVK/vkd3d-proton keep owning the
  default path (Rule 9, section 47). It's covering the cases they can't:
  brand-new hardware ahead of open-source driver support, vendor features
  or professional/compute workloads that only ship a Windows driver, and
  GPUs where the Linux driver is meaningfully behind.

**Consequences:**
- Added as Phase 11 in `ROADMAP.md`, explicitly sequenced after Phases 4-9
  (driver-cell prototype through modern ReactOS driver compatibility) since
  it has no chance of working before ReactOS's WDM/KMDF/PnP/NDIS-level
  compatibility work has already landed a simpler class of driver.
  Explicitly research/best-effort, per Rule 10 and section 55's guidance
  that early hardware milestones stay away from the primary GPU.
- Not a blocker for anything earlier. Section 47's performance rule stands:
  a driver-hosted GPU is an opt-in path for otherwise-unsupported hardware,
  never a detour on the default Vulkan path normal games take.
