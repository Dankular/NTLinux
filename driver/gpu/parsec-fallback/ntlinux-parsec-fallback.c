/*
 * ntlinux-parsec-fallback.c -- a real, live usermode client for the
 * "Parsec Virtual Display Adapter" (ParsecVDA), a third-party IddCx
 * virtual-display driver that ships pre-installed on hosts that use
 * Parsec for remote access -- this project's own Windows-host sandbox
 * is one (see ROADMAP.md Phase 11, "A real GPU became available this
 * session," where the same adapter was first noticed as evidence this
 * host is itself remotely operated).
 *
 * What this is: a fallback display device path. On a host that already
 * carries ParsecVDA, this program adds a real, usable virtual display,
 * holds it alive (per the header's own "ping under 100ms" contract),
 * then removes it again -- driven entirely from usermode via the
 * public API in parsec-vdd.h (github.com/nomi-san/parsec-vdd,
 * BSD-3-clause; fetched fresh by fetch-parsec-vdd-header.sh into
 * ./build/, never vendored into this repo -- ADR-0002/Rule 17, same
 * shape as driver/gpu/fetch-wdk-headers.sh).
 *
 * What this is NOT, stated precisely so it never gets conflated with
 * Phase 11's actual research: this is not ReactOS driver-cell hosting,
 * not VFIO/IOMMU-mediated, not a WDDM/DXGKRNL miniport, and doesn't
 * touch any of Phase 11's three blockers (ReactOS's own early WDDM
 * support, this project's DDK toolchain, or VFIO/IOMMU hardware). It's
 * a separate, smaller, genuinely live-verifiable capability that
 * happened to surface out of the same research pass -- see this
 * directory's own README.md for the exact live-run evidence and the
 * scope boundary, restated there too.
 *
 * Plain usermode Win32 (SetupAPI/CfgMgr32/DeviceIoControl) -- not a
 * kernel-mode driver, so none of the WDK-header/toolchain concerns
 * driver/gpu/wddm-probe.c documents apply here at all. Build with
 * build-parsec-fallback.bat (real MSVC, this Windows host only -- same
 * "Windows-host-only, not in the Linux/mingw-w64 Makefile" boundary
 * build-msvc-probe.bat already draws for the WDDM probes).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "build/parsec-vdd.h"

static volatile LONG g_keepUpdating = 0;
static HANDLE g_vdd = NULL;
static int g_addedIndex = -1;

static const char *DeviceStatusName(DeviceStatus s)
{
    switch (s) {
    case DEVICE_OK:               return "DEVICE_OK";
    case DEVICE_INACCESSIBLE:     return "DEVICE_INACCESSIBLE";
    case DEVICE_UNKNOWN:          return "DEVICE_UNKNOWN";
    case DEVICE_UNKNOWN_PROBLEM:  return "DEVICE_UNKNOWN_PROBLEM";
    case DEVICE_DISABLED:         return "DEVICE_DISABLED";
    case DEVICE_DRIVER_ERROR:     return "DEVICE_DRIVER_ERROR";
    case DEVICE_RESTART_REQUIRED: return "DEVICE_RESTART_REQUIRED";
    case DEVICE_DISABLED_SERVICE: return "DEVICE_DISABLED_SERVICE";
    case DEVICE_NOT_INSTALLED:    return "DEVICE_NOT_INSTALLED";
    default:                      return "<unrecognized>";
    }
}

/* Keeps the added display alive: parsec-vdd.h's own doc comment on
 * VddUpdate says to call it "in a side thread for each less than 100ms"
 * while any display is added. 50ms gives real headroom under that. */
static DWORD WINAPI UpdateThreadProc(LPVOID unused)
{
    (void)unused;
    while (InterlockedCompareExchange(&g_keepUpdating, 0, 0)) {
        VddUpdate(g_vdd);
        Sleep(50);
    }
    return 0;
}

/* Best-effort cleanup if the process is interrupted mid-hold (Ctrl+C) --
 * don't leave an added virtual display dangling on the host. */
static BOOL WINAPI ConsoleHandler(DWORD ctrlType)
{
    (void)ctrlType;
    InterlockedExchange(&g_keepUpdating, 0);
    if (g_vdd != NULL && g_vdd != INVALID_HANDLE_VALUE && g_addedIndex >= 0) {
        fprintf(stderr, "\nntlinux-parsec-fallback: interrupted, removing display %d before exit\n", g_addedIndex);
        VddRemoveDisplay(g_vdd, g_addedIndex);
        CloseDeviceHandle(g_vdd);
    }
    return FALSE; /* let the default handler terminate the process */
}

int main(int argc, char **argv)
{
    int holdSeconds = 5;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hold-seconds") == 0 && i + 1 < argc) {
            holdSeconds = atoi(argv[++i]);
        }
    }
    if (holdSeconds < 1) holdSeconds = 1;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    printf("ntlinux-parsec-fallback: querying %s (%s)...\n", VDD_ADAPTER_NAME, VDD_HARDWARE_ID);
    DeviceStatus status = QueryDeviceStatus(&VDD_CLASS_GUID, VDD_HARDWARE_ID);
    printf("ntlinux-parsec-fallback: device status = %s\n", DeviceStatusName(status));
    if (status != DEVICE_OK) {
        fprintf(stderr, "ntlinux-parsec-fallback: adapter not ready (status != DEVICE_OK) - not attempting to open it\n");
        return 1;
    }

    g_vdd = OpenDeviceHandle(&VDD_ADAPTER_GUID);
    if (g_vdd == NULL || g_vdd == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ntlinux-parsec-fallback: OpenDeviceHandle failed, GetLastError=%lu\n", GetLastError());
        return 1;
    }
    printf("ntlinux-parsec-fallback: opened device handle\n");

    int version = VddVersion(g_vdd);
    printf("ntlinux-parsec-fallback: driver minor version = %d\n", version);

    int before = GetSystemMetrics(SM_CMONITORS);
    printf("ntlinux-parsec-fallback: monitor count BEFORE add = %d\n", before);

    g_addedIndex = VddAddDisplay(g_vdd);
    if (g_addedIndex < 0) {
        fprintf(stderr, "ntlinux-parsec-fallback: VddAddDisplay failed (returned %d)\n", g_addedIndex);
        CloseDeviceHandle(g_vdd);
        return 1;
    }
    printf("ntlinux-parsec-fallback: added display, index = %d\n", g_addedIndex);

    InterlockedExchange(&g_keepUpdating, 1);
    HANDLE updateThread = CreateThread(NULL, 0, UpdateThreadProc, NULL, 0, NULL);
    if (updateThread == NULL) {
        fprintf(stderr, "ntlinux-parsec-fallback: CreateThread failed, GetLastError=%lu - removing display and exiting\n", GetLastError());
        VddRemoveDisplay(g_vdd, g_addedIndex);
        CloseDeviceHandle(g_vdd);
        return 1;
    }

    printf("ntlinux-parsec-fallback: holding for %d second(s) (ping thread running, <100ms interval)...\n", holdSeconds);
    Sleep((DWORD)holdSeconds * 1000);

    int during = GetSystemMetrics(SM_CMONITORS);
    printf("ntlinux-parsec-fallback: monitor count DURING (display added) = %d\n", during);

    InterlockedExchange(&g_keepUpdating, 0);
    WaitForSingleObject(updateThread, 2000);
    CloseHandle(updateThread);

    VddRemoveDisplay(g_vdd, g_addedIndex);
    printf("ntlinux-parsec-fallback: removed display index %d\n", g_addedIndex);
    g_addedIndex = -1;

    /* Give the OS a moment to actually retract the monitor before the
     * final readout - the add/remove IOCTLs themselves complete
     * synchronously, but Windows' own display-topology update after a
     * PnP-level change is not guaranteed instantaneous. */
    Sleep(500);
    int after = GetSystemMetrics(SM_CMONITORS);
    printf("ntlinux-parsec-fallback: monitor count AFTER remove = %d\n", after);

    CloseDeviceHandle(g_vdd);
    g_vdd = NULL;

    printf("ntlinux-parsec-fallback: done. before=%d during=%d after=%d\n", before, during, after);
    return 0;
}
