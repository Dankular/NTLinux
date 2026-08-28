# ntbridge Host

Linux-side (host) implementation of ntbridge.

**Owner:** NTLinux
**Status:** Implemented and verified (Phase 4); TAP network bridging added and verified (Phase 7)

`ntbridge-host.c` owns the `ivshmem-plain` backing file QEMU maps into
the driver cell's PCI BAR2 (see `driver/cell/launcher/ntcell`), creates
and version-guards the shared `ntbridge_shm_header_t` region (Rule 12 —
refuses to attach to a region stamped with a mismatched protocol
version rather than guessing at compatibility), and drives four
deliverables now (three from Phase 4, one from Phase 7):

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
- **TAP network bridge** (`--tap IFNAME`, Phase 7) — opens/creates a
  real Linux TAP device (`/dev/net/tun`, `IFF_TAP|IFF_NO_PI` — whole
  Ethernet frames, no extra framing) and bridges it to
  `net_tx_ring`/`net_rx_ring`: frames the guest NDIS miniport
  (`driver/net/reactos/ntnet.c`) sends land in `net_tx_ring` and get
  written straight to the TAP fd, appearing to Linux exactly like a
  frame a real NIC received; frames Linux sends out that same
  interface get read off the TAP fd and pushed into `net_rx_ring` for
  the miniport to indicate as received. No protocol translation — raw
  Ethernet frames both ways, matching ARCHITECTURE.md section 22's "NDIS
  bridge" design. Polls at a tighter 10ms tick than the other rings'
  100ms (still a poll, not an interrupt — see the doorbell note) since a
  10x-slower round trip would make even a synthetic test feel broken.

Exit status doubles as a real pass/fail oracle (`tests/reactos/
run-test.sh` relies on this): 0 only if the guest heartbeat was ever
observed *and* every seeded device was acknowledged. (`--tap` mode is
verified by a separate script, `driver/net/reactos/run-test.sh`, with
its own oracle — see below.)

**Verified for real:** run end-to-end against
`tests/reactos/ntbridge-guest-test` (the honest stand-in for the not-yet-
buildable real ReactOS driver — see `driver/ntbridge/reactos/README.md`)
over a genuine QEMU VM boundary via `tests/reactos/run-test.sh` — real
heartbeat detected, all 3 synthetic devices ACKed, guest log lines
received. See `ROADMAP.md` Phase 4 for the full account. `--tap` mode
verified the same way, plus a real Linux TAP device on the host side —
see `driver/net/reactos/run-test.sh` and `ROADMAP.md` Phase 7.

**Known gap:** `seed_synthetic_devices()` is a fixed synthetic list, not
a real sysfs/udev/netlink walk — that's the concrete next step, not
attempted here (Phase 4's success criterion only requires "ReactOS sees
synthetic devices supplied by Linux", which this meets; wiring up real
physical device discovery is separate follow-up work).

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
