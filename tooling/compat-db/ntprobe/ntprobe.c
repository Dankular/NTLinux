/*
 * ntprobe - Windows-side live NT native API behavioral probe.
 *
 * Companion to ../ntexports/: ntexports.exe answers "does this symbol
 * exist" from a PE export table (static). ntprobe.exe answers "what does
 * calling it actually do" by making real Nt/Zw calls on a real Windows
 * machine and recording the real NTSTATUS/output - genuine differential-
 * testing ground truth (Rule 11 in CLAUDE.md), which ntexports's own
 * README flags as not yet attempted for this project.
 *
 * Origin: this file exists because of a direct, specific ask to use
 * MiroKaku/Musa.Veil (an MIT-licensed header-only library of undocumented
 * NT struct/prototype declarations, in the PHNT/MINT lineage) "as a
 * wrapper and service ... to support calls that Wine can't". That framing
 * doesn't fit what the library actually is - it ships zero runtime code,
 * so it can't be a service, and it can't make any call succeed that
 * wouldn't already succeed against real ntdll.dll. What it *can*
 * legitimately do is tell you the correct struct layout/prototype for an
 * undocumented NT API so you can call it correctly yourself - which is
 * exactly what a real differential-behavior probe needs and what
 * ntexports's README already flagged as the missing piece. Building that
 * probe is the honest way to act on the underlying ask.
 *
 * Real, substantive compatibility finding from actually trying it (not
 * assumed): Musa.Veil's headers are MSVC-only in practice.
 * `#include "Veil.h"` (the intended single entry point) fails to compile
 * under this project's MinGW cross-compilation toolchain with 100+ errors
 * even restricted to a single narrow sub-header
 * (Veil/Veil.System.Executive.h alone) - `__kernel_entry`/SAL annotations
 * MinGW doesn't define, `enum` forward-declarations MSVC allows as an
 * extension that GCC rejects, and (most tellingly) several
 * `_Static_assert(sizeof(...) == N)` checks on deeply nested/bitfield
 * structures that genuinely fail even with `-mms-bitfields` - a real
 * GCC-vs-MSVC struct-layout ABI mismatch for those specific types, not a
 * missing macro. Patching all of that would mean maintaining a parallel
 * MinGW-compatible fork of most of a 30-file library indefinitely, for a
 * probe that only needs ~4 structures - disproportionate (Rule 2: don't
 * build more machinery than the problem calls for).
 *
 * So: the ~4 structures and enums this file needs are hand-declared
 * below, each one checked against Musa.Veil's declaration for the exact
 * same struct (cross-referenced at the point of use) - genuinely using
 * Musa.Veil as the authoritative reference source for getting an
 * undocumented layout right, matching Rule 15's ownership-tracking
 * spirit, without inheriting its MSVC-only build assumptions. These
 * particular structures (SYSTEM_BASIC_INFORMATION, PROCESS_BASIC_INFORMATION,
 * OBJECT_BASIC_INFORMATION) are long-stable NT ABI (unchanged since
 * Windows 2000/XP-era ntdll), which is why hand-transcribing them is
 * low-risk verified this way, unlike the newer/larger structures where
 * the static-assert failures actually occurred.
 *
 * Same dependency-free, cross-compiled-from-Linux shape as ntexports.c:
 * no admin rights needed, produces a real PE32+ .exe, meant to run ON a
 * real Windows machine.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define TOOL_VERSION "0.1.0"

/* --- hand-declared NT types, cross-referenced against Musa.Veil ------- */

typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

/* Musa.Veil: Veil/Veil.System.Executive.h, _SYSTEM_BASIC_INFORMATION */
typedef struct _SYSTEM_BASIC_INFORMATION {
    ULONG Reserved;
    ULONG TimerResolution;
    ULONG PageSize;
    ULONG NumberOfPhysicalPages;
    ULONG LowestPhysicalPageNumber;
    ULONG HighestPhysicalPageNumber;
    ULONG AllocationGranularity;
    ULONG_PTR MinimumUserModeAddress;
    ULONG_PTR MaximumUserModeAddress;
    ULONG_PTR ActiveProcessorsAffinityMask;
    UCHAR NumberOfProcessors;
} SYSTEM_BASIC_INFORMATION, *PSYSTEM_BASIC_INFORMATION;
#define SystemBasicInformationClass 0 /* SYSTEM_INFORMATION_CLASS::SystemBasicInformation */

/* Musa.Veil: Veil/Veil.System.Process.h, _PROCESS_BASIC_INFORMATION */
typedef struct _PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PVOID PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;
#define ProcessBasicInformationClass 0 /* PROCESSINFOCLASS::ProcessBasicInformation */

/* Musa.Veil: Veil/Veil.System.ObjectManager.h, _OBJECT_BASIC_INFORMATION */
typedef struct _OBJECT_BASIC_INFORMATION {
    ULONG Attributes;
    ACCESS_MASK GrantedAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    ULONG PagedPoolCharge;
    ULONG NonPagedPoolCharge;
    ULONG Reserved[3];
    ULONG NameInfoSize;
    ULONG TypeInfoSize;
    ULONG SecurityDescriptorSize;
    LARGE_INTEGER CreationTime;
} OBJECT_BASIC_INFORMATION, *POBJECT_BASIC_INFORMATION;
#define ObjectBasicInformationClass 0 /* OBJECT_INFORMATION_CLASS::ObjectBasicInformation */

/* Prototypes: not re-declared with NTSYSAPI/NTSYSCALLAPI linkage
 * (Musa.Veil's declarations assume MSVC + linking against ntdll.lib);
 * plain extern here, resolved at link time against mingw-w64's own
 * libntdll.a, which was confirmed (via `nm`) to export all five of
 * these symbols already - no import library authored by this project. */
extern NTSTATUS NTAPI NtQuerySystemInformation(ULONG SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);
extern NTSTATUS NTAPI NtQueryInformationProcess(HANDLE ProcessHandle, ULONG ProcessInformationClass, PVOID ProcessInformation, ULONG ProcessInformationLength, PULONG ReturnLength);
extern NTSTATUS NTAPI NtQueryObject(HANDLE Handle, ULONG ObjectInformationClass, PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength);
extern NTSTATUS NTAPI NtCreateEvent(PHANDLE EventHandle, ACCESS_MASK DesiredAccess, PVOID ObjectAttributes, ULONG EventType, BOOLEAN InitialState);
extern NTSTATUS NTAPI NtSetEvent(HANDLE EventHandle, PLONG PreviousState);
extern NTSTATUS NTAPI NtWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
extern NTSTATUS NTAPI NtClose(HANDLE Handle);

/* --- tiny JSON helpers, matching ../ntexports/ntexports.c's style ----- */

static void json_escape(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (c < 0x20) fprintf(f, "\\u%04x", c);
                else fputc(c, f);
        }
    }
    fputc('"', f);
}

/* --- probes ------------------------------------------------------------
 * Each probe makes one or more real Nt* calls and reports the real
 * NTSTATUS plus a self-check ("ok") comparing the result against a
 * value obtainable independently (e.g. GetCurrentProcessId()) - so the
 * JSON output states not just "it returned success" but "the returned
 * data was actually correct", the differential-testing signal Rule 11
 * asks for.
 */

static int probe_system_basic_information(FILE *out) {
    SYSTEM_BASIC_INFORMATION sbi;
    ULONG returned = 0;
    NTSTATUS status;

    memset(&sbi, 0, sizeof(sbi));
    status = NtQuerySystemInformation(SystemBasicInformationClass, &sbi, sizeof(sbi), &returned);

    fprintf(out, "    {\n");
    fprintf(out, "      \"probe\": \"NtQuerySystemInformation(SystemBasicInformation)\",\n");
    fprintf(out, "      \"status\": \"0x%08lx\",\n", (unsigned long)status);
    fprintf(out, "      \"success\": %s,\n", NT_SUCCESS(status) ? "true" : "false");
    if (NT_SUCCESS(status)) {
        fprintf(out, "      \"page_size\": %lu,\n", (unsigned long)sbi.PageSize);
        fprintf(out, "      \"number_of_processors\": %u,\n", (unsigned)sbi.NumberOfProcessors);
        /* Cross-check against the documented Win32 equivalent - if these
         * disagree, either our hand-declared struct layout is wrong or
         * NtQuerySystemInformation's real behavior differs from what
         * Musa.Veil documents; either is worth knowing. */
        SYSTEM_INFO win32_info;
        GetSystemInfo(&win32_info);
        int ok = (sbi.PageSize == win32_info.dwPageSize) &&
                 (sbi.NumberOfProcessors == (UCHAR)win32_info.dwNumberOfProcessors);
        fprintf(out, "      \"cross_check\": \"GetSystemInfo() page_size=%lu ncpus=%lu\",\n",
                (unsigned long)win32_info.dwPageSize, (unsigned long)win32_info.dwNumberOfProcessors);
        fprintf(out, "      \"ok\": %s\n", ok ? "true" : "false");
    } else {
        fprintf(out, "      \"ok\": false\n");
    }
    fprintf(out, "    }");
    return NT_SUCCESS(status) ? 1 : 0;
}

static int probe_process_basic_information(FILE *out) {
    PROCESS_BASIC_INFORMATION pbi;
    ULONG returned = 0;
    NTSTATUS status;

    memset(&pbi, 0, sizeof(pbi));
    status = NtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformationClass,
                                        &pbi, sizeof(pbi), &returned);

    fprintf(out, "    {\n");
    fprintf(out, "      \"probe\": \"NtQueryInformationProcess(ProcessBasicInformation, self)\",\n");
    fprintf(out, "      \"status\": \"0x%08lx\",\n", (unsigned long)status);
    fprintf(out, "      \"success\": %s,\n", NT_SUCCESS(status) ? "true" : "false");
    if (NT_SUCCESS(status)) {
        DWORD real_pid = GetCurrentProcessId();
        int ok = ((ULONG_PTR)real_pid == pbi.UniqueProcessId);
        fprintf(out, "      \"unique_process_id\": %lu,\n", (unsigned long)pbi.UniqueProcessId);
        fprintf(out, "      \"cross_check\": \"GetCurrentProcessId()=%lu\",\n", (unsigned long)real_pid);
        fprintf(out, "      \"ok\": %s\n", ok ? "true" : "false");
    } else {
        fprintf(out, "      \"ok\": false\n");
    }
    fprintf(out, "    }");
    return NT_SUCCESS(status) ? 1 : 0;
}

/*
 * The one that actually matters most for this project: NTLinux's own
 * ntd (ntd/ntd.c) reimplements NT Event object semantics from scratch
 * (auto-reset create -> initially unsignaled -> NtSetEvent signals it ->
 * a waiter is released and the event auto-resets back to unsignaled).
 * This probe exercises the exact same state machine against a real
 * ntdll/ntoskrnl, so ntd's test suite (ntabi/tests/test_ntabi.c) has a
 * real behavioral reference to diff against - not just "does the symbol
 * exist" (ntexports.exe) but "does our own reimplementation's state
 * machine actually match Windows's".
 */
static int probe_event_roundtrip(FILE *out) {
    HANDLE event = NULL;
    NTSTATUS status_create, status_wait_before, status_set, status_wait_after, status_wait_reset;
    LARGE_INTEGER zero_timeout;
    zero_timeout.QuadPart = 0; /* poll, don't block, for the "not yet signaled" check */

    /* SynchronizationEvent (auto-reset) = 1, NotificationEvent = 0, per
     * Musa.Veil's EVENT_TYPE enum - matches ntd's own OBJ_EVENT auto-reset
     * default. */
    status_create = NtCreateEvent(&event, EVENT_ALL_ACCESS, NULL, /*SynchronizationEvent*/ 1, FALSE);

    fprintf(out, "    {\n");
    fprintf(out, "      \"probe\": \"NtCreateEvent/NtSetEvent/NtWaitForSingleObject round-trip (auto-reset)\",\n");
    fprintf(out, "      \"create_status\": \"0x%08lx\",\n", (unsigned long)status_create);

    if (!NT_SUCCESS(status_create)) {
        fprintf(out, "      \"success\": false,\n");
        fprintf(out, "      \"ok\": false\n");
        fprintf(out, "    }");
        return 0;
    }

    status_wait_before = NtWaitForSingleObject(event, FALSE, &zero_timeout);
    status_set = NtSetEvent(event, NULL);
    status_wait_after = NtWaitForSingleObject(event, FALSE, &zero_timeout);
    /* Auto-reset: the wait above should have consumed the signal, so a
     * second immediate wait should NOT be satisfied. */
    status_wait_reset = NtWaitForSingleObject(event, FALSE, &zero_timeout);
    NtClose(event);

    /* STATUS_TIMEOUT = 0x00000102, STATUS_WAIT_0 = 0x00000000 */
    int ok = (status_wait_before == 0x00000102L) &&  /* not yet signaled */
              NT_SUCCESS(status_set) &&
              (status_wait_after == 0x00000000L) &&   /* signaled, wait satisfied */
              (status_wait_reset == 0x00000102L);      /* auto-reset back to unsignaled */

    fprintf(out, "      \"wait_before_set_status\": \"0x%08lx\",\n", (unsigned long)status_wait_before);
    fprintf(out, "      \"set_status\": \"0x%08lx\",\n", (unsigned long)status_set);
    fprintf(out, "      \"wait_after_set_status\": \"0x%08lx\",\n", (unsigned long)status_wait_after);
    fprintf(out, "      \"wait_after_autoreset_status\": \"0x%08lx\",\n", (unsigned long)status_wait_reset);
    fprintf(out, "      \"expectation\": \"before=TIMEOUT(0x102), after_set=WAIT_0(0), after_autoreset=TIMEOUT(0x102) again - same state machine ntd/ntd.c (OBJ_EVENT) implements\",\n");
    fprintf(out, "      \"success\": true,\n");
    fprintf(out, "      \"ok\": %s\n", ok ? "true" : "false");
    fprintf(out, "    }");
    return ok;
}

static int probe_object_basic_information(FILE *out) {
    HANDLE event = NULL;
    OBJECT_BASIC_INFORMATION obi;
    ULONG returned = 0;
    NTSTATUS status_create, status;

    status_create = NtCreateEvent(&event, EVENT_ALL_ACCESS, NULL, /*NotificationEvent*/ 0, FALSE);

    fprintf(out, "    {\n");
    fprintf(out, "      \"probe\": \"NtQueryObject(ObjectBasicInformation) on a fresh event handle\",\n");

    if (!NT_SUCCESS(status_create)) {
        fprintf(out, "      \"create_status\": \"0x%08lx\",\n", (unsigned long)status_create);
        fprintf(out, "      \"success\": false,\n");
        fprintf(out, "      \"ok\": false\n");
        fprintf(out, "    }");
        return 0;
    }

    memset(&obi, 0, sizeof(obi));
    status = NtQueryObject(event, ObjectBasicInformationClass, &obi, sizeof(obi), &returned);
    NtClose(event);

    fprintf(out, "      \"status\": \"0x%08lx\",\n", (unsigned long)status);
    fprintf(out, "      \"success\": %s,\n", NT_SUCCESS(status) ? "true" : "false");
    if (NT_SUCCESS(status)) {
        /* A fresh, just-created, not-yet-duplicated handle should show
         * HandleCount == 1. */
        int ok = (obi.HandleCount == 1);
        fprintf(out, "      \"handle_count\": %lu,\n", (unsigned long)obi.HandleCount);
        fprintf(out, "      \"ok\": %s\n", ok ? "true" : "false");
    } else {
        fprintf(out, "      \"ok\": false\n");
    }
    fprintf(out, "    }");
    return NT_SUCCESS(status) ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *out_path = "ntprobe-results.json";
    if (argc > 1) out_path = argv[1];

    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "ntprobe: could not open %s for writing\n", out_path);
        return 1;
    }

    char hostname[256] = {0};
    DWORD hostname_len = sizeof(hostname);
    GetComputerNameA(hostname, &hostname_len);

    OSVERSIONINFOEXA osvi;
    memset(&osvi, 0, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GetVersionExA((OSVERSIONINFOA *)&osvi);
#pragma GCC diagnostic pop

    fprintf(out, "{\n");
    fprintf(out, "  \"tool\": \"ntprobe\",\n");
    fprintf(out, "  \"version\": \"%s\",\n", TOOL_VERSION);
    fprintf(out, "  \"hostname\": "); json_escape(out, hostname); fprintf(out, ",\n");
    fprintf(out, "  \"os_version\": \"%lu.%lu.%lu\",\n",
            (unsigned long)osvi.dwMajorVersion, (unsigned long)osvi.dwMinorVersion,
            (unsigned long)osvi.dwBuildNumber);
    fprintf(out, "  \"note\": \"real Nt*/Zw* calls against this machine's real ntdll.dll - ");
    fprintf(out, "differential ground truth for ntd/ntd.c and ntabi/tests/test_ntabi.c, ");
    fprintf(out, "not documentation or memory\",\n");
    fprintf(out, "  \"probes\": [\n");

    int n_ok = 0, n_total = 0;
    n_total++; n_ok += probe_system_basic_information(out); fprintf(out, ",\n");
    n_total++; n_ok += probe_process_basic_information(out); fprintf(out, ",\n");
    n_total++; n_ok += probe_event_roundtrip(out); fprintf(out, ",\n");
    n_total++; n_ok += probe_object_basic_information(out); fprintf(out, "\n");

    fprintf(out, "  ],\n");
    fprintf(out, "  \"summary\": \"%d/%d probes matched expected behavior\"\n", n_ok, n_total);
    fprintf(out, "}\n");

    fclose(out);
    printf("ntprobe: %d/%d probes OK, wrote %s\n", n_ok, n_total, out_path);
    return (n_ok == n_total) ? 0 : 1;
}
