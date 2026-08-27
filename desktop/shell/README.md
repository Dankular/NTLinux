# Shell Integration

Taskbar/dock integration, notifications, for Windows app windows (ntcomp compositor integration target).

**Owner:** LingmoOS (reference/reuse target) + NTLinux (theming/integration)
**Status:** Not started

> **Reference target:** [LingmoOS](https://github.com/LingmoOS) — a
> modular Qt/QML/KWin desktop shell whose component split (`kscreen`,
> `lingmo-framework`, `lingmo-shell`, `osd`) maps closely onto the
> ShellBroker → DesktopHost/TaskbarHost/StartHost separation this
> directory needs for a Windows-like shell. See ADR-0004 in
> `docs/DECISIONS.md` before designing anything here from scratch.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
