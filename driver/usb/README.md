# USB Bridge

Bridges Windows USB client drivers to Linux via shared bulk-transfer rings (ARCHITECTURE.md section 27's "USB device cell", Phase 8).

**Owner:** NTLinux
**Status:** Protocol/transport/host-echo verified end-to-end (Phase 8); real ReactOS driver written and links, but not a full HC miniport, and not yet loaded inside live ReactOS

Same shape as `driver/ntbridge/`'s Phase 4 split and `driver/net/`'s
Phase 7 split, applied to USB — with one real, load-bearing difference
from both, stated up front rather than discovered later:

## Not a virtual USB Host Controller Driver

mingw-w64's DDK ships `usb.h`/`usbioctl.h` — the real `URB`/
`IOCTL_INTERNAL_USB_SUBMIT_URB` shapes a client driver stacked *above* a
bus uses — but **not** `usbport.h`, the internal miniport interface a
from-scratch Host Controller Driver would need to register with
ReactOS's `usbport.sys`. Confirmed absent by direct filesystem search
(`find /usr/x86_64-w64-mingw32/include -iname usbport.h` — nothing),
not assumed. So this is not (yet) a general "any pre-existing vendor
`.sys` binds unmodified" bridge, the way `driver/net/`'s NDIS miniport
genuinely is for NDIS drivers. Getting there needs either that header
(from ReactOS's own tree, or hand-written against ReactOS's public
`usbport.sys` source) or a different integration point — real follow-up
work, not attempted in this pass.

What this pass builds instead: `driver/usb/reactos/ntusb.c`, a WDM bus
driver (same shape as `driver/ntbridge/reactos/ntbridge_pnp.c`) that
attaches to the ivshmem PCI function and creates exactly **one** child
PDO representing a single synthetic vendor-class USB device (one bulk
IN endpoint, one bulk OUT endpoint). That child PDO's
`IRP_MJ_INTERNAL_DEVICE_CONTROL` handler answers
`IOCTL_INTERNAL_USB_SUBMIT_URB` directly, using real `struct _URB`
shapes — the same request a real vendor USB client driver bound on top
of it would send. Short of a real HC miniport, this is what "a vendor
Windows USB driver operates a device unavailable through a native Linux
driver" mechanically needs: a device that shape of driver could bind
to, whose URBs get answered by NTLinux instead of real hardware.

## What's real here

- `driver/ntbridge/protocol/ntbridge_protocol.h` — protocol bumped to
  **v3** (Rule 12), adding `usb_req_ring`/`usb_resp_ring`: whole
  bulk-transfer payloads, one ring per direction, same
  simplicity-over-throughput tradeoff `net_tx_ring`/`net_rx_ring` made
  in Phase 7. Descriptors (device/config/string) are **not** bridged —
  they're synthesized locally in `ntusb.c` since there's no real
  hardware behind them for Linux to be authoritative over; only the
  bulk *data* genuinely needs to cross.
- `driver/usb/reactos/ntusb.c`/`.h` — the bus driver described above.
  Builds clean (`-Wall -Wextra`, zero warnings) and links into a real
  PE32+ `native`-subsystem `.sys`.
- `driver/usb/reactos/ntusb_test.c` — a usermode test client, opens the
  child PDO's symlink (`\\.\NTLUSB0`) and drives a test-only
  `IOCTL_NTUSB_TEST_BULK_TRANSFER` that internally builds a real `URB`
  and calls the *same* dispatch function
  (`NtusbProcessUrb`) `IOCTL_INTERNAL_USB_SUBMIT_URB` would — see that
  file and `ntusb.c`'s header comment for exactly why this exists (a
  real client driver's internal submission is kernel-to-kernel only,
  unreachable from ordinary `DeviceIoControl`, and no real vendor
  driver exists in this project to test against instead).
- `driver/ntbridge/host/ntbridge-host`'s new `--usb-echo` mode — an
  honest **software-only** stand-in for a real bridge: answers every
  bulk request with a fixed, distinctly-tagged reply. Not a claim of
  real USB hardware access — this sandbox's kernel has no USB subsystem
  at all (`CONFIG_USB` not set, confirmed by direct inspection of
  `/proc/config.gz` — no usbfs, no `/dev/bus/usb`), the same
  architectural-absence category Phase 6 (VFIO) hit, not a config gap.
  The real design this stands in for — bridging to a physical device
  via Linux's `usbfs` (`USBDEVFS_SUBMITURB`/`USBDEVFS_REAPURB`) — is
  real follow-up work for an environment that actually has USB hardware.

**Verified for real, over a genuine QEMU VM boundary** (not
same-process, not mocked): the honest stand-in guest test client
(`tests/reactos/ntbridge-guest-test.c`, extended with a USB round trip
the same way it was extended with a net round trip in Phase 7) pushes a
tagged request onto `usb_req_ring`; `ntbridge-host --usb-echo`
(running in a separate host process) drains it and pushes a
distinctly-tagged reply onto `usb_resp_ring`; the guest confirms the
content. See `driver/usb/reactos/run-test.sh` and `ROADMAP.md` Phase 8
for the full transcript.

**What this proves precisely:** the USB bridge transport — the wire
format, the ring protocol, and the host-side echo responder — is real
and correct, verified over a genuine QEMU VM boundary. What it does
**not** yet prove: that `ntusb.c`, running as an actual bus driver
inside a real, booted ReactOS kernel, behaves correctly, or that a real
(or even a synthetic test) vendor client driver can bind to its child
PDO and successfully submit a real internal URB. That gap is tracked in
`ROADMAP.md` Phase 14 alongside the matching gaps in
`ntbridge_pnp.c` (Phase 4), `vsdev.c`'s PnP path (Phase 5), and
`ntnet.c` (Phase 7) — one pass at driving ReactOS's real PnP-triggered
install flow, not four separate ones.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
