# Proton

Proton fork acting as a compatibility/profile layer on top of NTLinux, rather than the fundamental Windows runtime. Loses low-level NT translation responsibility as NTLinux matures (ARCHITECTURE.md section 38).

**Owner:** Valve/Proton upstream + NTLinux (profile layer)
**Status:** Phase 0 in progress (packaging)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
