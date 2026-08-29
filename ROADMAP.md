# NTLinux Roadmap

Phases mirror `docs/ARCHITECTURE.md` section 43. Work the current phase
before starting later ones — compatibility and a usable system come before
architectural purity (Rule 10).

Legend: ⬜ not started · 🟨 in progress · ✅ done

---

## Phase 0 — Baseline distro ✅ (verified as far as this sandbox allows)

**Goal:** a fully usable gaming Linux distro, before any custom NT
architecture exists. If a user booted this image today, they'd have a
working Linux desktop with Wine/Proton/Steam already working the normal way.

This phase builds the **reference distro** (`distro/`) only — a curated
bundle of upstream packages, no NTLinux runtime code yet (Phases 1-3) and no
ReactOS driver cell (Phases 4+). See ADR-0001/ADR-0002 in
`docs/DECISIONS.md`: consume upstream packages, don't vendor; keep KVM/VFIO
out of scope here — it belongs to the driver-cell phases.

**Base decisions (defaults — see "Open decisions" in `CLAUDE.md`):**
- Foundation: Arch Linux, built via `archiso` (`distro/image/`).
- Session: Gamescope (game mode) as primary, Plasma (Wayland) as desktop
  mode.

**Task breakdown:**

- [x] Repo scaffold + canonical docs (`docs/ARCHITECTURE.md`, `ROADMAP.md`,
      `CLAUDE.md`, per-directory `README.md`s).
- [x] `distro/image/` — archiso profile skeleton (`profiledef.sh`,
      `packages.x86_64`) that builds a bootable ISO.
- [x] `distro/packages/base.list` — canonical Phase 0 package manifest,
      annotated by doc section.
- [x] `kernel/config/` — `CONFIG_NTSYNC` confirmed **on the actual built
      kernel**, not just assumed: booted the image and found `/dev/ntsync`
      already present at boot (`crw-rw-rw- 10, 261`), the misc-device node
      ntsync registers when built in. `CONFIG_KVM`/`CONFIG_VFIO*`/
      `CONFIG_IOMMUFD` stay out of scope for Phase 0 per the scope note
      above — Phase 4+'s concern, not checked here.
- [x] Wine + Steam packaged and present, `ntsync` confirmed live. Verified
      by booting the ISO and running commands in a real root shell (not
      just inspecting the profile):
      - `wine --version` → `wine-11.16 (Staging)`.
      - `pacman -Q` confirms `steam`, `pipewire`, `wireplumber`,
        `wine-staging`, `gamescope`, `plasma-desktop`, `mesa`,
        `vulkan-icd-loader` all installed at the expected versions.
      - Vulkan wired correctly: `libvulkan.so`/`libvulkan_radeon.so`/
        `libvulkan_intel*.so` present via `ldconfig`, and
        `/usr/share/vulkan/icd.d/` has `radeon_icd.json`,
        `intel_icd.json`, `intel_hasvk_icd.json` — both Mesa Vulkan
        drivers register correctly.
      - `gamescope`, `steam`, `wine` binaries all present on `$PATH`.
      **Not verified:** DXVK/vkd3d-proton actually rendering through a
      real GPU, or a full Steam/Proton game launch — this sandbox has no
      GPU/KVM passthrough (software-only QEMU), so that needs real
      hardware. Everything checkable without a GPU (packages present,
      binaries present, ntsync live, Vulkan ICDs registered) is confirmed.
- [x] Build-test the archiso profile end-to-end. Done in-session: no Arch
      host was available, so an `archlinux:latest` container was used
      instead (Docker daemon started manually, routed through the
      session's TLS-intercepting proxy via `--network host` + the CA
      bundle — see `distro/image/README.md`). Result: a real
      `mkarchiso -v` run against this exact profile completed
      successfully and produced `ntlinux-<date>-x86_64.iso` (~2.3GB, 630
      resolved packages). The first attempt failed profile validation
      (missing bootloader trees); fixed by basing `distro/image/` on
      archiso's own `releng` profile instead of hand-authoring syslinux/
      efiboot/mkinitcpio config (see the commit that added
      `distro/image/syslinux,efiboot,grub,airootfs`).
- [x] Boot the built ISO **and log in** — confirmed via QEMU (software
      emulation, no KVM available in this sandbox): SeaBIOS → ISOLINUX
      renders the real boot menu → kernel + initramfs load →
      `systemd[1]` starts → live root reached → autologin lands at a
      real `[root@archiso ~]#` shell, not just a login banner. Getting
      here took two real bugs, both found only by actually booting, not
      by inspection:
      1. releng's `airootfs/etc/passwd` points root at `/usr/bin/zsh`,
         which our trimmed package list doesn't install → "no shell" at
         login. Fixed by switching root to `/bin/bash` (guaranteed by
         `base`) — see `distro/image/airootfs/etc/passwd`.
      2. releng's autologin override only covers `tty1` (the virtual
         console), not the serial console — fine for local/interactive
         use, useless for headless QEMU/CI testing. Added a matching
         `serial-getty@ttyS0.service.d/autologin.conf` override.
      One cosmetic, non-fatal systemd warning throughout
      (`ModemManager1.service` is an unresolvable alias); no kernel
      panic, no boot failure.
      **Not verified:** a graphical Plasma/Gamescope session — this
      sandbox has no KVM/GPU passthrough. Console-level boot, login, and
      every package/binary/device check above is confirmed; rendering a
      desktop needs real hardware or a KVM-enabled runner.

**Success criterion (from `docs/ARCHITECTURE.md`):** a fully usable gaming
Linux distro exists before any custom NT architecture code is written. Met
at the console/package level; a real machine is needed to confirm the
graphical session and an actual game launch end-to-end.

---

## Phase 1 — Native Windows executable UX ✅ (verified as far as this sandbox allows)

Deliver `ntloader`, PE binary-format registration, per-app environment
management, desktop file generation, a Windows application installer.

**Success criterion:** `./notepad.exe` works without explicitly invoking
`wine`. **Met, literally, and verified live in this session** — not just
implemented, actually run and screenshotted.

**Task breakdown:**

- [x] `ntloader` (`ntloader/ntloader.c`) — reads the PE header enough to
      route by architecture, creates/reuses a per-app environment (§37),
      and `execve()`s into `wine`. Deliberately Gen1 (§3.2): real PE
      loading stays Wine's job (Rule 1), not reimplemented here. Compiles
      clean with `-Wall -Wextra`, zero warnings.
- [x] PE binary-format registration — `distro/image/airootfs/usr/lib/binfmt.d/ntlinux-pe.conf`,
      a `systemd-binfmt` rule matching the `MZ` magic bytes to
      `/usr/libexec/ntloader`. Verified for real: registered directly
      with the kernel's `binfmt_misc` (`/proc/sys/fs/binfmt_misc/register`)
      in this session, not just written to a config file.
- [x] Per-application NT environment manager — `ensure_app_dirs()` in
      `ntloader.c`, matched by the same logic in `tooling/installer/ntlinux-app`
      (Python). Creates `~/.local/share/ntlinux/apps/<id>/{drive-c,registry,
      compat,dll-overrides,state,prefix}` per §37. The `<id>` scheme
      (basename + FNV-1a-64 hash of the resolved path) is intentionally
      duplicated between the C and Python implementations — documented in
      both, not an oversight — and verified to produce **identical** IDs
      for the same file from both tools.
- [x] Desktop file generation — `ntlinux-app desktop`/`run-installer`
      write real `.desktop` entries under `~/.local/share/applications/`,
      with icons extracted from the PE binary's own resources via
      `wrestool`/`icotool` (`icoutils` — Rule 1, not a hand-rolled ICO
      parser). `Exec=` is just the `.exe`'s path, since `binfmt_misc` +
      `ntloader` already make it directly executable.
- [x] Windows application installer — `ntlinux-app run-installer` runs a
      Windows installer in a fresh prefix, diffs `Program Files*`
      before/after to find what it installed, and generates the desktop
      entry for that program. (The installer-diffing path itself wasn't
      exercised against a real installer in this session — no installer
      binary was on hand to test against, only a portable `.exe`
      (`notepad.exe`); the underlying mechanics — prefix creation,
      before/after diffing, desktop-file generation — are shared code
      paths already verified by the `desktop` subcommand test below.)

**Verified live, end to end, in this session** (Ubuntu sandbox — Wine
9.0 + a virtual X display via Xvfb, not the NTLinux reference image; see
"Known gap" below):

1. Compiled `ntloader`, installed it at `/usr/libexec/ntloader`, and
   registered it with the running kernel's `binfmt_misc` for real
   (`/proc/sys/fs/binfmt_misc/ntlinux-pe` came back `enabled`).
2. Copied Wine's own bundled `notepad.exe` (a real PE32+ binary,
   `/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/notepad.exe`) to a local
   directory, `chmod +x`, and ran **`./notepad.exe` directly** — no
   `wine`, no `ntloader`, nothing but the bare filename, exactly the
   literal success criterion above.
3. The kernel dispatched it through `ntloader`, which created a fresh
   per-app environment, set `WINEPREFIX`, and `exec`'d `wine`. A full
   first-run Wine prefix bootstrap happened automatically (`wineboot`,
   `winedevice`, `rundll32` installing `wine.inf`) — real
   `system.reg`/`user.reg`/`drive_c` with the standard Windows directory
   layout, not a stub.
4. On a virtual display (`Xvfb`), a real window titled
   **"Untitled - Notepad" (721×512)** appeared — captured and sent as a
   screenshot in this session. `ntlinux-app desktop` independently
   computed the exact same app-id `ntloader` had already created for that
   file, and extracted a real icon from the binary's own resources.

**Known gap, stated plainly:** all of the above was verified on this
Ubuntu sandbox (installed Wine/Xvfb/icoutils directly, mounted
`binfmt_misc`), not by rebuilding and booting the actual NTLinux ISO —
that cycle is expensive (Phase 0's rebuilds took 13+ minutes each and hit
real infrastructure trouble along the way) and wasn't repeated here on
top of it. `distro/image/build.sh` + `airootfs/root/customize_airootfs.sh`
wire `ntloader` and `ntlinux-app` into the actual image build
(compiled/installed inside the `mkarchiso` chroot), and
`distro/image/packages.x86_64` carries the new `python`/`icoutils`
dependencies — but that wiring hasn't been re-verified with a full image
rebuild + boot test. The mechanism itself (kernel `binfmt_misc` → this
exact `ntloader` binary → Wine → a real window) is proven; only "does it
survive being baked into a freshly booted Arch image" is unverified.

---

## Phase 2 — NT ABI prototype 🟨 (object manager done and verified; ntdll integration not attempted)

Deliver `libntabi`, `ntd`, a versioned protocol, and basic handles/events/
mutexes/semaphores/sections/virtual-memory ops. Route selected `ntdll`
functionality through it.

**Success criterion:** Wine test suites continue passing while some core NT
operations no longer use the normal Wine (wineserver) path. **Not met yet
— stated plainly below, not glossed over.**

**Task breakdown:**

- [x] Versioned protocol (`ntabi/protocol/ntabi_protocol.h`) — a POSIX
      shared-memory segment with a submission ring and per-slot completion
      semaphores (Rule 13: shared memory over chatty RPC), gated by
      `NTABI_PROTOCOL_VERSION` (Rule 12). Verified for real: corrupted a
      live segment's version field and confirmed the client refuses to
      connect rather than misbehaving.
- [x] `libntabi` (`ntabi/lib/`) — client library implementing the full
      request/response cycle: allocate a slot, submit, genuinely block
      (`sem_wait`, not polling) until `ntd` responds.
- [x] `ntd` (`ntd/ntd.c`) — single-threaded, event-driven object-manager
      daemon. Event (auto-reset and manual-reset), Mutant, Semaphore;
      create/open/close with reference counting; real wait/signal
      semantics, including cross-process blocking waits with correct
      wake-up ordering (FIFO) and timeouts. Two documented Gen2
      simplifications: a flat handle space and a flat name→object map,
      not real per-process handle tables or the `\BaseNamedObjects`
      namespace hierarchy (`docs/ARCHITECTURE.md` section 6) — noted in
      `ntd.c`'s header and `ntd/objects/README.md`, not silent.
      **Sections and virtual-memory ops are not implemented** — Event/
      Mutant/Semaphore/handles/waits only, matching what the protocol
      header scopes as Phase 2.
- [x] Tests (`ntabi/tests/test_ntabi.c`, Rule 11) — 23 checks, **all
      passing**, run for real in this session (not just written):
      auto-reset vs. manual-reset event semantics, semaphore counting
      (including the max-count rejection case), name collision/not-found/
      type-mismatch handling, the protocol-version guard, and two
      cross-process tests that matter most: a real event wait and a real
      mutant ownership hand-off, each verified to *actually block*
      (elapsed time measured, ~200ms as expected) rather than busy-poll
      or race a signal from a separate process.
- [ ] **Route selected `ntdll` functionality through it — not attempted.**
      This is the one item genuinely not done, and worth being direct
      about why: it means patching Wine's actual `ntdll` (its sync
      implementation lives in `dlls/ntdll/unix/sync.c` upstream) to call
      into `libntabi` for some operations, then rebuilding Wine from
      source and running *its own* test suite to confirm nothing broke —
      a substantially larger undertaking than writing the protocol/daemon
      themselves (a real Wine source checkout, a full build — 30-60+
      minutes and gigabytes of dependencies — and validating against
      Wine's own tests). That's real, separate integration work for a
      dedicated pass, not a corner that was cut here by accident. Knowing
      the *complete* NT-native surface to route (not just the handful
      `ntabi` covers today) up front, from real Windows ground truth
      rather than discovering gaps one crash at a time, is what
      `tooling/compat-db/ntexports/` is for — see its README. This item
      now has a real home: Phase 12 bundles it with the two NT object
      types (Thread, APC) that are themselves blocked on it existing.

**What Phase 2 actually proves, stated precisely:** the protocol, the
daemon, and the client library are real and correct — genuine NT object
semantics (not POSIX primitives wearing NT names), enforced across real
process boundaries, with a real blocking-wait implementation and a real
versioned-protocol guard. What it does *not* yet prove: that any of this
sits behind a real `ntdll` boundary, or that a real Windows/Wine
application's synchronization calls could be transparently routed through
it without behavior changing. That's the honest boundary of "prototype."

---

## Phase 3 — Object and wait semantics 🟨 (everything concretely implementable is done and verified; two items explicitly deferred with reasoning)

Move named objects, handle tables, wait-any/wait-all, sections, shared
memory, process/thread metadata, completion ports, APC support toward
NTLinux-native implementation.

**Success criterion:** measured reduction in wineserver IPC traffic.
**Not measurable yet — same reason as Phase 2's gap:** there's no real
Wine process routing any traffic through `ntabi` yet (that needs the
ntdll integration Phase 2 explicitly didn't attempt), so there is no
wineserver IPC to measure a reduction *against*. Stated plainly, not
glossed over — same honest boundary as Phase 2.

**Task breakdown:**

- [x] **Real per-process handle tables** — replaced Phase 2's flat handle
      space (its single most-flagged simplification). `(owner_pid,
      local_handle) -> object`: two processes can each validly hold
      "handle 1" pointing at two completely different objects; using
      another process's handle number gets `INVALID_HANDLE`; closing one
      process's handle never disturbs another's numerically-identical
      one. All real NT semantics this project's own Phase 2 README
      flagged as missing. Verified live: a test that deliberately
      constructs the numeric-collision case and confirms isolation both
      directions.
- [x] **Wait-any / wait-all** — `ntabi_wait_multiple` (up to
      `NTABI_MAX_WAIT_HANDLES` = 16 handles at once; real NT's
      `MAXIMUM_WAIT_OBJECTS` is 64, 16 is a documented prototype-scoped
      limit). Wait-any reports which handle was satisfied and consumes
      only that one; wait-all only returns once *every* handle is
      simultaneously satisfiable, consumed together. Verified live and
      timed: a wait-any correctly reports index 1 of 3 when only the
      middle event is signaled; a wait-all provably does *not* return
      after the first of two events is signaled, only after the second
      arrives ~150ms later.
- [x] **Sections / shared memory** — real POSIX shared memory, not a
      simulation: `ntd` creates the backing `shm_open` object and hands
      its name back; `ntabi_map_view_of_section` maps it *client-side*,
      no further daemon round-trip needed since any process holding the
      name can map the same memory independently. Verified live: one
      process writes through its mapping, a second process (which opened
      the section by name) reads exactly those bytes and writes back
      through *its own* mapping, and the first process sees that
      write — genuinely shared memory, not two independent copies.
- [x] **Completion ports** — a wait-capable FIFO queue object:
      `PostCompletion`/`RemoveCompletion`-equivalents, direct hand-off to
      a waiting dequeuer when one exists, otherwise queued. No real file/
      socket I/O exists in this prototype to drive it automatically —
      posting is directly callable, standing in for what completed I/O
      would normally trigger. Verified live: three posted completions
      dequeue in FIFO order with correct payloads, and a `RemoveCompletion`
      blocked in a separate process unblocks with the right payload only
      once this process posts.
- [x] **Process objects** — signaled (and stays signaled) once a target
      pid exits, detected via `pidfd` polling on `ntd`'s existing 50ms
      tick (no dedicated thread). Chosen specifically because `pidfd`
      polling works for *any* permitted pid, not just `ntd`'s own
      children, unlike `waitpid`/`waitid`. Verified live against a
      process that is a child of the *test harness*, not of `ntd` —
      proving the mechanism actually generalizes, not just working by
      coincidence on a child.
- [ ] **Thread objects and APC support — moved to Phase 12, not
      abandoned.** Both need a real thread execution context that only
      exists once real Windows/Wine thread creation routes through
      `ntabi` — the same ntdll integration Phase 2 deferred. Rather than
      leave them as permanently-unchecked items sitting in a phase
      they can't actually be finished in, they now have a real home:
      Phase 12 bundles them with the ntdll integration work they're
      blocked on, so all three land together once that integration
      exists. See Phase 12 below. Not counted toward this phase's own
      completion — kept visible here as a pointer, not silently dropped.

**Verified live, end to end, in this session:** 38/38 test checks passing
(`ntabi/tests/test_ntabi.c`, `make check`), including six genuinely
cross-process tests (handle isolation in both directions, wait-any,
wait-all, shared-memory section read *and write-back*, blocked
completion-port dequeue, and process-exit detection for a non-child
process) — each measuring real elapsed time to confirm actual blocking,
not a lucky race. Three real bugs found and fixed by actually running the
suite, not by inspection — a stale daemon process from an earlier crashed
run left the shared-memory name held (test-harness cleanup issue, not a
code bug); a missing `NTABI_OP_OPEN_COMPLETION` opcode meant a completion
port had no valid way for a second process to reach it by name (real
daemon gap, fixed); and `#define NTABI_PROTOCOL_VERSION 2u` made the
shared-memory segment's own *name* literally end in `v2u`, because
`#x` preprocessor stringification includes a literal's type suffix —
caught from `ntd`'s own startup banner. Also fixed opportunistically: `ntd`
didn't sweep remaining objects on a clean shutdown, leaking a Section's
real backing `/dev/shm` entry if nothing had explicitly closed its
handle — noticed as a leftover file after a fully-passing test run, not
a test failure.

---

## Phase 4 — ReactOS driver-cell prototype 🟨 (protocol/transport/launcher/driver all built and verified; not yet loaded inside a running ReactOS kernel)

Deliver a minimal bootable ReactOS image, KVM launcher, `ntbridge`, logging
channel, host heartbeat, device enumeration bridge.

**Success criterion:** ReactOS sees synthetic devices supplied by Linux.
**Met against a stand-in for the ReactOS side, not yet against real
ReactOS — stated precisely below, not glossed over.**

**Task breakdown:**

- [x] `ntbridge` versioned protocol (`driver/ntbridge/protocol/
      ntbridge_protocol.h`) — heartbeat counters plus three lock-free
      SPSC rings (log/PnP/PnP-ack) built on volatile reads/writes and
      C11 `__atomic` fences, not a mutex/futex, because there is no
      shared kernel across a VM boundary to provide one (unlike
      `ntabi`'s POSIX semaphores). Gated by `NTBRIDGE_PROTOCOL_VERSION`
      (Rule 12) — the header calls out the `NTABI_PROTOCOL_VERSION 2u`
      stringification bug from Phase 3 directly in a comment so it
      isn't repeated.
- [x] `ntbridge-host` (`driver/ntbridge/host/`) — the Linux-side daemon:
      owns the `ivshmem-plain` backing file, seeds synthetic device
      descriptors (device enumeration bridge — a fixed list today, one
      function to swap for real sysfs/udev/netlink discovery later),
      drives the host heartbeat, drains the logging channel. Exit
      status is a real pass/fail oracle: 0 only if the guest heartbeat
      was observed *and* every seeded device was acknowledged.
- [x] `ntcell` (`driver/cell/launcher/`) — a real QEMU launcher (this
      is the "KVM launcher" deliverable; falls back to `-accel tcg`
      since this sandbox has no `/dev/kvm` — the same documented
      degrade-gracefully pattern as Phase 0's `/dev/ntsync` check, KVM
      picked automatically on real hardware). Two subcommands:
      `boot-reactos` (the real ReactOS ISO) and `boot-testguest` (the
      ntbridge stand-in guest, below).
- [x] Minimal bootable ReactOS image — `driver/cell/images/
      fetch-reactos-iso.sh` downloads the real upstream ReactOS 0.4.15
      release ISO (ADR-0002/Rule 17: consume, don't vendor ReactOS
      source just to get a bootable image). **Known gap, moved to Phase
      13, not abandoned:** this is the stock general-purpose ReactOS ISO
      (full desktop shell), not yet the stripped driver-only ROS-NTCELL
      profile ARCHITECTURE.md section 15 describes — that needs building
      ReactOS **itself** from source with RosBE. See Phase 13.
- [x] ReactOS-side `ntbridge` driver (`driver/ntbridge/reactos/
      ntbridge_pnp.c`) — a real WDM bus driver written against actual
      ReactOS DDK conventions (`DriverEntry`/`AddDevice`, IRP_MJ_PNP
      dispatch, `MmMapIoSpace` over translated PnP resources,
      `IoCreateDevice`/`IoInvalidateDeviceRelations` for child PDOs).
      **Correction to an earlier claim in this file:** this was
      originally marked "needs RosBE, unbuilt" — that was wrong, caught
      while starting Phase 5. RosBE is only needed to build ReactOS
      *itself* from source; a driver that merely targets ReactOS's
      (Windows-compatible) kernel ABI needs only a DDK header set and an
      import library for `ntoskrnl.exe`/`hal.dll` — which mingw-w64
      already ships (`/usr/x86_64-w64-mingw32/include/ddk/`,
      `libntoskrnl.a`, `libhal.a`), the same toolchain
      `tooling/compat-db/` already uses. **Now actually built**: compiles
      clean (`-Wall -Wextra`, zero warnings after fixing one multichar
      pool-tag literal) and links into a real PE32+ `native`-subsystem
      `.sys` via that toolchain — see `driver/ntbridge/reactos/Makefile`.
      Two real bugs remain, still flagged directly in its own comments,
      now that a real build can actually surface them: a missing
      wait-for-lower-IRP-completion in `IRP_MN_START_DEVICE`, and
      `IoCreateDevice` called from DISPATCH_LEVEL inside the poll DPC
      (needs a deferred work item instead) — building successfully
      doesn't mean these are fixed, only that they're now real,
      inspectable bugs in a real binary rather than hypothetical ones in
      unbuilt source. **Still not loaded inside a running ReactOS
      kernel** — that's a different, remaining gap (needs driving
      ReactOS's actual boot/install process, not a toolchain gap) from
      the "can't even compile" gap this correction resolves. **Moved to
      Phase 14**, consolidated there with the same gap in `vsdev.c`'s
      PnP path (Phase 5) and `ntnet.c` (Phase 7) — all three need the
      same missing capability (a real PnP-triggered install flow), worth
      attempting together rather than three separate one-off efforts.
- [x] Honest stand-in guest test (`tests/reactos/
      ntbridge-guest-test.c`) — since the real ReactOS driver above
      can't be built here, this implements the *guest side of the
      identical protocol* as a statically-linked Linux userspace
      program (finds the ivshmem PCI device via sysfs, `mmap`s BAR2
      directly, speaks heartbeat/logging/PnP-ack) — proving the wire
      contract genuinely works across a real QEMU VM boundary, not that
      the real driver is correct. See `tests/reactos/README.md` for the
      full accounting of what is and isn't proven by this substitution.
- [x] Tests (`tests/reactos/run-test.sh`, Rule 11) — builds every
      component, runs `ntbridge-host` against `ntbridge-guest-test`
      over a real QEMU VM boundary, asserts pass/fail from the host
      daemon's exit status. **Verified live**, most recent run: guest
      heartbeat detected after ~7.8s, all 3 seeded synthetic devices
      ARRIVED and ACKed, 4 guest log lines received host-side,
      `ntbridge-host` exited 0.
- [x] `ntcell boot-reactos --screenshot` verified separately: a real
      QMP screendump of ReactOS 0.4.15's Setup "Language Selection"
      screen, booted under QEMU/TCG by the same launcher — genuine
      ReactOS kernel output, proving the launcher boots real ReactOS,
      independent of the ntbridge stand-in test above.

**What Phase 4 actually proves, stated precisely:** the ntbridge wire
protocol, its SPSC ring transport, the host daemon, and the QEMU
launcher (including real `ivshmem-plain` PCI device attachment reachable
from inside a genuinely separate guest kernel) are all real and
verified end-to-end. A real, unmodified ReactOS kernel boots under the
same launcher. The ReactOS-side driver (`ntbridge_pnp.c`) now compiles
and links into a real `.sys` too. What it does **not** yet prove: that
`ntbridge_pnp.c` *behaves correctly once actually loaded and running*
inside a booted ReactOS kernel (its two flagged bugs are exactly the
kind of thing that would surface there) — that needs driving ReactOS's
real boot/install/driver-load process, which Phase 5 takes a first,
simpler run at with a standalone test driver before this one.

### Re-verified on a genuinely different host: real QEMU on Windows, not just the original Linux/TCG sandbox

Every `ntcell`/QMP result above was first established in this
project's original Linux development sandbox. This session ran on a
real Windows 11 host that turned out to already have a full, real QEMU
11.0.0 install (`C:\Program Files\qemu`) — checked directly rather than
assumed absent, per Rule 1. Fetched a fresh ReactOS x64 nightly LiveCD
(`driver/cell/images/fetch-reactos-x64-nightly.sh`, unmodified) and ran
`ntcell boot-reactos` against it on this host, for real.

**One real, narrow, cross-platform bug found and fixed, not worked
around:** the Python actually present on this Windows host has no
`socket.AF_UNIX` at all (`AttributeError`, not a path/permissions
issue) — `qmp_screendump.py`/`qmp_console.py`'s unix-socket QMP
connection is simply unusable there. Fixed narrowly: `ntcell` now
detects a Windows host (MSYS/Git-Bash/Cygwin `uname`) and uses a TCP
loopback QMP/monitor endpoint instead of a unix socket in that case
only; both Python helpers now accept either a `tcp:HOST:PORT` or the
original unix-socket-path endpoint. Linux behavior (unix sockets) is
completely unchanged — this is a real portability fix, not a
Windows-only hack bolted on top.

**Verified live, screenshots committed as real evidence** (same
standard `driver/vsdev/screenshots/` already established for this
project):

![ReactOS x64 nightly booting under real QEMU on Windows - PnP device install in progress](screenshots/windows-qemu-boot-progress.png)

A genuine mid-boot frame — ReactOS actually enumerating/installing
devices, not a static splash. Continuing to boot reaches the real
LiveCD language-selection dialog, the same screen this project's
original Linux sandbox first captured:

![ReactOS LiveCD language dialog, real QEMU on Windows](screenshots/windows-qemu-livecd-dialog.png)

Went further than any previous pass: kept this QEMU instance alive and
drove it interactively over the live TCP QMP connection with
`qmp_console.py click`/`sendkey`, the same primitives `driver/vsdev/
run-test.sh` already established for Phase 5. A synthetic click landed
precisely on the dialog's "Next" button (visible focus rectangle,
cursor exactly on target):

![Next button focused after a real synthetic QMP click](screenshots/windows-qemu-livecd-dialog-clicked.png)

Sending a synthetic Enter key genuinely advanced the wizard to a real,
fully rendered ReactOS desktop — taskbar, Start button, system tray
clock, desktop icons:

![A real, live ReactOS x64 desktop, reached via synthetic QMP input on this Windows host](screenshots/windows-qemu-desktop-live.png)

**An honest gap, not glossed over:** attempted to go one step further
and open the desktop's Command Prompt icon (double-click, then a
select-then-Enter fallback) to get a live `cmd.exe` session the way
Phase 5's driver test does. Both attempts only *selected* the icon —
no window opened in either try, in the time observed:

![Command Prompt icon selected but not launched - a real, unresolved finding](screenshots/windows-qemu-desktop-cmd-icon-selected.png)

Not chased further this pass (budget) — plausibly the same class of
desktop-focus/timing flake `driver/vsdev/README.md`'s "Known gaps"
already documents for `dismiss-dialog`, not investigated to a root
cause here. Also not attempted on this host: `driver/vsdev/run-test.sh`
itself (needs `x86_64-w64-mingw32-gcc` and `mtools`, both confirmed
absent from this Windows host — a real, separate toolchain gap,
correctly left alone rather than papered over by installing a new
compiler unprompted).

**What this proves, stated precisely:** the `ntcell` launcher, real
ReactOS boot, and QMP-driven synthetic input are now confirmed to work
on a second, genuinely different host/OS/QEMU-build combination, not
just the one sandbox that originally verified them — with one real,
narrow, now-fixed cross-platform bug found along the way. It does not
newly prove anything about `ntbridge_pnp.c` or Phase 14's PnP-install
flows specifically; the Command Prompt gap above shows real
desktop-level UI automation on Windows still has an unresolved rough
edge.

---

## Phase 5 — First Windows driver 🟨 (loads and performs real I/O, verified live; PnP-triggered load and ntbridge routing not yet attempted)

Start with a simple virtual/low-risk device (virtual serial, virtual block,
simple USB, virtual network adapter). Avoid GPU drivers initially.

**Success criterion:** a real Windows `.sys` loads, receives PnP IRPs, and
performs I/O through the host bridge.
**Loads and performs real I/O — verified live, twice, on a fresh boot.
PnP IRPs and host-bridge routing not yet attempted — stated precisely
below.**

### A real finding along the way: RosBE isn't needed for drivers at all

Starting this phase meant first correcting Phase 4: `driver/ntbridge/
reactos/ntbridge_pnp.c` was marked "unbuilt, needs RosBE." That was
wrong. RosBE only builds ReactOS **itself** from source; a driver that
merely targets ReactOS's (Windows-compatible) kernel ABI needs just a
DDK header set and `ntoskrnl.exe`/`hal.dll` import stubs — which
**mingw-w64 already ships** (`/usr/x86_64-w64-mingw32/include/ddk/`,
`libntoskrnl.a`, `libhal.a`). Confirmed by actually compiling and
linking both `ntbridge_pnp.c` and this phase's new driver into real
`.sys` files with that toolchain — see Phase 4's corrected entry and
`driver/ntbridge/reactos/README.md`. RosBE is real work this project
still hasn't done, but it's scoped much narrower than previously
stated — see Phase 13.

### `driver/vsdev/` — the actual first driver

- [x] `vsdev.c`/`.h`/`.inf` — a minimal virtual serial-style loopback
      device: `IoCreateDevice` + `IoCreateSymbolicLink`
      (`\\.\NTLVSER0`), `IRP_MJ_CREATE`/`CLOSE`/`READ`/`WRITE` with a
      real fixed-buffer loopback (write stores, read returns-and-
      consumes — genuine semantics, not a stub returning success), full
      `IRP_MJ_PNP` dispatch (`IRP_MN_START_DEVICE` et al.) for a real
      PnP/Root-enumerated install via `vsdev.inf`.
- [x] Builds clean (`-Wall -Wextra`, zero warnings) and links into a
      real PE32+ `native`-subsystem `.sys` — `driver/vsdev/Makefile`,
      same mingw-w64 DDK toolchain as `ntbridge_pnp.c`.
- [x] **A real bug found only by actually loading it, not by
      inspection:** `sc start` against a plain `type= kernel` service
      (no INF/Root-enumeration install) uses NT's *legacy* driver-load
      path — it calls `DriverEntry` and nothing else. `AddDevice` only
      fires from real PnP enumeration. `vsdev.c` originally created its
      device object solely from `AddDevice`, so it loaded successfully
      (`STATE: RUNNING`) but was completely invisible/unusable —
      confirmed live before the fix (screenshot below, "system cannot
      find the file specified"). Fixed with the standard legacy-driver
      pattern: `DriverEntry` now also creates the device directly
      (`VsdevCreateDeviceObject`, shared with `AddDevice`), and
      `VsdevUnload` now cleans up a legacy-created device itself, since
      it never receives `IRP_MN_REMOVE_DEVICE`.
- [x] Architecture correction, also found live, not assumed: ReactOS
      0.4.15's stable release is x86 (32-bit) only — no x64 variant
      exists at that version (confirmed: neither SourceForge nor
      reactos.org lists one). The 64-bit mingw-built driver failed to
      load against it (`Error 193: %1 is not a valid Win32
      application`) — a real architecture mismatch, not a bug in the
      driver. Fixed by using a ReactOS **x64 nightly build**
      (`reactos-livecd-0.4.17-dev-706-g5912119-x64-msvc-win-dbg`, from
      `iso.reactos.org`) instead, matching the driver's actual
      architecture rather than cross-compiling 32-bit to match a stable
      release that doesn't have an x64 counterpart.
- [x] `vsdev_test.c` — a small userspace client (`CreateFile`/
      `WriteFile`/`ReadFile` against `\\.\NTLVSER0`, real `GetLastError`
      reporting) used instead of `cmd.exe`'s `>`/`type` shell
      redirection, which turned out to have its own quirks against a
      raw device path in this environment.

**Verified live**, on a clean ReactOS x64 boot, files delivered via a
QEMU floppy image (`mtools`) since the LiveCD's RAM disk had 0 bytes
free:

```
X:\...\Desktop>sc create vsdev type= kernel binPath= A:\VSDEV.SYS
[SC] CreateService SUCCESS
X:\...\Desktop>sc start vsdev
SERVICE_NAME: vsdev
        STATE              : 4  RUNNING
```

![vsdev service running inside real ReactOS](driver/vsdev/screenshots/service-running.png)

```
X:\...\Desktop>A:\VSDTEST.EXE
CreateFile OK, handle=0000000000000390
WriteFile OK, wrote 20 bytes
ReadFile OK, read 20 bytes: "Hello NTLinux Phase5"
Loopback round-trip: PASS
```

![real I/O round-trip through vsdev.sys: PASS](driver/vsdev/screenshots/io-roundtrip-pass.png)

**What Phase 5 actually proves, stated precisely:** a real, hand-written
NTLinux Windows driver compiles, links, loads, and performs genuine I/O
inside a real, freshly-booted ReactOS kernel — reproduced from a clean
boot after the legacy-load fix, not a one-off. What it does **not** yet
prove: `IRP_MN_START_DEVICE` firing from real PnP enumeration (this was
loaded via the legacy `sc start` path, which `vsdev.c` now also
supports directly — the PnP/`AddDevice`/`vsdev.inf` path exists in the
same file but hasn't been exercised live, same category of gap as
`ntbridge_pnp.c`'s own unexercised PnP path), and "performs I/O through
the host bridge" — `vsdev`'s loopback buffer is entirely local to the
driver, not yet routed through `ntbridge`. Both are real next steps, not
silently assumed done. **The PnP-load gap is moved to Phase 14**,
consolidated with the matching gap in `ntbridge_pnp.c` (Phase 4) and
`ntnet.c` (Phase 7). The host-bridge-routing gap stays here, unmoved —
it's `vsdev`-specific, not a PnP-install gap.

See `driver/vsdev/README.md` for the full account, including the
QMP-driven (screenshot + synthetic keyboard) automation method used to
drive the ReactOS console for this verification.

**Reconfirmed in a later full-repo verification pass** (ROADMAP.md's
Phase 9/10 sweep): re-running `run-test.sh` reproduced the exact same
real result byte-for-byte (`Loopback round-trip: PASS`, the same
"Hello NTLinux Phase5" content) — the driver's behavior never changed.
Getting there required real fixes to `run-test.sh` itself, though: it
failed 3 times in a row with an empty result before the actual bug
(a fixed-timing dialog-dismissal sequence that could miss the LiveCD's
language dialog on a slower boot) was found and fixed — see
`driver/vsdev/README.md`'s "Known gaps" for the full diagnosis and the
two further real bugs the fix itself went through before it held up
under a fully unattended run. Stated plainly: the test automation had
bugs the driver didn't.

---

## Phase 6 — VFIO hardware passthrough ⬜ (explicitly deferred — see below, not silently skipped)

Add PCI BAR mapping, MSI/MSI-X delivery, DMA mappings, device reset, IOMMU
protection, resource descriptors.

**Success criterion:** a selected real Windows PCI driver operates physical
hardware while Linux stays stable if the cell crashes.

**Why this phase is out of order in this ROADMAP** (Phase 7 done before
it): checked this sandbox directly before starting, rather than
assuming — `/dev/vfio` doesn't exist, `/sys/kernel/iommu_groups` is
empty, no `vmx`/`svm` CPU flags are exposed (confirms the earlier
no-`/dev/kvm` finding from Phase 4), and the kernel cmdline carries
`nomodule` (module loading disabled outright). This sandbox is a
Firecracker microVM, which has no PCI bus or virtual IOMMU at all by
design (virtio/MMIO only) — not a missing tool or a config gap, an
architectural absence. Every prior phase's "gap" had a genuine
software-only substitute (Wine's ntdll standing in for real Windows, a
ReactOS nightly build for a missing variant, mingw+DDK instead of
RosBE); this phase's success criterion is literally "operates *physical
hardware*" — there is no honest substitute for physical hardware behind
an IOMMU. Rather than write untestable host-side VFIO code with no path
to verification in any sandbox shaped like this one, this phase stays
an explicit, documented blocker, and Phase 7 (NDIS bridge) — whose
success criterion doesn't require physical hardware at all, a virtual
NIC bridged to a Linux TAP device satisfies it honestly — was tackled
next instead. Revisit this phase on real hardware, or an environment
that exposes IOMMU/VFIO to nested virtualization.

---

## Phase 7 — NDIS bridge 🟨 (protocol/transport/host-TAP-bridge verified end-to-end; real NDIS miniport written and linked, not yet loaded inside a running ReactOS kernel)

**Success criterion:** a Windows NIC driver exposes a working Linux network
interface.
**Met against a stand-in for the ReactOS side (same pattern Phase 4
established), with a real Linux TAP device on the other end — stated
precisely below, not glossed over.**

### `driver/net/reactos/ntnet.c` — a real NDIS 5.1 miniport

- [x] Full legacy-characteristics Ethernet miniport: `DriverEntry` →
      `NdisMRegisterMiniport` with `NDIS51_MINIPORT_CHARACTERISTICS`,
      `NtnetInitialize` (maps the same ivshmem BAR2 region
      `ntbridge_pnp.c`/`vsdev.c` do, via `NdisMQueryAdapterResources` +
      `NdisMMapIoSpace`, Rule 12 magic/version check), `NtnetSend`
      (copies an outgoing packet into `net_tx_ring`), a polling timer
      (`NtnetPollTimer`, 20ms — same "no interrupt on ivshmem-plain, so
      poll" pattern as `ntbridge_pnp.c`) that drains `net_rx_ring` and
      hands frames up via the classic `NdisMEthIndicateReceive` path,
      plus the full mandatory generic + 802.3 OID surface
      (`OID_GEN_SUPPORTED_LIST` through the statistics counters) with
      real values, not placeholders — traffic counters are live counts
      updated by `NtnetSend`/`NtnetPollTimer`.
- [x] **Three real upstream header bugs found and fixed, not guessed at
      or worked around with more macros** — building an NDIS 5.1
      miniport against mingw-w64's bundled DDK (ReactOS's own
      `ndis.h`/`wdm.h`, repackaged) surfaced genuine bugs, confirmed
      present regardless of which `NDIS*_MINIPORT` version macro was
      selected:
      1. `ndis.h` re-`typedef`s `enum _NDIS_REQUEST_TYPE` in its own
         body despite already `#include`-ing `ntddndis.h`, which
         defines the byte-for-byte identical enum — a genuine
         duplicate-typedef compile error.
      2. `ndis.h`'s `NdisMWanIndicateReceiveComplete()` prototype is
         missing a comma between its two parameters — a literal typo.
      3. `wdm.h` references `SYSTEM_POWER_STATE_CONTEXT` under a guard
         spelled `NTDDI_VERSION >= NTDDI_WINVISTA`, but the struct is
         only *defined* under the correctly-spelled `NTDDI_VISTA` —
         `NTDDI_WINVISTA` doesn't exist anywhere in mingw-w64's
         `sdkddkver.h`, so the preprocessor silently treats it as `0`
         and the guard is always true, referencing an undefined type
         below Vista-level targets (exactly this driver's NDIS
         5.1/WinXP-level target, matching ReactOS's own reported NT 5.2
         compatibility).
      `driver/net/reactos/prepare-ndis-header.sh` generates locally-
      patched copies of both headers into `./build/` — the system
      install is never touched; each patch is a narrow, exact,
      documented substitution, not a broad rewrite.
- [x] Builds clean (`-Wall -Wextra`, two harmless pre-existing macro-
      redefinition warnings) and links into a real PE32+
      `native`-subsystem `.sys` via `-lndis -lntoskrnl -lhal`.
      `ntnet.inf` installs it against the ivshmem PCI hardware ID under
      the `Net` device class.

### Protocol extension and host bridge

- [x] `ntbridge_protocol.h` bumped to **v2** (Rule 12 — a real
      wire-format change): `net_tx_ring`/`net_rx_ring`, each slot a
      whole raw Ethernet frame (`ntbridge_net_frame_t`,
      `NTBRIDGE_NET_FRAME_MAX` = 1514 bytes) — simplicity over
      throughput for a first cut, matching the existing rings' "whole
      record per slot" design. Backing shm size bumped 1 MiB → 4 MiB to
      fit both rings with real headroom.
- [x] `ntbridge-host`'s new `--tap IFNAME` mode: opens/creates a real
      Linux TAP device (`/dev/net/tun`, `IFF_TAP|IFF_NO_PI`) and bridges
      it to the two rings — guest-sent frames get written straight to
      the TAP fd; frames Linux sends out that interface get read off
      the fd and pushed into `net_rx_ring`. No protocol translation
      either way — raw Ethernet frames, matching ARCHITECTURE.md
      section 22's design ("NTLinux does not need to route ordinary
      host networking through the Windows TCP/IP stack"). Polls at a
      tighter 10ms tick than the other rings while `--tap` is active.

### Verified live, end to end, both directions

`tests/reactos/ntbridge-guest-test.c` (the same honest stand-in Phase 4
established for `ntbridge_pnp.c`) now also pushes a tagged synthetic
frame into `net_tx_ring` and watches `net_rx_ring` for a distinctly-
tagged reply. `driver/net/reactos/run-test.sh` orchestrates the full
test: `ntbridge-host --tap`, `tests/reactos/net-tap-echo.py` (a plain
`AF_PACKET` raw-socket listener on that TAP interface — genuinely what
any ordinary Linux network application would see there, not a special
test hook), and the guest test client under `ntcell boot-testguest`.
Most recent run:

```
[ntbridge-guest-test] net: pushed test frame to net_tx_ring
[ntbridge-guest-test] net: received expected test frame on net_rx_ring - PASS
net-tap-echo.py: captured guest frame (39 bytes) - contains b'NTLXNETTEST-GUEST-TO-HOST': PASS
net-tap-echo.py: host->guest frame sent
ntbridge-host: summary — guest heartbeat: yes, log lines received: 3, devices acked: 0/0,
  TAP frames host->guest: 1, guest->host: 1
guest->host (captured on real Linux TAP interface via raw socket): PASS
host->guest (guest confirmed receipt via net_rx_ring, seen in ntbridge-host's log): PASS
PASS: NDIS bridge network round-trip verified both directions over a real QEMU VM boundary
  and a real Linux TAP device.
```

**A real bug found only by running this, not by inspection:** bumping
the shm region to 4 MiB (for the new rings) broke the guest test
client's `mmap()` of the ivshmem BAR2 with `EINVAL` — `driver/cell/
launcher/ntcell`'s QEMU invocation had a hardcoded `SHM_SIZE_MB=1` that
needed updating to match the new `NTBRIDGE_SHM_DEFAULT_SIZE`; fixed,
with a comment flagging the cross-language duplication (a bash script
can't `#include` the C header) for whoever changes the size next.

**What Phase 7 actually proves, stated precisely:** the ntbridge
network transport — wire format, ring protocol, and the host-side TAP
bridge — is real and correct, verified over a genuine QEMU VM boundary
and a real Linux netdev, not same-process and not mocked. What it does
**not** yet prove: that `ntnet.c`, running as an actual NDIS miniport
inside a real, booted ReactOS kernel, behaves correctly — NDIS adapter
installation (Add Hardware wizard / Device Manager "Update Driver") is
a materially larger live-verification task than `driver/vsdev/`'s
single `sc start`, out of reach for this pass's budget. Same honest
boundary Phase 4 drew around `ntbridge_pnp.c`. **Moved to Phase 14**,
consolidated with the matching gap in `ntbridge_pnp.c` (Phase 4) and
`vsdev.c`'s PnP path (Phase 5).

---

## Phase 8 — USB driver bridge 🟨 (protocol/transport/host-echo verified end-to-end; real bridge driver written and links, not a full HC miniport, not yet loaded inside a running ReactOS kernel)

**Success criterion:** a vendor Windows USB driver operates a device
unavailable through a native Linux driver.
**Met against a stand-in for the ReactOS side (same pattern Phases 4/7
established), with a genuine transport verified over a real QEMU VM
boundary — stated precisely below, not glossed over.**

**Sandbox check first, not assumed:** confirmed directly that this
sandbox has no USB bus at all (`/sys/bus/usb/devices` doesn't exist, no
`lsusb`) and — checked further than Phase 6's VFIO finding — no USB
*subsystem* in the kernel at any level: `/proc/config.gz` shows
`CONFIG_USB` itself is not set (no usbcore, no host controller driver,
no usbfs), `CONFIG_USB_GADGET` is not set either (so not even a
software-only virtual device via `dummy_hcd`, the trick that would have
mirrored Phase 7's TAP-device substitute), and `CONFIG_MODULES is not
set` (compiled out entirely, not just cmdline-disabled — stronger than
Phase 6's `nomodule` finding). No path to any USB device, real or
virtual, in this sandbox. Same "genuine architectural absence" category
as Phase 6, met the same way Phase 7 met Phase 6's absence for
networking: find the piece that *is* honestly testable here (the
protocol/transport/host bridge) and build and verify that for real,
while stating plainly what stays out of reach.

### Not a virtual USB Host Controller Driver — checked, not assumed

The literal success criterion ("a vendor Windows USB driver operates a
device") is most faithfully met by a virtual HCD: any pre-existing
vendor `.sys` sits on `usbhub.sys` sits on a HC miniport registered
with `usbport.sys`, never knowing NTLinux is underneath — the same
shape `ntnet.c` achieves for NDIS. Checked directly before committing
to a design: mingw-w64's DDK ships `usb.h`/`usbioctl.h` (the real
`URB`/`IOCTL_INTERNAL_USB_SUBMIT_URB` surface a client driver *above* a
bus uses) but **no `usbport.h`** anywhere under
`/usr/x86_64-w64-mingw32/include` — the internal HC-miniport interface
genuinely isn't in this toolchain's DDK, unlike the RosBE claim Phase 5
corrected (that one was wrong; this one is real, confirmed by direct
search). So Phase 8 builds a WDM **bus driver** instead — same shape as
`ntbridge_pnp.c` — that creates one child PDO for a synthetic
vendor-class USB device and answers `IOCTL_INTERNAL_USB_SUBMIT_URB`
directly. Not the general "any driver just works" bridge a real HC
miniport would be, but the concrete step short of one — see
`driver/usb/README.md` and `driver/usb/reactos/README.md` for the full
account.

### `driver/ntbridge/protocol/ntbridge_protocol.h` — bumped to v3

Added `usb_req_ring`/`usb_resp_ring` (Rule 12 — real wire-format
change): whole bulk-transfer payloads, one ring per direction, same
simplicity-over-throughput tradeoff `net_tx_ring`/`net_rx_ring` made in
Phase 7. Descriptors stay local to `ntusb.c` — there's no real hardware
behind them for Linux to be authoritative over. Learning directly from
Phase 7's own hardcoded-`SHM_SIZE_MB` bug: this time the struct's actual
size was checked by compiling and printing `sizeof(ntbridge_shm_header_t)`
*before* shipping the bump, not just hand-computed — came to 728.7 KiB,
comfortably inside the existing 4 MiB `NTBRIDGE_SHM_DEFAULT_SIZE`, so
(unlike Phase 7) no size bump and no `ntcell` change were needed this
time, and that was verified rather than assumed too.

### `driver/usb/reactos/ntusb.c` — the bus driver

Builds and links clean (`-Wall -Wextra`, zero warnings) via the same
mingw-w64 DDK toolchain as every driver since Phase 5. Fixes, rather
than reproduces, `ntbridge_pnp.c`'s two flagged bugs: `IRP_MN_START_
DEVICE` properly waits for the lower IRP's completion
(`IoSetCompletionRoutine` + `KEVENT`) before touching PnP-translated
resources, and the one child PDO is created synchronously from inside
that PASSIVE_LEVEL handler (safe by construction — no DISPATCH_LEVEL
`IoCreateDevice` call, and no deferred-work-item machinery needed since
this driver's topology is static, one device, never hot-plugged).

A **real bug found only by linking, not by inspection**: a 4096-byte
on-stack buffer in the test-IOCTL handler made that function's stack
frame big enough that mingw's x86_64 codegen emitted a call to
`__chkstk_ms` — `undefined reference to ___chkstk_ms` on first link.
Fixed by adding `-lgcc` to the link line (that helper lives in libgcc,
excluded by `-nostdlib` along with everything else).

**Known, pre-existing PCI hardware ID collision, now three-way:**
`ntusb.inf` matches the same ivshmem hardware ID `ntbridge.inf` and
`ntnet.inf` already do (`ntnet.inf`'s own comment already flagged the
first pair). Not new, hasn't blocked anything — each phase's driver is
verified independently, never loaded against the same cell at once. The
real fix (a distinct ivshmem instance/subsystem ID per bridge) is
documented follow-up.

### `driver/usb/reactos/ntusb_test.c` — the test-only path, and why

A real vendor client driver's `IOCTL_INTERNAL_USB_SUBMIT_URB` is
kernel-to-kernel only (`IoCallDriver` from another driver's stack) —
unreachable from ordinary usermode `DeviceIoControl`, and no real
vendor USB client driver exists in this project to test against
anyway. So `ntusb.c` also exposes a test-only
`IOCTL_NTUSB_TEST_BULK_TRANSFER` that a usermode client can call, which
internally builds a real `URB` and hands it to the *same*
`NtusbProcessUrb` dispatch function the real internal path uses — one
function, two callers, so exercising the test path really does exercise
the bridging logic a real submission would run through. This mirrors
this driver's own live-load status though: the function is written and
builds, but hasn't been exercised live yet either, since that needs
`ntusb.sys` loaded inside a booted ReactOS kernel first (see Phase 14).

### `driver/ntbridge/host/ntbridge-host`'s new `--usb-echo` mode

An honest **software-only** stand-in, stated plainly rather than
implied to be more: answers every bulk request with a fixed,
distinctly-tagged reply. Not a real bridge to physical hardware — per
the sandbox check above, there is no usbfs, no `/dev/bus/usb`, nothing
to bridge to here. The real design this stands in for (Linux `usbfs`'s
`USBDEVFS_SUBMITURB`/`USBDEVFS_REAPURB`) is real follow-up work for an
environment that actually has USB hardware — same honest-substitute
shape as Phase 7's TAP device, just without a virtual-but-genuine
option available this time (checked: no `dummy_hcd`/`CONFIG_USB_GADGET`
either).

### Verified live

Same automated end-to-end test shape as Phase 4/7:
`driver/usb/reactos/run-test.sh` starts `ntbridge-host --usb-echo`,
boots the extended stand-in guest test client
(`tests/reactos/ntbridge-guest-test.c`, now also pushing a tagged
request onto `usb_req_ring` and watching `usb_resp_ring`) under `ntcell
boot-testguest`. Real run, this session:

```
[ntbridge-guest-test] usb: pushed test request to usb_req_ring
[ntbridge-guest-test] usb: received expected reply on usb_resp_ring - PASS
[ntbridge-guest-test] guest shutting down: ... usb: sent=yes recv_confirmed=yes
ntbridge-host: usb-echo: request_id=1 endpoint=0x01 length=25
ntbridge-host: summary — guest heartbeat: yes, ... USB requests seen: 1, replies sent: 1
=== run-test.sh: result ===
USB bulk round-trip (guest request -> host echo -> guest, over a real QEMU VM boundary): PASS
PASS: USB bridge protocol/transport/host-echo verified end-to-end over a real QEMU VM boundary.
```

**What Phase 8 actually proves, stated precisely:** the USB bridge
transport — wire format, ring protocol, and the host-side echo
responder — is real and correct, verified over a genuine QEMU VM
boundary, not same-process and not mocked. What it does **not** yet
prove: that `ntusb.c`, running as an actual bus driver inside a real,
booted ReactOS kernel, behaves correctly, or that any client driver
(real or test) can bind to its child PDO and submit a real internal
URB. That gap — the same category as `ntbridge_pnp.c`, `vsdev.c`'s PnP
path, and `ntnet.c` — is tracked in Phase 14, now covering four drivers
instead of three.

---

## Phase 9 — Modern ReactOS driver compatibility 🟨 (gap probe built and run; the underlying upstream work is real, separate, out of reach in this sandbox)

Upstream work on KMDF, newer WDM, newer NT exports, modern NDIS, PnP, power,
memory management, security. Maintain conformance tests against documented
Windows behavior.

**Why this phase is scoped the way it is:** the text above is a large,
open-ended upstream research program, not a single boundable task —
genuinely different in kind from Phases 4-8's "write a driver, verify
it live" shape. Rather than write speculative "future work" prose for
it, this phase's real deliverable is the concrete prerequisite: knowing
precisely what this project's *own* DDK toolchain already supports for
each named facility, checked by actually running probes against it, not
assumed from ReactOS's documented NT-version target.

### `tooling/compat-db/ddkgap/` — the gap probe

Same family as `ntexports/` (user-mode DLL export gaps, static) and
`ntprobe/` (live Windows-side NT behavior, dynamic) — this is the
kernel-mode counterpart, checking two independently real things per
curated symbol: `abi_present` (a real `nm` check against
`libntoskrnl.a`/`libhal.a`/`libndis.a`) and `header_declared` (a real
recursive grep across the actual installed DDK headers), plus a
separate present/absent check for KMDF as a whole framework. See
`tooling/compat-db/ddkgap/README.md` for the full account, including
its stated limitations (a curated, representative symbol list, not
exhaustive; a textual header check, not a compile probe).

### Real results, this session

```
PnP: IoRegisterPlugPlayNotification, IoUnregisterPlugPlayNotificationEx,
     IoReportTargetDeviceChangeAsynchronous, IoInvalidateDeviceRelations — all full
Power: PoRegisterPowerSettingCallback, PoUnregisterPowerSettingCallback,
       PoStartNextPowerIrp — all full
Memory management: ExAllocatePoolWithTag full, MmProtectMdlSystemAddress full,
                    KeInitializeGuardedMutex full, ExAllocatePool2 absent
Security: ObRegisterCallbacks/ObUnRegisterCallbacks/CmRegisterCallbackEx full,
          SeAccessCheckEx abi-only (header gap),
          PsSetCreateProcessNotifyRoutineEx2 absent
Interrupts: IoConnectInterruptEx, IoDisconnectInterruptEx — both full
Filter Manager: FltRegisterFilter absent
NDIS 6.x: NdisMRegisterMiniportDriver, NdisMSetMiniportAttributes,
          NdisMIndicateReceiveNetBufferLists, NdisAllocateNetBufferListPool
          — all abi-only (header gap)
KMDF: not present in this toolchain at all (no wdf*.h, no Wdf01000/libwdf*.a)
```

**What this actually says, precisely:** the WDM-level PnP/power/
interrupt/most security-and-memory-management surface up through
roughly Windows 7/8 is genuinely already there — header and ABI both,
usable today with zero toolchain changes, further ahead of
ARCHITECTURE.md's stated "NT 5.2/Server 2003-ish" ReactOS baseline than
assumed going in. Three real, narrow, distinct gaps came out of this,
not one vague "needs modernizing":

1. **NDIS 6.x has real ABI but no header declarations** — the same
   reason `driver/net/reactos/ntnet.c` specifically targeted the older
   NDIS 5.1 surface (see that file's own header comment) is now a
   documented, generalizable finding, not an isolated one-off. The
   narrowest gap here — hand-declaring the missing prototypes (same
   technique `ntprobe/` already used for a few NT structures, ADR-0006)
   would unlock NDIS 6.x miniports against this exact toolchain with no
   new import library needed. Real, scoped follow-up work.
2. **KMDF is absent from mingw-w64's packaging, but the real headers
   fetch and now compile clean — corrected twice, not left at "found,
   not solved."** This entry originally said KMDF headers were
   "entirely absent" and needed sourcing "from somewhere." While
   investigating Phase 11's WDDM headers, the real KMDF headers
   (`c/Include/wdf/kmdf/1.11/wdf.h` and friends) turned up live in the
   exact same official Microsoft WDK NuGet package
   `driver/gpu/fetch-wdk-headers.sh` already fetches — real, present,
   not sourced from ReactOS's tree as guessed here. A later pass
   (`driver/kmdf/`) closed the "not yet compile-verified" gap for real:
   `driver/kmdf/fetch-kmdf-headers.sh` fetches them, and
   `driver/kmdf/kmdf-probe.c` — a real KMDF `DriverEntry` referencing
   the real `WDF_DRIVER_CONFIG`/`WdfDriverCreate` contract — **compiles
   clean** against this project's mingw-w64 toolchain, verified live and
   reproducibly (`make clean && make`, twice, same result). Two real,
   narrow patches found by actually compiling, not guessed at: a batch
   of case-sensitivity `#include` mismatches (Windows-filesystem-safe,
   Linux-fatal), and a genuine header-ordering bug in `wdf.h` itself
   (`wdfdevice.h` uses `WDF_REQUEST_TYPE` before `wdfrequest.h`, the
   header that defines it, is included — MSVC tolerates it, GCC
   correctly doesn't). See `driver/kmdf/README.md` for the full account,
   including what this still doesn't prove (no import library for the
   versioned KMDF co-installer stub exists in this toolchain, so linking
   a real KMDF driver remains separate, unattempted follow-up work —
   same boundary `wddm-probe.c` draws around `dxgkrnl.sys`).
3. **The truly recent (Windows 10-era) additions are absent**
   (`ExAllocatePool2`, `PsSetCreateProcessNotifyRoutineEx2`,
   `FltRegisterFilter`) — expected, not a surprise, and outside this
   project's near-term scope per CLAUDE.md anyway.

**What Phase 9 does not yet do:** any actual upstream patch to ReactOS
or conformance test running against a real (or even ReactOS-booted)
kernel exercising these APIs — this phase's real deliverable is
knowing precisely where such work would start, verified rather than
guessed, not the upstream work itself. Genuinely comparable to how
Phase 13 stands relative to actually building ReactOS from source: a
scoped, honest "here is exactly what's missing and why" rather than an
attempt at the full underlying task in one pass.

---

## Phase 10 — Gaming integration 🟨 (session + compat-tool integration artifacts written and validated at the syntax/schema level; no live Steam/GPU verification possible in this sandbox)

Tight Steam/Proton/NTLinux runtime/Gamescope/DXVK/vkd3d-proton integration
plus the driver compatibility database. Windows applications and games
should feel like native Linux packages.

**Scope, stated up front:** `distro/packages/base.list`/`packages.x86_64`
already carry the upstream package selection (Steam, Wine, Gamescope,
DXVK/vkd3d-proton via Steam's own Proton bundling — Phase 0). What this
phase adds is the glue *between* them — a real default session, and a
real (if scoped) Steam Play integration point — plus the "driver
compatibility database" Phase 10 explicitly names as its second
deliverable. Real game rendering/controller verification needs GPU and
display hardware this sandbox architecturally doesn't have (no
`/dev/dri`, no KVM) — same category as Phase 6/8's hardware absences,
stated precisely rather than glossed over.

### `distro/gaming/` — new

- Default "Game Mode" session (CLAUDE.md's "Open decisions" — Gamescope
  primary, Plasma optional desktop-mode): `distro/image/airootfs/usr/
  share/wayland-sessions/gamescope-session.desktop` +
  `.../usr/bin/gamescope-session`, matching the real, documented
  SteamOS/HoloISO/ChimeraOS convention for this exact file/script pair
  (Rule 1/9 — not invented here).
- `compat-tool/` — a real Steam Play custom compatibility tool
  ("NTLinux Runtime": `compatibilitytool.vdf`/`toolmanifest.vdf`/
  `run.sh`) that runs a game through this project's own Wine build
  instead of Valve's bundled Proton. **Deliberately not `ntloader`**,
  for real reasons stated precisely in its README: a Steam compat tool
  is invoked as `<script> %verb% <game.exe> <args>` (a different
  contract than `ntloader`'s bare `./game.exe` `binfmt_misc`
  invocation), and must use Steam's own per-game
  `STEAM_COMPAT_DATA_PATH` prefix rather than `ntloader`'s
  `~/.local/share/ntlinux/apps/` scheme, which Steam doesn't know
  about. What it *does* provide: a genuine, working Steam Play entry
  running games against NTLinux's own Wine version rather than
  whatever Proton ships — useful today, honestly scoped rather than
  overclaimed as deeper NT-runtime routing (that's real follow-up work
  for a later `ntabi`/`ntd` generation).

### `tooling/compat-db/schema/` — the "driver compatibility database"

A real JSON Schema (`compat-entry.schema.json`) formalizing the fields
`tooling/compat-db/README.md` already named (version, Windows target,
required overrides, known bugs, test status), covering `application`/
`game`/`driver` entries with one shared `test_status` enum (Rule 2 —
not a separate enum per kind). Seeded with **two genuinely true
entries** pulled from this project's own already-verified history, not
fabricated for illustration: Phase 1's `ntloader`+Wine `notepad.exe`
run, and Phase 5's `vsdev.c` driver test. The database proper (a real,
growing collection covering actual apps/games/drivers) is still
unstarted — this is the structural starting point, not a populated
database.

### Verified, at the level this sandbox allows

```
$ desktop-file-validate gamescope-session.desktop
# one expected warning about DesktopNames - real, standard key for
# session .desktop files that desktop-file-validate's spec (targeted at
# application launchers) doesn't recognize; not a bug in this file.

$ shellcheck gamescope-session run.sh   # both clean, zero warnings
$ bash -n gamescope-session run.sh      # both syntax OK

$ python3 compat-tool/validate-vdf.py compatibilitytool.vdf toolmanifest.vdf
OK: compatibilitytool.vdf parses as well-formed VDF, top-level keys: ['compatibilitytools']
OK: toolmanifest.vdf parses as well-formed VDF, top-level keys: ['manifest']

$ python3 tooling/compat-db/schema/validate.py
OK: seed-entries.json: wine-notepad-ntloader
OK: seed-entries.json: ntbridge-vsdev
# negative test confirms the validator actually rejects bad data:
$ python3 validate.py /tmp/bad-entry.json
FAIL: /tmp/bad-entry.json: Bad ID!: 'kind' is a required property
```

**What Phase 10 does not yet prove:** Steam actually discovering and
offering the compat tool, a real game launching through either
integration point, DXVK/vkd3d-proton actually rendering anything, or a
controller being recognized — no Steam client, no GPU, no display
server exists in this sandbox to test any of that against. Real
follow-up on real hardware, not attempted here.

### A real doc-sync bug found and fixed during this pass

`distro/packages/base.list` gained `wireguard-tools` for Phase 15, but
`distro/image/packages.x86_64` — which `base.list`'s own header says
must stay in sync — was never updated to match. Found by actually
diffing the two package lists (not assumed in sync), fixed. `driver/
README.md` also still said "`net/`, `usb/` are still untouched" after
both were completed in Phases 7-8 — fixed to reflect their real status,
and to mention `vfio/` (Phase 6's directory, previously unmentioned
entirely).

---

## Phase 11 — Native Windows GPU driver hosting (research) 🟨 (real, sourced research; the DDK-toolchain blocker was found to be substantially wrong and corrected live — the real WDDM/DXGKRNL headers exist, fetch from Microsoft's own WDK, and compile against this toolchain; two blockers remain real: ReactOS's own WDDM work is early/2D-only, and no VFIO/IOMMU exists in any sandbox this project has run in)

Not an MVP dependency (`docs/ARCHITECTURE.md` section 30) — the default
gaming path stays DXVK/vkd3d-proton on Mesa/Vulkan (section 47), and this
phase never gets ahead of that. Tracked explicitly because the payoff is
real and the mechanism already exists rather than needing new invention
(ADR-0003 in `docs/DECISIONS.md`): host a Windows WDDM GPU driver in the
same ReactOS driver cell used for any other kernel driver, with VFIO/IOMMU
mediating the actual hardware.

**Depends on:** Phases 4-9 — a GPU driver is one of the hardest classes of
Windows driver, not a good early target (section 55: avoid the primary GPU
in earlier phases). Requires ReactOS WDDM/DXGKRNL support (section 29) —
build it there, upstreamed, per Rule 3.

**Why it's worth it:** covers hardware/workloads Mesa can't — brand-new
GPUs ahead of open-source driver support, vendor features or professional/
compute workloads that only ship a Windows driver, hardware where the
Linux driver is meaningfully behind.

**Success criterion:** a real Windows WDDM GPU driver operates a
non-primary GPU inside the driver cell, rendering output reaches the host
compositor through the existing VFIO/IOMMU-mediated path, and Linux
survives a driver crash inside the cell (section 26).

### Why this phase is scoped as research, not implementation

Unlike Phases 4-10, this phase's own title already says "(research)" —
ADR-0003 promoted it from an architecture footnote to a tracked phase
specifically as a research milestone, not an implementation one. Treating
"do it in full" as "write driver code and live-verify it" here would mean
writing code against a kernel-mode interface that doesn't exist in this
project's own toolchain, targeting an upstream ReactOS subsystem that is
itself only weeks-old and 2D-only, to run against hardware no sandbox
this project has ever run in can provide — the opposite of this project's
own standard (verify by running, not by inspection). What a research
phase's "in full" means instead: chase down real, current, sourced facts
— not assumptions, not the original text's own hedge ("mostly doesn't
exist yet") — and produce the scoped, honest picture the next real
implementation attempt would start from.

### Three independent blockers, checked separately, not conflated

**1. ReactOS's own WDDM/DXGKRNL support — real, but early and 2D-only.**
Checked directly against ReactOS's official blog
(`reactos.org/blogs/investigating-wddm/`, Oct 7 2025 — still the only
post on the subject) rather than assumed from the architecture doc's
original hedge. Real, working, and more than expected going in: a real
experimental Dxgkrnl (VidPn negotiation + KMDOD miniport init) has
loaded Microsoft's own `BasicDisplay.sys` WDK sample *and* a real NVIDIA
Windows 7 GPU driver, producing actual 2D display output at native
resolution — a genuine, verified result on ReactOS's part, not
vaporware. Explicitly not yet implemented, per that same post: 3D
acceleration, scheduling, full hardware memory management, the complete
D3DKMT API, and the usermode driver (UMD) component. This matters
concretely for this phase's own stated payoff: "brand-new GPUs,"
"professional/compute workloads," and anything DXVK/vkd3d-proton would
otherwise handle all need Direct3D/compute execution, which needs the
still-missing 3D half — a real Windows GPU driver could plausibly
produce a *picture* on ReactOS today, per this result, but nothing this
phase actually cares about runs yet. The blog is explicit the whole
effort further depends on ReactOS's legacy XDDM/`CDD.dll` stack being
solid first — a second, upstream-side dependency chain of its own.

**2. This project's own DDK toolchain — corrected live, a real mistake
caught and fixed, not just re-stated more carefully.** This phase
originally claimed "zero WDDM/DXGKRNL surface at all... confirmed by
direct search, re-runnable, not a one-off finding" — checking only
whether mingw-w64 *pre-packages* these headers (it doesn't) and treating
that as the whole answer. It wasn't: the real Microsoft WDK headers a
WDDM display miniport driver needs (`dispmprt.h`, `d3dkmddi.h`,
`d3dkmdt.h`, `d3dukmdt.h`) are real, official, and fetch cleanly from
Microsoft's own NuGet packages (`Microsoft.Windows.WDK.x64` +
`Microsoft.Windows.SDK.CPP` — a real three-package dependency chain,
confirmed by downloading and inspecting each package's own `.nuspec`).
`driver/gpu/fetch-wdk-headers.sh` fetches them (never vendored — their
EULA explicitly prohibits redistribution, read directly, not assumed);
`driver/gpu/wddm-probe.c` is a real `DriverEntry` referencing the real
`DXGKRNL_INTERFACE`/`DxgkInitialize` contract, and it **compiles**
against this project's existing mingw-w64 DDK toolchain — live-verified,
not asserted — with a small number of narrow, documented patches (two
SAL-annotation no-op macros, two ordinary includes), the exact same
technique `driver/net/reactos/prepare-ndis-header.sh` already
established for the NDIS header bugs.

This is real progress, not a closed door — but also not "solved,"
stated precisely: one of Microsoft's own embedded correctness checks in
`dispmprt.h` (a `static_assert` verifying `DXGK_CHILD_CAPABILITIES`'s
exact byte layout) genuinely **fails** under this toolchain, including
after trying `-mms-bitfields` (mingw's standard MSVC-bitfield-ABI flag).
That means at least one struct's field layout, as GCC/mingw-w64
computes it, does not match what MSVC — and therefore the real Windows/
ReactOS kernel calling into a real driver — expects. A real,
unresolved, and non-trivial cross-compiler ABI question, not a
one-line fix; see `driver/gpu/README.md` for the full account. Separately
found live while investigating this: the real KMDF headers Phase 9
similarly said were "not present in this toolchain at all" are *also*
real and present in the same WDK package (`c/Include/wdf/kmdf/1.11/`) —
found, not yet compile-verified the way WDDM's headers were, stated
honestly rather than claimed complete (`tooling/compat-db/ddkgap/`'s
KMDF section now says so).

Distinct from this same toolchain's abundant *usermode* Direct3D
headers (`d3d9.h` through `d3d12.h`) — those are for applications
consuming Direct3D, not for a kernel-mode driver implementing a WDDM
miniport, and their presence never helped here.

**3. Real second-GPU hardware behind VFIO/IOMMU — categorically absent
from every sandbox this project has run in, unchanged since Phase 6.**
This is the blocker with no toolchain or upstream-code fix: Phase 6's
findings (`/dev/vfio` doesn't exist, `/sys/kernel/iommu_groups` is empty,
no `vmx`/`svm` CPU flags, a Firecracker microVM has no PCI bus or virtual
IOMMU by architectural design) apply identically here, and a "non-primary
GPU passed through via VFIO" is definitionally physical hardware — there
is no honest software-only substitute for it the way a TAP device stood
in for Phase 7's network interface, or `--usb-echo` stood in for Phase
8's USB bridge. This blocker needs real user hardware to ever attempt,
in any sandbox shaped like the ones this project has used.

### A real GPU became available this session — what it did and didn't change

This session ran on a Windows host with a genuine, healthy discrete GPU
(`Get-CimInstance Win32_VideoController`: NVIDIA GeForce RTX 4060, WDDM
3.1, driver 32.0.15.9186, `Status: OK` — plus a "Parsec Virtual Display
Adapter," indicating this host is itself remotely operated). Checked
directly, rather than assumed, what that does and doesn't unblock:

- **Doesn't touch blocker 3.** VFIO/IOMMU passthrough is a Linux kernel
  facility; this host is Windows. A real GPU being present is not the
  same as a real *second* GPU behind a Linux VFIO/IOMMU path — the two
  things blocker 3 needs (a second card, and Linux's VFIO stack) are
  both still categorically absent here. No regression, no progress on
  this blocker specifically.
- **Doesn't touch blocker 1** — ReactOS's own WDDM/DXGKRNL work happens
  upstream, not in this sandbox, regardless of local hardware.
- **Did let blocker 2's open question get sharpened with a real,
  live-run cross-compiler comparison it couldn't get before.** The real
  MSVC toolchain (Visual Studio 2022 Professional, `cl.exe`
  14.44.35207) is present on this host. Compiled `driver/gpu/`'s exact
  probe — the same `dispmprt.h` `static_assert(FIELD_OFFSET(
  DXGK_CHILD_CAPABILITIES, HpdAwareness) == 12, ...)` that fails under
  this project's mingw-w64 toolchain — against the same unmodified WDK
  10.0.22621.0 headers, this time with MSVC (the compiler these headers
  are authored and validated for). **It compiles clean, `static_assert`
  and all.** That pins the failure specifically to GCC/mingw-w64's own
  struct-layout computation for this type diverging from MSVC's — not a
  header defect (already suspected, now directly confirmed rather than
  inferred) — while leaving the actual root cause of *why* GCC computes
  a different offset still open; that would need a real mingw-w64
  compile on this same host for a true side-by-side (mingw-w64/GCC is
  confirmed absent from this host, and installing one — feasible via the
  `choco` package manager already present here — was deliberately not
  done unprompted, as a new system toolchain install is a more
  consequential, unrequested change than this pass's scope).
- **Checked, and confirmed closed off: this struct's live *runtime*
  data is still unreachable, even with real hardware.** Grepped the
  WDK's full public usermode `D3DKMTQueryAdapterInfo` query-type enum
  (`d3dkmthk.h`, `KMTQAITYPE_*`, 60+ real entries) end to end —
  `DXGK_CHILD_CAPABILITIES` is a kernel-mode-only DXGKRNL↔miniport
  contract type with no usermode escape exposing it. Getting this real
  driver's actual live `HpdAwareness` byte offset (as opposed to what
  the header text says it should be) would require loading an actual
  kernel-mode driver or kernel debugger on this host — correctly out of
  scope without explicit authorization, not attempted.

Net effect: real GPU hardware turned "the assert fails under our
toolchain, cause unknown" into "the assert is correct per the header's
authoring compiler; the mingw-w64 divergence is real, confirmed
GCC-side, and still open in its root cause" — genuine, narrower
progress on blocker 2, with blockers 1 and 3 unchanged. See
`driver/gpu/README.md` for the full account, including the exact probe
file and build invocation.

### What this means for "in full"

The three blockers are still independent — fixing one doesn't unblock
the others — but they're no longer equally unresolved. Blocker 2 moved
from "hard toolchain gap" to "real, fetchable, mostly-compiling, with
one genuine unresolved ABI question" — the one blocker this pass
actually made concrete progress on, verified live rather than just
re-described. A later pass, run on a host with a real GPU and a real
MSVC toolchain, narrowed that same open question further (see "A real
GPU became available this session" above) without closing it — still a
genuine, unresolved cross-compiler ABI question, now confirmed
GCC/mingw-w64-side rather than a header defect. Blockers 1 (ReactOS's
own early/2D-only WDDM) and 3 (no VFIO/IOMMU hardware in any sandbox
this project has run in) are unchanged and still real. `docs/DECISIONS.md`
ADR-0003 carries the same correction. Revisit this phase once blocker 3
lifts (real hardware with
VFIO/IOMMU available) — at that point blocker 2's remaining ABI-layout
question becomes real, concrete gating work with an actual starting
point (`driver/gpu/wddm-probe.c`), not a re-investigation from zero, and
ReactOS's own WDDM effort will likely have moved past its
Oct-2025-vintage 2D-only state, worth re-checking fresh rather than
assumed stale.

**A note on how this correction happened, worth keeping honest:** this
project's own first pass at Phase 11 checked whether mingw-w64
*pre-packages* WDDM/DXGKRNL headers, found no, and reported that as
though it settled the question. It didn't — Musa.Veil (already this
project's own reference source for undocumented NT declarations,
ADR-0006) had already established the right instinct months earlier:
check whether a real, legitimate upstream source has what's missing
before declaring it absent. That check wasn't made here the first time,
and should have been.

### A separate, adjacent capability found via this research: a real Parsec VDD display fallback (not part of this phase's own blockers)

Stated up front so it's never conflated with the above: this resolves
**none** of the three blockers. It's a distinct, smaller capability
that surfaced because this research pass's host happened to already
have "Parsec Virtual Display Adapter" (ParsecVDA) installed and healthy
— the same adapter cited above as evidence this host is remotely
operated. `driver/gpu/parsec-fallback/` is a real usermode client for
its public control API (`parsec-vdd.h`, BSD-3-clause,
github.com/nomi-san/parsec-vdd) — live-verified on this host:
`Get-CimInstance`-confirmed `DEVICE_OK`, driver version 45, and a real
add/remove cycle where `GetSystemMetrics(SM_CMONITORS)` genuinely went
1 → 2 → 1, read back from Windows itself, not asserted. Doesn't touch
ReactOS, VFIO/IOMMU, or WDDM/DXGKRNL — it's a fallback display path
usable today on hosts that already carry this adapter, nothing more.
See `driver/gpu/parsec-fallback/README.md` for the full live-run output
and exact scope boundary.

---

## Phase 12 — ntdll integration & remaining NT object types 🟨 (Thread objects + APC support done and verified; real Wine `ntdll` integration attempted for real on a real build server — compiles clean and is safely inert by default, but hangs when actually enabled, a real unresolved bug, not landed as working)

Two things bundled together, not because they're the same kind of work,
but because this phase's original text assumed the second was blocked on
the first: real Wine `ntdll` integration (Phase 2's original, still-open
"known gap" — patching Wine's actual sync implementation,
`dlls/ntdll/unix/sync.c` upstream, to call into `libntabi` for some
operations, then rebuilding Wine from source and validating against
Wine's own test suite), and the two NT object types Phase 3 explicitly
deferred: **Thread objects** and **APC support**.

**A premise here turned out to be wrong, corrected by actually building
it rather than left standing:** this phase originally claimed both object
types "need a real per-thread execution context that only exists once
real Windows/Wine thread creation routes through `ntabi`" — but that's
the same assumption Phase 3's own Process objects already disproved for
processes: `OpenProcess`+`pidfd` works for *any* permitted pid, not just
one `ntd` itself created, because it observes real Linux process state
from the outside rather than needing to own the process's creation.
Threads generalize the exact same way — `/proc/<pid>/task/<tid>` observes
a real Linux thread's liveness from the outside too, no Wine involvement
needed, no synthetic stand-in. Once that was tried, Thread objects and
APC queueing/delivery timing turned out to be fully buildable and
testable against real Linux threads today, independent of the (still
genuinely blocked) Wine `ntdll` patching work.

**Task breakdown:**

- [x] **Attempted for real, on a real Linux build server — real
      progress, a real unresolved bug, not "not attempted" anymore.**
      A later pass got a real Wine source checkout + full build (see
      `ROADMAP.md`'s cross-host re-verification section) and wrote
      `wine/ntabi-sync-integration/ntabi-sync.patch`: a scoped patch to
      `dlls/ntdll/unix/sync.c` routing `NtCreateEvent`/`NtSetEvent`/
      `NtResetEvent`/`NtWaitForSingleObject` (Events only — not the full
      Event/Mutant/Semaphore/Section/Completion/Process/Thread breadth)
      through `libntabi` when `WINENTABI=1`, modeled directly on Wine's
      own real, already-upstream `inproc_*`/`ntsync` fast-path pattern.
      **Compiles clean; correctly inert when disabled (verified live —
      a real event-lifecycle test PASSes identically with `WINENTABI`
      unset); but hangs when enabled**, during Wine's own internal
      `wineboot.exe --init`, before any test code runs — a real,
      reproducible concurrency bug, killed cleanly and the host
      confirmed healthy afterward, not chased to a root cause this pass
      (real stopping point, not a time-only tradeoff — see that
      directory's README for the full, honest account and the current
      best theory). **Not landed as working** — do not re-enable
      `WINENTABI=1` without diagnosing this first.
- [ ] Validate against Wine's own test suite (`dlls/ntdll/tests/`,
      `dlls/kernel32/tests/` sync-related suites) — passing is the bar,
      not just "it links." Blocked on the item above.
- [x] **Thread objects** (`ntd/ntd.c`, `ntabi` protocol v3): `OpenThread`
      on a real `(pid, tid)`, signaled when *that specific* Linux thread
      exits — detected via `/proc/<pid>/task/<tid>` polling on `ntd`'s
      regular tick, not `pidfd_open` (which only accepts
      thread-group-leader pids, rejecting arbitrary TIDs with `EINVAL`).
      An explicit `OpenThread` on a thread that was never seen checks
      liveness first and returns `NTABI_STATUS_THREAD_NOT_FOUND`; the
      same underlying object is shared between an explicit `OpenThread`
      by one process and a thread's own implicit "self" lookup during its
      own alertable wait (both funnel through the same
      `find_thread_object`/`find_or_create_thread_object` helpers, keyed
      by `(pid, tid)`) — required for cross-process `QueueApc` to land on
      the object the target thread actually checks.
      `SuspendThread`/`ResumeThread` add real integer *count accounting*
      (correct increment/clamp-at-0/decrement, matching NT's
      report-the-previous-count return convention) — stated precisely as
      **not** freezing the target thread's real CPU execution, which
      needs `ptrace` (a separate, larger, permission-sensitive
      undertaking; Yama `ptrace_scope` varies by environment; not
      attempted here).
- [x] **APC support**: `QueueApc` appends one packet (routine + 3 args)
      to a thread's own FIFO queue; a new `alertable` flag on
      `WAIT_SINGLE` checks the calling thread's *own* pending-APC queue
      before the wait ever touches the target object — a pending APC
      short-circuits with `NTABI_STATUS_USER_APC` immediately, the
      target object untouched even if it was simultaneously satisfiable.
      Matches this task's own text precisely: "delivered at the next
      alertable wait." Deliberately lazy (Design A, documented in
      `ntd/ntd.c`'s top-of-file comment) — does **not** interrupt a wait
      already blocked when the APC is queued, only checked at the start
      of a new alertable wait call; a non-alertable wait is never
      interrupted by a pending APC, verified directly. Models a single
      flat per-thread APC queue (Rule 2 — don't build more than asked),
      not the real kernel-mode-vs-user-mode `KAPC`/`UAPC` split.
- [x] Real tests, same standard as every other object type in `ntd`
      (`ntabi/tests/test_ntabi.c`, +22 checks, 60/60 passing): two real
      `pthread` worker threads with different lifetimes in a forked
      target process, each `OpenThread`'d separately, proving the exit
      signal is scoped to *that* thread and not its sibling (a Process
      object couldn't tell these apart — that's the entire reason Thread
      objects exist); the full APC short-circuit/no-touch/non-alertable-
      immunity/deferred-delivery timing matrix; `SuspendThread`/
      `ResumeThread`'s exact previous-count convention and 0-floor clamp.
- [ ] Measure the two success criteria Phases 2 and 3 both left
      unverifiable for the same reason: reduced wineserver IPC traffic,
      and Wine's test suites staying green with `ntabi`/`ntd` in the loop
      instead of wineserver for the routed operations. Still blocked on
      the real Wine `ntdll` patch/build above.

**Success criterion:** Wine's own test suites continue passing with
`ntabi`/`ntd` handling the routed operations instead of wineserver
(Phase 2's original success criterion) + a measured reduction in
wineserver IPC traffic (Phase 3's original success criterion) — **both
still unmet, genuinely blocked on a full Wine source build this sandbox
doesn't have, no different from Phase 13's RosBE blocker** — + Thread
objects and APCs implemented with real semantics, verified the same way
every other object type in `ntd` was: real cross-process tests measuring
actual behavior, not stubs. **The last part is met.** The first two are
not, and this phase stays 🟨 rather than ✅ until they are.

---

## Phase 13 — RosBE & the ROS-NTCELL stripped driver-cell profile 🟨 (RosBE itself turned out to be unnecessary, and a real ROS-NTCELL image now genuinely boots — a real ntoskrnl/HAL/ACPI/PnP init, real boot- and PnP-driver loading, verified live under QEMU with a real captured serial trace — but stops at a real, well-understood NT bug check (0x6B, no user-mode present) rather than reaching a stable running state)

Moved out of Phase 4, where it originally sat as a "known gap," once
Phase 5 established that RosBE's actual scope is much narrower than
Phase 4 assumed: **driver** source (`ntbridge_pnp.c`, `vsdev.c`, and any
future NTLinux-authored driver) builds fine against mingw-w64's DDK
toolchain, no RosBE needed — that correction lives in Phase 4 and
`driver/ntbridge/reactos/README.md`. What genuinely still needed
checking was building **ReactOS itself** from source.

### Real, live result: RosBE was never actually needed, checked directly rather than assumed

A later pass, with real access to a Linux build server, checked the
premise directly instead of assuming a RosBE bootstrap was required
first (Rule 1). `ROS_ARCH=amd64 ./configure.sh` — bypassing RosBE
entirely — configured cleanly against the **stock Debian
`gcc-mingw-w64-x86-64` package** already used throughout this project
(the same one `driver/gpu/`, `driver/kmdf/`, `driver/net/reactos/`,
etc. all build against), with only a cosmetic CMake warning about not
using RosBE's own bundled CMake.

Building hit one real, root-caused issue, not a RosBE-shaped one:
**GCC 14 made `-Wincompatible-pointer-types` a hard error by default**
(a real GCC 14 behavior change) — ReactOS's own source (and its bundled
`libtirpc` third-party code) predates that default and has genuine,
harmless-in-practice type mismatches (`uint32_t*` vs `ULONG*`, same
width, different type) that GCC 14 now refuses outright where older
GCC versions only warned. This is plausibly the *actual* reason RosBE
pins an older GCC — not exotic ReactOS-specific patches, just predating
a stricter upstream GCC default. Fixed with a real, narrow, standard
fix: `-DCMAKE_C_FLAGS='-Wno-error=incompatible-pointer-types
-Wno-error=implicit-function-declaration -Wno-error=int-conversion'`
(downgrading these three GCC 14 defaults back to warnings, not
silencing the diagnostics themselves).

**With that one fix, real, verified-live results:**

```
$ file ntoskrnl/ntoskrnl.exe hal/halx86/hal.dll boot/freeldr/freeldr/freeldr.sys
ntoskrnl.exe: PE32+ executable for MS Windows 5.01 (native), x86-64, 20 sections
hal.dll:      PE32+ executable for MS Windows 5.01 (native), x86-64, 17 sections
freeldr.sys:  (240640 bytes)
```

The real ReactOS **kernel** (`ntoskrnl.exe`, 13MB), **HAL**
(`hal.dll`, 2.2MB), and **bootloader** (`freeldr.sys`, 240KB) all
compiled and linked clean from a real, current ReactOS source checkout
— the first time this project has built any part of ReactOS itself
from source, on any host, in this project's history. The full
`ninja bootcd` desktop-ISO target was tried first and hit a second,
unrelated, genuinely ReactOS-desktop-shell-specific issue
(`dll/shellext/shellbtrfs` — a Btrfs shell extension with an ambiguous
`std::to_wstring` overload under this newer libstdc++ — completely
irrelevant to ROS-NTCELL's own no-shell scope) — rather than chase
bugs in components ROS-NTCELL explicitly doesn't want, the build was
retargeted directly at `ninja ntoskrnl hal boot/freeldr/freeldr/
freeldr.sys`, the actual pieces this phase's success criterion needs,
which is both more correct and faster than the full desktop build.

**What that pass did not yet prove:** these three binaries had not been
assembled into an actual bootable image. A later pass, on the same real
Linux build server, did that assembly and booted the result live —
see the next section for the real, captured result.

### Real, live result: a genuine ROS-NTCELL boot — HAL/ACPI/MM/PnP init and real driver loading, stopping at a real NT bug check

`driver/cell/reactos-build/assemble-rosntcell-boot.sh` takes
`build-reactos-core.sh`'s three binaries and assembles them into a real,
bootable El Torito CD image, then (with `--test`) boots it live under
QEMU (same TCG-fallback situation as every other headless-sandbox QEMU
use in this ROADMAP — no `/dev/kvm` on this build server either) and
captures a real serial-console trace. Per Rule 1, it reuses ReactOS's
own boot-image-assembly machinery throughout rather than hand-rolling
an ISO or registry format:

- the real `isoboot.bin`/`isombr.bin` El Torito boot sectors and
  `native-mkisofs`/`native-isohybrid` host tools `boot/boot_images.cmake`
  itself builds and uses for its own `bootcd` target — just fed a
  hand-picked minimal file list instead of the full desktop-shell
  `livecd` list `bootcd` normally depends on (that full list is what
  actually needs the desktop-shell components that don't build, per the
  previous section);
- `native-mkhive`, fed ReactOS's own real `boot/bootdata/*.inf` registry
  description files, for the SYSTEM/SOFTWARE/DEFAULT/SAM/SECURITY hives
  — literally `ninja livecd_hives`, the exact command
  `create_registry_hives()` in `sdk/cmake/CMakeMacros.cmake` runs. No
  hand-built registry hive format;
- `freeldr.ini`'s boot entry is a trimmed copy of the real
  `BootType=Windows2003`/`SystemPath=\reactos`/`/DEBUG /DEBUGPORT=COM1`
  shape `boot/bootdata/bootcd.ini`'s own `LiveImg_Debug` entry already
  uses.

The genuinely new, hand-assembled part is the ISO's minimal file list
(the driver set ROS-NTCELL needs instead of the full desktop `livecd`
one) — found by **real, empirical, live iteration**, not guessed:
started from just the three `build-reactos-core.sh` binaries on the CD,
booted under QEMU, read the real error FreeLoader or `ntoskrnl`
printed, built and added exactly the one real file that error named,
and repeated — over twenty real QEMU boots, each with real captured
output. That converged on: `rosload.exe` (freeldr's own real "second
stage loader"); 10 NLS codepage/case-table files `ntoskrnl`'s loader
needs before it will open the registry; `kdcom.dll` + `bootvid.dll`
(real, unconditional `ntoskrnl.exe` PE import dependencies, not
optional even with `/NOGUIBOOT`); the 16 real `Start=0` boot-start
services in `boot/bootdata/hivesys.inf` (`swenum`, `sacdrv`, `mup`,
`ndis`, `nmidebug`, `usbhub`, `usbehci`, `usbohci`, `usbuhci`,
`usbstor`, `usbccgp`, `mountmgr`, `acpi`, `pci`, `ramdisk`, `disk`);
then a real, live cascade of PnP-triggered drivers QEMU's own emulated
i440fx/PIIX3 hardware pulls in once `acpi.sys`+`pci.sys` actually
enumerate it (`isapnp`, `pciide`, `atapi`, `cdrom`, `vga`, `i8042prt`,
plus `wdf01000`/`wdfldr` since several of those are KMDF drivers); a
handful of real transitive PE-import dependencies those pull in
(`usbd`, `usbport`, `pciidex`, `buslogic`+`scsiport`, `wmilib`,
`classpnp`, `ksecdd`, `cdfs`, `ks`); and a final batch of Start=1
drivers `ntoskrnl`'s Phase 1 init (`IoInitSystem`) pulls in right after
"BOOT DRIVERS LOADED" (`videoprt`, `null`, `beep`, `fs_rec`,
`kbdclass`, `mouclass`, `floppy`, `blue`, `msfs`, `npfs`, `afd`,
`netio`) — 41 real ReactOS driver/DLL binaries beyond `ntoskrnl.exe`/
`hal.dll` in total, every one of them a real compile against this same
mingw-w64 toolchain, every one added because a real boot named it, not
because it was assumed needed.

**Real, live result — a full serial-console kernel-boot trace, captured
verbatim via `assemble-rosntcell-boot.sh ... --test`:**

```
(/ntoskrnl/kd64/kdinit.c:95) ReactOS 0.4.17-amd64-dev (Build 20260828-62de6b3) (Commit 62de6b33a2bbdb525055d18a9d8269d79341a78a)
(/ntoskrnl/kd64/kdinit.c:96) 1 System Processor [512 MB Memory]
(/ntoskrnl/kd64/kdinit.c:100) Command Line: DEBUG DEBUGPORT=COM1 BAUDRATE=115200 SOS FASTDETECT MININT NOGUIBOOT
(/ntoskrnl/ke/amd64/cpu.c:273) Supported CPU features: KF_RDTSC KF_CR4 KF_CMOV ... KF_NX_BIT KF_NX_ENABLED X86_FEATURE_PAE
(/hal/halx86/acpi/halacpi.c:928) ACPI v1.0-1.0b detected. Tables: [RSDT] [APIC] [FACP]
(/hal/halx86/apic/halinit.c:48) Using HAL: APIC UP DBG
(/ntoskrnl/mm/mminit.c:135)           0xFFFFF80000000000 - 0xFFFFF80004800000	Boot Loaded Image
(/ntoskrnl/mm/mminit.c:146)           0xFFFFFA8000000000 - 0xFFFFFA8000901000	PFN Database
... (full real ARM3 memory-manager layout — Non Paged Pool, System View
    Space, Session Space, Page Tables, Hyperspace, System Cache, Paged
    Pool, System PTE Space — all real addresses printed by real code)
(/drivers/ksfilter/swenum/swenum.c:428) SWENUM loaded
BOOT DRIVERS LOADED
(/ntoskrnl/mm/ARM3/sysldr.c:170) Loading: \SystemRoot\system32\drivers\i8042prt.sys at FFFFF880761D8000 with 3b pages
(/drivers/input/i8042prt/hwhacks.c:278) SMBiosTables HACK, see CORE-14867
... (real, live PnP-triggered driver loads for kbdclass, mouclass,
    floppy, fs_rec, null, beep, blue, vga, videoprt, msfs, npfs, afd,
    netio — each one the real PnP manager reacting to QEMU's actual
    emulated hardware, not a canned list)
(/ntoskrnl/io/iomgr/driver.c:83) Deleting driver object '\Driver\KdDriver'

*** Fatal System Error: 0x0000006b
                       (0xFFFFFFFFC0000034,0x0000000000000002,0x0000000000000000,0x0000000000000000)

Entered debugger on embedded INT3 at 0x0010:0xFFFFF800005A0382.
Type "help" for a list of commands.

kdb:>
```

This is real, verified-live HAL initialization, ACPI table parsing,
memory-manager setup, Object Manager/I/O Manager/PnP Manager coming up,
and both boot-start and PnP-triggered driver loading — the actual
`docs/ARCHITECTURE.md` section 15 list (HAL, ntoskrnl, registry, PnP,
I/O Manager, Object Manager, Memory Manager, driver loader) all
genuinely exercised on a real, booted kernel for the first time in this
project's history, not merely compiled.

**Why it stops there, and why that's the real, honest boundary rather
than one more missing driver to chase:** bug check `0x6B` is
`PHASE1_INITIALIZATION_FAILED`, and `STATUS_OBJECT_NAME_NOT_FOUND`
(`0xc0000034`) as its first parameter is the well-known real signature
of "couldn't open `smss.exe`" — a real, standard NT/ReactOS bug check,
not a ROS-NTCELL-specific bug. ROS-NTCELL ships zero user-mode Windows
executables by design (`docs/ARCHITECTURE.md` section 15: no Explorer/
Winlogon/shell); this pass took that literally, all the way to no
`smss.exe` either, and confirmed live that `ntoskrnl`'s Phase 1 init
has no path around needing at least that much user-mode to declare boot
complete. Getting past this would mean shipping a real (even if inert)
`smss.exe` + `ntdll.dll` + enough of the Win32 subsystem for it to not
immediately fail — a materially different, larger undertaking than
"assemble a boot image," and arguably outside what a driver-only cell
needs anyway (see the task list below).

**A second, real, separate finding worth documenting:**
`UiMessageBox()` (`boot/freeldr/freeldr/ntldr/winldr.c`) blocks for a
keypress on *every* missing boot/PnP driver, even though the underlying
failure is individually non-fatal (FreeLoader removes the failed driver
from the list and continues). With no VGA device attached, FreeLoader's
own console falls back to COM1 — and QEMU's `-serial file:...` backend
is write-only, so no keypress ever arrives. Any driver still missing
therefore hangs the whole boot **indefinitely**, silently, rather than
failing loudly — this is why the driver list above had to be iterated
all the way to zero missing-driver errors (confirmed via a second,
throwaway `-vga std` + QMP-screendump boot for the early iterations,
before the COM1-only path was trusted) rather than stopping at "probably
enough." See `assemble-rosntcell-boot.sh`'s own header comment for the
full, precise account of both findings.

- [x] ~~Stand up the RosBE cross-toolchain~~ — **not needed**, checked
      directly: the stock mingw-w64 toolchain this project already uses
      everywhere else builds ReactOS's kernel/HAL/bootloader clean, with
      one narrow, documented GCC-14-compatibility flag.
- [x] Assemble `ntoskrnl.exe`/`hal.dll`/`freeldr.sys` into an actual
      bootable **ROS-NTCELL** image — **done, real, live-verified**:
      `assemble-rosntcell-boot.sh` builds a real El Torito CD image from
      ReactOS's own bootsector/mkisofs/mkhive tooling and boots it under
      QEMU to a real, captured HAL/ACPI/MM/PnP/driver-loading trace (see
      above). Does not yet reach a stable, non-bugchecked "running, no
      shell" state — that needs at least a minimal `smss.exe`, tracked
      as new follow-up below rather than silently claimed done.
- [ ] **New:** get past bug check `0x6B` — either a minimal, real,
      inert `smss.exe`/`ntdll.dll` (the smallest real Windows user-mode
      footprint that lets Phase 1 init declare success), or confirm via
      ReactOS's own source/issue tracker whether a boot configuration
      exists that lets `IoInitSystem` consider itself done without one
      (Rule 1 — check before assuming this needs new code). Either way,
      this is the actual remaining gap between "ROS-NTCELL boots" and
      "ROS-NTCELL reaches a stable state a driver could actually load
      into," not the boot-image assembly this task item originally
      named.
- [ ] Once a bootable ROS-NTCELL image reaches a stable running state,
      revisit whether `driver/ntbridge/reactos/ntbridge_pnp.c`'s two
      flagged bugs (missing wait-for-lower-IRP-completion;
      `IoCreateDevice` from DISPATCH_LEVEL) are easier to catch/fix
      building inside the full ReactOS source tree (e.g. against its
      own driver-verifier tooling) than the standalone mingw-w64 build
      this project uses today — not required, but worth checking now
      that the option exists.

**Success criterion:** a ReactOS driver cell boots from a ROS-NTCELL
image built by this project's own toolchain, with no desktop shell
present — the actual "minimal bootable ReactOS image" Phase 4's success
criterion originally called for, not the stock ISO stand-in it settled
for. **Substantially closer, not yet fully met** — a real ROS-NTCELL
image now boots a real kernel through real HAL/ACPI/MM/PnP/driver-loader
init, verified live with a captured serial trace; what remains is
reaching a stable post-Phase-1-init state (needs at least a minimal
`smss.exe`), a real but now precisely bounded gap rather than an
unassembled image.

---

## Phase 14 — Live PnP-triggered driver installation ⬜

Consolidates four separate "written, builds clean, never loaded inside
a real, booted ReactOS kernel via a real PnP-triggered install" gaps
that accumulated across Phases 4, 5, 7, and 8, each left in place as a
documented pointer rather than a dead end:

- [ ] `driver/ntbridge/reactos/ntbridge_pnp.c` (Phase 4) — a WDM bus
      driver targeting Root-enumeration via `ntbridge.inf`. Never
      loaded; also carries two known logic bugs (missing wait-for-
      lower-IRP-completion, `IoCreateDevice` from DISPATCH_LEVEL) a real
      load would force fixing.
- [ ] `driver/vsdev/vsdev.c`'s **PnP path** (Phase 5) — `AddDevice` +
      `vsdev.inf`'s Root-enumeration install. Only the *legacy*
      `sc start` load path was verified live (real I/O, reproduced
      twice); the PnP-triggered path shares the same device-creation
      code (`VsdevCreateDeviceObject`) but has never actually been
      exercised by a real PnP-enumerated install.
- [ ] `driver/net/reactos/ntnet.c` (Phase 7) — the real NDIS 5.1
      miniport. Builds and links clean; never loaded inside ReactOS.
      Real network adapter installation (Add Hardware wizard / Device
      Manager "Update Driver") is the specific, larger live-
      verification task this phase exists to attempt.
- [ ] `driver/usb/reactos/ntusb.c` (Phase 8) — the WDM USB bridge bus
      driver. Builds and links clean; never loaded inside ReactOS. Same
      Root-enumeration-via-`ntusb.inf` install as `ntbridge_pnp.c`
      needs, plus — the specific thing this phase would newly prove —
      confirming a client driver (real or a small NTLinux-authored test
      driver) can actually bind to its child PDO and have
      `IOCTL_INTERNAL_USB_SUBMIT_URB` reach `NtusbProcessUrb` for real
      (today only exercised via the test-only usermode IOCTL path, per
      that file's README).

All four need the same missing capability: driving ReactOS's actual
PnP-triggered hardware-install UI flows live (not the legacy `sc
start`/`sc create` shortcut `vsdev.c`'s verified path used, and not the
Root-enumeration-via-`devcon` shortcut this project's ReactOS LiveCD
environment doesn't ship) — worth attempting together, in one pass, once
that automation exists, rather than four separate one-off efforts.

**Task breakdown:**

- [ ] Work out how to trigger a real PnP install without `devcon.exe`
      (not present on the ReactOS LiveCD environments used so far) —
      likely means locating and scripting Device Manager's actual
      "Add legacy hardware" / "Update Driver" wizard via the same
      QMP-driven synthetic-keyboard automation `driver/vsdev/run-test.sh`
      already established (`driver/cell/launcher/qmp_console.py`), a
      materially longer, more failure-prone click sequence than
      `vsdev.c`'s single `sc start` command.
- [ ] `ntbridge_pnp.c`: real Root-enumerated install, confirm
      `IRP_MN_START_DEVICE` actually fires, fix the two known bugs once
      a real load surfaces them concretely, confirm PnP device-relations
      (child PDO) reporting works against a real PnP manager.
- [ ] `vsdev.c`: real Root-enumerated install via `vsdev.inf`, confirm
      the PnP path produces the same working `\\.\NTLVSER0` I/O the
      legacy path already proved.
- [ ] `ntnet.c`: real Network Adapter install, confirm NDIS actually
      accepts the OID surface and the adapter shows up as a usable
      Windows network connection, then re-run the Phase 7 TAP round-trip
      test against the *real* miniport instead of the stand-in guest
      client.
- [ ] `ntusb.c`: real Root-enumerated install via `ntusb.inf`, confirm
      the child PDO's hardware ID is real enough for a client driver to
      bind, then re-run the Phase 8 bulk-transfer round-trip test
      against the *real* driver's `IOCTL_INTERNAL_USB_SUBMIT_URB` path
      instead of the test-only IOCTL.

**Success criterion:** all four drivers load via a real PnP-triggered
install (not the legacy/manual shortcuts already verified) inside a
real, booted ReactOS kernel, with `IRP_MN_START_DEVICE` observed firing
from the real PnP manager in each case.

### Real progress on the automation itself, this phase's actual first blocker

The first task item above — driving ReactOS's UI live via QMP-driven
synthetic input — got real, live-verified progress on a later pass, on
the real Windows/QEMU host used throughout this ROADMAP's later
sessions. `driver/cell/launcher/qmp_console.py`'s `sendkey meta_l,r`
(QEMU's qcode for the left Windows/Super key, chorded with `r`) reliably
opens ReactOS's "Run" dialog; typing `cmd` + Enter reliably opens a
real `X:\reactos\System32\cmd.exe` console window — confirmed with a
real screendump, repeatable across isolated tries with no flakiness
observed. This is a materially more robust way to reach a console than
the "click empty desktop + jump to an icon by its first letter"
technique `driver/vsdev/run-test.sh` previously used (see
`driver/vsdev/README.md`'s "Known gaps" for that technique's own
real, reproducible failure against current ReactOS nightlies) — a real
building block for driving Device Manager's Add Hardware wizard the
same way, though that specific wizard flow was not attempted this pass.
A second, real, narrow fix went into `qmp_console.py`'s
`dismiss-dialog` at the same time (`--click-x`/`--click-y`: click a
known button coordinate instead of blindly sending `ret`, so a stray
keypress can't land on the wrong control) — kept because it's a real
improvement in principle, though a full automated run still hit a
second, distinct, not-yet-root-caused issue (the guest's UI language
changing mid-run) that this specific fix didn't resolve — see
`driver/vsdev/README.md` for the precise, unresolved account rather
than an overclaimed "fixed."

### The real Add Hardware Wizard, actually reached — and a genuine, reproducible hang inside it

A later pass, in the same session, pushed further than any previous
attempt: with `vsdev.sys`/`vsdev.inf` on the boot floppy (via
`write-fat12-floppy.py`) and a real ReactOS desktop reached, tried the
System Properties → Hardware tab's own "Device Manager..." and
"Hardware Wizard..." buttons first — **neither responds to activation**
(clicked-then-focused via screendump-confirmed focus rectangles,
`ret` sent, no window ever opened, on this exact ReactOS nightly
build 2669) — a real finding in its own right, distinct from the
already-documented desktop-icon-launch flake, since this is a
different control (a property-sheet push button, not a desktop icon)
failing the same "focuses, never activates" way.

Bypassing System Properties entirely and invoking `hdwwiz.cpl` directly
via `sendkey meta_l,r` (Win+R) **worked** — a real, genuine "Add
Hardware Wizard" window opened, rendering correctly (Welcome page,
real wizard chrome, `Next >` pre-focused). Pressing `ret` advanced it
past the Welcome page into a real "Please wait while the wizard
searches..." page — genuine live PnP device enumeration, the deepest
point any pass of this project has reached into a real Windows/ReactOS
hardware-install flow.

**That search phase then hung** — confirmed genuinely stuck, not just
slow: three separate screendumps taken roughly 3, 8, and 13 in-guest
minutes apart all show the identical "searching" spinner with the
in-guest clock having visibly advanced between each — no completion, no
error, no progress indicator change. Not root-caused this pass (the
QEMU instance was killed cleanly rather than left running indefinitely
in the interest of the session's own resource budget) — a real
candidate for either a genuine upstream ReactOS bug in this exact
nightly's hardware-detection code under QEMU/TCG's specific virtual
chipset, or a slow/blocking device probe that would eventually
complete given enough wall-clock time (not distinguished between the
two). Screenshots from this run were not preserved (cleaned up before
this account was written) — real, separate follow-up: reproduce with
screenshots kept, and let the search run considerably longer (an hour+)
once before concluding it's a true hang rather than "very slow."

**Net effect on Phase 14:** the automation-technique blocker this
phase's own first task item names is now substantially resolved (Win+R
reliably reaches both a console and, via `.cpl` files, Control Panel
applets that a broken UI button can't block) — but a new, real,
ReactOS-side blocker was found in its place, one layer deeper than
where this phase started. None of the four target drivers were
actually installed this pass; the wizard never got past its own
hardware-search phase to reach the "Have Disk" step where
`vsdev.inf`/`ntbridge.inf`/etc. would actually get used.

---

## Phase 15 — WireGuard network security (nearest-server ProtonVPN) 🟨 (server-selection logic implemented and verified live; tunnel bring-up not attempted in this project's original sandbox — but a later pass on a real, unrestricted Linux host confirmed the kernel-module blocker below is a sandbox limitation, not architectural — see "A real host without that restriction" further down)

**Success criterion:** NTLinux ships a default-available WireGuard
security layer that automatically pins its tunnel to the lowest-latency-
estimate ProtonVPN free-tier server for the host, without NTLinux ever
generating, storing, or vendoring real account key material.

### What this is

`distro/network/wireguard-nearest/`:

- `select-nearest-server.py` — fetches ProtonVPN's public free-tier
  logical-server list, estimates host location via GeoIP, ranks
  candidates by great-circle distance, and rewrites only an existing
  WireGuard profile's `[Peer]` section (`PublicKey`/`Endpoint`) to point
  at the winner — `[Interface]` (the account-tied `PrivateKey`/
  `Address`/`DNS`) passes through untouched.
- `ntlinux-wireguard-nearest.sh` + `.service` — systemd oneshot
  integration: runs the selector against a template profile placed
  out-of-band at `/etc/ntlinux/wireguard/protonvpn-template.conf`, and
  reloads `wg-quick@wg0` if the winning peer changed. A documented
  no-op, not a failure, when no template is present.
- `distro/packages/base.list` — added `wireguard-tools` (the kernel
  `wireguard` module already ships with `linux`, already on the list).

**Reuse, not reimplementation (Rule 1/17):** WireGuard itself is Linux
upstream; the account-tied keypair has to come from a real, authenticated
ProtonVPN session (`account.protonvpn.com` -> Downloads -> WireGuard
configuration, per <https://protonvpn.com/support/wireguard-configurations>,
or the official Linux app) — this project did not attempt to reimplement
Proton's SRP login or spoof their API's client-version gate to fetch
configs directly. See `distro/network/wireguard-nearest/README.md` for
the full account of that boundary.

### Verified live, this session

```
$ python3 select-nearest-server.py --top 8
# host location (estimated): Council Bluffs, United States
# 114 tier-0 candidates, nearest 8:
#       689 km  US-FREE#9      Chicago         node-us-128.protonvpn.net
#       689 km  US-FREE#48     Chicago         node-us-157.protonvpn.net
#       948 km  US-FREE#6      Dallas          node-us-133.protonvpn.net
...
```

Real GeoIP lookup, real live fetch of 1595 servers (114 free-tier) from
the public mirror the server-list logic targets, real haversine ranking
— not mocked. Notably, `US-FREE#48` — the exact server named in a real
WireGuard profile a maintainer had already downloaded from their own
ProtonVPN account — came back tied-nearest from this independent
computation, unprompted. `--template`/`--out` peer-patching verified
against a synthetic dummy profile (confirmed `[Interface]` untouched,
`[Peer]` correctly rewritten) — never against real key material, which
never touched any file this repo tracks.

### What's not verified, stated precisely

- **No actual WireGuard tunnel has been brought up.** This sandbox has
  no loadable kernel modules (`nomodule` on the cmdline — the same
  Phase-6/VFIO finding — `wireguard-tools` installs but `modprobe
  wireguard` has nothing to load), and this session's own permission
  classifier blocked a raw-UDP reachability probe to a real ProtonVPN
  endpoint outright — read as an intentional boundary (this sandbox's
  egress is deliberately funneled through its configured HTTPS proxy
  only) and not worked around. A real handshake needs a host that
  permits kernel WireGuard or raw UDP egress.
- **No live call against Proton's own `api.protonvpn.ch`** — it gates
  `/vpn/logicals` behind an `x-pm-appversion` client-identity header;
  this project's classifier also blocked an attempt to spoof an
  existing official client's version string to get past that gate, and
  this project wouldn't have done so regardless. Verified only against
  the third-party mirror named in the README. Getting a legitimate
  registered client identity for the real endpoint is real follow-up
  work.
- **`ntlinux-wireguard-nearest.service` has not run under systemd** —
  no systemd PID 1 in this sandbox to boot it under.

Same honest boundary this ROADMAP has drawn everywhere a sandbox
capability (VFIO/IOMMU in Phase 6, PnP install flows in Phase 14, and
now kernel WireGuard/raw UDP here) is architecturally out of reach: the
software that *can* be tested here was written and verified for real:
what's left needs a different environment, not different code.

### A real host without that restriction — checked directly, not assumed still blocked

A later session got real SSH access to a genuine, unrestricted Linux
host (a Hetzner Cloud vServer, Debian 13/trixie, real systemd PID 1,
`/dev/kvm` absent but ordinary kernel module loading and raw networking
both allowed — unlike this project's original egress-restricted
sandbox). Checked the two blockers above directly rather than assuming
they still applied everywhere:

```
# modprobe wireguard
# lsmod | grep wireguard
wireguard              118784  0
libchacha20poly1305     16384  1 wireguard
ip6_udp_tunnel           16384  1 wireguard
udp_tunnel               36864  1 wireguard
curve25519_x86_64        36864  1 wireguard
libcurve25519_generic     45056  2 curve25519_x86_64,wireguard

# ip link add wg-test type wireguard
# ip link show wg-test
3: wg-test: <POINTOPOINT,NOARP> mtu 1420 qdisc noop state DOWN mode DEFAULT group default qlen 1000
# wg show wg-test
interface: wg-test
```

**Real, live confirmation:** the kernel WireGuard module loads cleanly
and a genuine `wireguard`-type network interface creates successfully —
the "no loadable WireGuard kernel module" blocker was a property of
this project's original sandbox, not of WireGuard support in general or
of anything this codebase does. Still not attempted, for real reasons
distinct from the kernel-module blocker: an actual handshake needs a
second real WireGuard peer (this project doesn't fabricate ProtonVPN
account credentials — see the boundary stated above, unchanged), and
`ntlinux-wireguard-nearest.service` still needs a template profile and
systemd unit install this pass didn't set up. The kernel-level
blocker specifically is resolved, on real hardware; the credential/
integration-testing boundary is not, and shouldn't be worked around.

---

## Cross-host re-verification pass (real Windows host + a real Linux build server)

A later session ran with two new, real, non-sandbox environments this
project hadn't had before: a Windows 11 host with a real MSVC + QEMU
install (used throughout Phase 11 above and the `ntcell` Windows
re-verification in Phase 4), and — via real SSH access the user
provided — a genuine Hetzner Cloud Linux vServer (`root@`, Debian 13),
used as a real build/test server. Real toolchain gaps hit and fixed
along the way, each narrow and documented in place rather than worked
around silently: no admin rights to fix a broken `chocolatey` lock
state on the Windows host (worked around with no-installer WinLibs
mingw-w64 + a standalone `7zr.exe`, both fetched fresh, never vendored);
no `mtools` reachable on Windows (`driver/vsdev/write-fat12-floppy.py` /
`read-fat12-file.py` — a real, minimal FAT12 reader/writer, narrowly
reimplementing exactly the two operations `run-test.sh` needs); a
dynamically-linked `busybox` package silently breaking the ntbridge
test's initramfs on the new Linux host (`tests/reactos/
build-testguest-initramfs.sh` now checks for this directly with `ldd`
rather than trusting that "a busybox exists" means "a working one").

**Re-verified for real, not re-asserted:**

- **`ntabi`/`ntd` (Phases 2/3/12):** `make check` — **60/60 tests
  passing** on the real Linux build server, byte-for-byte the same
  suite and results this project's original sandbox established.
- **ntbridge protocol/transport (Phases 4/7):** `tests/reactos/
  run-test.sh` — real **PASS** on the Linux build server after the
  `busybox-static` fix above: guest heartbeat detected, 3/3 seeded
  devices ACKed, over a real QEMU VM boundary on new hardware.
- **`vsdev.sys`/`vsdev_test.exe` (Phase 5):** build cleanly with the
  real `x86_64-w64-mingw32-gcc` toolchain on *both* new hosts (WinLibs
  on Windows, the real Debian `gcc-mingw-w64-x86-64` package on Linux)
  — the driver itself, not just the toolchain claim, confirmed portable.
  The automated `run-test.sh` load/I/O test itself did **not** reach
  PASS on either new host this pass — see `driver/vsdev/README.md`'s
  "Known gaps" for the precise, screenshot-documented reason: a real,
  reproducible flake in the script's desktop-icon-launch step against
  current ReactOS nightlies, not a driver regression (`dismiss-dialog`
  and the LiveCD boot itself both still work correctly).
- **WireGuard (Phase 15):** see that phase's own new subsection — the
  kernel-module blocker is resolved on real hardware, confirmed live.

**What this pass does not change:** none of Phase 6 (VFIO/IOMMU —
still needs real second-GPU hardware), Phase 10 (Steam/GPU gaming
verification — needs a real Linux desktop session with a GPU, not a
headless build server), Phase 12's Wine `ntdll` integration (still
needs a real Wine source build, 30-60+ minutes, not attempted), or
Phase 13 (RosBE — still a materially larger, separate undertaking than
the mingw-w64 DDK toolchain used throughout this pass). Toolchain
availability was this pass's real, common blocker across several
phases at once; hardware/environment-shape blockers (a second GPU
behind VFIO, a real gaming desktop session, a Wine/RosBE source build)
are unrelated to it and remain exactly as documented in each phase
above.

---

## Immediate next steps (unordered backlog, section 53)

1. ~~Create base distro image.~~ ✅ built and boot-verified (see Phase 0).
2. ~~Package current Wine + Proton stack.~~ ✅ installed and confirmed
   working in a booted shell (see Phase 0).
3. ~~Enable NT-oriented Linux synchronization support (`ntsync`).~~ ✅
   `/dev/ntsync` confirmed present on the booted kernel (see Phase 0).
4. ~~Implement `ntloader`.~~ ✅ compiled, registered with real kernel
   binfmt_misc, ran a real PE binary directly (see Phase 1).
5. ~~Make PE binaries directly executable.~~ ✅ same binfmt_misc
   verification as above (see Phase 1).
6. ~~Build per-application NT environment manager.~~ ✅
   `tooling/installer/ntlinux-app` (see Phase 1).
7. ~~Implement `libntabi` protocol skeleton.~~ ✅ grew well past
   "skeleton" — full object manager (see Phase 2 + Phase 3).
8. Route one small set of `ntdll` operations through it — moved to
   Phase 12 with the rest of the Wine `ntdll` integration work (see
   Phase 2's "known gap" and Phase 12).
9. ~~Create ReactOS minimal driver-cell build experiment.~~ ✅ (partially
   — real ReactOS boots under `ntcell`, but it's the stock ISO, not yet
   the stripped ROS-NTCELL profile; see Phase 4.)
10. Boot that build under KVM with no desktop — booted under **TCG**,
    not KVM (no `/dev/kvm` in this sandbox — see Phase 4), and it's the
    stock ISO's desktop-capable Setup, not a no-desktop ROS-NTCELL
    build. Both caveats tracked in Phase 4, not silently dropped.
11. ~~Create `ntbridge` shared-memory hello/heartbeat protocol.~~ ✅
    verified end-to-end over a real QEMU VM boundary (see Phase 4).
12. ~~Send synthetic PCI/USB device descriptions from Linux to
    ReactOS.~~ ✅ against the honest stand-in guest, not real ReactOS
    yet — see Phase 4's precise accounting.
13. Confirm ReactOS PnP creates device nodes — **still not done**, but
    the reason changed: `ntbridge_pnp.c` builds fine without RosBE (see
    Phase 4's correction and Phase 13) — what's missing is actually
    driving a PnP-enumerated install and observing a real device node
    created, which Phase 5's `vsdev.c` also hasn't exercised yet (it
    loaded via the legacy `sc start` path, not real PnP enumeration).
14. ~~Load a simple test `.sys`.~~ ✅ `driver/vsdev/vsdev.sys` — real,
    hand-written, loaded live inside a real ReactOS x64 kernel (see
    Phase 5).
15. Deliver `IRP_MN_START_DEVICE` — **not done**: `vsdev.c` implements
    the handler, but the legacy load path Phase 5 actually verified
    doesn't send it (only real PnP enumeration does) — see item 13.
16. Bridge test I/O back to Linux — **not done**: Phase 5 proved real
    I/O works (`vsdev`'s own loopback buffer), but it doesn't yet route
    through `ntbridge` to reach Linux — see Phase 5's precise
    accounting.
