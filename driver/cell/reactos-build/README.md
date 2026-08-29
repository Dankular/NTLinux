# Real ReactOS Kernel/HAL/Bootloader Build (Phase 13)

The real, live-verified finding behind ROADMAP.md Phase 13's status change: RosBE was never actually necessary to build ReactOS's own kernel.

**Owner:** ReactOS (upstream source) + NTLinux (`build-reactos-core.sh`, the GCC-14 compatibility fix; `assemble-rosntcell-boot.sh`, the boot-image assembly and QEMU boot test)
**Status:** Real, live-verified — kernel/HAL/bootloader compile and link clean; a real ROS-NTCELL image now boots under QEMU through HAL/ACPI/memory-manager/PnP init and real driver loading, captured live via a real serial console trace, stopping at a real, well-understood NT bug check (0x6B, no user-mode present) rather than reaching a stable running state — see "A real, bootable ROS-NTCELL image" below

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

## What that pass did not yet prove

These three binaries were not yet assembled into an actual bootable
image — no boot CD/floppy layout, no stripped-down registry hive, no
confirmation of what minimal driver set `freeldr`/`ntoskrnl` need
loaded to reach a usable state. That assembly work is real, and is what
the rest of this document (and `assemble-rosntcell-boot.sh`) covers.

## A real, bootable ROS-NTCELL image

`assemble-rosntcell-boot.sh` takes `build-reactos-core.sh`'s three
binaries and turns them into a real, bootable El Torito CD image, then
(with `--test`) boots it live under QEMU and captures a real serial
console trace. Per Rule 1, it reuses ReactOS's own boot-image-assembly
machinery throughout instead of hand-rolling an ISO or registry format
— see the script's own header comment for the precise account (which
real ReactOS targets/tools it reuses for what, and exactly which real
QEMU boot named which real missing file, in order — 20-plus real
iterations, not guessed). In short:

- **Reused, not reinvented:** the real `isoboot.bin`/`isombr.bin` El
  Torito boot sectors and `native-mkisofs`/`native-isohybrid` host
  tools `boot/boot_images.cmake` itself uses for `bootcd`; `native-
  mkhive` fed ReactOS's own real `boot/bootdata/*.inf` files for the
  registry hives (`ninja livecd_hives` — the exact command
  `create_registry_hives()` runs); a `freeldr.ini` boot entry shaped
  like `boot/bootdata/bootcd.ini`'s own `LiveImg_Debug` entry.
- **Hand-assembled, because this is the genuinely new part:** the ISO's
  minimal file list — a hand-picked driver set instead of the full
  desktop-shell `livecd` list `bootcd` normally depends on. Found by
  real, live iteration: boot under QEMU, read the real error, add
  exactly the one real file it named, repeat. 41 real driver/DLL
  binaries beyond `ntoskrnl.exe`/`hal.dll` resulted — every one added
  because a real boot named it, not because it was assumed needed (the
  full list, in the order discovered, is in the script's header
  comment).

**Real, live result** — a real serial-console trace captured via
`assemble-rosntcell-boot.sh reactos-src build-dir --test`:

```
(/ntoskrnl/kd64/kdinit.c:95) ReactOS 0.4.17-amd64-dev (Build 20260828-62de6b3) (Commit 62de6b33a2bbdb525055d18a9d8269d79341a78a)
(/ntoskrnl/kd64/kdinit.c:96) 1 System Processor [512 MB Memory]
(/hal/halx86/acpi/halacpi.c:928) ACPI v1.0-1.0b detected. Tables: [RSDT] [APIC] [FACP]
(/hal/halx86/apic/halinit.c:48) Using HAL: APIC UP DBG
(/ntoskrnl/mm/mminit.c:135)           0xFFFFF80000000000 - 0xFFFFF80004800000	Boot Loaded Image
(/ntoskrnl/mm/mminit.c:146)           0xFFFFFA8000000000 - 0xFFFFFA8000901000	PFN Database
... (full real ARM3 memory-manager layout)
(/drivers/ksfilter/swenum/swenum.c:428) SWENUM loaded
BOOT DRIVERS LOADED
(/ntoskrnl/mm/ARM3/sysldr.c:170) Loading: \SystemRoot\system32\drivers\i8042prt.sys at FFFFF880761D8000 with 3b pages
... (real, live PnP-triggered driver loads: kbdclass, mouclass, floppy,
    fs_rec, null, beep, blue, vga, videoprt, msfs, npfs, afd, netio)

*** Fatal System Error: 0x0000006b
                       (0xFFFFFFFFC0000034,0x0000000000000002,0x0000000000000000,0x0000000000000000)

kdb:>
```

Real, verified-live HAL init, ACPI table parsing, memory-manager setup,
Object/I/O/PnP Manager coming up, and both boot-start and live
PnP-triggered driver loading — the actual `docs/ARCHITECTURE.md`
section 15 list genuinely exercised on a booted kernel, not merely
compiled, for the first time in this project's history.

## Why it stops there — a real NT bug check, not one more missing driver

Bug check `0x6B` is `PHASE1_INITIALIZATION_FAILED`, and
`STATUS_OBJECT_NAME_NOT_FOUND` (`0xc0000034`) as its first parameter is
the well-known real signature of "couldn't open `smss.exe`" — a
standard NT/ReactOS bug check, not a ROS-NTCELL-specific one.
ROS-NTCELL ships zero user-mode Windows executables by design; this
pass confirmed live that `ntoskrnl`'s Phase 1 init genuinely has no
path around needing at least a real `smss.exe` (plus `ntdll.dll` and
enough of the Win32 subsystem for it not to immediately fail) before it
will declare boot complete. That is real, separate follow-up work — see
`ROADMAP.md` Phase 13's task list — not a boot-image-assembly gap.

A second, real, separate finding: FreeLoader's `UiMessageBox()` blocks
for a keypress on *every* missing boot/PnP driver (even though the
underlying failure is individually non-fatal), and with no VGA device
attached its console falls back to COM1, where QEMU's `-serial
file:...` backend can never supply a keypress — so any driver still
missing hangs the whole boot indefinitely and silently rather than
failing loudly. See the script's header comment for the full account of
both findings, including how the early iterations were cross-checked
via a throwaway `-vga std` + QMP-screendump boot before the COM1-only
path was trusted.

`driver/ntbridge/reactos/ntbridge_pnp.c`'s two known bugs still haven't
been revisited against this real tree (see `ROADMAP.md` Phase 13's task
list — blocked on reaching a stable, non-bugchecked boot state first).

See [`ROADMAP.md`](/ROADMAP.md) Phase 13 for the full phase status and
[`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) section 15 for
ROS-NTCELL's own scope definition. Before extending this, check whether
ReactOS's own upstream CI/build scripts or issue tracker already cover
the remaining "boot with no `smss.exe`" problem, per Rule 1 in
`CLAUDE.md`.
