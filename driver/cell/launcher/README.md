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

`qmp_console.py` is a second QMP helper alongside `qmp_screendump.py`,
grown out of Phase 5's driver verification work
(`driver/vsdev/run-test.sh`): where `qmp_screendump.py` only captures a
frame, `qmp_console.py` also sends synthetic keyboard input (single
qcodes or whole strings, with US-QWERTY shift handling) so a script can
actually drive ReactOS's text-mode dialogs and a `cmd.exe` session
end-to-end, not just observe them. Every primitive in it was exercised
live, repeatedly, verifying `vsdev.sys` — not written speculatively.
Gained a `click` command (needs a real absolute-pointing device
attached in the QEMU invocation — `-usb -device usb-tablet`, a PS/2
mouse can't be driven this way) and a `dismiss-dialog` command during a
later full-repo verification pass, after re-running `driver/vsdev/
run-test.sh` surfaced a real, reproducible flake in its old fixed-sleep
dialog-dismissal timing — see `driver/vsdev/README.md`'s "Known gaps"
for the full diagnosis (found by actually re-running the script
repeatedly with real screendumps, not by inspection) and the two real
bugs `dismiss-dialog`'s own implementation went through before it held
up under a fully unattended run.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
