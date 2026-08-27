# ntdll Boundary

NTLinux-specific alterations to ntdll's lower boundary, progressively routing NT calls through libntabi/ntd instead of wineserver (ARCHITECTURE.md section 3.2 generations 1-5).

**Owner:** NTLinux (boundary) + Wine (implementation)
**Status:** Not started — but `libntabi`/`ntd` (what this would route into) are implemented and verified; see `ntabi/README.md` and `ROADMAP.md` Phase 2

Routing real Wine `ntdll` calls (`dlls/ntdll/unix/sync.c` upstream, for
the sync-object calls `ntabi` currently covers) through `libntabi` means
patching Wine's own source and rebuilding it — that's the actual work
this directory is for, and it's explicitly not attempted yet (Phase 2's
"known gap"). What already exists and is tested: the thing this boundary
would call into.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
