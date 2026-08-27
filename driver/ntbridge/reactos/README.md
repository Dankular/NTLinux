# ntbridge ReactOS Side

ReactOS-side (cell) implementation of ntbridge.

**Owner:** NTLinux
**Status:** Written, NOT built or run (Phase 4) — see "Known gap" below

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

**Known gap, stated plainly:** this file has never been compiled or
run. Building it for real means dropping it into an actual ReactOS
source tree and wiring it into that tree's build (a `CMakeLists.txt`
entry under something like `drivers/ntlinux/ntbridge/`), which requires
the RosBE cross-toolchain — not reachable in this sandbox (same category
of gap as the unbuilt Wine `ntdll` integration tracked in Phase 12, and
the "no real Windows machine" caveat on `tooling/compat-db/ntexports/`).
Two known issues are flagged directly in code comments rather than
hidden, so a real build-and-test pass has concrete things to fix, not a
blank slate:

- `IRP_MN_START_DEVICE` forwards the IRP to the lower (PCI bus) driver
  without waiting for its completion before touching PnP-translated
  resources — needs the standard `IoSetCompletionRoutine` + `KEVENT`
  wait pattern.
- `NtBridgePollDpc` calls `IoCreateDevice` (a PASSIVE_LEVEL,
  paged-pool-touching operation) from inside a DPC while holding a
  spinlock — real NT would bugcheck on this. Needs to defer PDO
  creation to a work item (`IoAllocateWorkItem`/`IoQueueWorkItem`)
  queued from the DPC instead of doing it inline.

What *is* verified: the exact protocol this driver is meant to speak —
`ntbridge_protocol.h`'s wire format, the SPSC ring transport, heartbeat,
logging, and PnP descriptor/ack flow — has been proven correct over a
real QEMU VM boundary using the honest stand-in in `tests/reactos/
ntbridge-guest-test.c`, which implements the *guest side of the same
protocol* in ordinary Linux userspace (mmap'ing the same ivshmem BAR2
via sysfs, no kernel driver needed for a userspace consumer). So the
wire contract this file needs to satisfy is real and tested; this
file's own correctness against that contract is not, yet.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
