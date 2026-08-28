#!/usr/bin/env python3
"""ddkgap.py — Phase 9's first deliverable: a real, re-runnable probe of
this toolchain's actual kernel-mode (driver) compatibility surface,
grounding "Upstream work on KMDF, newer WDM, newer NT exports, modern
NDIS, PnP, power, memory management, security" (ROADMAP.md Phase 9) in
what's *actually* present today rather than assumption.

Same family as ntexports/ (user-mode DLL export gap analysis, static)
and ntprobe/ (live Windows-side NT behavior probe, dynamic) — this is
the kernel-mode counterpart to ntexports/: not "what does a real Windows
machine export" (no Windows machine available in this sandbox to ask),
but "what does *this project's own DDK toolchain* actually expose,
right now" — the concrete prerequisite for any of Phase 9's driver
compatibility work, the same way ntexports/ is the prerequisite for the
user-mode compatibility database.

For each curated symbol this checks two independent, real things:
  - abi_present: is the symbol importable from the relevant NT
    kernel-mode import library (ntoskrnl.exe/hal.dll/ndis.sys) —
    checked with `nm` against the actual .a file, not assumed from a
    version number.
  - header_declared: does *some* header under this toolchain's DDK
    include tree actually declare it — checked with a real recursive
    grep against the actual installed headers, not against
    documentation.

A symbol can be ABI-present but NOT header-declared (found live in this
project: NdisMSetMiniportAttributes/NdisMRegisterMiniportDriver, the
NDIS 6.x miniport registration entry points — the import stub exists,
but no packaged header actually prototypes it, so using it today would
mean hand-declaring the prototype yourself, the same category of gap
tooling/compat-db/ntprobe/ already worked around for a few NT structures
citing Musa.Veil, ADR-0006). That distinction is the whole point of
checking both independently instead of just one proxy signal.

Usage:
    python3 ddkgap.py [--ddk-inc DIR] [--lib-dir DIR] [--out report.json]
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_DDK_INC = Path("/usr/x86_64-w64-mingw32/include/ddk")
DEFAULT_ROOT_INC = Path("/usr/x86_64-w64-mingw32/include")
DEFAULT_LIB_DIR = Path("/usr/x86_64-w64-mingw32/lib")

# name -> import library basename (without lib.../.a), curated from real
# driver-relevant NT kernel APIs spanning WDM-classic through roughly
# Windows 8-era additions, plus the two genuinely cutting-edge ones
# (ExAllocatePool2, PsSetCreateProcessNotifyRoutineEx2) included
# specifically to test whether this DDK's ceiling is closer to
# "Server 2003" (ARCHITECTURE.md's own stated ReactOS compatibility
# level) or has crept further forward.
CATEGORIES = {
    "PnP": [
        ("IoRegisterPlugPlayNotification", "ntoskrnl"),
        ("IoUnregisterPlugPlayNotificationEx", "ntoskrnl"),
        ("IoReportTargetDeviceChangeAsynchronous", "ntoskrnl"),
        ("IoInvalidateDeviceRelations", "ntoskrnl"),
    ],
    "Power": [
        ("PoRegisterPowerSettingCallback", "ntoskrnl"),
        ("PoUnregisterPowerSettingCallback", "ntoskrnl"),
        ("PoStartNextPowerIrp", "ntoskrnl"),
    ],
    "Memory management": [
        ("ExAllocatePoolWithTag", "ntoskrnl"),
        ("ExAllocatePool2", "ntoskrnl"),  # Windows 10 2004+ - expected absent
        ("MmProtectMdlSystemAddress", "ntoskrnl"),
        ("KeInitializeGuardedMutex", "ntoskrnl"),
    ],
    "Security": [
        ("SeAccessCheckEx", "ntoskrnl"),
        ("ObRegisterCallbacks", "ntoskrnl"),
        ("ObUnRegisterCallbacks", "ntoskrnl"),
        ("CmRegisterCallbackEx", "ntoskrnl"),
        ("PsSetCreateProcessNotifyRoutineEx2", "ntoskrnl"),  # Windows 10 - expected absent
    ],
    "Interrupts": [
        ("IoConnectInterruptEx", "ntoskrnl"),
        ("IoDisconnectInterruptEx", "ntoskrnl"),
    ],
    "Filter Manager (minifilters)": [
        ("FltRegisterFilter", "ntoskrnl"),  # expected fully absent
    ],
    "NDIS 6.x (connectionless miniport model)": [
        ("NdisMRegisterMiniportDriver", "ndis"),
        ("NdisMSetMiniportAttributes", "ndis"),
        ("NdisMIndicateReceiveNetBufferLists", "ndis"),
        ("NdisAllocateNetBufferListPool", "ndis"),
    ],
}

# KMDF isn't a per-symbol question - it's a whole separate framework
# (Wdf01000.sys + a versioned WDFFUNCTIONS table, wdf.h and friends) that
# either exists in this toolchain or doesn't.
KMDF_HEADER_GLOBS = ("wdf*.h",)
KMDF_LIB_GLOBS = ("libwdf*.a", "*Wdf01000*")

# Same whole-framework shape as KMDF, for Phase 11 (ROADMAP.md, ADR-0003):
# the kernel-mode WDDM/DXGKRNL miniport interface (dispmprt.h/d3dkmddi.h/
# d3dkmdt.h/d3dukmdt.h - real filenames, confirmed by actually fetching
# Microsoft's own WDK, not guessed) is a distinct thing from the
# *usermode* Direct3D headers (d3d9.h/d3d11.h/d3d12.h etc.) this
# toolchain already ships in abundance - this checks specifically
# whether mingw-w64 *pre-packages* the kernel-mode side.
#
# It doesn't, and this check still correctly reports that - but "not
# pre-packaged in mingw-w64" is not the same finding as "doesn't exist
# in this toolchain's reach" the way this comment's own earlier version
# implied. driver/gpu/fetch-wdk-headers.sh fetches the real headers
# live from Microsoft's official NuGet packages (never vendored, see
# that script's own header comment for why) and driver/gpu/
# wddm-probe.c demonstrates they compile against this same DDK
# toolchain with a few narrow patches - real, live-verified, not
# reflected by this static presence check alone. See driver/gpu/
# README.md for the full corrected account, including one genuine,
# unresolved cross-compiler ABI finding that check doesn't surface
# either.
WDDM_HEADER_GLOBS = ("dispmprt.h", "d3dkmddi.h", "d3dkmdt.h", "d3dukmdt.h", "dxgkrnl*.h", "d3dkmthk*.h")
WDDM_LIB_GLOBS = ("*dxgkrnl*",)


def lib_path(lib_dir: Path, name: str) -> Path:
    return lib_dir / f"lib{name}.a"


def nm_symbols(archive: Path) -> set[str]:
    if not archive.exists():
        return set()
    try:
        out = subprocess.run(["nm", str(archive)], capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return set()
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            syms.add(parts[-1].lstrip("_"))  # mingw import stubs sometimes carry a leading underscore
    return syms


def header_declares(inc_dirs: list[Path], symbol: str) -> str | None:
    """Returns the header basename that declares `symbol` as a whole
    word, or None. Real recursive grep against the actual installed
    headers - not a guess from a version number or a doc page."""
    pattern = re.compile(r"\b" + re.escape(symbol) + r"\b")
    for inc_dir in inc_dirs:
        if not inc_dir.is_dir():
            continue
        for header in inc_dir.rglob("*.h"):
            try:
                text = header.read_text(errors="ignore")
            except OSError:
                continue
            if pattern.search(text):
                return header.name
    return None


def check_framework(ddk_inc: Path, root_inc: Path, lib_dir: Path,
                     header_globs: tuple[str, ...], lib_globs: tuple[str, ...]) -> dict:
    """Whole-framework presence check (KMDF, WDDM/DXGKRNL, ...) - these
    aren't a per-symbol question the way an individual ntoskrnl export
    is; either the framework's headers/import library exist in this
    toolchain at all, or they don't."""
    headers = []
    for inc_dir in (ddk_inc, root_inc):
        if inc_dir.is_dir():
            for pat in header_globs:
                headers.extend(str(p) for p in inc_dir.rglob(pat))
    libs = []
    for pat in lib_globs:
        libs.extend(str(p) for p in lib_dir.glob(pat))
    return {"headers_found": sorted(set(headers)), "libs_found": sorted(set(libs)),
            "available": bool(headers or libs)}


def run(ddk_inc: Path, root_inc: Path, lib_dir: Path) -> dict:
    inc_dirs = [ddk_inc, root_inc]
    lib_cache: dict[str, set[str]] = {}
    report = {
        "categories": {},
        "kmdf": check_framework(ddk_inc, root_inc, lib_dir, KMDF_HEADER_GLOBS, KMDF_LIB_GLOBS),
        "wddm": check_framework(ddk_inc, root_inc, lib_dir, WDDM_HEADER_GLOBS, WDDM_LIB_GLOBS),
    }

    for category, symbols in CATEGORIES.items():
        entries = []
        for symbol, lib in symbols:
            if lib not in lib_cache:
                lib_cache[lib] = nm_symbols(lib_path(lib_dir, lib))
            abi_present = symbol in lib_cache[lib]
            declared_in = header_declares(inc_dirs, symbol)
            entries.append({
                "symbol": symbol,
                "import_lib": lib,
                "abi_present": abi_present,
                "header_declared": declared_in is not None,
                "declared_in": declared_in,
            })
        report["categories"][category] = entries

    return report


def print_summary(report: dict) -> None:
    def status(e: dict) -> str:
        if e["abi_present"] and e["header_declared"]:
            return "full"
        if e["abi_present"]:
            return "abi-only (header gap)"
        if e["header_declared"]:
            return "header-only (unusual)"
        return "absent"

    for category, entries in report["categories"].items():
        print(f"\n{category}:")
        for e in entries:
            marker = {"full": "OK  ", "abi-only (header gap)": "GAP ",
                      "header-only (unusual)": "??? ", "absent": "MISS"}[status(e)]
            extra = f" (declared in {e['declared_in']})" if e["header_declared"] else ""
            print(f"  [{marker}] {e['symbol']:<40} {status(e)}{extra}")

    k = report["kmdf"]
    print(f"\nKMDF (Kernel-Mode Driver Framework):")
    if k["available"]:
        print(f"  [OK  ] present — headers: {k['headers_found']}, libs: {k['libs_found']}")
    else:
        print("  [GAP ] not pre-packaged by mingw-w64 - same corrected framing as the WDDM"
              " check below: the real KMDF headers (c/Include/wdf/kmdf/1.11/*.h - wdf.h,"
              " wdfdevice.h, and friends) are real and present in the same official Microsoft"
              " WDK NuGet package driver/gpu/fetch-wdk-headers.sh already fetches for WDDM."
              " Found live while investigating that (same package, same 'not pre-packaged here"
              " != does not exist' correction), but NOT yet compile-tested against this"
              " toolchain the way the WDDM headers were - stated honestly as found-not-verified,"
              " not claimed complete.")

    w = report["wddm"]
    print(f"\nWDDM/DXGKRNL (kernel-mode graphics miniport interface, Phase 11/ADR-0003):")
    if w["available"]:
        print(f"  [OK  ] present — headers: {w['headers_found']}, libs: {w['libs_found']}")
    else:
        print("  [GAP ] not pre-packaged by mingw-w64 - but NOT the same as 'doesn't exist':"
              " the real Microsoft WDK headers (dispmprt.h/d3dkmddi.h/d3dkmdt.h/d3dukmdt.h) are"
              " real, official, and fetch cleanly from Microsoft's own NuGet packages"
              " (driver/gpu/fetch-wdk-headers.sh - never vendored here, their EULA forbids"
              " redistribution). A real DriverEntry referencing the real DXGKRNL_INTERFACE"
              " contract compiles against them on this same toolchain, live-verified"
              " (driver/gpu/wddm-probe.c) - with one genuine, unresolved finding: a Microsoft-"
              " authored static_assert checking DXGK_CHILD_CAPABILITIES's byte layout fails"
              " under GCC/mingw-w64 (a real cross-compiler ABI mismatch, not a spurious header"
              " bug). See driver/gpu/README.md for the full, corrected account. Distinct from"
              " the usermode Direct3D headers (d3d9.h, d3d11.h, d3d12.h, ...) this toolchain"
              " already ships in abundance - those don't help a kernel-mode WDDM miniport at"
              " all. ReactOS's own DXGKRNL support is separately still early/2D-only (Oct 2025"
              " blog post) - a real, different blocker from this toolchain question.")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ddk-inc", type=Path, default=DEFAULT_DDK_INC)
    ap.add_argument("--root-inc", type=Path, default=DEFAULT_ROOT_INC)
    ap.add_argument("--lib-dir", type=Path, default=DEFAULT_LIB_DIR)
    ap.add_argument("--out", type=Path, help="write full JSON report here")
    args = ap.parse_args()

    report = run(args.ddk_inc, args.root_inc, args.lib_dir)
    print_summary(report)

    if args.out:
        args.out.write_text(json.dumps(report, indent=2))
        print(f"\nFull report written to {args.out}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
