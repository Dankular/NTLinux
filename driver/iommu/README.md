# IOMMU Integration

IOMMUFD/IOMMU mapping so DMA from a Windows driver is protected host memory, never arbitrary Linux memory (ARCHITECTURE.md section 24, Rule 6).

**Owner:** Linux IOMMU (upstream) + NTLinux (integration)
**Status:** Not started (Phase 6 target)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
