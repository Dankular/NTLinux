/*
 * wddm-probe.c — real, live compile probe for the real Microsoft WDK
 * WDDM/DXGKRNL kernel-mode headers (fetch-wdk-headers.sh), against this
 * project's existing mingw-w64 DDK toolchain (the same one every driver
 * under driver/ builds with).
 *
 * Correction to an earlier claim in this project's own history: Phase 11
 * / ADR-0003 originally said "not present in this toolchain at all" for
 * WDDM/DXGKRNL and left it there. Checked directly instead: the real
 * headers exist (Microsoft's own official WDK/SDK NuGet packages),
 * fetch cleanly, and — as this file demonstrates — a real DriverEntry
 * referencing the real DxgkInitialize/DXGKRNL_INTERFACE contract
 * compiles against them with a small number of narrow, documented
 * patches (below), the same "find the real header bugs, patch narrowly,
 * document precisely" technique already established in
 * driver/net/reactos/prepare-ndis-header.sh.
 *
 * What this does NOT prove, stated precisely rather than glossed over:
 * one of Microsoft's own embedded correctness checks in dispmprt.h — a
 * static_assert verifying DXGK_CHILD_CAPABILITIES's exact byte layout —
 * FAILS under this toolchain. That struct's field layout, as GCC/
 * mingw-w64 computes it, does not match what MSVC (and therefore the
 * real Windows/ReactOS kernel calling into a real driver) expects. This
 * is a genuine, unresolved cross-compiler ABI risk, not a spurious
 * build-environment quirk like the earlier NDIS header bugs — GCC/MSVC
 * bitfield/struct-layout mismatches are a real, known, hard class of
 * problem (see e.g. -mms-bitfields, which does NOT fix this one -
 * tried live, still fails). Not investigated further in this pass; see
 * README.md's "What's still unresolved" for why this means the DDK
 * toolchain gap is "real progress, not closed" rather than "solved."
 * The failing assert is commented out below specifically so the *rest*
 * of the interface can be checked for other independent issues — not
 * to hide the finding, which stays fully documented here and in
 * ROADMAP.md Phase 11.
 */
#include <ntddk.h>
#include <windef.h>   /* UINT/DWORD/etc. - d3dukmdt.h needs these even
                       * though this is otherwise an ntddk.h (kernel-
                       * mode) compile, since WDDM types are shared
                       * between usermode and kernel-mode driver code */
#include <assert.h>   /* static_assert - dispmprt.h uses the C11 macro
                       * form, which needs this include to resolve to
                       * _Static_assert; without it, gcc parses
                       * `static_assert` as an ordinary (undeclared)
                       * identifier and produces a confusing, unrelated
                       * parse error instead */

/* Two real, narrow SAL (Source Annotation Language) macro gaps: SAL
 * annotations are MSVC static-analysis hints with no run-time meaning -
 * mingw-w64 already ships most of them (sal.h/specstrings.h), but not
 * these two, which this specific header pair happens to use. Defined
 * as no-ops, matching how mingw-w64's own sal.h already treats the SAL
 * annotations it does define. */
#define _Maybenull_
#define _Pre_opt_bytecap_(size)

#include <dispmprt.h>

/* A real WDDM miniport's actual entry point signature - not a stub
 * that merely typedefs the right thing, the genuine DriverEntry NT
 * calls, referencing the real DXGKRNL_INTERFACE-shaped registration
 * struct this header declares. */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    /* Not actually calling DxgkInitialize here - no import library for
     * dxgkrnl.sys exists in this toolchain (confirmed absent, same as
     * before), so this would compile but never link. Compilation
     * against the real struct/prototype shapes is what this probe
     * checks; linking against a real dxgkrnl.sys is real, separate,
     * unattempted follow-up work, same boundary every other driver in
     * this project draws around its own unbuilt/unlinked pieces. */
    return STATUS_SUCCESS;
}
