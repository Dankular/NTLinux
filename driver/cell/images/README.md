# Cell Boot Images

Minimal bootable ReactOS Driver Cell images (ROS-NTCELL build profile): HAL, ntoskrnl, registry, PnP, I/O Manager, Object Manager, Memory Manager, driver loader, bridge transport only — no shell/Explorer/Winlogon.

**Owner:** ReactOS (base) + NTLinux (build profile)
**Status:** Not started (Phase 4 target)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
