# Graphics Stack

Vendored D3D-on-Vulkan translation layers. Do not rewrite Direct3D (ARCHITECTURE.md section 10, Rule 9).

**Owner:** DXVK / vkd3d-proton upstream
**Status:** Phase 0 in progress (packaging)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Consume, don't vendor:** prefer the upstream/distro package over vendoring source here; only vendor to carry a patch with an upstreaming plan. See ADR-0002 in `docs/DECISIONS.md`.
