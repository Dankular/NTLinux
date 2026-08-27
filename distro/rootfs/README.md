# Rootfs Overlays

Filesystem overlays and config files baked into the image on top of upstream packages (branding, defaults, NTLinux-specific units).

**Owner:** NTLinux
**Status:** Not wired into the build yet

`mkarchiso` only reads one overlay directory (`distro/image/airootfs/`), so
until there's a second image target worth sharing files with, NTLinux
rootfs additions (e.g. `usr/lib/binfmt.d/ntlinux-pe.conf`, the
`root/customize_airootfs.sh` build hook) live directly under
`distro/image/airootfs/` instead of here — see that directory and
`distro/image/README.md`. This directory stays reserved for when a second
build target (e.g. a non-archiso packaging) makes a shared overlay worth
factoring out.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
