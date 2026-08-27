# NT ABI Headers

Public C headers for the NT/Linux ABI protocol, consumed by ntdll and by ntd.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 2 + Phase 3)

`ntabi.h` — the public libntabi client API: `ntabi_connect`,
`ntabi_create_event`/`_mutant`/`_semaphore`, `_wait_single`/`_wait_multiple`,
`_create_section`/`_open_section`/`_map_view_of_section`,
`_create_completion`/`_open_completion`/`_post_completion`/`_remove_completion`,
`_open_process`, etc. Not yet consumed by ntdll (that's real, separate
Wine-integration work — see `ROADMAP.md` Phase 2's "known gap"); consumed
today by `ntabi/tests/test_ntabi.c`, which exercises the full API for
real, including across real process boundaries (38 passing checks as of
Phase 3).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
