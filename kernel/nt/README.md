# NT Kernel Facilities

Long-term target for NT semantics moved into Linux kernel space (wait primitives, section ops, completion ports) per ARCHITECTURE.md section 49. Only populated once profiling proves the benefit.

**Owner:** NTLinux
**Status:** Not started (Phase 3+ candidate)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
