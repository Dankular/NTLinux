# ReactOS

Vendored ReactOS source and NTLinux-specific patches. ReactOS owns NT kernel semantics, WDM, I/O Manager, Object Manager, PnP, device stacks, kernel synchronization, driver loader, NT memory-manager contract (ARCHITECTURE.md section 13, 51).

**Owner:** ReactOS upstream
**Status:** Not started (Phase 4 target)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
