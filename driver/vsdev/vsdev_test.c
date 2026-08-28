/*
 * vsdev_test - tiny userspace test client for vsdev.sys (Phase 5).
 *
 * Opens \\.\NTLVSER0 directly via CreateFileW/WriteFile/ReadFile and
 * prints real Win32 error codes to its own console - a more reliable
 * verification path than cmd.exe's `>`/`type` shell redirection, which
 * turned out to have its own quirks against a raw device path in this
 * environment (see driver/vsdev/README.md for what that looked like).
 * Builds as an ordinary Win32 console app (no DDK needed) - this is a
 * userspace tool, not kernel code.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\NTLVSER0", GENERIC_READ | GENERIC_WRITE,
                            0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed, GetLastError=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    printf("CreateFile OK, handle=%p\n", h);

    const char *msg = "Hello NTLinux Phase5";
    DWORD written = 0;
    if (!WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL)) {
        printf("WriteFile failed, GetLastError=%lu\n", (unsigned long)GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("WriteFile OK, wrote %lu bytes\n", (unsigned long)written);

    char buf[256];
    ZeroMemory(buf, sizeof(buf));
    DWORD read = 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &read, NULL)) {
        printf("ReadFile failed, GetLastError=%lu\n", (unsigned long)GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("ReadFile OK, read %lu bytes: \"%s\"\n", (unsigned long)read, buf);

    int ok = (read == written) && (memcmp(buf, msg, written) == 0);
    printf("Loopback round-trip: %s\n", ok ? "PASS" : "FAIL");

    CloseHandle(h);

    /* Also drop the result on the floppy (A:\RESULT.TXT) so a host-side
     * script driving this over QMP (see run-test.sh) can read a real
     * pass/fail verdict back without needing OCR on a screendump or a
     * network connection into the guest - the floppy image is a file on
     * the host either way. */
    HANDLE rf = CreateFileW(L"A:\\RESULT.TXT", GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (rf != INVALID_HANDLE_VALUE) {
        const char *verdict = ok ? "PASS\r\n" : "FAIL\r\n";
        DWORD w = 0;
        WriteFile(rf, verdict, (DWORD)strlen(verdict), &w, NULL);
        CloseHandle(rf);
    }

    return ok ? 0 : 1;
}
