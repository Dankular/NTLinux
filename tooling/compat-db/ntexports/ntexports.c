/*
 * ntexports - Windows-side PE export-table walker.
 *
 * Runs ON a real Windows machine and dumps the *actual* exported function
 * surface (name, ordinal, RVA or forwarder target) of ntdll.dll and the
 * other core system DLLs, straight from their real PE export directories -
 * not from documentation, not from Wine/ReactOS source, ground truth from
 * an actual Windows install. This is what tooling/compat-db/'s gap
 * analysis (see analyze.py in this directory) diffs against Wine's and
 * ReactOS's own .spec files to find out what's actually missing, in one
 * pass, rather than discovering gaps by patching reactively one crash
 * report at a time.
 *
 * Deliberately dependency-free: only the Win32 API (kernel32, version.dll)
 * and the C runtime MinGW statically links in. No admin rights required
 * (it opens files read-only, no code is ever loaded/executed) and no
 * external tooling needed - run the .exe, get a .json file.
 *
 * Build (cross-compiled from Linux, see Makefile): produces a real PE32+
 * .exe. Parsing logic in this file was verified in-session by actually
 * running the built .exe under Wine against Wine's own bundled ntdll.dll
 * (a genuine, format-correct PE32+ file, even though its *contents* are
 * Wine's reimplementation rather than Microsoft's) - see
 * tooling/compat-db/ntexports/README.md for that verification, and run it
 * on a real Windows install for the real, authoritative export surface.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define TOOL_VERSION "0.1.0"

/* --- default DLL list ---------------------------------------------------
 * ntdll.dll first and foremost - the actual native NT API surface this
 * project's ntabi/ntd exist to cover (docs/ARCHITECTURE.md section 1.3).
 * The rest are the major Win32-layer DLLs Wine already owns (section 51),
 * included so the gap analysis has real ground truth for those too, not
 * just the NT layer. Windows' hundreds of tiny api-ms-win-*.dll "API set"
 * forwarders are deliberately not in this default list - almost all of
 * them just forward straight through to one of these, and including them
 * would balloon the scan without adding real information; pass -f to scan
 * an explicit list instead if you need those too. */
static const char *g_default_dlls[] = {
    "ntdll.dll",
    "kernel32.dll", "kernelbase.dll",
    "user32.dll", "win32u.dll", "gdi32.dll", "gdi32full.dll",
    "advapi32.dll", "sechost.dll",
    "ole32.dll", "combase.dll", "oleaut32.dll", "rpcrt4.dll",
    "ws2_32.dll", "mswsock.dll",
    "shell32.dll", "shlwapi.dll",
    "winmm.dll", "avrt.dll",
    "dbghelp.dll",
    "setupapi.dll", "cfgmgr32.dll",
    "d3d9.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "d2d1.dll",
    "xinput1_4.dll", "dinput8.dll",
    NULL
};

typedef struct {
    char name[512];
    uint32_t ordinal;
    uint32_t rva;        /* 0 if this is a forwarder */
    char forwarder[512]; /* empty if this is a real export */
} export_entry_t;

/* --- tiny growable arrays, no deps -------------------------------------- */

typedef struct {
    export_entry_t *items;
    size_t count, cap;
} export_list_t;

static void export_list_push(export_list_t *l, const export_entry_t *e) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->items = realloc(l->items, l->cap * sizeof(*l->items));
    }
    l->items[l->count++] = *e;
}

/* --- JSON string escaping ------------------------------------------------ */

static void json_escape(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  fputs("\\\"", f); break;
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

/* Same escaping, for wchar_t* (Windows paths, hostnames, ...) - found by
 * actually running this against real paths under Wine: writing a raw
 * "C:\windows\..." path via %ls produced invalid JSON (\w, \s, ... aren't
 * valid JSON escapes), caught by parsing the tool's own output with
 * Python's json module rather than eyeballing it. */
static void json_escape_w(FILE *f, const wchar_t *s) {
    fputc('"', f);
    for (; *s; s++) {
        wchar_t c = *s;
        switch (c) {
            case L'"':  fputs("\\\"", f); break;
            case L'\\': fputs("\\\\", f); break;
            case L'\n': fputs("\\n", f); break;
            case L'\r': fputs("\\r", f); break;
            case L'\t': fputs("\\t", f); break;
            default:
                if (c < 0x20) fprintf(f, "\\u%04x", (unsigned)c);
                else fprintf(f, "%lc", c);
        }
    }
    fputc('"', f);
}

/* --- PE export directory parsing ---------------------------------------- */

/* Converts an RVA to a file offset by walking the section table - we're
 * parsing the file directly (not a loaded/relocated module), so this is
 * necessary; RVAs and file offsets only coincide by section alignment
 * accident. Returns 0 (a real file can't have a valid pointer at offset 0,
 * that's inside the DOS header) on failure. */
static DWORD rva_to_offset(const uint8_t *base, DWORD rva,
                            PIMAGE_SECTION_HEADER sections, WORD nsections) {
    for (WORD i = 0; i < nsections; i++) {
        DWORD start = sections[i].VirtualAddress;
        DWORD size = sections[i].Misc.VirtualSize ? sections[i].Misc.VirtualSize
                                                    : sections[i].SizeOfRawData;
        if (rva >= start && rva < start + size)
            return sections[i].PointerToRawData + (rva - start);
    }
    (void)base;
    return 0;
}

typedef struct {
    char export_dll_name[256];
    WORD machine;
    DWORD timestamp;
    export_list_t exports;
    int ok;
    char error[256];
} scan_result_t;

static void scan_dll(const wchar_t *path, scan_result_t *out) {
    memset(out, 0, sizeof(*out));

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        snprintf(out->error, sizeof(out->error), "CreateFile failed (%lu)", GetLastError());
        return;
    }

    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        snprintf(out->error, sizeof(out->error), "CreateFileMapping failed (%lu)", GetLastError());
        CloseHandle(hFile);
        return;
    }
    const uint8_t *base = (const uint8_t *)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
    CloseHandle(hFile);
    if (!base) {
        snprintf(out->error, sizeof(out->error), "MapViewOfFile failed (%lu)", GetLastError());
        return;
    }

    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        snprintf(out->error, sizeof(out->error), "not a PE file (bad DOS signature)");
        UnmapViewOfFile(base);
        return;
    }

    const uint8_t *nt_base = base + dos->e_lfanew;
    DWORD nt_sig = *(const DWORD *)nt_base;
    if (nt_sig != IMAGE_NT_SIGNATURE) {
        snprintf(out->error, sizeof(out->error), "not a PE file (bad NT signature)");
        UnmapViewOfFile(base);
        return;
    }

    const IMAGE_FILE_HEADER *fh = (const IMAGE_FILE_HEADER *)(nt_base + sizeof(DWORD));
    out->machine = fh->Machine;
    out->timestamp = fh->TimeDateStamp;

    const uint8_t *opt_base = (const uint8_t *)fh + sizeof(IMAGE_FILE_HEADER);
    WORD opt_magic = *(const WORD *)opt_base;

    DWORD export_rva, export_size;
    PIMAGE_SECTION_HEADER sections;
    WORD nsections = fh->NumberOfSections;

    if (opt_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        const IMAGE_OPTIONAL_HEADER64 *oh = (const IMAGE_OPTIONAL_HEADER64 *)opt_base;
        export_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        export_size = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        sections = (PIMAGE_SECTION_HEADER)(opt_base + fh->SizeOfOptionalHeader);
    } else if (opt_magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        const IMAGE_OPTIONAL_HEADER32 *oh = (const IMAGE_OPTIONAL_HEADER32 *)opt_base;
        export_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        export_size = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        sections = (PIMAGE_SECTION_HEADER)(opt_base + fh->SizeOfOptionalHeader);
    } else {
        snprintf(out->error, sizeof(out->error), "unrecognized optional header magic 0x%04x", opt_magic);
        UnmapViewOfFile(base);
        return;
    }

    if (export_rva == 0) {
        snprintf(out->error, sizeof(out->error), "no export directory (not a DLL, or exports nothing)");
        UnmapViewOfFile(base);
        return;
    }

    DWORD export_off = rva_to_offset(base, export_rva, sections, nsections);
    if (!export_off) {
        snprintf(out->error, sizeof(out->error), "export directory RVA not in any section");
        UnmapViewOfFile(base);
        return;
    }
    const IMAGE_EXPORT_DIRECTORY *ed = (const IMAGE_EXPORT_DIRECTORY *)(base + export_off);

    DWORD name_off = rva_to_offset(base, ed->Name, sections, nsections);
    if (name_off)
        snprintf(out->export_dll_name, sizeof(out->export_dll_name), "%s", (const char *)(base + name_off));

    DWORD func_tab_off = rva_to_offset(base, ed->AddressOfFunctions, sections, nsections);
    DWORD name_tab_off = rva_to_offset(base, ed->AddressOfNames, sections, nsections);
    DWORD ord_tab_off  = rva_to_offset(base, ed->AddressOfNameOrdinals, sections, nsections);

    const DWORD *func_tab = func_tab_off ? (const DWORD *)(base + func_tab_off) : NULL;
    const DWORD *name_tab = name_tab_off ? (const DWORD *)(base + name_tab_off) : NULL;
    const WORD  *ord_tab  = ord_tab_off  ? (const WORD  *)(base + ord_tab_off)  : NULL;

    /* Track which ordinals got a name, so we can also report the
     * ordinal-only exports afterward (functions exported with no name -
     * common for a handful of ntdll internals). */
    uint8_t *named_mask = func_tab ? calloc(ed->NumberOfFunctions, 1) : NULL;

    if (func_tab && name_tab && ord_tab) {
        for (DWORD i = 0; i < ed->NumberOfNames; i++) {
            DWORD name_str_off = rva_to_offset(base, name_tab[i], sections, nsections);
            WORD func_index = ord_tab[i];
            if (!name_str_off || func_index >= ed->NumberOfFunctions) continue;
            DWORD func_rva = func_tab[func_index];
            if (named_mask) named_mask[func_index] = 1;

            export_entry_t e;
            memset(&e, 0, sizeof(e));
            snprintf(e.name, sizeof(e.name), "%s", (const char *)(base + name_str_off));
            e.ordinal = ed->Base + func_index;

            if (func_rva >= export_rva && func_rva < export_rva + export_size) {
                DWORD fwd_off = rva_to_offset(base, func_rva, sections, nsections);
                if (fwd_off) snprintf(e.forwarder, sizeof(e.forwarder), "%s", (const char *)(base + fwd_off));
                e.rva = 0;
            } else {
                e.rva = func_rva;
            }
            export_list_push(&out->exports, &e);
        }
    }

    if (func_tab && named_mask) {
        for (DWORD i = 0; i < ed->NumberOfFunctions; i++) {
            if (named_mask[i] || func_tab[i] == 0) continue;
            export_entry_t e;
            memset(&e, 0, sizeof(e));
            snprintf(e.name, sizeof(e.name), "(no name, ordinal only)");
            e.ordinal = ed->Base + i;
            DWORD func_rva = func_tab[i];
            if (func_rva >= export_rva && func_rva < export_rva + export_size) {
                DWORD fwd_off = rva_to_offset(base, func_rva, sections, nsections);
                if (fwd_off) snprintf(e.forwarder, sizeof(e.forwarder), "%s", (const char *)(base + fwd_off));
            } else {
                e.rva = func_rva;
            }
            export_list_push(&out->exports, &e);
        }
    }

    free(named_mask);
    out->ok = 1;
    UnmapViewOfFile(base);
}

/* --- driving the scan ---------------------------------------------------- */

static void get_search_dirs(wchar_t *sys64, size_t sys64_len,
                             wchar_t *sys32, size_t sys32_len) {
    GetSystemDirectoryW(sys64, (UINT)sys64_len); /* native (x64 on x64 Windows) */
    UINT n = GetSystemWow64DirectoryW(sys32, (UINT)sys32_len); /* SysWOW64, absent on 32-bit Windows */
    if (n == 0) sys32[0] = L'\0';
}

static void write_scan_json(FILE *f, const wchar_t *dir_label, const char *dllname,
                             const wchar_t *fullpath, const scan_result_t *r) {
    fprintf(f, "    {\n");
    fprintf(f, "      \"requested_name\": "); json_escape(f, dllname); fprintf(f, ",\n");
    fprintf(f, "      \"search_dir\": "); json_escape_w(f, dir_label); fprintf(f, ",\n");
    fprintf(f, "      \"path\": "); json_escape_w(f, fullpath); fprintf(f, ",\n");
    if (!r->ok) {
        fprintf(f, "      \"found\": false,\n");
        fprintf(f, "      \"error\": "); json_escape(f, r->error); fprintf(f, "\n");
        fprintf(f, "    }");
        return;
    }
    fprintf(f, "      \"found\": true,\n");
    fprintf(f, "      \"export_dll_name\": "); json_escape(f, r->export_dll_name); fprintf(f, ",\n");
    fprintf(f, "      \"machine\": \"%s\",\n",
            r->machine == IMAGE_FILE_MACHINE_AMD64 ? "AMD64" :
            r->machine == IMAGE_FILE_MACHINE_I386 ? "I386" :
            r->machine == IMAGE_FILE_MACHINE_ARM64 ? "ARM64" : "OTHER");
    fprintf(f, "      \"pe_timestamp\": %lu,\n", (unsigned long)r->timestamp);
    fprintf(f, "      \"export_count\": %zu,\n", r->exports.count);
    fprintf(f, "      \"exports\": [\n");
    for (size_t i = 0; i < r->exports.count; i++) {
        const export_entry_t *e = &r->exports.items[i];
        fprintf(f, "        {\"name\": ");
        json_escape(f, e->name);
        fprintf(f, ", \"ordinal\": %u", e->ordinal);
        if (e->forwarder[0]) {
            fprintf(f, ", \"forwarder\": ");
            json_escape(f, e->forwarder);
        } else {
            fprintf(f, ", \"rva\": %u", e->rva);
        }
        fprintf(f, "}%s\n", (i + 1 < r->exports.count) ? "," : "");
    }
    fprintf(f, "      ]\n");
    fprintf(f, "    }");
}

int wmain(int argc, wchar_t *argv[]) {
    const char *dlls_default_storage[256];
    const char **dlls = g_default_dlls;
    int ndlls = 0;
    while (dlls[ndlls]) ndlls++;

    char list_file[MAX_PATH] = {0};
    char out_path[MAX_PATH] = "ntexports-out.json";

    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-f") == 0 && i + 1 < argc) {
            wcstombs(list_file, argv[++i], sizeof(list_file));
        } else if (wcscmp(argv[i], L"-o") == 0 && i + 1 < argc) {
            wcstombs(out_path, argv[++i], sizeof(out_path));
        } else if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0) {
            wprintf(L"ntexports %hs - PE export table walker\n", TOOL_VERSION);
            wprintf(L"usage: ntexports.exe [-f dll-list.txt] [-o out.json]\n");
            wprintf(L"  -f  one DLL filename per line, instead of the built-in default list\n");
            wprintf(L"  -o  output JSON path (default: ntexports-out.json)\n");
            return 0;
        }
    }

    int custom_count = 0;
    if (list_file[0]) {
        FILE *lf = fopen(list_file, "r");
        if (!lf) { fprintf(stderr, "cannot open %s\n", list_file); return 1; }
        char line[256];
        while (custom_count < 256 && fgets(line, sizeof(line), lf)) {
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
            if (!n) continue;
            char *copy = _strdup(line);
            dlls_default_storage[custom_count++] = copy;
        }
        fclose(lf);
        dlls = dlls_default_storage;
        ndlls = custom_count;
    }

    wchar_t sys64[MAX_PATH], sys32[MAX_PATH];
    get_search_dirs(sys64, MAX_PATH, sys32, MAX_PATH);

    wchar_t hostname[256] = L"unknown";
    DWORD hostname_len = 256;
    GetComputerNameW(hostname, &hostname_len);

    OSVERSIONINFOEXW osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionExW((OSVERSIONINFOW *)&osvi); /* deprecated but universally available; fine for a diagnostic tool */

    FILE *out = fopen(out_path, "w");
    if (!out) { fprintf(stderr, "cannot create %s\n", out_path); return 1; }

    fprintf(out, "{\n");
    fprintf(out, "  \"scan_metadata\": {\n");
    fprintf(out, "    \"tool_version\": \"%s\",\n", TOOL_VERSION);
    fprintf(out, "    \"hostname\": "); json_escape_w(out, hostname); fprintf(out, ",\n");
    fprintf(out, "    \"os_version\": \"%lu.%lu.%lu\"\n",
            osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
    fprintf(out, "  },\n");
    fprintf(out, "  \"dlls\": [\n");

    int printed_any = 0;
    int total_exports = 0, total_found = 0, total_missing = 0;

    for (int i = 0; i < ndlls; i++) {
        wchar_t wdllname[128];
        mbstowcs(wdllname, dlls[i], 128);

        struct { const wchar_t *dir; } candidates[2] = { {sys64}, {sys32} };
        for (int c = 0; c < 2; c++) {
            if (!candidates[c].dir[0]) continue;
            wchar_t fullpath[MAX_PATH];
            swprintf(fullpath, MAX_PATH, L"%ls\\%ls", candidates[c].dir, wdllname);

            scan_result_t r;
            scan_dll(fullpath, &r);

            if (printed_any) fprintf(out, ",\n");
            write_scan_json(out, c == 0 ? L"System32" : L"SysWOW64", dlls[i], fullpath, &r);
            printed_any = 1;

            if (r.ok) {
                total_found++;
                total_exports += (int)r.exports.count;
                wprintf(L"  [%ls] %hs: %zu exports\n", c == 0 ? L"System32" : L"SysWOW64",
                        dlls[i], r.exports.count);
            } else {
                total_missing++;
                wprintf(L"  [%ls] %hs: %hs\n", c == 0 ? L"System32" : L"SysWOW64", dlls[i], r.error);
            }
            free(r.exports.items);
        }
    }

    fprintf(out, "\n  ],\n");
    fprintf(out, "  \"summary\": {\"dlls_found\": %d, \"dlls_missing\": %d, \"total_exports\": %d}\n",
            total_found, total_missing, total_exports);
    fprintf(out, "}\n");
    fclose(out);

    wprintf(L"\nWrote %hs (%d DLLs found, %d not found/failed, %d total exports)\n",
            out_path, total_found, total_missing, total_exports);
    return 0;
}
