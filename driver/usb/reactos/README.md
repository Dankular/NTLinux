# USB Bridge — ReactOS Side (ntusb)

ReactOS-side USB bus driver half of the USB bridge (Phase 8, ARCHITECTURE.md section 27's "USB device cell").

**Owner:** NTLinux
**Status:** Builds clean and links into a real `.sys` (Phase 8); not yet loaded/tested inside a running ReactOS kernel

## Why this is a bus driver, not a Host Controller Driver

The literal Phase 8 success criterion — "a vendor Windows USB driver
operates a device unavailable through a native Linux driver" — is most
faithfully satisfied by a virtual USB **Host Controller Driver**: any
pre-existing vendor `.sys` sits on top of ReactOS's own `usbhub.sys`,
which sits on top of a HC miniport registered with `usbport.sys`, and
never needs to know the "hardware" underneath is actually NTLinux's
bridge. That's exactly the shape `driver/net/reactos/ntnet.c` achieves
for NDIS (a real NDIS miniport any NDIS-conformant driver's traffic
would legitimately flow through).

Checked directly before committing to this design (not assumed):
mingw-w64's DDK ships `usb.h`, `usbioctl.h`, `usbdlib.h`, `usbbusif.h`,
`usbprint.h`, `usbscan.h`, `usbstorioctl.h`, `usbkern.h`,
`usbprotocoldefs.h` — the real, modern `URB`/`USBD_STATUS`/
`IOCTL_INTERNAL_USB_SUBMIT_URB` surface a client driver stacked *above*
a bus uses — but no `usbport.h` anywhere under
`/usr/x86_64-w64-mingw32/include`. That header defines the *internal*
miniport contract (`USBPORT_REGISTRATION_PACKET`,
`MINIPORT_OpenEndpoint`, `MINIPORT_SubmitTransfer`, root-hub emulation
callbacks) a real HC miniport registers with `usbport.sys` — genuinely
absent from this toolchain, not merely undocumented. Getting it would
mean pulling it from ReactOS's own source tree (it's ReactOS/Windows-
internal, not part of the redistributable DDK mingw-w64 packages) or
hand-transcribing it against ReactOS's public `usbport.sys` source the
way `tooling/compat-db/ntprobe/` hand-transcribes a few NT structures
from Musa.Veil (ADR-0006) — real, scoped follow-up work, not attempted
in this pass, and flagged here rather than silently worked around.

So instead: `ntusb.c` is a WDM **bus driver**, same shape as
`driver/ntbridge/reactos/ntbridge_pnp.c` — attaches to the ivshmem PCI
function (`NtusbAddDevice`), maps BAR2, and creates exactly **one**
child PDO (`NtusbCreateChildPdo`) representing a single synthetic
vendor-class USB device (`bDeviceClass = 0xFF`, one bulk IN endpoint at
`0x81`, one bulk OUT endpoint at `0x01`). That child PDO answers
`IRP_MN_QUERY_ID` with a real hardware ID
(`USB\VID_1AF4&PID_1005&REV_0100`) so a real vendor driver's INF could
in principle match it, and its `IRP_MJ_INTERNAL_DEVICE_CONTROL` handler
answers `IOCTL_INTERNAL_USB_SUBMIT_URB` directly — the same request a
real client driver bound on top would send. This is not the general
"any driver just works" bridge a real HC miniport would be, but it *is*
a device a real client driver could bind to and get real (bridged)
transfer behavior from — the concrete step short of a full HC miniport.

## Fixed here, not reproduced: ntbridge_pnp.c's flagged START_DEVICE bug

`ntbridge_pnp.c`'s own comments flag that its `IRP_MN_START_DEVICE`
handler forwards the IRP to the lower (PCI bus) driver without waiting
for that lower IRP's completion before touching PnP-translated
resources — a real bug, left visible rather than silently fixed,
because that file has never been built or run (see its own README).
`ntusb.c`'s `IRP_MN_START_DEVICE` handling uses the standard
`IoSetCompletionRoutine` + `KEVENT` wait pattern instead
(`NtusbLowerCompletion`) — genuinely waits for the lower stack before
calling `NtusbStartFdo`. Also unlike `ntbridge_pnp.c`'s flagged
DISPATCH_LEVEL bug (`CreateChildPdo` called from a poll DPC, unsafe):
`ntusb.c` creates its one child PDO synchronously from inside
`IRP_MN_START_DEVICE` itself, which always runs at PASSIVE_LEVEL — safe
by construction, and simpler than `ntbridge_pnp.c`'s deferred-work-item
requirement, because this driver's device topology is static (one
synthetic device, never hot-plugged) rather than dynamic.

## The pre-existing PCI hardware ID collision, now three-way

`ntusb.inf` matches `PCI\VEN_1AF4&DEV_1110` — the same ivshmem hardware
ID `driver/ntbridge/reactos/ntbridge.inf` and `driver/net/reactos/
ntnet.inf` already match (`ntnet.inf`'s own header comment already
flagged the ntbridge/ntnet pair). This makes it a three-way collision,
not a new problem this file introduces. Real fix: a distinct ivshmem
device instance (or at least a distinct PCI subsystem-vendor-id/
subsystem-device-id QEMU can stamp per `-device ivshmem-plain`) per
bridge type — real follow-up work. Hasn't blocked anything so far
because each phase's driver has only ever been verified independently,
never loaded against the same cell simultaneously.

## A real linker finding, not by inspection

`NtusbDispatchDeviceControl`'s on-stack `UCHAR
localData[NTUSB_MAX_TRANSFER]` (4096 bytes) makes that function's stack
frame large enough that mingw's x86_64 codegen emits a call to
`__chkstk_ms` (the standard Windows stack-probe helper for frames past
one page) — `x86_64-w64-mingw32-ld: undefined reference to
___chkstk_ms`, a genuine link failure on first build, not predicted in
advance. `__chkstk_ms` lives in `libgcc`, not `ntoskrnl`/`hal`, and
`-nostdlib` excludes it by default. Fixed by adding `-lgcc` to
`LDLIBS` (see `Makefile`) — pulls in just that one compiler-support
routine, nothing requiring an OS to run under.

## What's actually still unverified

**Builds clean:** `make` in this directory compiles with `-Wall
-Wextra` (zero warnings) and links into a real PE32+ `native`-subsystem
`.sys` via `-lntoskrnl -lhal -lgcc`.

**Not yet loaded or run inside a booted ReactOS kernel.** Same category
of gap as `ntbridge_pnp.c`, `vsdev.c`'s PnP path, and `ntnet.c` — needs
a real PnP-triggered install (this driver's FDO needs real PnP
resource assignment against the ivshmem PCI function to reach BAR2 at
all, so — unlike `vsdev.c` — there's no legacy `sc start` shortcut
available here either: a legacy load gets `DriverEntry` and nothing
else, no PnP-negotiated resources, no shared memory to map). Tracked
in `ROADMAP.md` Phase 14, consolidated with the other three drivers'
matching gap.

**What *is* verified**, over a real QEMU VM boundary: the exact wire
protocol and transport this driver is meant to use —
`usb_req_ring`/`usb_resp_ring`, and the host-side `--usb-echo` responder
(`driver/ntbridge/host/`) — using the honest stand-in guest test client
in `tests/reactos/ntbridge-guest-test.c` (same role as Phase 4's
stand-in for `ntbridge_pnp.c`). See `driver/usb/reactos/run-test.sh` and
`ROADMAP.md` Phase 8 for that result. `NtusbProcessUrb` — the actual
URB dispatch core `ntusb.c` would run for a real client driver's
submission — is written and builds, and is *designed* to be exercised
live via `ntusb_test.c`'s test-only IOCTL once this driver is ever
loaded inside real ReactOS, but that hasn't happened yet either. So the
wire contract this driver needs to satisfy is real and tested; this
file's own correctness against that contract, running inside ReactOS,
is not, yet.

## Known simplifications, stated precisely (Rule 2, not oversights)

- **Transfers complete synchronously against the ring's current state**,
  not via a pended/deferred IRP completion. An OUT transfer pushes and
  returns success immediately (same tradeoff `NtnetSend` made for
  `net_tx_ring` in Phase 7); an IN transfer does one immediate
  `try_pop` and returns whatever is (or isn't) already queued, rather
  than blocking until data arrives. Implementing real IRP pending
  (`IoMarkIrpPending` + a completion routine invoked once data shows
  up) is real follow-up work, not required by anything this pass
  actually tests.
- **`GET_DESCRIPTOR_FROM_DEVICE` only implements the device descriptor**,
  not configuration or string descriptors — enough for what this pass
  tests (no real descriptor-fetching client driver exists to test
  against), not a complete USB enumeration surface.
- **`SELECT_CONFIGURATION` doesn't validate the caller's requested pipe
  count/types** against what it hands back — there is exactly one legal
  shape (two bulk pipes) to validate against, and getting it wrong just
  means a later `BULK_OR_INTERRUPT_TRANSFER` rejects an unrecognized
  `PipeHandle` anyway.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
