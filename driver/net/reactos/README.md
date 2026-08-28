# NDIS Bridge — ReactOS Side (ntnet)

ReactOS-side NDIS miniport half of the NDIS bridge (Phase 7, ARCHITECTURE.md section 22).

**Owner:** NTLinux
**Status:** Builds clean and links into a real `.sys` (Phase 7); not yet loaded/tested inside a running ReactOS kernel

`ntnet.c` is a real, legacy-characteristics (NDIS 5.1) Ethernet miniport
driver, maps the same ivshmem BAR2 region `driver/ntbridge/reactos/
ntbridge_pnp.c` and `driver/vsdev/vsdev.c` already do, and moves whole
raw Ethernet frames across `ntbridge_protocol.h`'s new
`net_tx_ring`/`net_rx_ring` instead of touching real hardware:
`NtnetSend` copies an outgoing frame into `net_tx_ring` (the host reads
it off and injects it into a Linux TAP device — see `driver/ntbridge/
host/`); a polling timer (same "no interrupt in ivshmem-plain, so poll"
pattern as `ntbridge_pnp.c`'s `NtBridgePollDpc`) drains `net_rx_ring`
and hands each frame up the NDIS stack via the classic
`NdisMEthIndicateReceive` path as a real received packet. `ntnet.inf`
installs it against the same ivshmem PCI hardware ID as
`driver/ntbridge/reactos/ntbridge.inf`, but under the `Net` device
class for the Network Adapters install path.

Implements the mandatory generic + 802.3 OID surface NDIS requires
before it treats an adapter as valid (`OID_GEN_SUPPORTED_LIST` through
the 802.3 statistics counters) with real values — link speed and MTU
reflect what this adapter genuinely offers over `ntbridge`, and the
traffic counters (`XmitOk`/`RcvOk`/`XmitError`/`RcvError`) are live
counts updated by `NtnetSend`/`NtnetPollTimer`, not frozen placeholders.

## Three real header bugs found and fixed, not worked around

Building this against mingw-w64's bundled DDK (ReactOS's own
`ndis.h`/`wdm.h`, repackaged — same toolchain `ntbridge_pnp.c`/
`vsdev.c` use, still not RosBE) surfaced three genuine bugs in those
headers, independent of any macro choice on this project's part
(checked — present regardless of which `NDIS*_MINIPORT` version is
selected):

1. `ndis.h` re-`typedef`s `enum _NDIS_REQUEST_TYPE` in its own body even
   though it already `#include`s `ntddndis.h`, which defines the
   byte-for-byte identical enum under its own include guard — a genuine
   duplicate-typedef compile error.
2. `ndis.h`'s `NdisMWanIndicateReceiveComplete()` prototype is missing a
   comma between its two parameters — a literal typo (this driver
   doesn't even call that WAN-specific function; it just has to parse
   past the declaration).
3. `wdm.h` references `SYSTEM_POWER_STATE_CONTEXT` inside the IRP Power
   minor-function union under a guard spelled `NTDDI_VERSION >=
   NTDDI_WINVISTA`, but the struct itself is only *defined* under the
   correctly-spelled `NTDDI_VERSION >= NTDDI_VISTA` a few thousand lines
   earlier. `NTDDI_WINVISTA` doesn't exist anywhere in mingw-w64's
   `sdkddkver.h`, so the preprocessor silently treats it as `0` and the
   guard is always true — referencing a type that's genuinely undefined
   below Vista-level targets, which is exactly this driver's NDIS
   5.1/WinXP-level target (matching ReactOS's own reported NT 5.2
   compatibility).

`prepare-ndis-header.sh` generates locally-patched copies of both
headers into `./build/` — **does not touch the system-installed
headers** — and the Makefile puts `build/` first on the include path so
the patched copies shadow the originals for this build only. Every
patch is a narrow, exact substitution (documented in the script itself)
against the specific broken line(s), not a broad rewrite.

## What's actually still unverified

**Builds clean:** `make` in this directory compiles with `-Wall
-Wextra` (two harmless, pre-existing macro-redefinition warnings from
`ndis.h`/`ntddndis.h` both defining `NDIS_PROTOCOL_ID_MAX`/`_MASK` to
the identical value `0x0f` — cosmetic, not a real conflict) and links
into a real PE32+ `native`-subsystem `.sys` via `-lndis -lntoskrnl
-lhal`.

**Not yet loaded or run inside a booted ReactOS kernel.** Installing an
NDIS miniport goes through a materially more involved flow than
`driver/vsdev/`'s single `sc start` (real network adapter installation
via the Add Hardware wizard or Device Manager's "Update Driver"), out
of reach for this pass's verification budget. What **is** verified,
over a real QEMU VM boundary: the exact wire protocol and transport
this driver is meant to use — `net_tx_ring`/`net_rx_ring`, and a real
Linux TAP device bridge (`driver/ntbridge/host/`) — using the honest
stand-in guest test client in `tests/reactos/ntbridge-guest-test.c`
(same role as Phase 4's stand-in for `ntbridge_pnp.c`). See `driver/
net/reactos/run-test.sh` and `ROADMAP.md` Phase 7 for that result. So
the wire contract this driver needs to satisfy is real and tested; this
file's own correctness against that contract, and its OID surface's
completeness against a real NDIS wrapper's validation, are not, yet.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
