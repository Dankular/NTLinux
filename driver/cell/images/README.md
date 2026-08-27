# Cell Boot Images

Minimal bootable ReactOS Driver Cell images (ROS-NTCELL build profile): HAL, ntoskrnl, registry, PnP, I/O Manager, Object Manager, Memory Manager, driver loader, bridge transport only — no shell/Explorer/Winlogon.

**Owner:** ReactOS (base) + NTLinux (build profile)
**Status:** Stock upstream ISO fetched and boot-verified (Phase 4); the stripped ROS-NTCELL profile itself not started

`fetch-reactos-iso.sh` downloads the real ReactOS 0.4.15 release ISO
from upstream (SourceForge) rather than vendoring ReactOS source into
this repo (ADR-0002/Rule 17: consume the upstream release). The ISO
itself is gitignored — this directory holds only the fetch script, not
the 150MB+ binary.

**Known gap, stated plainly:** this is the *stock* general-purpose
ReactOS release ISO (full desktop: Explorer, Winlogon, GDI shell), not
yet the stripped driver-only ROS-NTCELL profile ARCHITECTURE.md
section 15 describes. Producing that profile means building ReactOS
from source with a custom `bootcd.cmake`/build target, which needs the
RosBE cross-toolchain — not reachable in this sandbox (same category of
gap as `driver/ntbridge/reactos/`'s unbuilt driver). The stock ISO
exists here so `driver/cell/launcher/ntcell` has a real ReactOS kernel
to boot today, proving the launcher itself works — trimming to
ROS-NTCELL is separate, tracked follow-up, not silently assumed done.

**Verified for real:** `ntcell boot-reactos --screenshot ...` boots this
ISO under QEMU and captures a real QMP screendump of ReactOS 0.4.15's
Setup "Language Selection" screen — genuine ReactOS kernel output, not
a mockup. See `ROADMAP.md` Phase 4.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
