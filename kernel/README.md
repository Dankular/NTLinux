# Kernel

Linux kernel configuration and NTLinux-specific patches. Linux remains the real host kernel (see ARCHITECTURE.md section 1.2) — this directory adapts/configures it, it does not fork its core.

**Owner:** Linux (upstream) + NTLinux (config/integration)
**Status:** Phase 0 in progress

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
