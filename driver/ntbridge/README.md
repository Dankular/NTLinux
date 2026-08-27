# ntbridge

High-performance host/cell protocol: control, PnP, MMIO, interrupt, DMA, network, storage, USB, logging, power, device-lifecycle queues over shared memory (ARCHITECTURE.md section 17).

**Owner:** NTLinux
**Status:** PnP, logging, and heartbeat queues implemented and verified end-to-end (Phase 4); control/MMIO/interrupt/DMA/network/storage/USB/power queues not yet started (Phase 5+ territory, once real drivers need them)

Phase 4 scoped ntbridge down to what its own success criterion needs
("ReactOS sees synthetic devices supplied by Linux") rather than
standing up all eleven queue types section 17 lists at once — three
subdirectories, each independently verified:

- `protocol/` — the versioned wire format (`ntbridge_protocol.h`):
  heartbeat counters plus `log_ring`/`pnp_ring`/`pnp_ack_ring`, built on
  lock-free SPSC rings since there's no shared kernel across a VM
  boundary to provide a mutex/futex with.
- `host/` — `ntbridge-host`, the Linux-side daemon: owns the
  `ivshmem-plain` backing file, seeds synthetic device descriptors,
  drives the host heartbeat, and drains the logging channel.
- `reactos/` — the real ReactOS-side WDM bus driver source
  (`ntbridge_pnp.c`), written against actual ReactOS DDK conventions
  but **not yet built or run** — needs the RosBE toolchain, out of
  reach in this sandbox. See that directory's README for exactly what's
  unverified and the two known bugs flagged in its own comments.

**Verified for real:** the protocol + host + transport (everything
except the ReactOS driver itself) proven end-to-end over a genuine QEMU
VM boundary, using an honest stand-in guest client
(`tests/reactos/ntbridge-guest-test.c`) in place of the real ReactOS
driver — heartbeat, logging, and all seeded synthetic devices
arrived-and-acked. See `tests/reactos/run-test.sh` and `ROADMAP.md`
Phase 4.

The remaining queue types (MMIO, interrupt, DMA, network, storage, USB,
power, device-lifecycle beyond simple arrival/removal) aren't invented
yet because nothing calls for them until a real driver
(`docs/ARCHITECTURE.md` Phase 5's "first Windows driver") actually needs
one — no point designing wire formats for operations with no concrete
consumer to validate them against.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
