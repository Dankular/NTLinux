# Desktop Integration

Windows windows become normal Wayland surfaces sharing one desktop with Linux apps (ARCHITECTURE.md section 9).

**Owner:** Wayland compositor (upstream) + LingmoOS (shell reference/reuse target, see `desktop/shell/`) + NTLinux (integration)
**Status:** Not started (Phase 1+ target)

> Shell look-and-feel (taskbar, Start menu, notifications) is planned to
> build on [LingmoOS](https://github.com/LingmoOS)'s existing Qt/QML/KWin
> shell rather than from scratch — see ADR-0004 in `docs/DECISIONS.md`.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
