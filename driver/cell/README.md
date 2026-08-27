# Driver Cell

ReactOS Driver Cell: launcher, boot images, and configuration for the minimal KVM-hosted ReactOS kernel that hosts Windows drivers (ARCHITECTURE.md sections 14-15).

**Owner:** NTLinux
**Status:** Launcher + stock boot image working and verified (Phase 4); stripped ROS-NTCELL build profile not started

`launcher/ntcell` boots a real ReactOS kernel under QEMU (KVM when
available, TCG fallback otherwise) — see `launcher/README.md` and
`images/README.md` for what's verified versus what's still the stock
general-purpose ReactOS ISO rather than the stripped driver-only
ROS-NTCELL profile section 15 describes. `config/cell0.conf` sketches
the per-cell profile shape Mode A/B/C (section 27) will eventually
select between, though the launcher doesn't consume it yet.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
