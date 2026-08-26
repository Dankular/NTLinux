# Device Broker (ntpnpd)

Bridges Linux physical device discovery (sysfs/udev/netlink) into ReactOS PnP Manager device nodes. Linux provides physical facts; ReactOS keeps the NT device-tree view (ARCHITECTURE.md sections 18-19).

**Owner:** NTLinux
**Status:** Not started

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
