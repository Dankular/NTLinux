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

**Update (Phase 11, real research pass):** "WDDM/DXGKRNL support that
mostly doesn't exist yet" undersold and oversimplified ReactOS's actual
state, checked directly rather than assumed. Real, current facts,
sourced: ReactOS's official blog (`reactos.org/blogs/investigating-wddm/`,
Oct 7, 2025 — still the only post on the topic as of this check) reports
a real, working, early Dxgkrnl implementation — VidPn negotiation and
KMDOD miniport init are enough to run Microsoft's own `BasicDisplay.sys`
WDK sample *and* a real NVIDIA Windows 7 GPU driver, producing actual
display output at native resolution. Explicitly not yet working: 3D
acceleration, scheduling, full memory management, the complete D3DKMT
API, and the usermode driver (UMD) component — meaning Direct3D/compute
workloads (this ADR's actual payoff scenarios) still have no path, only
passive 2D display does. The blog is explicit that this whole effort
depends on ReactOS's XDDM/`CDD.dll` foundation being solid first.

Separately, and unaffected by ReactOS's own progress: this project's own
mingw-w64 DDK toolchain has **zero** WDDM/DXGKRNL kernel-mode headers or
import libraries (`tooling/compat-db/ddkgap/` checks this directly, now
as a standing, re-runnable check, not a one-off finding) — distinct from
the abundant *usermode* Direct3D headers (`d3d9.h`/`d3d11.h`/`d3d12.h`)
already present for ordinary Windows applications. Even setting hardware
aside, NTLinux's own toolchain has nothing to build a cell-side WDDM
integration against yet; those headers would need to come from ReactOS's
own in-progress work or a compatible subset of Microsoft's public WDK.

Phase 6's hardware finding stands unchanged and is the harder blocker in
practice: this project has not run in any sandbox with VFIO/IOMMU/real
PCI passthrough available, and Phase 11's success criterion requires a
real second physical GPU passed through via VFIO — categorically
different from a toolchain or upstream-code gap, needs real user
hardware to ever attempt. See `ROADMAP.md` Phase 11 for the full,
sourced writeup.

---

## ADR-0004 — Desktop shell: LingmoOS is the reference/reuse target for Windows-like look and feel

**Date:** 2026-08-27

**Decision:** For `desktop/` — the eventual taskbar/dock, Start menu,
notifications, settings, and file-manager integration described in
`docs/ARCHITECTURE.md` section 9 — use
[LingmoOS](https://github.com/LingmoOS) as the primary reuse/reference
target rather than designing a shell from scratch. Confirmed by inspecting
the project directly (not just characterization): LingmoOS's `shell32`
component tree contains `kscreen` (a real, recognizable KDE display-
management component), `lingmo-framework`, `lingmo-shell`, and `osd`, a
naming/structure pattern that mirrors KDE Plasma's own split (Plasma's
`kscreen`, `plasma-framework`, `plasma-shell` equivalents) — i.e. a
modular, independently-running-component shell built on Qt/QML with KWin
as the window manager, not a monolithic desktop environment.

**Rationale:** This maps closely onto the **ShellBroker → DesktopHost /
TaskbarHost / StartHost** separation NTLinux wants for replicating
Windows' own shell model (`explorer.exe` internally separates desktop,
taskbar, and Start UI as distinct pieces coordinated through a broker,
not one monolithic process). LingmoOS already has that kind of
component split, and KWin is itself Wayland-native and themeable —
reusing that structure (Rule 1) avoids building an entire desktop-shell
architecture from zero, which is exactly the kind of large, generic,
already-solved-elsewhere problem the project's reuse-first principle
argues against tackling independently.

**Consequences:**
- `desktop/shell/` direction: adapt/theme LingmoOS's existing shell
  components toward NT/Windows-style look and feel (taskbar, Start menu,
  notification/status area, file manager chrome), rather than a
  from-scratch shell. Whether that means forking specific components,
  running them alongside new NT-themed UI, or reusing only the
  broker/D-Bus architecture with entirely new components on top is an
  open implementation question for when `desktop/` work actually starts —
  not decided by this ADR.
- Before deep integration work begins: verify LingmoOS's exact toolkit,
  D-Bus surface, and KWin integration against its actual source (this ADR
  is based on directory-structure inspection of the public repo, not a
  full source read) — treat specifics here as a strong lead, not a
  verified API contract, until that read happens.
- Section 9's "Potential future compositor: `ntcomp`" note in
  `docs/ARCHITECTURE.md` should be read alongside this: `ntcomp` may end
  up being an NTLinux KWin configuration/theme plus LingmoOS-derived shell
  components, rather than a from-scratch compositor or shell.
- Not scoped to any phase currently in progress. `desktop/` remains "Not
  started" per its directory README — this is a forward-looking
  architecture decision recorded for when that work begins, distinct from
  Phase 1's `.desktop` launcher-file generation (`tooling/installer/`),
  which is unrelated (freedesktop.org application launchers, not the
  shell itself).

---

## ADR-0005 — Driver-cell prototype: consume a ReactOS release ISO now; poll-based ivshmem-plain for ntbridge; verify against an honest stand-in guest until RosBE is reachable

**Date:** 2026-08-27

**Decision:** Three related choices made building the Phase 4 driver-cell
prototype:

1. `driver/cell/images/fetch-reactos-iso.sh` downloads the real upstream
   ReactOS release ISO (currently 0.4.15) rather than vendoring ReactOS
   source and building the stripped ROS-NTCELL profile
   (`docs/ARCHITECTURE.md` section 15) from scratch. Extends
   ADR-0002/Rule 17's "consume, don't vendor" further than originally
   scoped (that ADR covered `graphics/`/`proton/`; this applies the same
   reasoning to the ReactOS boot image itself for the prototype stage).
2. `driver/ntbridge/protocol/ntbridge_protocol.h`'s transport is QEMU
   `ivshmem-plain` — shared memory with **no** interrupt/doorbell — with
   both sides polling on a fixed tick, rather than `ivshmem-doorbell` +
   eventfd from the start.
3. Phase 4's ntbridge protocol/transport/host are verified against an
   honest stand-in guest client (`tests/reactos/ntbridge-guest-test.c`,
   plain Linux userspace speaking the identical wire protocol) rather
   than waiting until the real ReactOS-side driver
   (`driver/ntbridge/reactos/ntbridge_pnp.c`) can be built and tested.

**Rationale:**

1. Building ReactOS from source needs the RosBE cross-toolchain, which
   is a substantial, separate undertaking (comparable in kind to the
   full Wine source build Phase 2/12 already deferred for the same
   reason) and wasn't reachable in the sandbox that built this phase.
   A real, unmodified ReactOS kernel booting under `ntcell` is still a
   genuine, useful proof that the *launcher* works — it just isn't yet
   the eventual driver-only image.
2. `ivshmem-doorbell` needs an `ivshmem-server` process brokering eventfd
   descriptors to every attached VM — real infrastructure with its own
   failure modes, worth adding once polling latency actually matters for
   a real driver's timing requirements, not before there's a concrete
   consumer to measure it against. Polling is simpler, and the exact
   same tradeoff `ntd`'s 50ms tick already makes for `pidfd` exit
   detection (Phase 3) — consistent with how this project already
   prefers the simplest mechanism that's still genuinely correct.
3. Rule 11 requires tests for every NT compatibility implementation;
   refusing to test *anything* until RosBE exists would leave the
   entire ntbridge transport — the part actually reusable regardless of
   which OS eventually sits in the cell — unverified for an
   indeterminate stretch of future work. The stand-in shares the exact
   protocol header with the real driver, so it's a genuine
   protocol-conformance test, not a mock of one; ROADMAP.md and every
   README touching this are explicit that it does not validate
   `ntbridge_pnp.c` itself.

**Consequences:**
- `driver/cell/images/*.iso` is gitignored; fetched, not vendored.
- `reactos/upstream/` stays empty until a real ReactOS source build is
  actually attempted — no submodule-vs-snapshot decision made
  prematurely (see that directory's README).
- `driver/ntbridge/protocol/ntbridge_protocol.h` documents the
  doorbell upgrade path directly in its header comment, so it isn't
  forgotten once a real driver's latency needs force the question.
- `driver/ntbridge/reactos/ntbridge_pnp.c` exists as real, reviewable
  driver source with its known gaps (two flagged bugs, needs RosBE) but
  is explicitly marked unbuilt everywhere it's referenced — ROADMAP.md,
  its own README, `driver/ntbridge/README.md`, and
  `tests/reactos/README.md` all state the same boundary consistently,
  so no document overstates what Phase 4 actually proved.

**Correction (Phase 5):** the "needs RosBE" claim above about
`ntbridge_pnp.c` was wrong, caught while starting Phase 5's driver work.
RosBE is only needed to build ReactOS *itself* from source; a driver
that merely targets ReactOS's (Windows-compatible) kernel ABI needs only
a DDK header set and `ntoskrnl.exe`/`hal.dll` import stubs — which
mingw-w64 already ships. `ntbridge_pnp.c` now builds clean with that
toolchain (see its README's own correction note); RosBE's real,
narrower scope — building ReactOS's own kernel from source for the
stripped ROS-NTCELL profile — is tracked as Phase 13. Left this original
entry in place rather than editing it away, so the decision trail shows
what was believed at the time and what was later found, not a silently
rewritten record.

---

## ADR-0006 — Musa.Veil is a reference source for NT struct/prototype declarations, not a vendored dependency; hand-declare the small subset actually needed

**Date:** 2026-08-27

**Decision:** [MiroKaku/Musa.Veil](https://github.com/MiroKaku/Musa.Veil)
(MIT-licensed, header-only NT API declarations — same lineage as System
Informer's PHNT and Chuyu-Team's MINT) is used as a **reference source**
for getting undocumented NT struct/enum layouts right, not compiled or
linked into this repo. `tooling/compat-db/ntprobe/ntprobe.c` hand-declares
the ~4 structures it needs (`SYSTEM_BASIC_INFORMATION`,
`PROCESS_BASIC_INFORMATION`, `OBJECT_BASIC_INFORMATION`, three
information-class values), each cross-referenced against Musa.Veil's own
declaration at the point of use, and resolves the `Nt*` function
prototypes it calls against mingw-w64's own `libntdll.a` import stub
(confirmed present via `nm`) rather than against anything Musa.Veil
provides.

**Rationale:** Musa.Veil is not, and cannot be, "a wrapper and service to
NTLinux to support calls that Wine can't" (the framing this ADR responds
to) — it ships zero runtime code, so there's no service to wrap, and no
call it can make succeed that wouldn't already succeed against a real
`ntdll.dll`. Its actual, legitimate value is as ground truth for
undocumented struct layouts. But actually attempting to `#include` its
headers under this project's MinGW cross-compilation toolchain (the one
`ntexports.exe`/`ntprobe.exe` both use) failed hard and instructively:
101 errors from the library's own intended entry point (`Veil.h`), 763
from trying to isolate a single narrow sub-header instead — MSVC-only SAL
macros MinGW doesn't define, MSVC-extension `enum` forward declarations
GCC rejects, real redefinition conflicts against MinGW's own
`<windows.h>`, and — most tellingly — several `_Static_assert(sizeof(...)
== N)` checks on deeply nested/bitfield-heavy structures that fail even
with `-mms-bitfields`, a genuine GCC-vs-MSVC struct-layout ABI mismatch
for those specific types. Patching that to make the full library MinGW-
buildable would mean maintaining a parallel fork of most of a 30-file
library indefinitely, for a probe that needs four structures total — a
cost wildly disproportionate to the need (Rule 2), and not the kind of
small, upstreamable patch Rule 3 favors taking on (the incompatibility is
architectural to the library's MSVC-only design, not a bug).
Hand-transcribing the specific structures needed is low-risk precisely
because they're long-stable NT ABI (unchanged since the Windows
2000/XP-era `ntdll`) — not the newer/larger structures where the
static-assert failures actually occurred — and keeps the ownership of
"what's correct" traceable to Musa.Veil's declaration at each use site
(Rule 15), without inheriting a large, non-portable dependency for four
structs.

**Consequences:**
- No Musa.Veil source is vendored or fetched at build time anywhere in
  this repo; nothing here depends on it compiling.
- `tooling/compat-db/ntprobe/` exists as a new tool (companion to
  `ntexports/`) doing real differential behavioral testing — actual
  `Nt*`/`Zw*` calls against a real `ntdll`, not just export-table
  presence — verified live under Wine in this session: 4/4 probes
  passed, including the exact auto-reset Event state machine `ntd/ntd.c`
  reimplements, giving Phase 12's eventual `ntdll` integration work a
  real behavioral reference to validate against.
- If a future need genuinely requires a much larger slice of undocumented
  NT struct coverage than hand-transcription stays proportionate for,
  revisit MinGW-compatibility patching against Musa.Veil (or building the
  relevant tool with MSVC instead of cross-compiling, if a Windows build
  environment becomes available) rather than continuing to hand-transcribe
  indefinitely — this ADR's conclusion is scoped to today's four-struct
  need, not a permanent verdict on the library.
