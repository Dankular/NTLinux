# NT ABI Tests

Protocol and API conformance tests for libntabi/ntd (Rule 11: every NT compatibility implementation requires tests).

**Owner:** NTLinux
**Status:** Implemented and passing (Phase 2)

`test_ntabi.c` — 23 checks, all passing: auto-reset vs. manual-reset event
semantics, semaphore counting (including the max-count rejection case),
name collision/not-found/type-mismatch handling, the shared-memory
protocol-version guard, and — the two that actually matter — a real
cross-process event wait and a real cross-process mutant ownership
hand-off, both verified to *actually block* (measured elapsed time, not
just a correct return code) rather than busy-poll or race. Self-contained:
starts its own `ntd` instance, runs everything, tears it down. Run with
`make check` in this directory.

Not yet differential against real Windows/Wine behavior (Rule 11's fuller
ambition) — there's no ntdll integration yet to compare against (see
`ROADMAP.md` Phase 2's "known gap"). What's tested is genuine NT object
semantics against the real daemon, not mocks.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
