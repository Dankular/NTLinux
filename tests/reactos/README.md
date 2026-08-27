# ReactOS Driver-Cell Tests

Tests for the ReactOS driver-cell prototype (Phase 4/5 success criteria).

**Owner:** NTLinux
**Status:** Implemented and passing (Phase 4)

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

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
