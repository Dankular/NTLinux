# NT ABI Headers

Public C headers for the NT/Linux ABI protocol, consumed by ntdll and by ntd.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 2)

`ntabi.h` — the public libntabi client API (`ntabi_connect`,
`ntabi_create_event`/`_mutant`/`_semaphore`, `_wait_single`, etc.). Not yet
consumed by ntdll (that's real, separate Wine-integration work — see
`ROADMAP.md` Phase 2's "known gap"); consumed today by
`ntabi/tests/test_ntabi.c`, which exercises the full API for real,
including across real process boundaries.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
