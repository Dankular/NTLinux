# NDIS 6.x (Connectionless Miniport Model) Toolchain Probe

Real, live compile-*and-link* verification that this project's mingw-w64 DDK toolchain can build a real NDIS 6.x miniport against the real Microsoft NDIS 6.x headers (ROADMAP.md Phase 9).

**Owner:** Microsoft (the real WDK/SDK headers this pulls from) + NTLinux (the probe/patches)
**Status:** Real, live-verified — `ndis6-probe.c` compiles clean *and* links clean against this toolchain's real `libndis.a`

## Follow-up to `tooling/compat-db/ddkgap/`'s own finding

`tooling/compat-db/ddkgap/` (README.md, ROADMAP.md Phase 9) found, live and directly, that `NdisMRegisterMiniportDriver`, `NdisMSetMiniportAttributes`, `NdisMIndicateReceiveNetBufferLists`, and `NdisAllocateNetBufferListPool` — the NDIS 6.x connectionless miniport model's registration and receive-indication entry points — are all real import stubs in this toolchain's `libndis.a`, but genuinely undeclared by any header mingw-w64 packages. Re-confirmed directly in this pass (not just trusted) — see "Re-verified, directly" below.

Checked in CLAUDE.md Rule 1's priority order before hand-declaring anything: the real, current Microsoft WDK/SDK NuGet packages (`Microsoft.Windows.WDK.x64` / `Microsoft.Windows.SDK.CPP`, the same ones `driver/gpu/fetch-wdk-headers.sh` and `driver/kmdf/fetch-kmdf-headers.sh` already fetch from) turn out to carry a complete, current `km/ndis.h` (~529KB) plus its full `km/ndis/` and `shared/ndis/` support-header directories — found live by listing each package's own contents, not assumed from the WDDM/KMDF precedent. Musa.Veil (ADR-0006) and hand-declaring from `learn.microsoft.com` were never needed.

## Re-verified, directly

```
$ python3 tooling/compat-db/ddkgap/ddkgap.py
...
NDIS 6.x (connectionless miniport model):
  [GAP ] NdisMRegisterMiniportDriver              abi-only (header gap)
  [GAP ] NdisMSetMiniportAttributes                abi-only (header gap)
  [GAP ] NdisMIndicateReceiveNetBufferLists        abi-only (header gap)
  [GAP ] NdisAllocateNetBufferListPool             abi-only (header gap)

$ x86_64-w64-mingw32-nm /usr/x86_64-w64-mingw32/lib/libndis.a | grep NdisMRegisterMiniportDriver
0000000000000000 I __imp_NdisMRegisterMiniportDriver
0000000000000000 T NdisMRegisterMiniportDriver
# (same for the other three - all four real import stubs, confirmed)

$ grep -rw NdisMRegisterMiniportDriver /usr/x86_64-w64-mingw32/include/
# (zero output - not declared anywhere in the entire installed include tree, not just ddk/)
```

## `fetch-ndis6-headers.sh` — what it fetches, and why each piece is there

Downloads the same two official Microsoft NuGet packages `driver/gpu/fetch-wdk-headers.sh` and `driver/kmdf/fetch-kmdf-headers.sh` already use, and extracts a real, current NDIS 6.x header set into `./build/{km,shared}/` (gitignored) — walked live, one real compile error at a time, rather than guessed in advance:

- **`km/`** (from the WDK package): `ndis.h` itself, its two top-level companions (`netpnp.h`, `xfilter.h`), and its whole `km/ndis/` support-header directory (`nbl.h`, `nblapi.h`, `nblreceive.h`, and friends — the NET_BUFFER_LIST machinery the two data-path symbols actually operate on).
- **`shared/`** (from the base SDK package): `ntddndis.h` (classic NDIS OID/media-type definitions) and its own `shared/ndis/` support headers, plus everything `ndis.h`'s `#include` chain needed that turned out to be either genuinely missing from mingw-w64 or a genuinely outdated copy of a name mingw-w64 *does* ship — found by compiling, not assumed:
  - `sal.h`/`specstrings*.h`/`no_sal2.h`/`concurrencysal.h`/`sdv_driverspecs.h`/`driverspecs.h` — the real, modern SAL 2.0 annotation macro set (`__ANNOTATION`, `__PRIMOP`, and friends) `driverspecs.h` needs; mingw-w64 ships only an older, simpler `specstrings.h` that doesn't define these.
  - `ws2def.h`/`ws2ipdef.h`/`in6addr.h`/`inaddr.h`/`winapifamily.h`/`winpackagefamily.h` — `ntddndis.h` itself `#include`s `<ws2def.h>`/`<ws2ipdef.h>` for `SOCKADDR_INET` and friends (NDIS 6.x's NDK/RDMA OIDs use them); mingw-w64's own `ws2ipdef.h` assumes a prior `<winsock2.h>` (usermode) include this kernel-mode compile never does, and fails outright without it.
  - `ifdef.h`/`ipifcons.h` — mingw-w64 ships its own `ifdef.h`, but a genuinely older revision that predates `NET_IF_OBJECT_ID`/`IF_QUERY_OBJECT`/`IF_SET_OBJECT`/`NET_PHYSICAL_LOCATION` (confirmed absent with a direct grep), which `ndis.h`'s NDIS 6.x interface-object-query surface needs.
  - `wlantypes.h` — needed to fix a real case-sensitivity bug (see below).
  - `windot11.h`/`qos.h` — pulled in far down `ndis.h` for 802.11 and QoS OID support.

None of this is vendored — same ADR-0002/Rule 17 shape as every other fetch script in this project: the WDK's own EULA (extracted alongside these headers as `build/WDK-LICENSE.txt`) prohibits redistribution, so `fetch-ndis6-headers.sh` only ever writes into the gitignored `./build/` directory.

## Real, narrow patches found live — the same technique, three more times

Same "find the real header bugs, patch narrowly, document precisely" technique as `driver/net/reactos/prepare-ndis-header.sh`, `driver/gpu/fetch-wdk-headers.sh`'s `dispmprt.h` patch, and `driver/kmdf/fetch-kmdf-headers.sh`'s two patches — every one below was found by actually compiling `ndis6-probe.c`, not by inspection in advance.

1. **Case-sensitivity** (`fetch-ndis6-headers.sh`, in `./build/`): `windot11.h` `#include`s `<WlanTypes.h>` for the real `shared/wlantypes.h` — invisible on Windows' case-insensitive filesystem, a hard error on Linux. Fixed the same way `driver/kmdf/fetch-kmdf-headers.sh` already fixes this class of bug: symlink the exact case actually referenced, found by grepping real `#include` directives (anchored to line start, so a `#include <ndis.h>` mentioned only in a *comment* — `ntddndis.h`'s own file header does this — doesn't produce a spurious symlink; caught live, an earlier, less careful version of this check did exactly that), and only when no exact-case match exists anywhere on the compile's own two-directory include path already.
2. **Three narrow local declarations, in `ndis6-probe.c` itself** (not patched into the fetched headers — these are things the real WDK's own `km/wdm.h`/`km/miniport.h` define, but this probe deliberately does not fetch/swap in either of those two files: `wdm.h` is the base of this toolchain's entire `ntddk.h` chain, and `miniport.h` pulls in the legacy NDIS 3/4/5 miniport surface `ntnet.c` already targets a working copy of — swapping either wholesale is a materially bigger, riskier change than three narrow local declarations for symbols `ndis.h`'s own header text merely assumes some earlier header already provided):
   - `DECLSPEC_DEPRECATED_DDK` — real `km/miniport.h`'s own macro, for the real (default) branch this probe is actually in, reduces to nothing; copied verbatim.
   - `POOL_NX_ALLOCATION` — real `km/wdm.h` defines this NTDDI_WIN7+ pool-type flag as literal `512`; copied verbatim.
   - `KeGetCurrentProcessorIndex` — real `km/wdm.h` declares this `FORCEINLINE`, with a body reading a fixed KPCR offset via an architecture-specific intrinsic; only declared here (not reproduced), since `ndis.h`'s own `NdisCurrentProcessorIndex` (its sole caller) is itself `FORCEINLINE` and never instantiated by this probe.
   - `EXTERN_C_START`/`EXTERN_C_END` — real `shared/ntdef.h` defines these; this toolchain still resolves `<ntdef.h>` to mingw-w64's own copy (deliberately not replaced), which doesn't. Matches `shared/ntdef.h`'s own non-C++ branch exactly (both expand to nothing for a plain C compile).

## Real, live result

```
$ ./fetch-ndis6-headers.sh
fetch-ndis6-headers.sh: downloading Microsoft.Windows.WDK.x64 10.0.26100.2454...
fetch-ndis6-headers.sh: downloading Microsoft.Windows.SDK.CPP 10.0.26100.1742...
fetch-ndis6-headers.sh: extracting the real NDIS 6.x header set...
fetch-ndis6-headers.sh: ready in .../build (read WDK-LICENSE.txt - never commit these files, see this script's own header comment)

$ make link
x86_64-w64-mingw32-gcc -Wall -Wextra -Ibuild/shared -Ibuild/km -I/usr/x86_64-w64-mingw32/include/ddk -c -o ndis6-probe.o ndis6-probe.c
# 110 cosmetic warnings only (unknown #pragma warning/deprecated, old-style
# __inline placement, one multi-char constant, extra #endif tokens) -
# same harmless class of warning wddm-probe.c/kmdf-probe.c already
# document - no errors
x86_64-w64-mingw32-gcc -Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared -L/usr/x86_64-w64-mingw32/lib -o ndis6-probe.sys ndis6-probe.o -lndis -lntoskrnl -lhal
$ echo $?
0

$ x86_64-w64-mingw32-nm ndis6-probe.sys | grep -iE 'NdisMRegisterMiniportDriver|NdisMSetMiniportAttributes|NdisMIndicateReceiveNetBufferLists|NdisAllocateNetBufferListPool'
000000028d6b6078 I __imp_NdisAllocateNetBufferListPool
000000028d6b6080 I __imp_NdisMIndicateReceiveNetBufferLists
000000028d6b6088 I __imp_NdisMRegisterMiniportDriver
000000028d6b6090 I __imp_NdisMSetMiniportAttributes
000000028d6b19e8 T NdisAllocateNetBufferListPool
000000028d6b19e0 T NdisMIndicateReceiveNetBufferLists
000000028d6b19d8 T NdisMRegisterMiniportDriver
000000028d6b19d0 T NdisMSetMiniportAttributes
```
(addresses are ASLR-relative and will differ run to run - what matters is the `I`/`T` presence, not the exact offsets)

Re-run clean (`make clean && make link`) twice to confirm reproducibility, not a one-off — same result (exit 0, 0 errors, 110 warnings, all four symbols resolved) both times.

## Goes one step further than `driver/gpu/wddm-probe.c` / `driver/kmdf/kmdf-probe.c`

Both of those probes could only verify *compilation* — no import library exists in this toolchain for either `dxgkrnl.sys` (WDDM) or the versioned `Wdf01000` co-installer stub (KMDF), so neither could attempt a real link. NDIS is different: `libndis.a` is already part of this toolchain (it's what made the ABI half of the original gap real in the first place), so `make link` in this directory goes further and actually links `ndis6-probe.c`'s `DriverEntry` into a real `ndis6-probe.sys`, with the identical `-Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared` / `-lndis -lntoskrnl -lhal` shape `driver/net/reactos/Makefile` already uses for `ntnet.sys` — and `nm` on the result confirms all four target symbols resolved as real imported thunks, not left undefined.

## What this does not prove

A successful link only confirms the import stub resolves against this toolchain's own `libndis.a` — not that ReactOS's own `ndis.sys` genuinely implements the NDIS 6.x connectionless miniport contract end to end, and not that this driver was ever loaded by a running ReactOS kernel. Same "written against the real API, not yet loaded live" boundary `driver/net/reactos/ntnet.c`'s own header comment already draws for the older NDIS 5.1 surface — a real, separate, unattempted question (ReactOS's own NDIS 6.x implementation maturity specifically is not evaluated here).

## What this means for `tooling/compat-db/ddkgap/`'s own finding

The underlying gap ddkgap.py reports (`abi-only (header gap)`) is about *this toolchain's packaged headers*, which this probe doesn't change — re-running `ddkgap.py` unmodified still reports the same four symbols as header gaps, correctly, since it only checks the DDK's own installed include tree, not this directory's fetched-and-gitignored `build/`. What this directory adds is the answer to the natural next question ddkgap's own README already posed: "hand-declaring the missing prototypes... would unlock building an NDIS 6.x miniport against this exact toolchain" — confirmed true, live, without any hand-declaring at all (the real headers were fetchable), and confirmed to go further than that framing expected: it doesn't just compile, it links against the real ABI too.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) Phase 9 for how this fits. Before implementing anything here, check whether the capability already exists upstream per Rule 1 in `CLAUDE.md`.
