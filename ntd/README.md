# ntd

NT userspace service daemon: object manager, registry, service control manager, namespace, security, RPC surface (ARCHITECTURE.md sections 6, 33, 34, 35).

**Owner:** NTLinux
**Status:** Object manager subset implemented and verified (Phase 2 + Phase 3 + Phase 12's Thread/APC half); registry/services/security/RPC untouched

`ntd.c` implements the object-manager slice of what this directory is
scoped for: Event, Mutant, Semaphore, Section, I/O Completion port,
Process, and **Thread** objects; **real per-process handle tables** (Phase
3 — replaced Phase 2's flat handle space: two processes can hold
numerically identical handle values referencing completely different
objects, and closing one never disturbs the other); wait-any/wait-all
alongside single-object waits; and real wait/signal semantics throughout
(auto-reset vs. manual-reset events, semaphore counting, mutant ownership
hand-off, process-exit detection via `pidfd`, **thread-exit detection via
`/proc/<pid>/task/<tid>` polling — finer-grained than `pidfd`, which only
accepts thread-group-leader pids**) — served over the `ntabi`
shared-memory protocol, single-threaded and event-driven (no polling loop
of its own beyond the regular 50ms tick, no worker threads). See
`ntd/objects/README.md` for the full breakdown and its remaining
documented simplifications, and `ROADMAP.md` Phase 3/Phase 12 for the
verification account (60 passing tests as of Phase 12, including real
cross-process blocking waits, genuine shared memory, process-exit
detection for a process that isn't `ntd`'s own child, and real per-thread
exit-detection granularity — two sibling threads in the same process each
get their own handle, one signals independently of the other).

**Phase 12 adds real Thread objects and APC queueing** (protocol v3):
`OpenThread`, `QueueApc`, `SuspendThread`/`ResumeThread`, and an
`alertable` flag on `WAIT_SINGLE` — a pending APC on the calling
`(pid,tid)` short-circuits an alertable wait with `NTABI_STATUS_USER_APC`
before the wait ever touches the target object ("delivered at the next
alertable wait", matching `ROADMAP.md` Phase 12's task text precisely; a
non-alertable wait is never interrupted). Two things are stated precisely
as **not** implemented, not silently assumed: `SuspendThread`/
`ResumeThread` are real integer *accounting* only (correct
increment/clamp-at-0/decrement, tested) — they do not freeze the target
thread's actual CPU execution, which needs `ptrace` (a separate, larger,
permission-sensitive undertaking, not attempted here); and this models a
single flat APC queue per thread (Rule 2), not the real kernel-mode vs.
user-mode APC split. Real Wine `ntdll` integration (routing Wine's own
thread creation/sync through this daemon) remains separate, larger,
out-of-reach follow-up work — needs a full Wine source checkout and build
this sandbox doesn't have, same category of blocker as Phase 13's RosBE
requirement.

`ntd/registry/`, `ntd/services/`, `ntd/security/`, `ntd/rpc/` are
untouched — see their own READMEs. Nothing here is wired into a real
ntdll yet (see the Phase 2 "known gap" in `ROADMAP.md`, unchanged by
Phase 3/Phase 12).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
