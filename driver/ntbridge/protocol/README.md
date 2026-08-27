# ntbridge Protocol

Versioned wire protocol shared by the host and ReactOS sides of ntbridge.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 4)

`ntbridge_protocol.h` defines the shared-memory wire format used across
the Linux host <-> ReactOS driver-cell VM boundary (ARCHITECTURE.md
section 17): a versioned header (`NTBRIDGE_MAGIC`/
`NTBRIDGE_PROTOCOL_VERSION`, Rule 12 — the same "no `u` suffix" lesson
learned from `NTABI_PROTOCOL_VERSION` in Phase 3 is called out directly
in a comment here too), heartbeat counters for both directions, and
three lock-free single-producer/single-consumer rings (`log_ring`,
`pnp_ring`, `pnp_ack_ring`) built on plain volatile reads/writes plus
C11 `__atomic` acquire/release fences rather than a mutex or futex —
deliberately, because there is no shared kernel to provide one across
an actual VM boundary the way `ntabi`'s POSIX semaphores can assume a
shared Linux kernel. This is the same technique real ivshmem-based
drivers use.

Backing transport: QEMU `ivshmem-plain` (see
`driver/cell/launcher/ntcell`) — no interrupt/doorbell support in the
"plain" variant, so both sides poll on a fixed tick. That's a documented
choice, not an oversight; the upgrade path (`ivshmem-doorbell` +
eventfd, once polling latency actually matters for a real driver) is
noted directly in the header rather than left implicit.

**Verified for real:** `driver/ntbridge/host/ntbridge-host` (host side)
and `tests/reactos/ntbridge-guest-test` (guest side stand-in — see that
directory's README for why it's a stand-in) both compile this exact
header and exchange real messages — heartbeat, log lines, and PnP
device descriptors with acknowledgments — across a genuine QEMU VM
boundary. See `tests/reactos/run-test.sh` and `ROADMAP.md` Phase 4 for
the full account.

The ring push/pop helpers (`NTBRIDGE_RING_PUSH_DECL`) are deliberately
`static inline` and libc-independent (plain struct assignment + GCC/
Clang atomics, no `memcpy`/`malloc`) so the same functions compile
unchanged in the ReactOS kernel-mode driver
(`driver/ntbridge/reactos/ntbridge_pnp.c`) as in userspace — one
protocol definition, three consumers, no drift.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
