/*
 * ntusb.h - shared between ntusb.c (the ReactOS kernel driver) and
 * ntusb_test.c (its usermode test client), same split as
 * driver/vsdev/vsdev.c and vsdev_test.c. Plain #define/struct only, no
 * DDK types, so it includes cleanly into both a kernel driver
 * (ntddk.h already pulled in first) and an ordinary Win32 console app
 * (windows.h already pulled in first) - both bring CTL_CODE and the
 * FILE_DEVICE, METHOD_ and FILE_ANY_ACCESS constants from the same
 * header family.
 */

#ifndef NTUSB_H
#define NTUSB_H

#include <stdint.h>

#define NTUSB_DEVICE_NAME  L"\\Device\\NTLinuxUsb0"
#define NTUSB_SYMLINK_NAME L"\\DosDevices\\NTLUSB0"

/* The one synthetic device's endpoint addresses (bEndpointAddress -
 * direction bit 0x80 included, per USB spec). */
#define NTUSB_BULK_IN_ENDPOINT  0x81u
#define NTUSB_BULK_OUT_ENDPOINT 0x01u

/* Must not exceed NTBRIDGE_USB_DATA_MAX (ntbridge_protocol.h) - checked
 * with a _Static_assert in ntusb.c, not just asserted here in a comment. */
#define NTUSB_MAX_TRANSFER 4096u

/* Synthetic device identity for GET_DESCRIPTOR_FROM_DEVICE. VID reused
 * from ntbridge's existing ivshmem marker (0x1AF4, the same value
 * NTBRIDGE_IVSHMEM_VENDOR_ID already uses); PID deliberately distinct
 * from the ivshmem PCI device's own product ID (0x1110) so the PCI ID
 * space and this synthetic USB ID space don't read as the same device
 * - they're bridged over the same shared memory but are different
 * identities at their respective bus levels. */
#define NTUSB_USB_VENDOR_ID  0x1AF4u
#define NTUSB_USB_PRODUCT_ID 0x1005u

/* Test-only IOCTL. NOT the real interface a genuine USB client driver
 * uses - that's IOCTL_INTERNAL_USB_SUBMIT_URB via IRP_MJ_INTERNAL_
 * DEVICE_CONTROL, which is kernel-to-kernel only (IoCallDriver from
 * another driver's stack; unreachable from usermode DeviceIoControl).
 * This exists so a usermode test client can exercise the same
 * URB-processing core (NtusbProcessUrb in ntusb.c) that a real client
 * driver's internal submission would run through, without needing a
 * second kernel-mode driver just to be that caller - see ntusb.c and
 * this directory's README for why no real vendor client driver exists
 * to test against instead. */
#define IOCTL_NTUSB_TEST_BULK_TRANSFER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
/* One struct, used both directions of the same buffered IOCTL (the
 * SystemBuffer is the same memory in and out for METHOD_BUFFERED):
 * caller fills endpoint+length(+data, for an OUT transfer) on the way
 * in; the driver overwrites status+length(+data, for an IN transfer)
 * on the way out. Same shape as ntbridge_usb_urb_msg_t on purpose -
 * this is deliberately close to the wire format it ultimately drives. */
typedef struct ntusb_test_bulk_io {
    uint8_t endpoint;    /* NTUSB_BULK_IN_ENDPOINT or NTUSB_BULK_OUT_ENDPOINT */
    uint8_t reserved[3];
    uint32_t status;     /* out: 0 = success; in: ignored */
    uint32_t length;     /* in: valid (OUT) or requested (IN) bytes; out: bytes actually transferred */
    uint8_t data[NTUSB_MAX_TRANSFER];
} ntusb_test_bulk_io_t;
#pragma pack(pop)

#endif /* NTUSB_H */
