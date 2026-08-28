# ReactOS Driver-Cell Tests

Tests for the ReactOS driver-cell prototype (Phase 4/5/7 success criteria).

**Owner:** NTLinux
**Status:** Implemented and passing (Phase 4); network round-trip added and passing (Phase 7)

`run-test.sh` is the Rule 11 test for Phase 4's ntbridge deliverables:
builds `driver/ntbridge/host/ntbridge-host` and the components below,
starts the host daemon, boots the test guest under
`driver/cell/launcher/ntcell boot-testguest`, and asserts a real
pass/fail outcome from `ntbridge-host`'s exit status (0 only if the
guest heartbeat was observed *and* every seeded synthetic device was
acknowledged).

## Why a stand-in guest, stated honestly

Phase 4's real target is `driver/ntbridge/reactos/ntbridge_pnp.c` — a
genuine ReactOS WDM bus driver. It's written (see that directory's
README) but **not buildable in this sandbox**: compiling it into a real
`ntbridge.sys` requires dropping it into an actual ReactOS source tree
and building with the RosBE cross-toolchain, which isn't reachable here.
Rather than leave the entire ntbridge transport unverified until that
toolchain exists, `ntbridge-guest-test.c` implements the **guest side of
the exact same protocol** (`driver/ntbridge/protocol/
ntbridge_protocol.h`, `#include`d verbatim, not reimplemented) as an
ordinary statically-linked Linux userspace program: it finds the ivshmem
PCI device via sysfs (vendor 0x1AF4, device 0x1110), `mmap`s its BAR2
directly (no kernel driver needed for a userspace consumer — genuinely
different from how the real ReactOS driver reaches the same memory via
WDM resource translation, but the *memory layout and message flow* are
identical either way), and drives the guest side of the heartbeat,
logging, and PnP acknowledgment protocol.

`build-testguest-initramfs.sh` packages this binary plus a static
`busybox` into a minimal initramfs, booted directly by QEMU
(`-kernel`/`-initrd`, the host's own Ubuntu kernel — no distro install,
no ReactOS, on purpose) via `ntcell boot-testguest`.

**What this proves:** the wire protocol, the SPSC ring transport, the
host daemon, and the QEMU launcher's `ivshmem-plain` wiring all work for
real, across a genuine QEMU VM boundary — not same-process, not mocked.
**What this does NOT prove:** that `ntbridge_pnp.c` itself is a correct
ReactOS driver. That driver has two known bugs flagged in its own
comments (a missing wait-for-lower-IRP-completion, and calling
`IoCreateDevice` from DISPATCH_LEVEL) that only a real build would
force fixing. Say this plainly in any status report — "ntbridge works
end-to-end" is true of the protocol/transport/host, not yet of the
ReactOS driver.

**Verified live**, most recent run: guest heartbeat detected after
~7.8s, 3/3 synthetic devices ARRIVED and ACKed, 4 guest log lines
received host-side, `ntbridge-host` exited 0. Full transcript in
`ROADMAP.md` Phase 4.

`driver/cell/launcher/ntcell boot-reactos --screenshot ...` (exercised
separately, not by this script) additionally proves a real,
unmodified ReactOS 0.4.15 kernel boots under the same launcher — a real
QMP screendump of its Setup screen, not a mockup — covering the
"minimal bootable ReactOS image" half of Phase 4 that this stand-in
guest doesn't touch.

## Phase 7 addition: the network round trip

`ntbridge-guest-test.c` now also exercises `net_tx_ring`/`net_rx_ring`
(ARCHITECTURE.md section 22's NDIS bridge — see `driver/net/README.md`):
it pushes one tagged synthetic Ethernet frame into `net_tx_ring` at
startup and watches `net_rx_ring` for a distinctly-tagged inbound frame,
logging a `PASS`/content mismatch either way via `log_ring`.
`driver/net/reactos/run-test.sh` (not this directory's `run-test.sh` —
a separate script, since it also needs a real Linux TAP device and the
`tests/reactos/net-tap-echo.py` raw-socket helper) is what actually
drives this: it starts `ntbridge-host --tap`, starts `net-tap-echo.py`
listening on that TAP interface via a plain `AF_PACKET` raw socket
(genuinely what any ordinary Linux network application sees on that
interface, not a special test hook), boots this same guest test client,
and checks both directions — the guest's outbound frame really lands on
a real Linux netdev, and a frame injected into that netdev really
reaches the guest via `net_rx_ring`.

**Verified live**: guest heartbeat detected, `net: pushed test frame to
net_tx_ring` then `net: received expected test frame on net_rx_ring -
PASS` in the guest's own log; `net-tap-echo.py` independently reports
`captured guest frame ... PASS`; `ntbridge-host`'s summary shows `TAP
frames host->guest: 1, guest->host: 1`. A **real bug found running this
for the first time, not by inspection**: bumping
`NTBRIDGE_PROTOCOL_VERSION`'s shm region from 1 MiB to 4 MiB (to fit the
new rings) broke this guest client's `mmap()` of the ivshmem BAR2 with
`EINVAL` — `driver/cell/launcher/ntcell`'s QEMU invocation had a
hardcoded `SHM_SIZE_MB=1` that needed updating to match; fixed, with a
comment explaining the cross-language duplication a bash script can't
avoid by including the C header. Full transcript in `ROADMAP.md`
Phase 7.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
