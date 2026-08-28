# Native Windows GPU Driver Hosting

Hosting a real Windows WDDM GPU driver inside the ReactOS driver cell, VFIO/IOMMU-mediated (ARCHITECTURE.md section 30, ADR-0003, Phase 11).

**Owner:** ReactOS (WDDM/DXGKRNL kernel support) + Microsoft (the real WDK headers this pulls from) + Linux VFIO/IOMMU (upstream) + NTLinux (integration)
**Status:** Real, corrected research (Phase 11) — see "A real correction, made live" below before reading anything else in this file

## A real correction, made live

This README originally said the WDDM/DXGKRNL kernel-mode interface was
"not present in this toolchain at all" and treated that as settled. It
was wrong, caught directly rather than argued about: the real Microsoft
WDK/SDK headers a WDDM display miniport driver needs
(`dispmprt.h`, `d3dkmddi.h`, `d3dkmdt.h`, `d3dukmdt.h`) are real,
official, and legitimately fetchable from Microsoft's own NuGet
packages — and, as `fetch-wdk-headers.sh` and `wddm-probe.c` in this
directory demonstrate live, a real `DriverEntry` referencing the real
`DXGKRNL_INTERFACE`/`DxgkInitialize` contract **compiles** against this
project's existing mingw-w64 DDK toolchain, with a small number of
narrow, documented patches — the same technique already established in
`driver/net/reactos/prepare-ndis-header.sh` for the NDIS header bugs.

This does not mean Phase 11 is unblocked — see "What's still
unresolved" below for a real, serious, separately-discovered problem —
but "the toolchain has zero WDDM surface" was an incomplete finding,
not a settled fact, and it's corrected here, in `ROADMAP.md` Phase 11,
and in `docs/DECISIONS.md` ADR-0003 rather than left standing.

## `fetch-wdk-headers.sh` — what it does, and the license constraint that shapes it

Downloads two official Microsoft NuGet packages
(`Microsoft.Windows.WDK.x64` and `Microsoft.Windows.SDK.CPP` — a real,
three-package dependency chain confirmed by reading each package's own
`.nuspec`, not assumed) and extracts just the four header files needed
into `./build/` (gitignored).

**Never vendor these headers into this repo.** Their own EULA
(extracted alongside them as `build/WDK-LICENSE.txt` — read it)
explicitly prohibits redistribution: *"you will not... share, publish,
distribute, or lend the software... transfer the software or this
agreement to any third party."* This is ADR-0002/Rule 17 ("consume, don't
vendor") with real legal teeth behind it, not just a style preference —
`fetch-wdk-headers.sh` only ever writes into the gitignored `build/`
directory, fetched fresh by whoever needs it, exactly like
`driver/cell/images/fetch-reactos-x64-nightly.sh` already does for the
ReactOS ISO.

## Real, narrow patches applied — and one deliberately *not* hidden

- `#define _Maybenull_` / `#define _Pre_opt_bytecap_(size)` as no-ops —
  two SAL (Source Annotation Language) macros MSVC understands as
  static-analysis hints with no runtime meaning; mingw-w64 already
  defines most SAL macros (`sal.h`/`specstrings.h`) but not these two.
  `wddm-probe.c` defines them locally, same "narrow, local, documented"
  shape as every other header patch in this project.
- `#include <windef.h>` before the WDK headers — `d3dukmdt.h` uses
  ordinary Windows types (`UINT`, etc.) even though this is otherwise
  an `<ntddk.h>` (kernel-mode) compile, since WDDM types are shared
  between usermode and kernel-mode driver code.
- `#include <assert.h>` — `dispmprt.h` uses the C11 `static_assert`
  macro form, which needs this include to resolve correctly.
- **One `static_assert` is commented out by `fetch-wdk-headers.sh`,
  and this is the one patch that is not a "spurious header bug" fix —
  see immediately below.**

## What's still unresolved — a real, serious finding, not glossed over

`dispmprt.h` line 273 carries Microsoft's own embedded correctness
check: `static_assert(FIELD_OFFSET(DXGK_CHILD_CAPABILITIES,
HpdAwareness) == 12, "Type field has changed size")`. **This assertion
genuinely fails** when compiled with this toolchain — confirmed live,
including trying `-mms-bitfields` (mingw's standard flag for matching
MSVC's bitfield ABI), which does not fix it. That means
`DXGK_CHILD_CAPABILITIES`'s actual byte layout, as GCC/mingw-w64
computes it, does not match what MSVC (and therefore the real Windows/
ReactOS kernel calling into a real driver) expects.

This is categorically different from the NDIS header bugs
`prepare-ndis-header.sh` fixes (those were genuinely redundant/typo'd
lines, safe to remove) — a real GCC-vs-MSVC struct-layout mismatch is a
known, hard class of cross-compiler problem, not something a one-line
patch resolves. It was commented out in `fetch-wdk-headers.sh` only so
the *rest* of the ~11,000-line interface could be checked for other,
independent issues (none found — everything else that got exercised by
`wddm-probe.c`'s `#include` chain compiles clean, cosmetic warnings
only: unknown `#pragma warning`, a narrower-than-declared bitfield
type, non-standard `#endif (...)` tokens — none blocking).

**What this means concretely:** even setting aside Phase 11's other two
blockers (below), this toolchain cannot yet be trusted to produce a
struct layout a real WDDM-hosting kernel would parse correctly for
*every* structure in this interface — only confirmed clean for the ones
this one probe file's `#include` chain actually instantiates matching
Microsoft's own layout expectations, which is not the same as "verified
throughout." Real, separate follow-up work: find out whether this is a
`-mms-bitfields`-fixable packing/alignment issue for a different
compiler flag combination, a `#pragma pack` this project's probe is
missing, or a genuine, deeper incompatibility — not attempted in this
pass.

## The three blockers, corrected

1. **ReactOS's own WDDM/DXGKRNL support is real but early and 2D-only**
   — unchanged from the original research; see `ROADMAP.md` Phase 11
   for the sourced account (its official blog, Oct 7 2025).
2. **This project's DDK toolchain — corrected.** Not "zero WDDM
   surface" — the real headers fetch and mostly compile, as this
   directory now demonstrates live. What remains genuinely unresolved
   is the struct-layout question above, plus actually linking against a
   real `dxgkrnl.sys` import surface (no import library exists in this
   toolchain for that — confirmed absent, unchanged; `wddm-probe.c`
   compiles but was never asked to link against one).
3. **No VFIO/IOMMU/real PCI passthrough in any sandbox this project has
   run in** — unchanged, still the hardest blocker, still needs real
   user hardware.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
