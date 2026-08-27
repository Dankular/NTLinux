# Object Manager

NT object model: handle table, reference counting, object lifetime, named objects, waitability, cross-process handle duplication (ARCHITECTURE.md section 6).

**Owner:** NTLinux
**Status:** Implemented (as `ntd/ntd.c`, not split into this directory yet) — Phase 2 + Phase 3

The actual implementation lives in `ntd/ntd.c` at the daemon's top level
rather than physically under this directory — scope was small enough (one
object manager, no registry/services/security/RPC yet) that splitting it
out here would have been premature structure with no second component to
separate it from. Revisit once `ntd/registry/` or another sibling
actually has code, so "object manager" means something distinct from "the
whole daemon."

Implements: a **real per-process handle table** (Phase 3 — replaced
Phase 2's flat handle space: `(owner_pid, local_handle) -> object`, so two
processes can each validly hold "handle 1" pointing at two completely
different objects, and closing one's handle can never reach or disturb
another process's), reference counting (create/open increment, close
decrements, object freed at zero — now per-handle-entry, not per raw
handle number), object lifetime, named objects (flat name→object map,
still not the real namespace hierarchy — see `ntd/namespace/README.md`),
and waitability across six object types (Event, Mutant, Semaphore,
Section, I/O Completion port, Process), including wait-any/wait-all
across up to `NTABI_MAX_WAIT_HANDLES` (16) handles at once, all with real
blocking waits and timeouts, no polling for the client side (`ntd` itself
polls `pidfd`s and expiring deadlines on its own 50ms tick, not exposed to
clients as polling).

Cross-process handle duplication (`NtDuplicateObject`-equivalent) is
still not implemented — real per-process handle tables now exist to
duplicate *between*, unlike Phase 2, but nothing calls for it yet without
real process/thread creation routed through `ntd`.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
