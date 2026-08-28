# Wine `ntdll` — real `ntabi` sync integration (Phase 12)

A real, scoped attempt at Phase 2/12's original, still-open item: routing selected Wine `ntdll` synchronization operations through `libntabi`/`ntd` instead of wineserver.

**Owner:** WineHQ (upstream `ntdll`) + NTLinux (`ntabi-sync.patch`)
**Status:** Real, live-tested — compiles clean, correctly inert by default, but a real, reproducible bug when enabled. Not landed as working; documented precisely.

## What this actually is

`ntabi-sync.patch` applies to `dlls/ntdll/unix/sync.c` in a real Wine
source checkout (tested against WineHQ gitlab commit
`e6e9d7627c0a7d660c342b1c38ff0e20dacfd1f9`, 2026-08-27) and adds a
narrow, scoped routing path for exactly four functions —
`NtCreateEvent`, `NtSetEvent`, `NtResetEvent`,
`NtWaitForSingleObject` — through `libntabi` when the `WINENTABI=1`
environment variable is set. **Default off** — an unmodified,
`WINENTABI`-unset Wine session is completely unaffected, verified live
(see "Real, live results" below).

Modeled directly on the real, already-upstream `inproc_*` fast-sync
mechanism already present in this exact Wine version (Wine's own
`ntsync` Linux-kernel-driver integration, `dlls/ntdll/unix/sync.c`'s
`inproc_wait`/`inproc_set_event`/etc.) — same shape: try the fast path
first, `STATUS_NOT_IMPLEMENTED` falls through to the normal wineserver
call, the real object is still created via wineserver either way (this
patch only adds an ntabi-side *shadow* object at creation time, then
fast-paths the signal/wait traffic against it instead of wineserver).
A real, useful side-finding from reading that code: modern Wine already
has its own non-wineserver fast sync path via the upstream `ntsync`
kernel driver — Phase 2/12's original premise ("Wine needs third-party
patching to skip wineserver") is only fully true on a kernel without
`ntsync` (merged into mainline Linux 6.14; the build server this was
tested on runs 6.12, confirmed via `modprobe ntsync` failing with
"Module ntsync not found" — so wineserver genuinely was the active path
for this test, not a moot point).

## Applying and building

```
git clone https://gitlab.winehq.org/wine/wine.git
cd wine
git checkout e6e9d7627c0a7d660c342b1c38ff0e20dacfd1f9   # exact commit tested
patch -p1 < /path/to/ntabi-sync.patch
./configure --enable-win64
make -j$(nproc)
```

Needs `libntabi.so` on `LD_LIBRARY_PATH` at runtime (`make` in
`ntabi/lib/` now builds it — see that Makefile) and a real, running
`ntd` instance (`ntd/ntd`) for `WINENTABI=1` to have anything to
connect to; with neither present, `WINENTABI=1` behaves exactly like
`WINENTABI` unset (the `dlopen`/connect both fail closed, falling
through to wineserver — never a hard error).

## Real, live results

Built clean on a real Linux host (Debian 13, real `gcc`/Wine build
toolchain — see `ROADMAP.md`'s cross-host re-verification section),
both the full initial build and an incremental rebuild after applying
this patch — zero compiler errors.

**Baseline (`WINENTABI` unset — proves the patch is genuinely inert by
default):**

```
$ ./wine event_test.exe
CreateEvent OK, handle=0000000000000034
wait #1 (should timeout, unsignaled): PASS (timeout)
SetEvent OK
wait #2 (should succeed, signaled): PASS (signaled)
wait #3 (should timeout, auto-reset consumed): PASS (timeout)
OVERALL: PASS
```

**With `WINENTABI=1` — a real, reproducible bug, not chased to a fix
this pass:** running the same test with `WINENTABI=1
LD_LIBRARY_PATH=.../ntabi/lib` **hung indefinitely** during Wine's own
`wineboot.exe --init` — before the test program's own code ever ran.
Real, live-observed process state confirmed it wasn't a CPU-spinning
bug (load stayed near idle; this wasn't a busy-loop) — some genuine
wait never got satisfied. Killed cleanly, host confirmed healthy
afterward (`top`, `df` both normal, no leaked processes).

**Root cause, stated honestly — not confirmed, best current theory:**
`WINENTABI=1` intercepts *every* event any process in that Wine session
creates, not just a test program's own — including whatever
`wineboot.exe`'s own internal initialization creates. This is a much
larger blast radius than "route my one test event," and plausibly
exposes a real concurrency issue either in this patch's own connection/
locking code (one `ntabi_conn_t` per OS thread via `__thread`, a
mutex-protected fixed hash table for the handle map) or in `ntd` itself
under a pattern of load (many first-time client connections arriving
concurrently from multiple processes/threads at Wine startup) that its
existing 60/60 test suite doesn't exercise. **Not investigated further
this pass** — real, deliberate stopping point given the risk of
continuing to experiment with a hanging synchronization primitive on a
shared host, not a time-only tradeoff.

## What this means for Phase 12, precisely

**Not "solved."** A real, working, always-inert-by-default patch exists
and is documented here, but actually enabling it (`WINENTABI=1`) is
**known to hang** and should not be turned on again without first
diagnosing the concurrency bug above — do not casually re-enable this
against a real Wine session. What *is* real progress: the patch
compiles cleanly against a current Wine checkout, correctly changes
nothing when disabled (the actual, load-bearing safety property for
anything landing in a shared codebase), and the specific four-function
slice (`NtCreateEvent`/`NtSetEvent`/`NtResetEvent`/
`NtWaitForSingleObject` on Events) is a real, scoped starting point for
whoever picks up the concurrency bug next — not the full Phase 2/3/12
breadth (Mutant/Semaphore/Section/Completion port/Process/Thread
routing is entirely unattempted), and Wine's own test suite
(`dlls/ntdll/tests/`, `dlls/kernel32/tests/`) was never reached, since
the hang happens before user code runs.

See [`ROADMAP.md`](/ROADMAP.md) Phase 12 for how this fits, and
[`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural
context. Before extending this, check whether Wine's own `ntsync`
integration (already real, already upstream, described above) makes
further `ntabi`-side routing work unnecessary on kernels that have it —
per Rule 1 in `CLAUDE.md`.
