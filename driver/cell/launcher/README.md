# Cell Launcher (ntcell)

KVM launcher for the ReactOS Driver Cell.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 4)

`ntcell` is a QEMU launcher script — reusing QEMU/KVM rather than
writing a hypervisor (Rule 1). Named for its preferred accelerator even
though the development sandbox that built this has no `/dev/kvm` (no
nested virtualization available there — see `accel()`'s fallback to
`-accel tcg`, the same "degrade gracefully, document why" pattern
already used for `/dev/ntsync` in Phase 0). On real NTLinux hardware
with KVM, `accel()` picks `kvm` automatically, no script changes needed.

Two subcommands, matching what this sandbox could actually verify
end-to-end:

- `ntcell boot-reactos [--iso PATH] [--screenshot PATH] [--timeout SECS] [--with-ntbridge]`
  — boots the real ReactOS release ISO
  (`driver/cell/images/fetch-reactos-iso.sh`). Optionally captures a
  QMP `screendump` (`qmp_screendump.py`) so a real rendered ReactOS
  Setup screen can be checked into evidence, not just "the process
  didn't exit" — the same standard this project held itself to for the
  Phase 1 notepad.exe window.
- `ntcell boot-testguest [--duration SECS] [--timeout SECS]` — boots the
  `tests/reactos/ntbridge-guest-test` stand-in with an `ivshmem-plain`
  device attached, backed by `$NTCELL_SHM_PATH`. Pair with
  `driver/ntbridge/host/ntbridge-host` running against the same shm
  path to exercise the full ntbridge transport (`tests/reactos/
  run-test.sh` does exactly this).

**Verified for real:** both subcommands run in this sandbox. `boot-reactos`
produced a real screenshot of ReactOS 0.4.15's Setup "Language Selection"
screen — genuine ReactOS kernel output under TCG software emulation, not
a mockup. `boot-testguest` is exercised by `tests/reactos/run-test.sh`
end-to-end (see `ROADMAP.md` Phase 4 for the full log).

**Known gap:** only ever tested under `-accel tcg` here (no `/dev/kvm`
in this sandbox) — the `kvm` branch of `accel()` is straightforward
(QEMU's `-accel kvm` flag, same as everywhere else) but genuinely
unexercised. Does not yet read `driver/cell/config/*.conf` — see that
directory's README.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
