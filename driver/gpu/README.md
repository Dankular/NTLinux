# Native Windows GPU Driver Hosting

Hosting a real Windows WDDM GPU driver inside the ReactOS driver cell, VFIO/IOMMU-mediated (ARCHITECTURE.md section 30, ADR-0003, Phase 11).

**Owner:** ReactOS (WDDM/DXGKRNL kernel support) + Linux VFIO/IOMMU (upstream) + NTLinux (integration)
**Status:** Research complete for this pass — three independent blockers identified and checked directly, none fixable from any sandbox this project has run in; no code written (see below for why)

Not an MVP dependency (ARCHITECTURE.md section 30) — DXVK/vkd3d-proton
on Mesa/Vulkan stays the default gaming path. This directory exists for
the research-milestone work ADR-0003 tracks: could a real Windows WDDM
GPU driver, hosted in the same driver cell any other Windows kernel
driver uses, cover hardware/workloads Mesa can't (brand-new GPUs ahead
of open-source driver support, vendor features, professional/compute
workloads that only ship a Windows driver).

## Why no code lives here yet — three independent, directly-checked blockers

1. **ReactOS's own WDDM/DXGKRNL support is real but early and 2D-only.**
   Its official blog (`reactos.org/blogs/investigating-wddm/`, Oct 7
   2025) reports a working experimental Dxgkrnl — enough to run
   Microsoft's `BasicDisplay.sys` sample *and* a real NVIDIA Windows 7
   GPU driver, producing real 2D display output. 3D acceleration,
   scheduling, full memory management, the complete D3DKMT API, and the
   usermode driver component are explicitly not implemented yet — so
   nothing this phase actually cares about (Direct3D/compute workloads)
   runs on ReactOS today, even though a picture can appear.
2. **This project's own DDK toolchain has zero WDDM/DXGKRNL kernel-mode
   headers or import libraries.** `tooling/compat-db/ddkgap/` checks
   this directly and standingly (not a one-off) — no `dxgkrnl.h`,
   `d3dkmthk.h`, `d3dukmdt*.h`, `kmddod*.h`, no `dxgkrnl` import
   library, anywhere. The toolchain's abundant *usermode* Direct3D
   headers (`d3d9.h` through `d3d12.h`) don't help — those are for
   applications consuming Direct3D, not a kernel-mode driver
   implementing a WDDM miniport.
3. **No VFIO/IOMMU/real PCI passthrough in any sandbox this project has
   run in** — same finding as `driver/vfio/`/`driver/iommu/` (Phase 6),
   and the one blocker here with no toolchain or upstream-code fix.
   "A non-primary GPU passed through via VFIO" is definitionally
   physical hardware; there's no honest software-only substitute for it
   the way a TAP device stood in for Phase 7's network interface.

All three are independent — fixing one doesn't unblock the others.
None are fixable from inside a sandbox shaped like this project's.

## What would need to happen, in order, once real hardware is available

1. Confirm blocker 3 is actually lifted (real VFIO/IOMMU passthrough
   available, a real non-primary GPU to target).
2. Re-check ReactOS's WDDM progress fresh rather than assume this
   README's Oct-2025 snapshot is still current — `tooling/compat-db/
   ddkgap/` and a repeat of the blog-post check above are the concrete
   starting points.
3. Source real WDDM/DXGKRNL kernel-mode headers (from ReactOS's own
   tree, once its Dxgkrnl work has headers worth consuming, or a
   compatible WDK subset) into the DDK toolchain `driver/` already uses
   — the same mingw-w64 cross-compilation approach every driver here
   uses, not RosBE (Rule 1: reuse existing toolchain patterns).
4. Write a cell-side WDDM integration following this repo's established
   driver shape (`driver/vsdev/vsdev.c`, `driver/net/reactos/ntnet.c`,
   `driver/usb/reactos/ntusb.c` — WDM device object, real DDK headers,
   `ntbridge` for anything crossing the VM boundary) once there's a
   real interface to target.
5. Live-verify against a real Windows WDDM GPU driver, per this
   project's standing rule: written but unverified code is not "done."

See `ROADMAP.md` Phase 11 and `docs/DECISIONS.md` ADR-0003 for the full
sourced writeup this README summarizes.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
