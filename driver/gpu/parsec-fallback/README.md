# Parsec VDD Fallback Display Path

A real, live-verified fallback display device: usermode control of the
already-installed "Parsec Virtual Display Adapter" (ParsecVDA), a
third-party IddCx virtual-display driver that ships pre-installed on
hosts that use Parsec for remote access.

**Owner:** Parsec (nomi-san/parsec-vdd upstream API) + NTLinux (this
usermode client)
**Status:** Real, live-verified on this project's own Windows-host
sandbox — not part of Phase 11's driver-cell/VFIO research

## Scope — read this before anything else in this directory

This is **not** Phase 11 (Native Windows GPU driver hosting). It
doesn't touch ReactOS, isn't VFIO/IOMMU-mediated, isn't a WDDM/DXGKRNL
kernel-mode miniport, and resolves none of Phase 11's three blockers
(see `driver/gpu/README.md`). It's a separate, smaller, genuinely
finishable capability that surfaced out of the same research pass: this
project's own Windows-host sandbox turned out to already have
`ParsecVDA` installed and healthy (`ROADMAP.md` Phase 11, "A real GPU
became available this session" — the same adapter, first noticed there
as evidence the host is itself remotely operated). On any host that
already carries it, `ntlinux-parsec-fallback.exe` can add/remove a real,
usable virtual display, entirely from usermode.

## What it is

`parsec-vdd.h` (github.com/nomi-san/parsec-vdd, BSD-3-clause,
copyright Nguyen Duy 2023) is the real, public control API for
ParsecVDA: `QueryDeviceStatus`, `OpenDeviceHandle`, `VddAddDisplay`,
`VddUpdate` (keep-alive ping), `VddRemoveDisplay`. `fetch-parsec-vdd-
header.sh` fetches it fresh (pinned to commit
`a827c7137659b618d0a65f261ad8b2da1c74f772`) into gitignored `build/` —
never vendored (ADR-0002/Rule 17, same shape as `driver/gpu/
fetch-wdk-headers.sh`), though the license situation here is simpler:
the BSD-3-clause notice is embedded in the header's own top comment and
travels with the file whenever it's fetched.

`ntlinux-parsec-fallback.c` is a real usermode Win32 console client
(not kernel-mode — this whole capability is a usermode IOCTL client)
that: queries device status, opens the adapter, prints its version,
adds a display, holds it alive via a background `VddUpdate` thread
(<100ms interval, per the header's own contract), then removes it —
reading `GetSystemMetrics(SM_CMONITORS)` before/during/after so the
program's own output independently proves the display actually
appeared and disappeared, not just that the IOCTLs returned success.

Build with `build-parsec-fallback.bat` (real MSVC, this Windows host
only — same boundary `driver/gpu/build-msvc-probe.bat` draws for the
WDDM probes). Plain usermode Win32 — no `/kernel`, no WDK include
paths needed at all.

## Live-verified, real run on this host

Baseline, before any code ran (`Get-CimInstance Win32_PnPEntity` /
`Win32_DesktopMonitor`):

```
Name     : Parsec Virtual Display Adapter
DeviceID : ROOT\DISPLAY\0000
Status   : OK

Name                ScreenWidth ScreenHeight Status
----                ----------- ------------ ------
Default Monitor                              OK
Generic PnP Monitor                          OK
```

`build-parsec-fallback.bat` → `CL_ERRORLEVEL=0`, a real
`ntlinux-parsec-fallback.exe` produced. Running it
(`ntlinux-parsec-fallback.exe --hold-seconds 4`) against the real,
installed driver on this host, full real output:

```
ntlinux-parsec-fallback: querying Parsec Virtual Display Adapter (Root\Parsec\VDA)...
ntlinux-parsec-fallback: device status = DEVICE_OK
ntlinux-parsec-fallback: opened device handle
ntlinux-parsec-fallback: driver minor version = 45
ntlinux-parsec-fallback: monitor count BEFORE add = 1
ntlinux-parsec-fallback: added display, index = 0
ntlinux-parsec-fallback: holding for 4 second(s) (ping thread running, <100ms interval)...
ntlinux-parsec-fallback: monitor count DURING (display added) = 2
ntlinux-parsec-fallback: removed display index 0
ntlinux-parsec-fallback: monitor count AFTER remove = 1
ntlinux-parsec-fallback: done. before=1 during=2 after=1
```

A real, live transition: `SM_CMONITORS` genuinely went 1 → 2 → 1,
driven entirely by this program's IOCTLs against the real driver — not
asserted, read back from Windows' own monitor count both before and
after. Driver minor version 45 matches `parsec-vdd.h`'s own comment
about `VDD_IOCTL_UNKONWN` being "new in driver v0.45" — a second,
independent confirmation this is the real, current driver, not a stub.

## What this doesn't prove

Doesn't make ParsecVDA available on hosts that don't already have it —
it's third-party software this project doesn't install or vendor.
Doesn't touch Phase 11's own success criterion at all. Not tested under
sustained/production use (e.g. actually rendering a Linux desktop's
output onto the added display) — this pass proves the control path
works live, not a full display-pipeline integration.

See [`driver/gpu/README.md`](../README.md) for Phase 11's own scope,
[`ROADMAP.md`](/ROADMAP.md) for phase sequencing, and
[`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural
context. Before implementing anything here, check whether the
capability already exists upstream per Rule 1 in `CLAUDE.md`.
