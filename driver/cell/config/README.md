# Cell Config

Per-cell configuration profiles (Mode A/B/C granularity per ARCHITECTURE.md section 27).

**Owner:** NTLinux
**Status:** Example profile written (Phase 4); not yet consumed by the launcher

`cell0.conf` is an example "Mode A — one global NT driver cell" profile:
boot image, memory size, accelerator preference, and the ntbridge shm
path/size/protocol-version this cell would use. It gives Mode A's shape
a concrete starting point rather than inventing the config format only
once Mode B/C (per-driver-class or per-driver cell granularity) need
distinct profiles to select between.

**Known gap:** `driver/cell/launcher/ntcell` does not read this file
yet — it currently takes everything as command-line flags with fixed
defaults, which is sufficient for the single-cell Phase 4 prototype.
Wiring `ntcell` to actually parse files like this is real, separate
follow-up work.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
