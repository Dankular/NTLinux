# NT ABI Protocol

Versioned wire protocol definitions for the ntdll <-> ntd request queues (ARCHITECTURE.md section 5, Rule 12: cross-boundary protocols must be versioned).

**Owner:** NTLinux
**Status:** Implemented and verified — protocol v2 (Phase 3)

`ntabi_protocol.h` defines the wire format: a POSIX shared-memory segment
(`/ntlinux-ntabi-v2`) holding a submission ring plus a fixed slot table,
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
`ntd/namespace/README.md`. Thread objects and APCs are explicitly not
covered — see `ROADMAP.md` Phase 3's "known gap".

A real bug was found and fixed by actually running the daemon, not by
inspection: `#define NTABI_PROTOCOL_VERSION 2u` (the `u` unsigned-literal
suffix) made the shm segment's *name* literally end in `v2u`, because C
preprocessor stringification (`#x`) stringifies the raw token, suffix
included. Caught from `ntd`'s own startup banner, not a test failure.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
