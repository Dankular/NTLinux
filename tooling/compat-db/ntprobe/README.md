# ntprobe — real-Windows live NT API behavioral probe

Companion to `../ntexports/`: that tool answers "does this symbol exist"
from a PE export table (static). `ntprobe.exe` answers "what does calling
it actually do" — it makes real `Nt*`/`Zw*` calls on a real Windows
machine and records the real `NTSTATUS`/output. Genuine differential-
testing ground truth (Rule 11), which `ntexports`'s own README flagged as
not yet attempted for this project.

## Where this came from

Built in response to a specific ask: use
[MiroKaku/Musa.Veil](https://github.com/MiroKaku/Musa.Veil) "as a wrapper
and service to NTLinux to support calls that Wine can't." That framing
doesn't fit what the library actually is — confirmed by fetching and
reading it directly, not from memory: Musa.Veil is a **header-only
library of NT API declarations** (struct layouts, prototypes, constants
for undocumented `ntdll.dll`/`ntoskrnl.exe` internals — same lineage as
System Informer's PHNT and Chuyu-Team's MINT), MIT licensed. It ships
**zero runtime code**, so it can't be a service, and it can't make any
call succeed that wouldn't already succeed against a real `ntdll.dll` —
there's no implementation to "wrap." What it legitimately provides is the
correct type declaration for an undocumented NT API, which is exactly
what a real behavioral probe needs. `ntprobe.exe` is the honest way to
act on the underlying ask.

## A real compatibility finding, not assumed

Attempted actually including Musa.Veil's headers in this project's MinGW
cross-compilation toolchain (the same one `ntexports.exe` uses) before
writing anything by hand. `#include "Veil.h"` (the library's intended
single entry point) fails outright — 101 errors — and restricting to a
single narrow sub-header (`Veil/Veil.System.Executive.h` alone, just for
`SYSTEM_BASIC_INFORMATION`) fails *worse* (763 errors), because these
headers aren't designed to be included standalone. The failures are
substantive, not cosmetic:

- `__kernel_entry` and several SAL annotations (`_Post_ptr_invalid_`)
  are MSVC/WDK-only macros MinGW doesn't define.
- Forward `enum` declarations MSVC allows as an extension, which GCC
  rejects outright (`expected ';' before 'enum'`).
- Real struct/enum **redefinition conflicts** against MinGW's own
  `<windows.h>` (`MEM_ADDRESS_REQUIREMENTS`, `MEM_EXTENDED_PARAMETER`,
  `SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION` and others —
  MinGW's own headers have grown their own definitions of some of these
  over time).
- Several `_Static_assert(sizeof(...) == N)` checks on deeply nested,
  bitfield-heavy internal structures (`LDR_DATA_TABLE_ENTRY32`, `PEB32`,
  `TEB32`, `KUSER_SHARED_DATA` field offsets) **genuinely fail even with
  `-mms-bitfields`** — a real GCC-vs-MSVC struct-layout ABI mismatch for
  those specific types, not a missing macro fixable by a `#define`.

Patching all of that to get the full library building under MinGW would
mean maintaining a parallel MinGW-compatible fork of most of a 30-file
library, indefinitely — disproportionate for a probe that only needs
four structures (Rule 2: don't build more machinery than the problem
calls for; Rule 3's spirit of preferring upstreamable patches over
permanent forks doesn't favor taking this on either — the incompatibility
is architectural to the library's MSVC-only design assumptions, not a
small bug).

## What this file actually does instead

Hand-declares the ~4 structures/enums genuinely needed
(`SYSTEM_BASIC_INFORMATION`, `PROCESS_BASIC_INFORMATION`,
`OBJECT_BASIC_INFORMATION`, the three information-class values used),
each cross-referenced against Musa.Veil's own declaration for the exact
same struct at the point of use in `ntprobe.c` — genuinely using
Musa.Veil as the authoritative reference source for getting an
undocumented layout right (Rule 15's ownership-tracking spirit), without
inheriting its MSVC-only build assumptions. These particular structures
are long-stable NT ABI (unchanged since the Windows 2000/XP-era ntdll),
which is why hand-transcribing them is low-risk, unlike the newer/larger
structures where the static-assert failures actually occurred. Function
prototypes are declared `extern` directly (not with Musa.Veil's
`NTSYSAPI`/`NTSYSCALLAPI` MSVC-import-library linkage) and resolved at
link time against **mingw-w64's own `libntdll.a`** — confirmed via `nm`
to already export all five symbols this tool calls
(`NtQuerySystemInformation`, `NtQueryInformationProcess`, `NtQueryObject`,
`NtCreateEvent`, `NtSetEvent`, `NtWaitForSingleObject`, `NtClose`) — no
import library authored by this project, no Musa.Veil code compiled or
linked into the binary at all.

## Probes

1. `NtQuerySystemInformation(SystemBasicInformation)` — cross-checked
   against `GetSystemInfo()` (page size, CPU count).
2. `NtQueryInformationProcess(ProcessBasicInformation, self)` —
   cross-checked against `GetCurrentProcessId()`.
3. **`NtCreateEvent`/`NtSetEvent`/`NtWaitForSingleObject` round-trip** —
   the one that matters most for this project. Exercises the exact
   auto-reset Event state machine `ntd/ntd.c` reimplements from scratch
   (unsignaled → `NtSetEvent` → wait succeeds and consumes the signal →
   immediately unsignaled again) against a real `ntdll`, giving
   `ntabi/tests/test_ntabi.c` a genuine behavioral reference instead of
   only self-consistency.
4. `NtQueryObject(ObjectBasicInformation)` on a fresh handle — checks
   `HandleCount == 1`.

Each probe reports the real `NTSTATUS`, a cross-check against an
independent Win32 API where one exists, and an `ok` boolean — the JSON
output states whether the returned data was actually *correct*, not just
whether the call returned success.

```
ntprobe.exe [out.json]
```

## Verified

Compiled clean with `-Wall -Wextra`, zero warnings; produces a real
PE32+ `.exe`, same shape as `ntexports.exe`. **Run for real** in this
session under Wine (Wine's own bundled `ntdll.dll` — a genuine,
independent reimplementation, not real Windows): 4/4 probes passed,
including the auto-reset Event round-trip matching `ntd/ntd.c`'s exact
state machine (`STATUS_TIMEOUT` before set → `STATUS_WAIT_0` after set
→ `STATUS_TIMEOUT` again after the auto-reset consumed it).

**Known gap, stated plainly, same as `ntexports.exe`:** this has not run
against real Windows — only Wine's reimplementation. Wine agreeing with
itself on its own Event semantics is a real, useful signal (this project
depends on Wine's ntdll behaving like real Windows for Phase 12's
integration to matter at all) but is not the same as confirming against
Microsoft's actual `ntdll.dll`. Run it on a real Windows install and send
back the JSON for that comparison.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
