#!/usr/bin/env bash
# Downloads Wine's current .spec files for the same default DLL list
# ntexports.exe scans, for use with analyze.py --specs. Wine's specs are
# the closest thing to "what does Wine claim to implement" in one place -
# reusing them (Rule 1) instead of trying to derive the same information
# by grepping Wine's C source.
set -euo pipefail

OUT_DIR="${1:-./wine-specs}"
mkdir -p "$OUT_DIR"

# dll-name -> path within the Wine source tree (usually dlls/<name>/<name>.spec,
# a few live in a differently-named directory).
declare -A SPEC_PATHS=(
  [ntdll]="dlls/ntdll/ntdll.spec"
  [kernel32]="dlls/kernel32/kernel32.spec"
  [kernelbase]="dlls/kernelbase/kernelbase.spec"
  [user32]="dlls/user32/user32.spec"
  [win32u]="dlls/win32u/win32u.spec"
  [gdi32]="dlls/gdi32/gdi32.spec"
  [advapi32]="dlls/advapi32/advapi32.spec"
  [sechost]="dlls/sechost/sechost.spec"
  [ole32]="dlls/ole32/ole32.spec"
  [combase]="dlls/combase/combase.spec"
  [oleaut32]="dlls/oleaut32/oleaut32.spec"
  [rpcrt4]="dlls/rpcrt4/rpcrt4.spec"
  [ws2_32]="dlls/ws2_32/ws2_32.spec"
  [shell32]="dlls/shell32/shell32.spec"
  [shlwapi]="dlls/shlwapi/shlwapi.spec"
  [winmm]="dlls/winmm/winmm.spec"
  [dbghelp]="dlls/dbghelp/dbghelp.spec"
  [setupapi]="dlls/setupapi/setupapi.spec"
  [cfgmgr32]="dlls/cfgmgr32/cfgmgr32.spec"
  [d3d9]="dlls/d3d9/d3d9.spec"
  [d3d11]="dlls/d3d11/d3d11.spec"
  [dxgi]="dlls/dxgi/dxgi.spec"
  [dinput8]="dlls/dinput8/dinput8.spec"
)

BASE="https://raw.githubusercontent.com/wine-mirror/wine/master"
ok=0 fail=0
for name in "${!SPEC_PATHS[@]}"; do
  url="$BASE/${SPEC_PATHS[$name]}"
  if curl -sS -f -m 15 -o "$OUT_DIR/$name.spec" "$url"; then
    ok=$((ok+1))
  else
    echo "warn: failed to fetch $name.spec" >&2
    rm -f "$OUT_DIR/$name.spec"
    fail=$((fail+1))
  fi
done
echo "fetched $ok specs to $OUT_DIR ($fail failed)"
