# NT ABI Protocol

Versioned wire protocol definitions for the ntdll <-> ntd request queues (ARCHITECTURE.md section 5, Rule 12: cross-boundary protocols must be versioned).

**Owner:** NTLinux
**Status:** Implemented and verified — protocol v3 (Phase 12)

`ntabi_protocol.h` defines the wire format: a POSIX shared-memory segment
(`/ntlinux-ntabi-v3`) holding a submission ring plus a fixed slot table,
each slot carrying one request/response pair and its own completion
semaphore — real blocking waits, no polling (Rule 13). `NTABI_PROTOCOL_VERSION`
is embedded directly in the segment *name* (not just checked as a field
after connecting) and checked again at connect time — a mismatch can't
even successfully `mmap` the wrong-sized segment, let alone be silently
misread. Verified in-session, twice: by corrupting a live segment's
version field and confirming the client refuses to connect (Rule 12), and
for real when the v1→v2 protocol bump itself happened and the guard
continued working correctly.

**v1 (Phase 2):** Event, Mutant, Semaphore, single-object waits. One flat
handle space and one flat name→object map (documented simplifications).

**v2 (Phase 3):** adds wait-any/wait-all, Section (real shared memory,
mapped client-side), I/O Completion ports, Process objects (signaled on
exit, via `pidfd`) — and **replaces the flat handle space with real
per-process handle tables** (keyed by client pid). The flat name→object
map remains — still a documented simplification, see
`ntd/namespace/README.md`.

**v3 (Phase 12):** adds Thread objects (`OpenThread`, signaled when the
*specific* target thread exits — not the whole process — detected via
`/proc/<pid>/task/<tid>` polling, since `pidfd_open(2)` only accepts
thread-group-leader pids, not arbitrary TIDs) and APC support
(`QueueApc` + a new `alertable` flag on `WAIT_SINGLE` — a pending APC on
the calling thread short-circuits an alertable wait with
`NTABI_STATUS_USER_APC` before the wait is even attempted against the
target object, matching "delivered at the next alertable wait" per
ROADMAP.md Phase 12's task text). New `client_tid` field on the shared
slot (alongside the existing `client_pid`) identifies *which* thread of a
multi-threaded client issued a request — needed so a thread's own
alertable wait checks its own pending APC queue, not some other thread's.
`SuspendThread`/`ResumeThread` add real count *accounting* only (correct
increment/clamp-at-0/decrement, matching NT's return-value convention) —
not real CPU-execution freezing, which needs `ptrace` and isn't attempted
here.

A real bug was found and fixed by actually running the daemon, not by
inspection: `#define NTABI_PROTOCOL_VERSION 2u` (the `u` unsigned-literal
suffix) made the shm segment's *name* literally end in `v2u`, because C
preprocessor stringification (`#x`) stringifies the raw token, suffix
included. Caught from `ntd`'s own startup banner, not a test failure.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
