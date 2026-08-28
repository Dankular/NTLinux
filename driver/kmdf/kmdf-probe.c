/*
 * kmdf-probe.c -- real, live compile probe for the real Microsoft KMDF
 * (Kernel-Mode Driver Framework) headers (fetch-kmdf-headers.sh),
 * against this project's existing mingw-w64 DDK toolchain - the same
 * toolchain every other driver under driver/ builds with.
 *
 * Follow-up to ROADMAP.md Phase 9 and Phase 11 (docs/DECISIONS.md
 * ADR-0003's second correction): this project's own KMDF gap probe
 * (tooling/compat-db/ddkgap/) originally said "KMDF: not present in
 * this toolchain at all," checking only whether mingw-w64
 * *pre-packages* it. It doesn't, but the real headers are real,
 * official, and fetch cleanly from the same Microsoft WDK NuGet
 * package driver/gpu/fetch-wdk-headers.sh already uses for WDDM - found
 * live while investigating that phase, not yet compile-verified until
 * this file. Same "find the real header bugs, patch narrowly, document
 * precisely" technique as driver/net/reactos/prepare-ndis-header.sh and
 * driver/gpu/wddm-probe.c.
 *
 * What this does NOT prove, stated precisely: KMDF is a real framework,
 * not just headers - a real KMDF driver needs to link against a
 * version-specific WdfLdr/Wdf01000 co-installer stub library (WDF
 * drivers call into a versioned framework DLL loaded by the OS, not a
 * static import library the way ntoskrnl/hal are linked here) that
 * this toolchain doesn't have and this probe doesn't attempt to
 * provide - same boundary wddm-probe.c draws around dxgkrnl.sys. This
 * checks only that a real KMDF DriverEntry, referencing the real
 * WDF_DRIVER_CONFIG/WdfDriverCreate contract, compiles against the
 * real headers with this toolchain.
 */
#include <ntddk.h>
#include <wdf.h>

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);

    /* Not actually calling WdfDriverCreate here - no import library for
     * the versioned KMDF co-installer stub exists in this toolchain
     * (confirmed absent, same boundary as wddm-probe.c's dxgkrnl.sys).
     * Compilation against the real struct/prototype shapes is what
     * this probe checks; linking against a real WDF framework library
     * is real, separate, unattempted follow-up work. */
    status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(config);

    return status;
}
