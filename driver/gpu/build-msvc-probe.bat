@echo off
REM build-msvc-probe.bat -- compiles msvc-wddm-probe.c with the real MSVC
REM toolchain against a real, system-installed WDK. Windows-host-only;
REM see msvc-wddm-probe.c's own header comment for why this isn't part of
REM the project's normal Linux/mingw-w64 build (Makefile).
REM
REM Assumes: Visual Studio 2022 (any edition; adjust VCVARS below to
REM match) with the "Desktop development with C++" workload, and the WDK
REM 10.0.22621.0 (or adjust WDK_VER below) installed via the Windows Kits
REM installer. Edit the two paths below if your install differs -- these
REM are the real, tested paths from the session that produced the
REM CL_ERRORLEVEL=0 result documented in README.md.
REM
REM Reset PATH to a minimal Windows-only value before calling
REM vcvarsall.bat: a long inherited PATH (e.g. from Git-Bash/MSYS) can
REM make vcvarsall.bat's own internal PATH manipulation silently fail
REM ("The input line is too long."), which cascades into _AMD64_ never
REM getting defined and a confusing "No Target Architecture" error deep
REM inside ntdef.h. /kernel alone does not define _AMD64_ either -- pass
REM it explicitly.
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
set "WDK_VER=10.0.22621.0"
set "PATH=C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem"

call "%VCVARS%" x64
echo VCVARS_ERRORLEVEL=%ERRORLEVEL%

cd /d "%~dp0"
cl.exe /nologo /c /kernel ^
    /D_AMD64_ /D_WIN64 /D_M_X64 /DNTDDI_VERSION=0x0A000008 /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 ^
    /I"C:\Program Files (x86)\Windows Kits\10\Include\%WDK_VER%\km" ^
    /I"C:\Program Files (x86)\Windows Kits\10\Include\%WDK_VER%\shared" ^
    /I"C:\Program Files (x86)\Windows Kits\10\Include\%WDK_VER%\um" ^
    msvc-wddm-probe.c
echo CL_ERRORLEVEL=%ERRORLEVEL%
