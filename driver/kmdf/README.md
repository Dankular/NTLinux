# KMDF (Kernel-Mode Driver Framework) Toolchain Probe

Real, live compile-verification that this project's mingw-w64 DDK toolchain can build against the real Microsoft KMDF headers (ROADMAP.md Phase 9).

**Owner:** Microsoft (the real WDK headers this pulls from) + NTLinux (the probe/patches)
**Status:** Real, live-verified — `kmdf-probe.c` compiles clean end to end

## Follow-up to Phase 9's "found, not yet compile-verified" gap

`tooling/compat-db/ddkgap/` originally reported KMDF as "not present in
this toolchain at all," checking only whether mingw-w64 pre-packages
it. Investigating Phase 11's WDDM headers turned up the real KMDF
headers (`c/Include/wdf/kmdf/1.11/`) live in the same official
Microsoft WDK NuGet package `driver/gpu/fetch-wdk-headers.sh` already
uses — found, but never actually compiled against, until this
directory. `fetch-kmdf-headers.sh` fetches them the same way (never
vendored — ADR-0002/Rule 17, WDK EULA read directly); `kmdf-probe.c` is
a real KMDF `DriverEntry` referencing the real
`WDF_DRIVER_CONFIG`/`WdfDriverCreate` contract.

## Two real, narrow patches — found live by actually compiling, not by inspection

1. **Case-sensitivity.** These headers' own `#include` directives
   reference filenames in a different case than the files actually
   have on disk (e.g. `#include "WdfQueryInterface.h"` for
   `wdfqueryinterface.h`) — invisible on Windows' case-insensitive
   filesystem (what these headers are authored/tested against), a hard
   compile error on Linux. `fetch-kmdf-headers.sh` symlinks every case
   variant actually referenced across the header set, found by
   grepping for `#include` lines rather than guessed at.
2. **A real ordering bug in `wdf.h` itself.** `wdf.h` (the master
   include every real KMDF driver uses) includes `wdfdevice.h` *before*
   `wdfrequest.h` — but `wdfdevice.h`'s
   `WdfDeviceConfigureRequestDispatching` uses `WDF_REQUEST_TYPE`,
   which `wdfrequest.h` is the one that actually defines. MSVC
   tolerates this (not investigated further why); GCC correctly
   rejects it (`parameter 3 ('RequestType') has incomplete type`).
   Fixed by pulling `wdfrequest.h`'s `#include` earlier, right before
   `wdfdevice.h`'s — its own include guard makes the later, still-
   present inclusion a no-op, so this changes only ordering, nothing
   else. The same "find the real header bugs, patch narrowly, document
   precisely" technique as `driver/net/reactos/prepare-ndis-header.sh`
   and `driver/gpu/fetch-wdk-headers.sh`'s own `dispmprt.h` patch.

## Real, live result

```
$ ./fetch-kmdf-headers.sh
fetch-kmdf-headers.sh: downloading Microsoft.Windows.WDK.x64 10.0.26100.2454...
fetch-kmdf-headers.sh: extracting c/Include/wdf/kmdf/1.11/...
fetch-kmdf-headers.sh: ready in .../build ...

$ make
x86_64-w64-mingw32-gcc -Wall -Wextra -Ibuild/wdf/kmdf-1.11 -I/usr/x86_64-w64-mingw32/include/ddk -c -o kmdf-probe.o kmdf-probe.c
# cosmetic warnings only (-Wold-style-declaration on FORCEINLINE, same
# harmless class of warning wddm-probe.c already documents) - no errors
$ echo $?
0
```

Re-run clean (`make clean && make`) to confirm reproducibility, not a
one-off — same result both times.

## What this does not prove

KMDF is a real *framework*, not just headers — a real KMDF driver links
against a version-specific `WdfLdr`/`Wdf01000` co-installer stub
library (WDF drivers call into a versioned framework DLL loaded by the
OS at runtime, unlike `ntoskrnl`/`hal`'s static import libraries this
toolchain already has). No import library for that exists in this
toolchain, and this probe doesn't attempt to link against one — same
boundary `driver/gpu/wddm-probe.c` draws around `dxgkrnl.sys`.
Compilation against the real struct/prototype shapes is what's checked
here; linking a real KMDF driver is real, separate, unattempted
follow-up work.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) Phase 9 for how this fits. Before implementing anything here, check whether the capability already exists upstream per Rule 1 in `CLAUDE.md`.
