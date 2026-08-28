# libntabi

Client library linked into ntdll for talking to ntd over shared-memory request queues.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 2 + Phase 3 + Phase 12's Thread/APC half)

`libntabi.c`/`libntabi.a` — the client-side implementation: allocates a
slot in `ntd`'s shared-memory segment, submits a request, and genuinely
blocks (`sem_wait`, no polling) until `ntd` posts the response. Sections
are the one operation with no daemon round-trip at all after creation/open
— `ntabi_map_view_of_section` just `shm_open`s the name `ntd` handed back
and `mmap`s it directly. Built and tested with real cross-process object
waits, wait-any/wait-all, shared memory, completion ports, process-exit
detection, per-thread exit detection, and APC delivery timing — see
`ROADMAP.md` Phase 3/Phase 12. `submit()` now stamps each request's
`client_tid` (via a raw `SYS_gettid` syscall, same host-agnostic reasoning
as `ntd.c`'s own `pidfd_open`) alongside the existing `client_pid`, so
`ntd` can tell which specific thread of a multi-threaded client issued an
alertable wait. Not yet linked into ntdll itself (see that README's note
and the Phase 2 "known gap" in `ROADMAP.md`, which Phase 3/Phase 12 don't
change — real Wine `ntdll` patching remains separate, larger, out-of-reach
follow-up work needing a full Wine source build).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
