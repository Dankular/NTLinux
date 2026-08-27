# Object Manager

NT object model: handle table, reference counting, object lifetime, named objects, waitability, cross-process handle duplication (ARCHITECTURE.md section 6).

**Owner:** NTLinux
**Status:** Implemented (as `ntd/ntd.c`, not split into this directory yet) — Phase 2

The actual implementation lives in `ntd/ntd.c` at the daemon's top level
rather than physically under this directory — Gen2 scope was small enough
(one object manager, no registry/services/security/RPC yet) that splitting
it out here would have been premature structure with no second component
to separate it from. Revisit once `ntd/registry/` or another sibling
actually has code, so "object manager" means something distinct from "the
whole daemon."

Implements: handle table (flat, not per-process — documented Gen2
simplification, see `ntd/ntd.c`'s file header), reference counting
(create/open increment, close decrements, object freed at zero), object
lifetime, named objects (flat name→object map, not the real namespace
hierarchy), and waitability (Event/Mutant/Semaphore, real blocking waits
with timeouts, no polling). Cross-process handle duplication
(`NtDuplicateObject`-equivalent) is not implemented — out of Phase 2
scope, since there's no real per-process handle table yet to duplicate
between.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
