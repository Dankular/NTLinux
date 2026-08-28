# ntbridge ReactOS Side

ReactOS-side (cell) implementation of ntbridge.

**Owner:** NTLinux
**Status:** Builds clean and links into a real `.sys` (Phase 4); not yet loaded/tested inside a running ReactOS kernel

`ntbridge_pnp.c`/`.h` is a real WDM bus driver written against ReactOS's
actual DDK surface — `DriverEntry`/`AddDevice`, IRP_MJ_PNP dispatch
(`IRP_MN_START_DEVICE`, `IRP_MN_QUERY_DEVICE_RELATIONS`,
`IRP_MN_STOP_DEVICE`, `IRP_MN_REMOVE_DEVICE`), `MmMapIoSpace` over a
PnP-translated `CM_RESOURCE_LIST` to reach the ivshmem BAR2 shared
region, and `IoCreateDevice`/`IoInvalidateDeviceRelations` to expose
each `ntbridge_pnp_descriptor_t` arrival as a real child PDO
(ARCHITECTURE.md sections 18-20: "do not reinvent the IRP engine",
Rule 2/9). `ntbridge.inf` installs it against the ivshmem PCI hardware
ID (`PCI\VEN_1AF4&DEV_1110`) QEMU exposes for `-device ivshmem-plain`.
It `#include`s the exact same `ntbridge_protocol.h` the host side and
the stand-in guest test client use — one wire format, defined once.

## Correction to an earlier claim in this repo

This file was originally documented as "written but NOT built or run —
needs the RosBE cross-toolchain." That was wrong, and worth stating
plainly rather than quietly fixing: **RosBE is only needed to build
ReactOS itself from source.** A driver that merely targets ReactOS's
(Windows-compatible) kernel ABI — `ntoskrnl.exe`/`hal.dll` exports, the
same DDK structures every WDM driver uses — needs only a DDK header set
and import libraries for those two, which **mingw-w64 already ships**
(`/usr/x86_64-w64-mingw32/include/ddk/ntddk.h`, `.../wdm.h`,
`libntoskrnl.a`, `libhal.a`) — the exact same cross-compilation toolchain
`tooling/compat-db/ntexports/` and `tooling/compat-db/ntprobe/` already
use for Windows-side userspace tools. Nobody had tried compiling this
file against it until revisiting it for Phase 5; it turned out to just
work. See `ROADMAP.md` Phase 4's task breakdown for the same correction
in context, and the new Phase 13 for what genuinely still needs RosBE
(building ReactOS's own kernel from source — the stripped ROS-NTCELL
profile, `driver/cell/images/README.md`'s "known gap").

## What's actually still unverified

**Builds clean:** `make` in this directory (see `Makefile`) compiles
with `-Wall -Wextra` (zero warnings, after fixing one GCC-vs-MSVC
multichar pool-tag literal) and links into a real PE32+ `native`-
subsystem `.sys` via mingw-w64's `libntoskrnl.a`/`libhal.a`.

**Not yet loaded or run inside a booted ReactOS kernel.** That's a
different kind of gap from "can't compile" — it needs actually driving
ReactOS's real boot/install/driver-load process (copying the `.sys` in,
registering it as a service, starting it), which this project hasn't
attempted for *any* driver yet. Phase 5 takes a first run at exactly
that with a much simpler, standalone test driver before circling back to
this one — see `ROADMAP.md` Phase 5. **The live-load verification for
this driver is consolidated in `ROADMAP.md` Phase 14**, alongside the
same gap in `vsdev.c`'s PnP path (Phase 5) and `ntnet.c` (Phase 7) — one
pass at driving ReactOS's real PnP-triggered install flow, not three
separate ones. Two real bugs are flagged directly
in this file's own comments for whenever that load-testing happens,
since a real load is exactly what would surface them:

- `IRP_MN_START_DEVICE` forwards the IRP to the lower (PCI bus) driver
  without waiting for its completion before touching PnP-translated
  resources — needs the standard `IoSetCompletionRoutine` + `KEVENT`
  wait pattern.
- `NtBridgePollDpc` calls `IoCreateDevice` (a PASSIVE_LEVEL,
  paged-pool-touching operation) from inside a DPC while holding a
  spinlock — real NT would bugcheck on this. Needs to defer PDO
  creation to a work item (`IoAllocateWorkItem`/`IoQueueWorkItem`)
  queued from the DPC instead of doing it inline. (`CreateChildPdo`, the
  function this needs to call, already exists and is correct — it's
  just not wired up yet, marked `__attribute__((unused))` so the build
  stays warning-clean in the meantime.)

What *is* verified: the exact protocol this driver is meant to speak —
`ntbridge_protocol.h`'s wire format, the SPSC ring transport, heartbeat,
logging, and PnP descriptor/ack flow — has been proven correct over a
real QEMU VM boundary using the honest stand-in in `tests/reactos/
ntbridge-guest-test.c`, which implements the *guest side of the same
protocol* in ordinary Linux userspace (mmap'ing the same ivshmem BAR2
via sysfs, no kernel driver needed for a userspace consumer). So the
wire contract this file needs to satisfy is real and tested, and the
file itself is now real, compiled, linkable code — what remains
unverified is specifically its behavior once actually running inside
ReactOS.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
