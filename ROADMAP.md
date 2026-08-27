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
      `tooling/compat-db/ntexports/` is for — see its README.

**What Phase 2 actually proves, stated precisely:** the protocol, the
daemon, and the client library are real and correct — genuine NT object
semantics (not POSIX primitives wearing NT names), enforced across real
process boundaries, with a real blocking-wait implementation and a real
versioned-protocol guard. What it does *not* yet prove: that any of this
sits behind a real `ntdll` boundary, or that a real Windows/Wine
application's synchronization calls could be transparently routed through
it without behavior changing. That's the honest boundary of "prototype."

---

## Phase 3 — Object and wait semantics ⬜

Move named objects, handle tables, wait-any/wait-all, sections, shared
memory, process/thread metadata, completion ports, APC support toward
NTLinux-native implementation.

**Success criterion:** measured reduction in wineserver IPC traffic.

---

## Phase 4 — ReactOS driver-cell prototype ⬜

Deliver a minimal bootable ReactOS image, KVM launcher, `ntbridge`, logging
channel, host heartbeat, device enumeration bridge.

**Success criterion:** ReactOS sees synthetic devices supplied by Linux.

---

## Phase 5 — First Windows driver ⬜

Start with a simple virtual/low-risk device (virtual serial, virtual block,
simple USB, virtual network adapter). Avoid GPU drivers initially.

**Success criterion:** a real Windows `.sys` loads, receives PnP IRPs, and
performs I/O through the host bridge.

---

## Phase 6 — VFIO hardware passthrough ⬜

Add PCI BAR mapping, MSI/MSI-X delivery, DMA mappings, device reset, IOMMU
protection, resource descriptors.

**Success criterion:** a selected real Windows PCI driver operates physical
hardware while Linux stays stable if the cell crashes.

---

## Phase 7 — NDIS bridge ⬜

**Success criterion:** a Windows NIC driver exposes a working Linux network
interface.

---

## Phase 8 — USB driver bridge ⬜

**Success criterion:** a vendor Windows USB driver operates a device
unavailable through a native Linux driver.

---

## Phase 9 — Modern ReactOS driver compatibility ⬜

Upstream work on KMDF, newer WDM, newer NT exports, modern NDIS, PnP, power,
memory management, security. Maintain conformance tests against documented
Windows behavior.

---

## Phase 10 — Gaming integration ⬜

Tight Steam/Proton/NTLinux runtime/Gamescope/DXVK/vkd3d-proton integration
plus the driver compatibility database. Windows applications and games
should feel like native Linux packages.

---

## Phase 11 — Native Windows GPU driver hosting (research) ⬜

Not an MVP dependency (`docs/ARCHITECTURE.md` section 30) — the default
gaming path stays DXVK/vkd3d-proton on Mesa/Vulkan (section 47), and this
phase never gets ahead of that. Tracked explicitly because the payoff is
real and the mechanism already exists rather than needing new invention
(ADR-0003 in `docs/DECISIONS.md`): host a Windows WDDM GPU driver in the
same ReactOS driver cell used for any other kernel driver, with VFIO/IOMMU
mediating the actual hardware.

**Depends on:** Phases 4-9 — a GPU driver is one of the hardest classes of
Windows driver, not a good early target (section 55: avoid the primary GPU
in earlier phases). Requires ReactOS WDDM/DXGKRNL support that mostly
doesn't exist yet (section 29) — build it there, upstreamed, per Rule 3.

**Why it's worth it:** covers hardware/workloads Mesa can't — brand-new
GPUs ahead of open-source driver support, vendor features or professional/
compute workloads that only ship a Windows driver, hardware where the
Linux driver is meaningfully behind.

**Success criterion:** a real Windows WDDM GPU driver operates a
non-primary GPU inside the driver cell, rendering output reaches the host
compositor through the existing VFIO/IOMMU-mediated path, and Linux
survives a driver crash inside the cell (section 26).

---

## Immediate next steps (unordered backlog, section 53)

1. ~~Create base distro image.~~ ✅ built and boot-verified (see Phase 0).
2. ~~Package current Wine + Proton stack.~~ ✅ installed and confirmed
   working in a booted shell (see Phase 0).
3. ~~Enable NT-oriented Linux synchronization support (`ntsync`).~~ ✅
   `/dev/ntsync` confirmed present on the booted kernel (see Phase 0).
4. Implement `ntloader`.
5. Make PE binaries directly executable.
6. Build per-application NT environment manager.
7. Implement `libntabi` protocol skeleton.
8. Route one small set of `ntdll` operations through it.
9. Create ReactOS minimal driver-cell build experiment.
10. Boot that build under KVM with no desktop.
11. Create `ntbridge` shared-memory hello/heartbeat protocol.
12. Send synthetic PCI/USB device descriptions from Linux to ReactOS.
13. Confirm ReactOS PnP creates device nodes.
14. Load a simple test `.sys`.
15. Deliver `IRP_MN_START_DEVICE`.
16. Bridge test I/O back to Linux.
