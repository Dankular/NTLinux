# ntd

NT userspace service daemon: object manager, registry, service control manager, namespace, security, RPC surface (ARCHITECTURE.md sections 6, 33, 34, 35).

**Owner:** NTLinux
**Status:** Object manager subset implemented and verified (Phase 2 + Phase 3); registry/services/security/RPC untouched

`ntd.c` implements the object-manager slice of what this directory is
scoped for: Event, Mutant, Semaphore, Section, I/O Completion port, and
Process objects; **real per-process handle tables** (Phase 3 — replaced
Phase 2's flat handle space: two processes can hold numerically identical
handle values referencing completely different objects, and closing one
never disturbs the other); wait-any/wait-all alongside single-object
waits; and real wait/signal semantics throughout (auto-reset vs.
manual-reset events, semaphore counting, mutant ownership hand-off,
process-exit detection via `pidfd`) — served over the `ntabi`
shared-memory protocol, single-threaded and event-driven (no polling, no
worker threads). See `ntd/objects/README.md` for the full breakdown and
its remaining documented simplifications, and `ROADMAP.md` Phase 3 for
the verification account (38 passing tests, including real cross-process
blocking waits, genuine shared memory, and process-exit detection for a
process that isn't `ntd`'s own child).

**Thread objects and APCs are explicitly not implemented** — see
`ROADMAP.md` Phase 3's "known gap" for exactly why (both need real
per-thread execution-context integration this prototype has no way to
provide without the still-open Phase 2 ntdll-integration gap).

`ntd/registry/`, `ntd/services/`, `ntd/security/`, `ntd/rpc/` are
untouched — see their own READMEs. Nothing here is wired into a real
ntdll yet (see the Phase 2 "known gap" in `ROADMAP.md`, unchanged by
Phase 3).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
