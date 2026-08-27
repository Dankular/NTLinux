#!/usr/bin/env python3
"""analyze.py - turns ntexports.exe's real-Windows export dump into an
actual compatibility gap report against Wine's (and, if provided,
ReactOS's) own .spec files.

This is the point of the whole pipeline: instead of discovering that
NtSomething is missing when a game crashes on it, get the complete gap
list up front, once, from real Windows ground truth.

Usage:
    python3 analyze.py --scan windows-scan.json --specs wine-specs/ \\
        [--specs reactos-specs/] [--out gap-report.json]

--specs takes a directory of upstream .spec files (Wine's and/or
ReactOS's use the same format: `<ordinal-or-@> <type> <name>(<args>) [target]`,
one function per line, '#' comments). Filenames matter: kernel32.spec is
matched against the scanned kernel32.dll, etc. - see fetch_wine_specs.sh
in this directory for a script that downloads Wine's current specs for
the same default DLL list ntexports.exe scans.

Without real Windows scan data (i.e. run against ntexports.exe's own
output when it was pointed at Wine's *own* bundled DLLs, as this was
during development - see README.md), this only proves the analysis
pipeline itself works, not real NTLinux/Wine gaps: comparing Wine's DLLs
against Wine's own specs is close to a tautology. The real, valuable
report comes only after someone runs ntexports.exe on an actual Windows
machine and this script analyzes *that* output.
"""
import argparse
import json
import sys
from pathlib import Path

# The true native NT surface (docs/ARCHITECTURE.md section 1.3) - reported
# as its own category since it's this project's actual reason to exist,
# separate from the much larger general Win32 surface.
NT_NATIVE_PREFIXES = ("Nt", "Zw", "Rtl", "Ldr", "Csr", "Alpc", "Etw", "Dbg")

_IMPLEMENTED_KINDS = {"stdcall", "cdecl", "varargs", "thiscall", "fastcall",
                       "extern", "equate", "fortstdcall"}


def parse_spec(path: Path) -> dict[str, str]:
    """Returns {export_name: 'implemented'|'stub'}.

    Wine .spec line shape: `<ordinal-or-@> [-flag ...] <kind> [-flag ...]
    <name>[(<args>)] [target]`. Flags (-noname, -private, -syscall,
    -arch=win64, ...) can appear on *either* side of the kind keyword, not
    just before it - a first version of this parser assumed flags only
    ever preceded the kind, which silently mis-parsed every `@ stub -syscall
    NtWhatever` line in win32u.spec (flag after "stub") as exporting a
    function literally named "-syscall". Caught by comparing this parser's
    output against real DLL export names and finding suspicious 0% overlap
    on exactly the file that uses this pattern - not by inspection.
    Filtering out *all* flag tokens first, regardless of position, avoids
    the whole class of "which side is the flag on this time" bugs.
    """
    result = {}
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        tokens = line.split()
        if len(tokens) < 2:
            continue
        non_flag = [t for t in tokens[1:] if not t.startswith("-")]
        if len(non_flag) < 2:
            continue
        kind, name_tok = non_flag[0], non_flag[1]
        name = name_tok.split("(")[0]
        if kind in _IMPLEMENTED_KINDS:
            result[name] = "implemented"
        elif kind == "stub":
            result[name] = "stub"
    return result


def load_spec_dir(spec_dir: Path) -> dict[str, dict[str, str]]:
    """Returns {dll_name.dll: {export_name: status}}."""
    out = {}
    for spec_file in spec_dir.glob("*.spec"):
        dll_name = spec_file.stem + ".dll"
        out[dll_name] = parse_spec(spec_file)
    return out


def is_nt_native(name: str) -> bool:
    return name.startswith(NT_NATIVE_PREFIXES) and name != name.lower()


def analyze(scan: dict, specs_by_dll: dict[str, dict[str, str]]) -> dict:
    report = {"scan_metadata": scan.get("scan_metadata", {}), "dlls": []}

    # Prefer the 64-bit (System32) entry for each requested DLL name.
    by_name = {}
    for d in scan["dlls"]:
        if not d.get("found"):
            continue
        key = d["requested_name"]
        if key not in by_name or d["search_dir"] == "System32":
            by_name[key] = d

    for dll_name, d in sorted(by_name.items()):
        real_names = {e["name"] for e in d["exports"] if e["name"] != "(no name, ordinal only)"}
        spec = specs_by_dll.get(dll_name)

        entry = {"dll": dll_name, "real_export_count": len(real_names)}
        if spec is None:
            entry["status"] = "no_spec_available"
            report["dlls"].append(entry)
            continue

        implemented = {n for n in real_names if spec.get(n) == "implemented"}
        stubbed = {n for n in real_names if spec.get(n) == "stub"}
        missing = real_names - implemented - stubbed

        nt_missing = sorted(n for n in missing if is_nt_native(n))
        other_missing = sorted(n for n in missing if not is_nt_native(n))

        entry.update({
            "status": "analyzed",
            "implemented": len(implemented),
            "stubbed": len(stubbed),
            "missing": len(missing),
            "coverage_pct": round(100 * len(implemented) / len(real_names), 1) if real_names else 0,
            "missing_nt_native": nt_missing,
            "missing_other_sample": other_missing[:50],
            "missing_other_total": len(other_missing),
        })
        report["dlls"].append(entry)

    return report


def print_summary(report: dict) -> None:
    print(f"{'DLL':<20} {'exports':>8} {'impl':>6} {'stub':>6} {'missing':>8} {'coverage':>9}")
    for d in report["dlls"]:
        if d["status"] != "analyzed":
            print(f"{d['dll']:<20} {d['real_export_count']:>8}  (no upstream spec available)")
            continue
        print(f"{d['dll']:<20} {d['real_export_count']:>8} {d['implemented']:>6} "
              f"{d['stubbed']:>6} {d['missing']:>8} {d['coverage_pct']:>8}%")
        if d["missing_nt_native"]:
            sample = ", ".join(d["missing_nt_native"][:10])
            more = f" (+{len(d['missing_nt_native']) - 10} more)" if len(d["missing_nt_native"]) > 10 else ""
            print(f"    missing NT-native: {sample}{more}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scan", required=True, type=Path, help="ntexports.exe JSON output")
    ap.add_argument("--specs", action="append", required=True, type=Path,
                     help="directory of upstream .spec files (repeatable)")
    ap.add_argument("--out", type=Path, help="write full JSON gap report here")
    args = ap.parse_args()

    scan = json.loads(args.scan.read_text())
    specs_by_dll = {}
    for specs_dir in args.specs:
        specs_by_dll.update(load_spec_dir(specs_dir))

    if not specs_by_dll:
        print(f"analyze.py: no .spec files found in {args.specs}", file=sys.stderr)
        return 1

    report = analyze(scan, specs_by_dll)
    print_summary(report)

    if args.out:
        args.out.write_text(json.dumps(report, indent=2))
        print(f"\nFull report written to {args.out}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
