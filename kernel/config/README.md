# Kernel Config

Kernel config fragments required by NTLinux: CONFIG_NTSYNC (NT-oriented synchronization, mainline since Linux 6.14), KVM, VFIO, IOMMU/IOMMUFD.

**Owner:** NTLinux
**Status:** Phase 0 in progress

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
