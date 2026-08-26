# VFIO Integration

PCI BAR mapping, MSI/MSI-X delivery, device reset, resource descriptors for passing real hardware into a driver cell (ARCHITECTURE.md section 6/25/26).

**Owner:** Linux VFIO (upstream) + NTLinux (integration)
**Status:** Not started (Phase 6 target)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
