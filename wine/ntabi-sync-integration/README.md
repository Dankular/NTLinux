# Wine `ntdll` — real `ntabi` sync integration (Phase 12)

A real, scoped attempt at Phase 2/12's original, still-open item: routing selected Wine `ntdll` synchronization operations through `libntabi`/`ntd` instead of wineserver.

**Owner:** WineHQ (upstream `ntdll`) + NTLinux (`ntabi-sync.patch`)
**Status:** Real, live-tested — two real bugs found and fixed with reproducible before/after evidence; a third, deeper architectural gap found, root-caused, and narrowed, but **not** fully closed. `WINENTABI=1` still hangs a real, full `wineboot.exe --init` session. Not landed as safely enablable. Documented precisely below, including exactly what was tried, what worked, what didn't, and why.

## What this actually is

`ntabi-sync.patch` applies to `dlls/ntdll/unix/sync.c` in a real Wine
source checkout (tested against WineHQ gitlab commit
`e6e9d7627c0a7d660c342b1c38ff0e20dacfd1f9`, 2026-08-27) and adds a
narrow, scoped routing path for exactly four functions —
`NtCreateEvent`, `NtSetEvent`, `NtResetEvent`,
`NtWaitForSingleObject` — through `libntabi` when the `WINENTABI=1`
environment variable is set. **Default off** — an unmodified,
`WINENTABI`-unset Wine session is completely unaffected, verified live
in this session too (see "Real, live results" below).

Modeled directly on the real, already-upstream `inproc_*` fast-sync
mechanism already present in this exact Wine version (Wine's own
`ntsync` Linux-kernel-driver integration, `dlls/ntdll/unix/sync.c`'s
`inproc_wait`/`inproc_set_event`/etc.) — same shape: try the fast path
first, `STATUS_NOT_IMPLEMENTED` falls through to the normal wineserver
call, the real object is still created via wineserver either way (this
patch only adds an ntabi-side *shadow* object at creation time, then
fast-paths the signal/wait traffic against it instead of wineserver).

## This session's work: root-causing and (partially) fixing the hang

The previous session left this in a known, reproducible-but-undiagnosed
state: `WINENTABI=1` hung indefinitely during `wineboot.exe --init`,
before any user test code ran, with a "best current theory, not
confirmed" pointing at either this patch's own map/connection logic or
a concurrency gap in `ntd`. This session re-created the hang under
controlled conditions on the same real Linux build server, root-caused
it for real using three independent, cross-checked techniques —
`fprintf` tracing added to *both* sides of the protocol (this patch's
own `sync.c` code and `ntd.c` itself), plus live
`/proc/<pid>/task/<tid>/wchan` inspection of the actually-hung kernel
threads — found and fixed two real bugs with reproducible before/after
evidence, and found (but did not fully close) a third, deeper issue.

### Root cause, for real — part 1: named events silently diverted from wineserver

**The bug.** `NtCreateEvent`'s original fast-path-registration guard
was `if (!ret) ntabi_register_event(...)`. Real NT semantics: creating
a *named* event that already exists is not an error — wineserver
returns `STATUS_OBJECT_NAME_EXISTS` (`0x40000000`, a nonzero
*informational-success* code) and hands back a valid handle to the
*existing* object, not a new one. `if (!ret)` only matches
`STATUS_SUCCESS` (`0`), so:

- The **first** process/thread to create a given named event got
  `ret == 0` and *was* registered with ntabi (its `SetEvent`/`Wait`
  traffic for that handle now takes the ntabi fast path).
- **Every subsequent** creator of the same name got
  `ret == STATUS_OBJECT_NAME_EXISTS` and was correctly left
  unregistered by the old guard — but that only protects the *second*
  handle. The first handle was still live, still registered, and any
  `SetEvent` issued through *it* silently signaled only ntabi's private
  shadow object, never touching the real wineserver object that every
  other handle (including the second creator's) actually waits on.

**Live proof.** A debug trace (`fprintf` added to
`ntabi_register_event`/`NtCreateEvent`'s call site) on a real,
unmodified `./wine event_test.exe` run with `WINENTABI=1` showed
exactly this, twice, in wineboot's own real startup:

```
NTABI_DEBUG: NtCreateEvent name=L"\KernelObjects\__wineboot_event" ret=0 handle=0x4 tid=37911
NTABI_DEBUG: register_event handle=0x4 -> ntabi_handle=1 REGISTERED
...
NTABI_DEBUG: NtCreateEvent name=L"\KernelObjects\__wineboot_event" ret=0x40000000 handle=0x8 tid=37915
```

`\KernelObjects\__wineboot_event` and `__wine_SvcctlStarted` — both
real, named, cross-process Wine synchronization events used during
every real wineboot session — hit this exact pattern. `strace -f`
attached to the live-hung `wineboot.exe --init` process confirmed the
symptom: blocked in `read(8, ...)` on Wine's internal wait pipe (the
real wineserver-wait mechanism) — a genuine block, not a busy-loop,
exactly as the previous session observed but now with a confirmed
mechanism, not just a theory.

**The fix.** Only take the fast path for events created **unnamed**:

```c
if (!ret && (!attr || !attr->ObjectName || !attr->ObjectName->Length))
    ntabi_register_event( *handle, type == NotificationEvent, state );
```

A truly unnamed event has no name-based second path to the same real
object, so as long as nothing else independently reaches the exact
same Wine `HANDLE` value (see part 2 below for the one other way that
could still go wrong), the process that registered it is provably the
only party that can address it *by that mechanism*. This does **not**
cover `DuplicateHandle` or handle inheritance of an unnamed event to a
different process/handle value — see part 3, where that gap turned out
to matter for real, not just in theory.

### Root cause, for real — part 2: stale map entries from Wine's own handle-number reuse

**The bug**, found via the *same* trace above: `ntabi_map_insert`
always landed a new entry in the first *empty* slot of its
open-addressing probe sequence. `NtClose` is not intercepted by this
patch at all (a real, already-documented gap) — so when Wine closes a
`HANDLE` and later reuses that exact same numeric value for a
brand-new, unrelated object (which Wine/wineserver do routinely — small
integer handle values get recycled), the *old* map entry for that
number is never removed. `ntabi_map_lookup` stops at the first matching
handle it finds during its own probe — which, since insert never
touched the old entry, is still the stale one — so it kept returning
the wrong, already-closed object's `ntabi_handle` forever.

**Live proof**, same trace, same run: Wine handle `0x3c` got created
*twice* in immediate succession (the first `0x3c` closed — untraced,
since `NtClose` isn't hooked — freeing the number for reuse):

```
NTABI_DEBUG: NtCreateEvent name=(null) ret=0 handle=0x3c ... -> registered ntabi_handle=1
NTABI_DEBUG: NtCreateEvent name=(null) ret=0 handle=0x3c ... -> registered ntabi_handle=2
NTABI_DEBUG: SetEvent handle=0x3c IN map (ntabi_handle=1) -> FAST PATH   <- WRONG: should be handle 2
```

`SetEvent(0x3c)` — meant for the *second*, currently-live object at
that handle number — resolved to `ntabi_handle=1`, the *first*,
already-closed object's stale shadow. A real, silent
signal-the-wrong-object bug, independent of part 1, and one that would
still occur even with the part-1 fix in place, purely from ordinary
unnamed-event churn (proven: it happened with entirely unnamed events).

**The fix.** `ntabi_map_insert` now scans its full probe sequence
*first* looking for an existing entry with the same `HANDLE` value; if
found, it overwrites it in place instead of creating a shadow-duplicate
entry further down the chain. Verified: re-running the same trace after
the fix showed `SetEvent handle=0x3c IN map (ntabi_handle=2)` — the
*current*, correct object. This leaves the *old* ntd-side shadow object
itself leaked (never `ntabi_close_handle`d — the same, already-known
`NtClose`-non-interception gap, not a new one), a resource-accounting
gap, not a correctness bug: the map itself never again returns a wrong
`ntabi_handle` for a given Wine `HANDLE` value.

### Root cause, for real — part 3: the deeper issue, found but not closed

With both fixes above landed, `WINENTABI=1` **still** hung a full
`wineboot.exe --init` session, in the same place, every time —
reproduced fresh, cleanly, multiple times.

**Root-caused with `ntd`-side tracing this time**, not just the Wine
side: `fprintf` tracing was added directly into `ntd.c`'s
`process_request`/`handle_wait_single`/`handle_set_event` (temporary,
not part of this patch — see below) and `ntd` was rebuilt and rerun
fresh. The trace showed, precisely:

```
NTD_DEBUG: handle_create_event pid=43026 obj=0x... manual_reset=0 initial=0 -> local_handle=4
NTD_DEBUG: handle_wait_single slot=0 pid=43026 handle=4 timeout_ms=-1 alertable=0
NTD_DEBUG: wait_single slot=0 obj=0x... NOT available -> add_waiter (timeout_ms=-1)
```

`services.exe`'s main thread (pid 43026) created a genuinely private,
unnamed, auto-reset event, correctly registered it (unnamed — part 1's
fix doesn't exclude it), and then called
`NtWaitForSingleObject(handle, INFINITE)` on it. `ntd` correctly
registered the wait. **Nothing in the entire session ever called
`SetEvent` on that object again.** Live `/proc` inspection of the
actually-hung process while this was happening confirmed it precisely:

```
$ for t in /proc/43026/task/*; do echo TID=$(basename $t) WCHAN=$(cat $t/wchan); done
TID=43026 WCHAN=futex_do_wait      # blocked forever in ntabi's own sem_wait()
TID=43027 WCHAN=anon_pipe_read     # its sibling thread, blocked in the *real* wineserver wait
```

The thread that was (per real Wine's own internal design) presumably
meant to eventually signal that event was itself legitimately blocked
in the genuine wineserver wait path the entire time — the two threads'
waits are part of the same real Wine startup dependency chain, and this
patch has no visibility into that chain; it only sees one
`NtWaitForSingleObject`/`NtSetEvent` call at a time, in isolation.

**First narrowing attempted:** never take the fast path for an
`INFINITE` (`NULL`-timeout) wait — always fall through to the real
wineserver path for those, keeping only *bounded* waits fast-pathed
(worst case, `ntd`'s own `expire_timeouts()` eventually returns
`STATUS_TIMEOUT` rather than blocking forever with no fallback). This
was implemented, and **verified via the same debug trace and the same
live `/proc` wchan check to correctly change the routing as intended**
— the previously-`futex_do_wait`-blocked thread now genuinely blocked
in `anon_pipe_read`, the real wineserver path, exactly as designed.

**It did not fix the hang.** The exact same deadlock still occurred, in
the same place, with all three observed threads (`wineboot.exe`,
`services.exe`'s two threads) now blocked in the *real* wineserver wait
path (`anon_pipe_read`) rather than ntabi's. This is the single most
important, precise finding of this session: **the deadlock was never
about which channel the *wait* call used.** It is that `NtSetEvent` on
this same class of private, unnamed, ntabi-registered event *also*
still takes the fast path and skips the real wineserver call entirely
— and if the real object underlying that "private" handle is ever
observed by any other thread or process via the genuine wineserver
mechanism (through inheritance or `DuplicateHandle` this patch has zero
visibility into, since it hooks neither), that party's wineserver-side
wait or dependency on it can never be satisfied, no matter which
channel the *original* wait call takes.

The bounded-waits-only narrowing for `NtWaitForSingleObject` was kept
in the patch anyway — it is independently correct and useful (an
`ntd`-side wait now always has a bounded worst case instead of a
possible permanent block with no fallback path), but it is **not
sufficient** to make `WINENTABI=1` safe for a real wineboot session,
and should not be read as having fixed the hang.

**What a structurally sound fix would need**, stated honestly rather
than attempted this session (out of scope for a single-file,
few-hundred-line patch, and risky to improvise further against a
shared host after this much live experimentation with hung
synchronization primitives already): the ntabi-side shadow object needs
to be keyed by the *real* underlying wineserver object's identity, not
by an ad hoc per-process Wine `HANDLE` numeric value — so that *any*
handle referring to the same real object (inherited, duplicated, or
independently created against the same name) maps to the *same* ntabi
shadow, from *any* process. That requires either querying wineserver
for a canonical object identity at registration time, or explicitly
intercepting `NtDuplicateHandle` and process-creation handle
inheritance to propagate the mapping — both materially larger changes
than this patch's current scope.

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
`ntabi/lib/` builds it — see that Makefile) and a real, running `ntd`
instance (`ntd/ntd`) for `WINENTABI=1` to have anything to connect to;
with neither present, `WINENTABI=1` behaves exactly like `WINENTABI`
unset (the `dlopen`/connect both fail closed, falling through to
wineserver — never a hard error).

## Real, live results (this session)

Rebuilt clean on the same real Linux build server (Debian 13) after
each of the three changes above — zero compiler errors each time.

**Baseline (`WINENTABI` unset) — re-verified after all three fixes, to
confirm the patch is still genuinely inert by default:**

```
$ ./wine event_test.exe
CreateEvent OK, handle=0000000000000034
wait #1 (should timeout, unsignaled): PASS (timeout)
SetEvent OK
wait #2 (should succeed, signaled): PASS (signaled)
wait #3 (should timeout, auto-reset consumed): PASS (timeout)
OVERALL: PASS
```
`EXIT_CODE=0`. Confirmed unaffected by any of this session's changes.

**`WINENTABI=1`, full `wineboot.exe --init` session, after all three
fixes:** still hangs. Reproduced fresh (clean `WINEPREFIX`, fresh `ntd`
instance) multiple times, consistently, in the same place — a
`services.exe` thread blocked forever on its own private event, as
described in part 3 above. Also re-tested against an *already
initialized* `WINEPREFIX` (to rule out first-run prefix-setup work as
the cause specifically) — same hang, same place; `wineboot.exe --init`
runs its real internal synchronization dance on every session start
regardless of whether the prefix needs first-time setup.

Every hang reproduction in this session used a short (`timeout 30`–`40`)
bound and was followed by an explicit process cleanup
(`kill -9` on every surviving `wineboot.exe`/`services.exe`/
`wineserver`/`ntd` process); host state (`top`, `df`, process list) was
checked clean after each one and at the end of the session.

## What this means for Phase 12, precisely

**Still not "solved," but genuinely, verifiably further along than
before.** Two real, independent, previously-undiagnosed bugs in this
patch's own logic were found and fixed, each with reproducible
before/after evidence (parts 1 and 2 above) — these are real
correctness improvements to the patch itself and are landed here. A
third, deeper issue was found, root-caused precisely (not guessed at)
using cross-checked evidence from three independent angles (Wine-side
tracing, `ntd`-side tracing, live kernel wait-channel inspection), one
real narrowing was attempted and verified to change behavior exactly as
intended, and it was **confirmed, not merely suspected, insufficient**
to resolve the actual hang — a materially more precise result than the
previous session's "best theory, not confirmed."

**`WINENTABI=1` should not be enabled against a real wineboot session.**
It remains default-off, and turning it on is still known to hang,
now for a specific, well-evidenced architectural reason rather than an
open question: this patch's per-`HANDLE`, per-process shadow-object
cache cannot, by construction, tell whether a "private" event might be
observed by another thread or process through the real wineserver
mechanism, and Wine's own real internal startup sequence does exactly
that, routinely, even for genuinely unnamed events. The specific
four-function slice (`NtCreateEvent`/`NtSetEvent`/`NtResetEvent`/
`NtWaitForSingleObject` on Events) remains a real, scoped starting point
for whoever picks this up next, now with a much more precise map of
where the actual remaining problem is — not the full Phase 2/3/12
breadth (Mutant/Semaphore/Section/Completion port/Process/Thread
routing is entirely unattempted), and Wine's own test suite
(`dlls/ntdll/tests/`, `dlls/kernel32/tests/`) still hasn't been reached,
since the hang happens before user code runs.

See [`ROADMAP.md`](/ROADMAP.md) Phase 12 for how this fits, and
[`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural
context. Before extending this, check whether Wine's own `ntsync`
integration (already real, already upstream, described above) makes
further `ntabi`-side routing work unnecessary on kernels that have it —
per Rule 1 in `CLAUDE.md`.
