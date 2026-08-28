@echo off
REM build-parsec-fallback.bat -- compiles ntlinux-parsec-fallback.c with
REM the real MSVC toolchain. Windows-host-only, same boundary
REM driver/gpu/build-msvc-probe.bat draws for the WDDM probes -- see
REM that file's own header comment for why this isn't part of the
REM project's normal Linux/mingw-w64 build (Makefile).
REM
REM Unlike the WDDM probes, this is plain usermode Win32 (SetupAPI /
REM CfgMgr32 / DeviceIoControl against parsec-vdd.h) -- no /kernel, no
REM WDK include paths needed at all. Run fetch-parsec-vdd-header.sh
REM first (needs bash/curl - Git Bash works) to populate build/parsec-vdd.h.
REM
REM Assumes: Visual Studio 2022 (any edition; adjust VCVARS below to
REM match). Reset PATH to a minimal Windows-only value before calling
REM vcvarsall.bat for the same reason build-msvc-probe.bat does: a long
REM inherited PATH (e.g. from Git-Bash/MSYS) can make vcvarsall.bat's
REM own internal PATH manipulation silently fail ("The input line is too
REM long."), cascading into a confusing "No Target Architecture" error.
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
set "PATH=C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem"

call "%VCVARS%" x64
echo VCVARS_ERRORLEVEL=%ERRORLEVEL%

cd /d "%~dp0"
if not exist build\parsec-vdd.h (
    echo build\parsec-vdd.h not found - run fetch-parsec-vdd-header.sh first
    exit /b 1
)

cl.exe /nologo /W3 ^
    ntlinux-parsec-fallback.c ^
    /Fe:ntlinux-parsec-fallback.exe ^
    /link setupapi.lib cfgmgr32.lib user32.lib
echo CL_ERRORLEVEL=%ERRORLEVEL%
