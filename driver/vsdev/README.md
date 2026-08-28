# vsdev — NTLinux's first Windows driver

**Owner:** NTLinux
**Status:** Loads and performs real I/O, verified live (Phase 5); PnP-triggered load and `ntbridge` routing not yet attempted

A minimal virtual serial-style loopback device — `IRP_MJ_WRITE` stores a
buffer, `IRP_MJ_READ` returns and consumes it. Deliberately standalone:
no `ntbridge` shared-memory traffic, no child PDOs, none of
`driver/ntbridge/reactos/ntbridge_pnp.c`'s bus/PnP complexity — Phase 5's
own guidance is to start with a simple virtual device and avoid GPU
drivers initially, and Rule 2 says don't build more machinery than the
problem calls for. This proves loading + real I/O works at all on a real
ReactOS kernel, which nothing in this repository had attempted before
this phase.

## What's real here

- `vsdev.c`/`.h` — a real WDM driver: `DriverEntry`, `AddDevice`
  (PnP path), full `IRP_MJ_PNP` dispatch, `IRP_MJ_CREATE`/`CLOSE`/
  `READ`/`WRITE` with genuine loopback semantics (not a stub).
- `vsdev.inf` — installs it against the `Root` pseudo-bus for a real
  PnP-enumerated install (same pattern Microsoft's own WDK "Toaster"
  sample uses for a virtual/software-only device).
- `vsdev_test.c` — a small userspace client (`CreateFile`/`WriteFile`/
  `ReadFile` against `\\.\NTLVSER0`), used instead of `cmd.exe`'s
  `>`/`type` redirection (see "Real bugs found" below for why).
- `run-test.sh` — automated, unattended verification: boots a real
  ReactOS x64 kernel under QEMU, drives its console via synthetic
  keyboard input (`driver/cell/launcher/qmp_console.py`), reads a real
  pass/fail verdict back from the guest via a file dropped on the
  floppy image (`A:\RESULT.TXT`) — no guest networking, no OCR needed.

Same toolchain correction as `driver/ntbridge/reactos/`: builds via
mingw-w64's bundled DDK headers and `ntoskrnl`/`hal` import libraries,
not RosBE — see that directory's README for the full story.

## Real bugs found only by actually loading it, not by inspection

1. **`sc start` uses NT's legacy driver-load path, not PnP.** A plain
   `sc create ... type= kernel` + `sc start` (no INF/Root-enumeration
   install) calls `DriverEntry` and nothing else — `AddDevice` only
   fires from real PnP enumeration. `vsdev.c` originally created its
   device object solely inside `AddDevice`, so it loaded successfully
   (`STATE: RUNNING`) but was completely invisible and unusable —
   confirmed live: `sc start` reported success, but
   `\\.\NTLVSER0` didn't exist ("the system cannot find the file
   specified"). Fixed with the standard legacy-driver pattern:
   `DriverEntry` now also creates the device directly
   (`VsdevCreateDeviceObject`, shared with `AddDevice`), and
   `VsdevUnload` now cleans up a legacy-created device itself, since it
   never receives `IRP_MN_REMOVE_DEVICE`.

2. **Architecture mismatch: ReactOS 0.4.15 stable is x86-only.**
   Checked directly (not assumed): neither SourceForge's file listing
   nor reactos.org's download page lists an x64 variant of the 0.4.15
   stable release. The 64-bit mingw-built driver failed to load against
   it — `[SC] StartService FAILED 193: %1 is not a valid Win32
   application` — a genuine architecture mismatch (PE32+ driver against
   a 32-bit kernel), confirmed by inspecting the compiled `.sys` with
   `objdump -p` (`Subsystem: NT native`, `PE32+`, correct in isolation —
   just the wrong bitness for that ISO). Fixed by using a **ReactOS x64
   nightly build** instead (`driver/cell/images/
   fetch-reactos-x64-nightly.sh`, from `iso.reactos.org` — x64 support
   only exists as nightly CI builds, not a numbered release), matching
   the driver's actual architecture.

3. **`cmd.exe`'s `>`/`type` redirection didn't work against the raw
   device path** (`Can't redirect to file \.\NTLVSER0`,
   `type \.\NTLVSER0` → "system cannot find the file specified") even
   after the device object genuinely existed and was reachable — worked
   around, not root-caused (out of scope for this phase), by writing
   `vsdev_test.exe`, a tiny userspace client that opens the device
   directly via `CreateFileW`. That confirmed the device really was
   there and really did work: `CreateFile`/`WriteFile`/`ReadFile` all
   succeeded with a correct round-trip.

## Getting files into the guest: the LiveCD's RAM disk had 0 bytes free

`copy` into `%SystemRoot%\System32\drivers\` failed (`Access is
denied.`, `0 file(s) copied`) — traced to `dir X:\reactos\system32\
drivers` reporting `0 bytes free` on the LiveCD's RAM-disk system
volume, not a permissions problem. Worked around by pointing the
service's `binPath` directly at a floppy (`A:\VSDEV.SYS`) instead — NT
driver `ImagePath` doesn't have to point under `System32\drivers`, that's
just convention. Files were delivered via a `mtools`-built floppy image
hot-inserted through the QEMU monitor (`change floppy0 <path>`) — genuine
QEMU IDE+vvfat drive hot-plug attempts interfered with iPXE's boot-device
detection and hung the boot entirely (see the commit history / earlier
session notes); a floppy inserted after boot had no such issue.

## Verified live

Two full runs on a clean ReactOS x64 boot (the second, after the
legacy-load fix above):

```
X:\...\Desktop>sc create vsdev type= kernel binPath= A:\VSDEV.SYS
[SC] CreateService SUCCESS
X:\...\Desktop>sc start vsdev
SERVICE_NAME: vsdev
        STATE              : 4  RUNNING

X:\...\Desktop>A:\VSDTEST.EXE
CreateFile OK, handle=0000000000000390
WriteFile OK, wrote 20 bytes
ReadFile OK, read 20 bytes: "Hello NTLinux Phase5"
Loopback round-trip: PASS
```

Screenshots in `screenshots/` and embedded in `ROADMAP.md` Phase 5.
`run-test.sh` automates this exact sequence end-to-end and was run
successfully in this same session — see its own output for the current
pass/fail state. Reconfirmed in a later full-repo verification pass
(ROADMAP.md's Phase 9/10 sweep) with the exact same real output shown
above, byte for byte — including "Hello NTLinux Phase5" and a real
`CreateFile` handle value — genuine reproduction, not a cached/stale
result. `run-test.sh` itself needed real fixes during that same pass to
get back to reliable (see "Known gaps" below); the driver's own
behavior never changed.

## Known gaps, stated precisely

- **`run-test.sh`'s desktop icon launch is a real, reproducible, still-
  unresolved flake on current ReactOS nightlies — found live on two
  independent hosts, not fixed.** A later pass re-ran this script on
  two new, real hosts (a Windows machine with real QEMU, and a separate
  Linux cloud VPS with a real mingw-w64/mtools/QEMU install — see
  `ROADMAP.md` Phase 5's Windows/Linux re-verification for the full
  environment accounts) against a current ReactOS x64 nightly
  (`0.4.16-amd64-dev` build 2669, newer than the one `dismiss-dialog`
  was fixed against). `vsdev.sys`/`vsdev_test.exe` build and link
  cleanly with the real `x86_64-w64-mingw32-gcc` toolchain on both
  hosts (confirmed live), and the LiveCD language dialog now dismisses
  correctly (the `dismiss-dialog` fix above still holds). But the next
  step — clicking an empty desktop area, then `sendkey c` to jump
  keyboard focus to the "Command Prompt" icon by its first letter, then
  `ret` to launch it — reproducibly does **not** open a console window:
  a post-launch screendump (added during this pass, see `run-test.sh`)
  shows the desktop unchanged, icon not even highlighted/selected, on
  three separate attempts across both hosts. Bumping the timing (2s → 4s
  before the launch keystroke, 8s → 15s after) made no difference,
  ruling out simple slowness. Root cause not chased further this pass
  (budget) — plausibly a real change in this desktop-icon-first-letter-
  jump behavior in the ReactOS builds between when this was last
  verified and now, or a difference in how the desktop ListView takes
  focus on newer builds; not confirmed either way. **What this means
  concretely: the legacy `sc create`/`sc start` load path this file
  documents above as "verified live" was true when written and remains
  architecturally correct (the driver itself never changed), but is not
  currently re-reproducible by `run-test.sh` unattended against a
  current nightly** — a real regression in the test's own environment
  dependency, not in `vsdev.sys`. Real, separate follow-up: either find
  a more robust way to open a console (e.g. Win+R "Run" dialog, if that
  proves more reliable than desktop icon focus) or diagnose the icon-
  launch failure directly with ReactOS's own debug output.
- **PnP-triggered load never exercised.** `vsdev.inf` + `AddDevice`
  exist and are believed correct (same shape as the legacy path that
  *did* get verified, sharing `VsdevCreateDeviceObject`), but a real
  Root-enumerated install (`devcon install vsdev.inf Root\
  NTLINUX_VSDEV` or equivalent) hasn't been attempted — the ReactOS
  LiveCD environment used for verification doesn't ship `devcon.exe` or
  an obvious equivalent. This means `IRP_MN_START_DEVICE` firing from a
  real PnP manager — part of Phase 5's stated success criterion — is
  still unverified, same category of gap as `ntbridge_pnp.c`'s own
  unexercised PnP path. **Moved to `ROADMAP.md` Phase 14**, consolidated
  with that same `ntbridge_pnp.c` gap and `ntnet.c`'s (Phase 7) — one
  pass at real PnP-triggered installs, not three separate ones.
- **No `ntbridge` routing.** `vsdev`'s loopback buffer is entirely
  local to the driver. "Performs I/O through the host bridge" (Phase
  5's full success criterion) means routing this (or a similar) device's
  I/O through `driver/ntbridge/` back to Linux — a real next step, not
  attempted here on purpose (Rule 2: prove loading + I/O works in
  isolation first).
- ~~`run-test.sh`'s sleep-based timing is generous but not adaptive~~ —
  **fixed** during a later full-repo verification pass (ROADMAP.md's
  Phase 9/10 sweep), and worth recording exactly how, since it was a
  real, reproducible flake, not a hypothetical one: re-running this
  script during that sweep failed 3 times in a row with an empty
  `A:\RESULT.TXT`. Diagnosis (via manual, deliberately-paced QMP
  screendumps at each step, not guessing) found the LiveCD's
  language-selection dialog sometimes still open when the old fixed
  `sleep 90; ret` sequence assumed it long dismissed — real TCG
  boot-speed variance, host-load-dependent. Fixed in
  `driver/cell/launcher/qmp_console.py`'s new `dismiss-dialog` command:
  polls a screenshot pixel that's reliably part of the dialog body
  (a color the plain desktop background never shows) and sends `ret`
  on every poll where it's still detected, only proceeding once
  background reads back for several consecutive polls — adaptive, not
  a wider blind guess. Two more real bugs surfaced only by re-running
  the *fully automated* script (not caught by careful manual step-by-
  step testing, which paces itself slower): (1) polling could declare
  "dismissed" before the dialog had even rendered yet, since "never
  appeared" and "appeared then dismissed" both read as background at
  that pixel — fixed by requiring the dialog color to have actually
  been observed at least once first; (2) even then, 2 consecutive
  background reads wasn't enough confirmation (plausibly a repaint
  flicker during a transition to a second, build-specific dialog) —
  fixed by requiring 5. Also added a real USB tablet device
  (`-usb -device usb-tablet`) and a `click` command so a mouse click on
  empty desktop space can force keyboard focus back onto the desktop
  before the icon-search-by-letter step, regardless of what had focus
  after the dialog closed (a PS/2 mouse can't be driven this way — QMP's
  absolute positioning needs a real absolute-pointing device attached).
  Reproduced clean twice in a row after all three fixes landed.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
