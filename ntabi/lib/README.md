# libntabi

Client library linked into ntdll for talking to ntd over shared-memory request queues.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 2 + Phase 3)

`libntabi.c`/`libntabi.a` — the client-side implementation: allocates a
slot in `ntd`'s shared-memory segment, submits a request, and genuinely
blocks (`sem_wait`, no polling) until `ntd` posts the response. Sections
are the one operation with no daemon round-trip at all after creation/open
— `ntabi_map_view_of_section` just `shm_open`s the name `ntd` handed back
and `mmap`s it directly. Built and tested with real cross-process object
waits, wait-any/wait-all, shared memory, completion ports, and
process-exit detection — see `ROADMAP.md` Phase 3. Not yet linked into
ntdll itself (see that README's note and the Phase 2 "known gap" in
`ROADMAP.md`, which Phase 3 doesn't change).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
