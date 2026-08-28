/*
 * ntusb_test - userspace test client for ntusb.sys (Phase 8).
 *
 * Opens \\.\NTLUSB0 (the child PDO's symlink) and drives the test-only
 * IOCTL_NTUSB_TEST_BULK_TRANSFER path (ntusb.h/ntusb.c): sends a tagged
 * payload out the synthetic bulk OUT endpoint, then polls the synthetic
 * bulk IN endpoint for a distinctly-tagged reply - same "tagged
 * send / tagged reply, checked both ways" pattern
 * tests/reactos/ntbridge-guest-test.c's net round-trip test and
 * tests/reactos/net-tap-echo.py already established for Phase 7.
 *
 * Same honest boundary as every other stand-in test in this repo: this
 * exercises ntusb.c's real URB-processing core (NtusbProcessUrb) via the
 * test-only IOCTL, not via a real vendor USB client driver's
 * IRP_MJ_INTERNAL_DEVICE_CONTROL submission - see driver/usb/README.md
 * for exactly what that does and does not prove.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "ntusb.h"

static const char g_send_tag[] = "NTLXUSBTEST-GUEST-TO-HOST";

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\NTLUSB0", GENERIC_READ | GENERIC_WRITE,
                            0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed, GetLastError=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    printf("CreateFile OK, handle=%p\n", h);

    /* Bulk OUT: push a tagged payload toward the host. */
    {
        ntusb_test_bulk_io_t io;
        ZeroMemory(&io, sizeof(io));
        io.endpoint = (UCHAR)NTUSB_BULK_OUT_ENDPOINT;
        io.length = (UINT32)strlen(g_send_tag);
        memcpy(io.data, g_send_tag, io.length);

        DWORD returned = 0;
        if (!DeviceIoControl(h, IOCTL_NTUSB_TEST_BULK_TRANSFER, &io, sizeof(io), &io, sizeof(io), &returned, NULL)) {
            printf("OUT DeviceIoControl failed, GetLastError=%lu\n", (unsigned long)GetLastError());
            CloseHandle(h);
            return 1;
        }
        printf("OUT: status=%lu, transferred=%lu bytes (\"%s\")\n",
               (unsigned long)io.status, (unsigned long)io.length, g_send_tag);
        if (io.status != 0 || io.length != strlen(g_send_tag)) {
            printf("OUT transfer did not fully succeed\n");
            CloseHandle(h);
            return 1;
        }
    }

    /* Bulk IN: poll for the host's tagged reply (ntusb-echo, see
     * driver/usb/host/README.md) - a handful of short retries since the
     * host side needs at least one poll tick to notice the OUT payload
     * and push a reply back onto usb_resp_ring. */
    int ok = 0;
    for (int attempt = 0; attempt < 50 && !ok; attempt++) {
        ntusb_test_bulk_io_t io;
        ZeroMemory(&io, sizeof(io));
        io.endpoint = (UCHAR)NTUSB_BULK_IN_ENDPOINT;
        io.length = sizeof(io.data);

        DWORD returned = 0;
        if (!DeviceIoControl(h, IOCTL_NTUSB_TEST_BULK_TRANSFER, &io, sizeof(io), &io, sizeof(io), &returned, NULL)) {
            printf("IN DeviceIoControl failed, GetLastError=%lu\n", (unsigned long)GetLastError());
            break;
        }
        if (io.status == 0 && io.length > 0) {
            io.data[io.length < sizeof(io.data) ? io.length : sizeof(io.data) - 1] = 0;
            printf("IN: status=%lu, transferred=%lu bytes (\"%s\")\n",
                   (unsigned long)io.status, (unsigned long)io.length, (char *)io.data);
            if (strstr((char *)io.data, "NTLXUSBTEST-HOST-TO-GUEST") != NULL)
                ok = 1;
            break;
        }
        Sleep(100);
    }

    printf("USB bulk round-trip: %s\n", ok ? "PASS" : "FAIL");
    CloseHandle(h);

    /* Same floppy-drop pattern driver/vsdev/vsdev_test.c and
     * tests/reactos/ established - a real pass/fail verdict readable
     * from the host afterward with no OCR or guest networking needed. */
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
