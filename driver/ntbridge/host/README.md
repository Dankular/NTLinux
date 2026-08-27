# ntbridge Host

Linux-side (host) implementation of ntbridge.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 4)

`ntbridge-host.c` owns the `ivshmem-plain` backing file QEMU maps into
the driver cell's PCI BAR2 (see `driver/cell/launcher/ntcell`), creates
and version-guards the shared `ntbridge_shm_header_t` region (Rule 12 —
refuses to attach to a region stamped with a mismatched protocol
version rather than guessing at compatibility), and drives three of
Phase 4's four deliverables:

- **Host heartbeat** — bumps `host_heartbeat_seq`/`host_heartbeat_time_ns`
  on a fixed 100ms poll tick (no interrupt/doorbell on the
  `ivshmem-plain` transport — see the protocol header's note) and
  reports when the guest side starts answering back.
- **Logging channel** — drains guest-written entries from `log_ring` and
  prints them tagged (`[GUEST]`/level) to stdout.
- **Device enumeration bridge** — `seed_synthetic_devices()` pushes a
  small fixed set of synthetic `ntbridge_pnp_descriptor_t` records onto
  `pnp_ring` and tracks which the guest side acknowledges via
  `pnp_ack_ring`. This function is the one concrete hook point for the
  real integration (ARCHITECTURE.md section 18: walk
  `/sys/bus/pci/devices`/`/sys/bus/usb/devices` or listen for netlink
  uevents, translate each into the NT-flavored descriptor shape) —
  swapping it out doesn't touch anything else in this file.

Exit status doubles as a real pass/fail oracle (`tests/reactos/
run-test.sh` relies on this): 0 only if the guest heartbeat was ever
observed *and* every seeded device was acknowledged.

**Verified for real:** run end-to-end against
`tests/reactos/ntbridge-guest-test` (the honest stand-in for the not-yet-
buildable real ReactOS driver — see `driver/ntbridge/reactos/README.md`)
over a genuine QEMU VM boundary via `tests/reactos/run-test.sh` — real
heartbeat detected, all 3 synthetic devices ACKed, guest log lines
received. See `ROADMAP.md` Phase 4 for the full account.

**Known gap:** `seed_synthetic_devices()` is a fixed synthetic list, not
a real sysfs/udev/netlink walk — that's the concrete next step, not
attempted here (Phase 4's success criterion only requires "ReactOS sees
synthetic devices supplied by Linux", which this meets; wiring up real
physical device discovery is separate follow-up work).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
