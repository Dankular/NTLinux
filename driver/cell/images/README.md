# Cell Boot Images

Minimal bootable ReactOS Driver Cell images (ROS-NTCELL build profile): HAL, ntoskrnl, registry, PnP, I/O Manager, Object Manager, Memory Manager, driver loader, bridge transport only — no shell/Explorer/Winlogon.

**Owner:** ReactOS (base) + NTLinux (build profile)
**Status:** Stock upstream ISOs (x86 release + x64 nightly) fetched and boot-verified (Phase 4/5); the stripped ROS-NTCELL profile moved to Phase 13

`fetch-reactos-iso.sh` downloads the real ReactOS 0.4.15 **x86** release
ISO from upstream (SourceForge). `fetch-reactos-x64-nightly.sh`
downloads the latest ReactOS **x64** LiveCD nightly build from
`iso.reactos.org` — needed because ReactOS's stable releases are x86-only
(confirmed directly: neither SourceForge nor reactos.org lists an x64
variant of 0.4.15), and Phase 5 needed a real x64 kernel to load a real
x64-compiled driver against. Both consume upstream builds rather than
vendoring ReactOS source into this repo (ADR-0002/Rule 17). Both ISOs
are gitignored — this directory holds only the fetch scripts.

**Known gap, stated plainly:** both are *stock* general-purpose ReactOS
images (full desktop: Explorer, Winlogon, GDI shell), not the stripped
driver-only ROS-NTCELL profile ARCHITECTURE.md section 15 describes.
Producing that profile means building ReactOS from source with a custom
`bootcd.cmake`/build target, which needs the RosBE cross-toolchain — not
reachable in this sandbox. This is now **Phase 13**, moved there once
Phase 5 established that RosBE's scope is narrower than originally
thought (driver *source* like `ntbridge_pnp.c`/`vsdev.c` builds fine
without it — see `driver/ntbridge/reactos/README.md`). The stock ISOs
exist here so `driver/cell/launcher/ntcell` and `driver/vsdev/
run-test.sh` have real ReactOS kernels to boot today — trimming to
ROS-NTCELL is separate, tracked follow-up, not silently assumed done.

**Verified for real:** `ntcell boot-reactos --screenshot ...` boots the
x86 ISO under QEMU and captures a real QMP screendump of ReactOS
0.4.15's Setup "Language Selection" screen. The x64 nightly boots to a
real desktop and successfully loaded and ran a real driver
(`driver/vsdev/`) — see `ROADMAP.md` Phase 4 and Phase 5.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
