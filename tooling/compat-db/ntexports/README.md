# ntexports — real-Windows PE export walker + gap analyzer

Two-part tool, built in response to a direct ask: get the complete NT/Win32
API surface from a real Windows machine up front, and diff it against what
Wine/ReactOS actually implement — instead of discovering gaps one crash
report at a time while patching `ntdll` integration piecemeal.

## 1. `ntexports.exe` — runs on a real Windows machine

A small, dependency-free C program, cross-compiled from Linux with MinGW
(no Windows toolchain needed to build it, only to run it). Maps each
target DLL read-only and parses its PE export directory table directly —
no `LoadLibrary`, nothing executed, no admin rights needed. For every
exported function it records the name, ordinal, and either its RVA (a real
implementation) or its forwarder target (e.g. `kernel32.AcquireSRWLockExclusive`
→ `NTDLL.RtlAcquireSRWLockExclusive`) — a real, structural fact about which
DLL actually owns a given piece of behavior, not something you'd get from
just checking whether a name exists.

Scans a built-in default list (`ntdll.dll` first — the actual reason this
tool exists — then the major Win32-layer DLLs Wine already owns per
`docs/ARCHITECTURE.md` section 51) across both `System32` and `SysWOW64`,
or an explicit `-f list.txt` of DLL names. Writes one JSON file.

```
ntexports.exe [-f dll-list.txt] [-o out.json]
```

Run it on a real Windows install and send the resulting JSON back — that's
the actual ground truth this whole pipeline needs. **Nothing in this
repository has run it against real Windows yet** — see "Verified" below
for exactly what has and hasn't been confirmed.

## 2. `analyze.py` — diffs the scan against Wine's (or ReactOS's) `.spec` files

```
python3 analyze.py --scan windows-scan.json --specs wine-specs/ --out gap-report.json
```

`fetch_wine_specs.sh <dir>` downloads Wine's current `.spec` files for the
same default DLL list (Rule 1: reuse Wine's own record of what it
implements, don't re-derive it from source). Each `.spec` line classifies
an export as `stdcall`/`cdecl`/`extern`/... (really implemented) or `stub`
(exported but not really implemented — Wine kept the symbol so linking
succeeds, without the behavior behind it). The analyzer reports, per DLL:
implemented / stubbed / genuinely missing, with the NT-native surface
(`Nt`/`Zw`/`Rtl`/`Ldr`/`Csr`/`Alpc`/`Etw`/`Dbg` prefixes) called out
separately, since that's this project's actual focus, not the much larger
general Win32 export surface.

## Verified

Built and run for real in this session — against Wine's own bundled
64-bit system DLLs under Wine (not real Windows; see the gap below):

- Compiled clean with `-Wall -Wextra`, zero warnings; produces a real
  PE32+ `.exe`.
- Ran under Wine, parsed 28 DLLs, 11,893 total exports. Spot-checked
  `ntdll.dll`: all of `NtCreateFile`, `NtCreateEvent`,
  `NtWaitForSingleObject`, `RtlAllocateHeap`, `NtOpenKey`, `NtClose`
  correctly present.
- Forwarder detection genuinely fires and is correct: 723 forwarders
  found across the scan, including real, recognizable relationships like
  `kernel32!AcquireSRWLockExclusive → NTDLL.RtlAcquireSRWLockExclusive`.
- **Two real bugs found and fixed by actually running the pipeline**, not
  by inspection:
  1. Windows paths (`C:\windows\...`) were written into the JSON output
     unescaped — `\w`, `\s` aren't valid JSON escapes, so the tool's own
     output was invalid JSON until fixed (`json_escape_w` added,
     `ntexports.c`).
  2. `analyze.py`'s first `.spec` parser assumed flags like `-syscall`
     only ever appear *before* the `stdcall`/`stub` keyword. `win32u.spec`
     has lines like `@ stub -syscall NtBindCompositionSurface` (flag
     *after* the keyword), which the first parser silently mis-read as
     exporting a function named `-syscall` — every `win32u.dll` name
     came back as "missing" (0% overlap), which was suspicious enough to
     investigate rather than accept. Fixed by tokenizing and filtering
     flag tokens from any position instead of anchoring a regex to one.
- Full pipeline run end to end (`ntexports.exe` output → `analyze.py` →
  gap report) produced a sensible, informative result after both fixes:
  e.g. `ntdll.dll` 92.8% covered as "implemented", `win32u.dll` only 34%
  implemented but 866 more exports present as `stub` — a real, specific
  signal (Wine knows about a large Windows 11-era `NtUserXxx`
  composition/DWM syscall surface it hasn't implemented yet).

**Known gap, stated plainly:** all of the above compared Wine's own DLLs
against Wine's own `.spec` files — that mechanism-tests the tool chain,
it does not produce a real gap report. Wine vs. Wine is close to a
tautology (of course Wine mostly agrees with what Wine says it
implements); the genuinely useful report only exists once `ntexports.exe`
runs on **real Windows** and that output is fed through `analyze.py`. That
run hasn't happened — it needs a real Windows machine, which this
sandbox doesn't have.
