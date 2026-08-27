# ntd

NT userspace service daemon: object manager, registry, service control manager, namespace, security, RPC surface (ARCHITECTURE.md sections 6, 33, 34, 35).

**Owner:** NTLinux
**Status:** Object manager subset implemented and verified (Phase 2); registry/services/security/RPC untouched

`ntd.c` implements only the object-manager slice of what this directory is
scoped for: Event, Mutant, Semaphore objects, a flat handle table, a flat
name→object map, and real wait/signal semantics (auto-reset vs.
manual-reset events, semaphore counting, mutant ownership hand-off) served
over the `ntabi` shared-memory protocol, single-threaded and event-driven
(no polling). See `ntd/objects/README.md` for what's implemented and its
documented Gen2 simplifications, and `ROADMAP.md` Phase 2 for the full
verification account (23 passing tests, including real cross-process
blocking waits).

`ntd/registry/`, `ntd/services/`, `ntd/security/`, `ntd/rpc/` are
untouched — see their own READMEs. Nothing here is wired into a real
ntdll yet (see the Phase 2 "known gap" in `ROADMAP.md`).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
