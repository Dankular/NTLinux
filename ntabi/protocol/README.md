# NT ABI Protocol

Versioned wire protocol definitions for the ntdll <-> ntd request queues (ARCHITECTURE.md section 5, Rule 12: cross-boundary protocols must be versioned).

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 2 object-manager subset)

`ntabi_protocol.h` defines the wire format: a POSIX shared-memory segment
(`/ntlinux-ntabi-v1`) holding a submission ring plus a fixed slot table,
each slot carrying one request/response pair and its own completion
semaphore — real blocking waits, no polling (Rule 13). `NTABI_PROTOCOL_VERSION`
is checked at connect time by both sides; a mismatch is refused outright,
verified in-session by corrupting a live segment's version field and
confirming the client refuses to connect rather than misbehaving (Rule 12).

Scope is the object-manager subset only — Event, Mutant, Semaphore,
handles, waits — with two documented Gen2 simplifications (a flat handle
space and a flat name→object map, not per-process handle tables or the
real `\BaseNamedObjects` namespace hierarchy). See the header's own
comments and `ntd/README.md` for the rationale.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
