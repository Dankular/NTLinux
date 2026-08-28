/*
 * msvc-wddm-probe.c -- real-MSVC counterpart to wddm-probe.c, for the one
 * question wddm-probe.c's mingw-w64 build can't answer on its own: whether
 * dispmprt.h's static_assert(FIELD_OFFSET(DXGK_CHILD_CAPABILITIES,
 * HpdAwareness) == 12, ...) failure (see README.md, "What's still
 * unresolved") is a header defect or a GCC/mingw-w64-side struct-layout
 * divergence.
 *
 * This is NOT part of the project's normal Linux/mingw-w64 build (see
 * Makefile) - it requires an actual Windows host with a real MSVC
 * toolchain and the real WDK installed system-wide (Visual Studio's own
 * installer path, not fetch-wdk-headers.sh's NuGet-into-build/ path,
 * which targets the Linux/mingw-w64 cross-compile flow instead). It's
 * kept here, source-only, as a documented, reproducible artifact of a
 * real finding (ROADMAP.md Phase 11, "A real GPU became available this
 * session") - not wired into any CI or default build, and not meant to
 * be. See build-msvc-probe.bat in this directory for the exact, tested
 * invocation that produced the result documented in README.md.
 *
 * Does not modify or vendor the WDK headers - same boundary
 * fetch-wdk-headers.sh / wddm-probe.c draw on the mingw-w64 side.
 */
#include <ntddk.h>
#include <dispmprt.h>

/* The compiler's own diagnostic is the unfakeable readout here: this
 * either compiles clean (MSVC agrees the header's own layout expectation
 * is correct) or fails with the same message dispmprt.h itself carries
 * (it doesn't - see README.md for the live result: CL_ERRORLEVEL=0). */
static_assert(
    FIELD_OFFSET(DXGK_CHILD_CAPABILITIES, HpdAwareness) == 12,
    "MSVC-computed offset of HpdAwareness is NOT 12"
    );

/* Belt and suspenders: surface the literal numeric values via compile-time
 * #pragma message too, independent of the assert above, visible in build
 * output regardless. */
#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)
#pragma message("MSVC FIELD_OFFSET(DXGK_CHILD_CAPABILITIES, HpdAwareness) = " STRINGIFY(FIELD_OFFSET(DXGK_CHILD_CAPABILITIES, HpdAwareness)))
#pragma message("MSVC sizeof(DXGK_VIDEO_OUTPUT_CAPABILITIES) = " STRINGIFY(sizeof(DXGK_VIDEO_OUTPUT_CAPABILITIES)))
#pragma message("MSVC sizeof(DXGK_CHILD_CAPABILITIES) = " STRINGIFY(sizeof(DXGK_CHILD_CAPABILITIES)))

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    return STATUS_SUCCESS;
}
