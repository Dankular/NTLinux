# ReactOS Patches

NTLinux-specific patches against upstream ReactOS, kept minimal and upstreamed where the behavior is generic NT functionality (ARCHITECTURE.md section 28, Rule 3).

**Owner:** NTLinux
**Status:** Not started

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Consume, don't vendor:** prefer the upstream/distro package over vendoring source here; only vendor to carry a patch with an upstreaming plan. See ADR-0002 in `docs/DECISIONS.md`.
