# Wine Fork/Vendor

Vendored/forked Wine tree providing kernel32, kernelbase, user32, gdi32, advapi32, ole32, combase, shell32, ws2_32, winmm, etc. Do not reimplement these (ARCHITECTURE.md section 3.1, Rule 1).

**Owner:** Wine
**Status:** Phase 0 in progress (packaging)

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
