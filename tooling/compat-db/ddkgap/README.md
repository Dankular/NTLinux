# ddkgap — Kernel-Mode DDK Compatibility Gap Probe

Phase 9's first deliverable: a real, re-runnable probe of what this
project's actual DDK toolchain (mingw-w64's packaged headers + import
libraries, the same toolchain every driver in `driver/` builds against)
supports today, for the specific facilities ROADMAP.md Phase 9 names —
KMDF, newer WDM, newer NT exports, modern NDIS, PnP, power, memory
management, security.

**Owner:** NTLinux
**Status:** Built and run; real gap data captured (Phase 9)

## Why this exists

Phase 9's own text ("Upstream work on KMDF, newer WDM, newer NT
exports, modern NDIS, PnP, power, memory management, security.
Maintain conformance tests against documented Windows behavior") is a
large, open-ended research program, not a single boundable task. Before
any of that upstream work can be scoped honestly, this project needs to
know what its *own toolchain* already supports — the same reasoning
that produced `tooling/compat-db/ntexports/` (the user-mode equivalent:
before tracking "is `NtCreateFile` implemented," get the real export
list first) and that corrected Phase 4/5's original RosBE assumption
(checked by actually trying to build, not assumed). Rather than write
speculative "future work" prose for Phase 9, this tool answers, for
real, right now: for each curated driver-relevant NT kernel facility, is
it (a) importable from this DDK's `ntoskrnl`/`hal`/`ndis` import
libraries, and (b) actually declared by some header this DDK ships —
two independently real, run checks, not one proxy for both.

## What it checks, and how

- **`abi_present`**: `nm` against the real `.a` import library
  (`libntoskrnl.a`, `libhal.a`, `libndis.a`) for the exact symbol name.
- **`header_declared`**: a real recursive grep across the actual
  installed DDK + root mingw-w64 include trees for that symbol as a
  whole word — not a version-number heuristic, not a guess from
  documentation.
- **KMDF**: not a per-symbol question — checked as a single yes/no by
  looking for any `wdf*.h` header or `libwdf*.a`/`Wdf01000*` import
  library anywhere in the toolchain.
- **WDDM/DXGKRNL**: same whole-framework shape as KMDF, added for
  Phase 11/ADR-0003 (native Windows GPU driver hosting) — checks for
  any `dxgkrnl*.h`/`d3dkmthk*.h`/`d3dukmdt*.h`/`kmddod*.h` header or a
  `dxgkrnl` import library. Deliberately distinct from this toolchain's
  abundant *usermode* Direct3D headers (`d3d9.h`, `d3d11.h`, `d3d12.h`,
  ...), which are for applications consuming Direct3D, not a kernel-mode
  driver implementing a WDDM miniport.

A symbol can be `abi_present` without `header_declared` — found for
real, not hypothesized, running this: `NdisMRegisterMiniportDriver`,
`NdisMSetMiniportAttributes`, `NdisMIndicateReceiveNetBufferLists`,
`NdisAllocateNetBufferListPool` (the NDIS 6.x connectionless miniport
registration/data-path entry points) are all real import stubs in
`libndis.a`, but **no header in this DDK actually prototypes them** —
`driver/net/reactos/ntnet.c` had to target the older NDIS 5.1
`NdisMRegisterMiniport`/`NdisMEthIndicateReceive` surface specifically
*because* that's what `ndis.h` actually declares (see that file's own
header comment); this tool is what turns "ntnet.c happened to need
NDIS 5.1" into a documented, generalizable finding rather than an
isolated one-off.

## Real results, this session

```
$ python3 ddkgap.py

PnP:                                    all 4 checked — full
Power:                                  all 3 checked — full
Memory management:
  ExAllocatePoolWithTag                 full
  ExAllocatePool2                       absent   (Windows 10 2004+ - expected)
  MmProtectMdlSystemAddress             full
  KeInitializeGuardedMutex              full
Security:
  SeAccessCheckEx                       abi-only (header gap)
  ObRegisterCallbacks                   full
  ObUnRegisterCallbacks                 full
  CmRegisterCallbackEx                  full
  PsSetCreateProcessNotifyRoutineEx2    absent   (Windows 10 - expected)
Interrupts:                             both checked — full
Filter Manager (minifilters):
  FltRegisterFilter                     absent
NDIS 6.x (connectionless miniport model): all abi-only (header gap)
  as reported by this probe - but see the correction below

KMDF: not pre-packaged by mingw-w64 - but see the correction below
WDDM/DXGKRNL: not pre-packaged by mingw-w64 - but see the correction below
```

**Correction, made live, not left standing:** both "not present"
findings above checked only whether mingw-w64 *pre-packages* these
headers (it doesn't) and reported that as though it settled a bigger
question. It didn't. Investigating Phase 11 (`ROADMAP.md`) found the
real Microsoft WDK/SDK headers for both KMDF (`c/Include/wdf/kmdf/
1.11/*.h`) and WDDM/DXGKRNL (`dispmprt.h`/`d3dkmddi.h`/`d3dkmdt.h`/
`d3dukmdt.h`) are real, official, and fetch cleanly from Microsoft's own
NuGet packages — confirmed by actually downloading them, not by
checking whether mingw-w64 happens to include a copy.
`driver/gpu/fetch-wdk-headers.sh` fetches the WDDM set (never vendored —
the WDK's own EULA forbids redistribution); `driver/gpu/wddm-probe.c`
proves a real `DriverEntry` against the real `DXGKRNL_INTERFACE`
contract compiles against this toolchain, live-verified, with a few
narrow patches (same technique as the NDIS fixes) — with one genuine,
unresolved cross-compiler ABI finding (a Microsoft `static_assert`
that fails under GCC/mingw-w64) documented there, not hidden. KMDF's
real headers were found the same way but not yet compile-verified. See
`driver/gpu/README.md` and `ROADMAP.md` Phase 11 for the full account.

A third correction, made the same way, closes this file's own NDIS 6.x
finding above: `driver/net/reactos/ndis6-probe/fetch-ndis6-headers.sh`
fetches the same real WDK/SDK packages' full NDIS 6.x header set
(`km/ndis.h` and its support headers), and
`driver/net/reactos/ndis6-probe/ndis6-probe.c` — a real NDIS 6.x
connectionless-miniport `DriverEntry` referencing all four symbols
this probe flags — **compiles and links clean** against this
toolchain's real `libndis.a`, going one step further than the
KMDF/WDDM probes (neither has an import library to link against at
all). See `driver/net/reactos/ndis6-probe/README.md` for the full,
live-verified account.

## What this actually says about Phase 9's scope

Genuinely more complete than assumed going in: the WDM-level PnP,
power, interrupt, and most security/memory-management surface up
through roughly the Windows 7/8 era is **already fully present, header
and ABI both** — `IoRegisterPlugPlayNotification`,
`PoRegisterPowerSettingCallback`, `ObRegisterCallbacks`,
`CmRegisterCallbackEx`, `IoConnectInterruptEx`, and friends all work
today with no toolchain changes, consistent with ARCHITECTURE.md's own
stated ReactOS compatibility level (NT 5.2/Server 2003-ish) actually
running noticeably ahead of that in several of these areas.

Three real, narrow, distinct gaps, not one vague "needs modernizing":

1. **NDIS 6.x had ABI but no headers - closed, not left at "would
   unlock this."** The real Microsoft WDK/SDK packages (checked first,
   per CLAUDE.md Rule 1's own priority order) turned out to carry a
   complete, current NDIS 6.x header set - no hand-declaring, and no
   Musa.Veil/ADR-0006 fallback, needed at all.
   `driver/net/reactos/ndis6-probe/` fetches them and **verifies live
   that a real NDIS 6.x miniport `DriverEntry` compiles *and links*
   clean** against this project's existing mingw-w64 toolchain and its
   real `libndis.a` - the strongest of the three DDK-gap results in
   this file, since (unlike KMDF/WDDM) no new import library was ever
   needed. See `driver/net/reactos/ndis6-probe/README.md`.
2. **KMDF is absent from mingw-w64's own packaging — but the real
   headers are real and fetchable**, found live in the same official
   Microsoft WDK NuGet package Phase 11's WDDM work already fetches
   from. Not compile-verified against this toolchain yet (WDDM's
   headers were; KMDF's weren't, in this pass) — a real, scoped,
   concretely-startable follow-up now, not "needs sourcing from
   somewhere unknown" as originally written here.
3. **The genuinely-recent (Windows 10-era) additions are absent**
   (`ExAllocatePool2`, `PsSetCreateProcessNotifyRoutineEx2`,
   `FltRegisterFilter`/minifilters) — expected, not a surprise, and not
   this project's near-term target per CLAUDE.md's stated scope.

This is the concrete, scoped starting point for Phase 9's upstream work
— finding #1 is done, not just attemptable (`driver/net/reactos/
ndis6-probe/`, above); #2 is a real, separate, larger undertaking
(comparable in kind to the RosBE/Wine-source-build items Phases
2/12/13 already deferred for the same "needs a much bigger toolchain
component this sandbox can't provide" reason); #3 is out-of-scope by
design, not by gap.

## Known limitations, stated precisely

- The curated symbol list is representative, not exhaustive — a real,
  much larger sweep (every `wdm.h`/`ntddk.h`/`ndis.h` export vs. every
  documented modern Microsoft WDK symbol) is real follow-up work, not
  attempted here; this list was chosen to span each category Phase 9
  names with a handful of concrete, checkable examples per category.
- `header_declared` is a textual grep, not a compile probe — it would
  be fooled by a symbol name appearing only in a comment. Checked
  against this run's actual output and found no false positives (every
  "full" hit is a real prototype/macro, not a stray comment), but a
  compile-based check (attempt `#include` + take the symbol's address)
  would be more rigorous — real follow-up, not done here since the
  simpler check already produced accurate, spot-checked results.
- No real Windows/WDK machine's own header set to diff against (same
  gap `ntexports/README.md` already states for user-mode exports) — this
  measures *this toolchain's* surface, not a live comparison against
  Microsoft's actual current WDK.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
