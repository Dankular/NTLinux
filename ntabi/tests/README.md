# NT ABI Tests

Protocol and API conformance tests for libntabi/ntd (Rule 11: every NT compatibility implementation requires tests).

**Owner:** NTLinux
**Status:** Implemented and passing (Phase 2 + Phase 3) — 38/38

`test_ntabi.c` — 38 checks, all passing. Phase 2: auto-reset vs.
manual-reset event semantics, semaphore counting, name collision/not-found/
type-mismatch handling, the shared-memory protocol-version guard, real
cross-process event wait and mutant ownership hand-off (measured elapsed
time, not just a correct return code — proving actual blocking, not
busy-polling or a lucky race). Phase 3 adds: per-process handle table
isolation (two processes numerically reusing the same handle value can't
reach each other's objects, and closing one's handle doesn't disturb the
other's), wait-any (correct index reported) and wait-all (doesn't return
until *every* handle is signaled, verified by timing), a real cross-process
shared-memory section (write in one process, read *and write back* in
another, verified visible in the first), a completion port (FIFO order,
plus a real blocked cross-process dequeue), and process-exit detection via
`pidfd` for a process that is *not* a child of `ntd`'s own process (proving
the mechanism generalizes beyond parent-child, unlike plain `waitpid`).
Self-contained: starts its own `ntd` instance, runs everything, tears it
down. Run with `make check` in this directory.

**Found and fixed by actually running this suite, not by inspection**: a
segfault traced to a stale `ntd` process left over from an earlier crashed
run holding the shared-memory name (fixed by cleaning up test processes
between runs, not a code bug); a completion-port test that reused the
parent's raw handle number in the child process, which the new
per-process handle tables correctly rejected — the daemon was right, the
test was still written for Phase 2's flat handle space (fixed by adding
the `NTABI_OP_OPEN_COMPLETION` opcode this daemon was genuinely missing);
and a Phase-2-era test assertion ("open returns the same handle number")
that Phase 3's real per-process handle tables correctly no longer satisfy
— fixed by asserting the actually-correct behavior (a *new* handle number
to the *same* object) instead of weakening the daemon to match a
now-outdated test.

Not yet differential against real Windows/Wine behavior (Rule 11's fuller
ambition) — there's no ntdll integration yet to compare against (see
`ROADMAP.md` Phase 2's "known gap", unchanged by Phase 3). What's tested
is genuine NT object semantics against the real daemon, not mocks.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
