# ReactOS Upstream Tracking

Tracks/vendors the upstream ReactOS source tree (submodule or vendored snapshot; decide mechanism when Phase 4 starts).

**Owner:** ReactOS upstream
**Status:** Decision made for Phase 4: don't vendor source at all yet

Phase 4 needed a real, bootable ReactOS kernel to prove
`driver/cell/launcher/ntcell` actually works, but did **not** need to
build ReactOS from source — no code in this repo touches ReactOS's own
source tree yet (`driver/ntbridge/reactos/ntbridge_pnp.c` is new
NTLinux-side driver source that would eventually live *inside* a
ReactOS source tree, not a patch against existing ReactOS files). So
rather than deciding submodule-vs-vendored-snapshot prematurely, Phase 4
consumed the upstream **release ISO** instead (ADR-0002/Rule 17):
`driver/cell/images/fetch-reactos-iso.sh` downloads a real
ReactOS 0.4.15 build from SourceForge, gitignored, never vendored.

This directory stays empty until a real reason to touch ReactOS's own
source arrives — building the stripped ROS-NTCELL profile
(ARCHITECTURE.md section 15) or actually compiling
`driver/ntbridge/reactos/ntbridge_pnp.c` into a real `ntbridge.sys`,
both of which need the RosBE cross-toolchain this sandbox doesn't have.
When that happens, decide submodule vs. vendored snapshot then, with a
real build to validate the choice against instead of guessing now.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
