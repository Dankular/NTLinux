# Real ReactOS Kernel/HAL/Bootloader Build (Phase 13)

The real, live-verified finding behind ROADMAP.md Phase 13's status change: RosBE was never actually necessary to build ReactOS's own kernel.

**Owner:** ReactOS (upstream source) + NTLinux (`build-reactos-core.sh`, the GCC-14 compatibility fix)
**Status:** Real, live-verified — kernel/HAL/bootloader compile and link clean; a full bootable minimal image is not yet assembled

## What this corrects

Phase 13 originally said RosBE's cross-toolchain was "out of reach in
every sandbox this project has run in so far," treating it as the
first, load-bearing blocker before anything else in the phase could be
attempted. That premise was never actually tested — checked directly
this pass, per Rule 1 ("check whether the capability already exists
upstream/is already reachable before declaring it absent"), rather than
left standing. It doesn't hold: `ROS_ARCH=amd64 ./configure.sh`
against a real ReactOS checkout configures cleanly with the **stock
Debian `gcc-mingw-w64-x86-64` package** — the exact same toolchain
`driver/gpu/`, `driver/kmdf/`, `driver/net/reactos/`, and every other
driver in this repo already build against. No RosBE bootstrap, no
custom-built cross-GCC.

## The one real compile issue, and its real cause

Building the full desktop target hits real compile errors — but the
first and most instructive one (`dll/3rdparty/libtirpc/src/
auth_sspi.c`) is not a RosBE-shaped problem at all:

```
error: passing argument 11 of 'InitializeSecurityContextA' from incompatible pointer type
  ...uint32_t * {aka unsigned int *}
note: expected 'ULONG *' {aka 'long unsigned int *'} but argument is of type 'uint32_t *'
```

**GCC 14 made `-Wincompatible-pointer-types` (and a couple of related
diagnostics) hard errors by default** — a real, documented GCC 14
behavior change, not a ReactOS bug. `ULONG` and `uint32_t` are the same
width but different C types; older GCC versions only warned about
passing one where the other is expected, GCC 14 now refuses outright.
ReactOS's own source (including bundled third-party code like
`libtirpc`) predates this default. This is plausibly the *actual*
reason RosBE pins an older GCC version — not exotic ReactOS-specific
compiler patches, just avoiding a stricter upstream default that didn't
exist yet when RosBE's toolchain was built. Fixed narrowly, not by
silencing the diagnostics permanently, just downgrading them back to
warnings:

```
cmake -DCMAKE_C_FLAGS='-Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=int-conversion' .
```

## Scoped to what ROS-NTCELL actually needs

The full `ninja bootcd` target (the complete desktop-shell ISO) hit a
second, unrelated failure in `dll/shellext/shellbtrfs` (a Btrfs shell
extension, ambiguous `std::to_wstring` overload resolution under this
newer GCC 14/libstdc++ pairing) — a real bug, but in a component
ROS-NTCELL's own scope (`docs/ARCHITECTURE.md` section 15:
HAL/ntoskrnl/registry/PnP/IoMgr/ObMgr/MM/driver-loader/bridge-transport
only, no shell) never needed in the first place. Rather than debug
desktop-shell components this project doesn't want, `build-reactos-
core.sh` targets exactly `ntoskrnl`, `hal`, and `boot/freeldr/freeldr/
freeldr.sys` directly — the actual pieces this phase's success
criterion cares about.

## Real, live results

```
$ file ntoskrnl/ntoskrnl.exe hal/halx86/hal.dll
ntoskrnl.exe: PE32+ executable for MS Windows 5.01 (native), x86-64, 20 sections
hal.dll:      PE32+ executable for MS Windows 5.01 (native), x86-64, 17 sections

$ ls -la boot/freeldr/freeldr/freeldr.sys
-rw-r--r-- 1 root root 240640 ... freeldr.sys
```

`ntoskrnl.exe` (13,113,444 bytes), `hal.dll` (2,196,101 bytes),
`freeldr.sys` (240,640 bytes) — real, current-vintage PE binaries, all
linked clean on a real Linux build server (see `ROADMAP.md`'s
cross-host re-verification section for that environment). First time
any part of ReactOS itself has been built from source anywhere in this
project's history.

## What this does not yet prove

These three binaries are not yet assembled into an actual bootable
image — no boot CD/floppy layout, no stripped-down registry hive, no
confirmation of what minimal driver set `freeldr`/`ntoskrnl` need
loaded to reach a usable state. That assembly work is real, separate,
unattempted follow-up — but it's now a "put the pieces together"
problem, not a "can this even compile" problem. `driver/ntbridge/
reactos/ntbridge_pnp.c`'s two known bugs also haven't been revisited
against this real tree yet (see ROADMAP.md Phase 13's task list).

See [`ROADMAP.md`](/ROADMAP.md) Phase 13 for the full phase status and
[`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) section 15 for
ROS-NTCELL's own scope definition. Before extending this, check
whether ReactOS's own upstream CI/build scripts already solve the
remaining boot-image-assembly problem, per Rule 1 in `CLAUDE.md`.
