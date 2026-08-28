/*
 * vsdev - NTLinux's first Windows kernel driver (Phase 5, ROADMAP.md).
 *
 * A minimal virtual serial-style loopback device: install it against the
 * "Root" pseudo-bus (vsdev.inf), it receives real PnP IRPs from the PnP
 * manager (IRP_MN_START_DEVICE et al) exactly as any hardware-enumerated
 * driver would, creates one device object, and performs real I/O
 * (IRP_MJ_WRITE stores a buffer, IRP_MJ_READ returns and consumes it -
 * genuine loopback semantics, not a stub that just returns success).
 *
 * Deliberately does NOT reuse driver/ntbridge/reactos/ntbridge_pnp.c's
 * bus/child-PDO machinery or its still-unfixed known bugs (see that
 * file's README) - ARCHITECTURE.md section Phase 5 says start with a
 * simple virtual/low-risk device and avoid complexity, and Rule 2 says
 * don't build more machinery than the problem calls for. This is a
 * single flat FDO, no children, no ivshmem, no shared-memory bridge
 * traffic at all yet - "performs I/O through the host bridge" (Phase 5's
 * full success criterion) is staged: this driver first proves loading +
 * PnP + real I/O works at all on a real ReactOS kernel, which nothing in
 * this repository has attempted before now (ntbridge_pnp.c compiles but
 * has never been loaded - see its README). Routing this driver's I/O
 * through ntbridge is the natural next increment once this baseline is
 * confirmed working, not bundled into the same first step.
 *
 * Same DDK/toolchain correction as ntbridge_pnp.c (see driver/ntbridge/
 * reactos/README.md): builds via mingw-w64's bundled DDK headers and
 * ntoskrnl/hal import libraries, no RosBE needed - RosBE is only for
 * building ReactOS itself from source.
 */

#include <ntddk.h>

#define VSDEV_POOL_TAG (((ULONG)'v') | ((ULONG)'s' << 8) | ((ULONG)'d' << 16) | ((ULONG)'N' << 24)) /* "Ndsv" reversed = "vsdN" -> NTLinux vsdev */
#define VSDEV_DEVICE_NAME L"\\Device\\NTLinuxVSerial0"
#define VSDEV_SYMLINK_NAME L"\\DosDevices\\NTLVSER0"
#define VSDEV_LOOPBACK_BUFFER_SIZE 4096

typedef struct _VSDEV_EXTENSION {
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDeviceObject;
    BOOLEAN SymlinkCreated;
    /* TRUE when created directly from DriverEntry (legacy `sc start`
     * load - see the comment there), FALSE when created from AddDevice
     * (real PnP enumeration). No LowerDevice/PDO to forward IRP_MJ_PNP/
     * IRP_MJ_POWER to in the legacy case. */
    BOOLEAN IsLegacy;

    /* The actual "device": a fixed-size loopback buffer. Write stores
     * into it (overwriting, single-slot - not a byte-stream FIFO, kept
     * deliberately simple per this file's header comment), Read returns
     * what's there and clears it. Protected by a fast mutex since both
     * IRP_MJ_READ and IRP_MJ_WRITE run at PASSIVE_LEVEL. */
    FAST_MUTEX BufferLock;
    UCHAR Buffer[VSDEV_LOOPBACK_BUFFER_SIZE];
    ULONG BufferValidLength;
} VSDEV_EXTENSION, *PVSDEV_EXTENSION;

static NTSTATUS VsdevCreateDeviceObject(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject, BOOLEAN IsLegacy);

DRIVER_INITIALIZE DriverEntry;
DRIVER_ADD_DEVICE VsdevAddDevice;
DRIVER_UNLOAD VsdevUnload;
DRIVER_DISPATCH VsdevDispatchPnp;
DRIVER_DISPATCH VsdevDispatchPower;
DRIVER_DISPATCH VsdevDispatchCreateClose;
DRIVER_DISPATCH VsdevDispatchRead;
DRIVER_DISPATCH VsdevDispatchWrite;

static PVSDEV_EXTENSION ExtensionOf(PDEVICE_OBJECT DeviceObject) {
    return (PVSDEV_EXTENSION)DeviceObject->DeviceExtension;
}

static NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR information) {
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("vsdev: DriverEntry - NTLinux Phase 5 test driver loading\n");

    DriverObject->DriverExtension->AddDevice = VsdevAddDevice;
    DriverObject->DriverUnload = VsdevUnload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = VsdevDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = VsdevDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = VsdevDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = VsdevDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = VsdevDispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = VsdevDispatchWrite;

    /*
     * Real finding from actually loading this driver (not assumed):
     * `sc start` against a plain `type= kernel` service (no INF/Root-
     * enumeration install) uses NT's *legacy* driver-load path - it
     * calls DriverEntry and nothing else. AddDevice only fires once a
     * PnP-enumerated PDO exists (a real Root-enumerated install via
     * vsdev.inf/devcon, or real hardware). A driver relying solely on
     * AddDevice to create its device object is therefore invisible and
     * unusable when loaded this way - confirmed live: `sc start`
     * reported STATE=RUNNING (DriverEntry genuinely ran) but
     * \\.\NTLVSER0 didn't exist ("system cannot find the file
     * specified").
     *
     * Fix: create the device directly here too, the classic legacy WDM
     * pattern - real Windows/ReactOS drivers loaded via `sc start` with
     * no PnP install do exactly this. AddDevice (below) still exists and
     * still works for a real PnP/Root-enumerated install; whichever path
     * actually runs is now sufficient on its own.
     */
    status = VsdevCreateDeviceObject(DriverObject, NULL, TRUE);
    if (!NT_SUCCESS(status)) {
        DbgPrint("vsdev: DriverEntry's direct device creation failed: 0x%08lx "
                 "(a later AddDevice from real PnP enumeration can still succeed)\n",
                 (unsigned long)status);
    }

    DbgPrint("vsdev: DriverEntry complete\n");
    return STATUS_SUCCESS;
}

NTSTATUS
VsdevAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject
    )
{
    DbgPrint("vsdev: AddDevice called by PnP manager (PDO=%p)\n", PhysicalDeviceObject);
    return VsdevCreateDeviceObject(DriverObject, PhysicalDeviceObject, FALSE);
}

static NTSTATUS
VsdevCreateDeviceObject(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT PhysicalDeviceObject,
    BOOLEAN IsLegacy
    )
{
    NTSTATUS status;
    PDEVICE_OBJECT fdo = NULL;
    PVSDEV_EXTENSION ext;
    UNICODE_STRING devname, symlink;

    RtlInitUnicodeString(&devname, VSDEV_DEVICE_NAME);
    status = IoCreateDevice(
        DriverObject,
        sizeof(VSDEV_EXTENSION),
        &devname,
        FILE_DEVICE_SERIAL_PORT,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &fdo);
    if (!NT_SUCCESS(status)) {
        DbgPrint("vsdev: IoCreateDevice failed: 0x%08lx\n", (unsigned long)status);
        return status;
    }

    ext = ExtensionOf(fdo);
    RtlZeroMemory(ext, sizeof(*ext));
    ext->Self = fdo;
    ext->PhysicalDeviceObject = PhysicalDeviceObject;
    ext->IsLegacy = IsLegacy;
    ExInitializeFastMutex(&ext->BufferLock);

    if (!IsLegacy) {
        ext->LowerDevice = IoAttachDeviceToDeviceStack(fdo, PhysicalDeviceObject);
        if (ext->LowerDevice == NULL) {
            IoDeleteDevice(fdo);
            DbgPrint("vsdev: IoAttachDeviceToDeviceStack failed\n");
            return STATUS_DEVICE_REMOVED;
        }
    }

    RtlInitUnicodeString(&symlink, VSDEV_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlink, &devname);
    if (NT_SUCCESS(status)) {
        ext->SymlinkCreated = TRUE;
        DbgPrint("vsdev: created symlink %S -> %S (open as \\\\.\\NTLVSER0)\n",
                 VSDEV_SYMLINK_NAME, VSDEV_DEVICE_NAME);
    } else {
        DbgPrint("vsdev: IoCreateSymbolicLink failed: 0x%08lx (device still usable via \\Device path)\n",
                 (unsigned long)status);
    }

    fdo->Flags |= DO_BUFFERED_IO;
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("vsdev: device created (legacy=%d), FDO=%p\n", IsLegacy, fdo);
    return STATUS_SUCCESS;
}

VOID
VsdevUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    PDEVICE_OBJECT device = DriverObject->DeviceObject;

    DbgPrint("vsdev: DriverUnload\n");

    /* A PnP-created device (real AddDevice path) is already gone by the
     * time DriverUnload runs - IRP_MN_REMOVE_DEVICE handles that. A
     * legacy device (created directly from DriverEntry - see that
     * function's comment) has no PnP manager tracking it and never gets
     * that IRP, so DriverUnload is the only place left to clean it up. */
    while (device != NULL) {
        PDEVICE_OBJECT next = device->NextDevice;
        PVSDEV_EXTENSION ext = ExtensionOf(device);

        if (ext->IsLegacy) {
            if (ext->SymlinkCreated) {
                UNICODE_STRING symlink;
                RtlInitUnicodeString(&symlink, VSDEV_SYMLINK_NAME);
                IoDeleteSymbolicLink(&symlink);
            }
            IoDeleteDevice(device);
            DbgPrint("vsdev: legacy device %p deleted\n", device);
        }

        device = next;
    }
}

NTSTATUS
VsdevDispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    UNREFERENCED_PARAMETER(DeviceObject);
    DbgPrint("vsdev: %s\n", stack->MajorFunction == IRP_MJ_CREATE ? "CREATE" : "CLOSE");
    return CompleteIrp(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
VsdevDispatchRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PVSDEV_EXTENSION ext = ExtensionOf(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG requested = stack->Parameters.Read.Length;
    ULONG copy_len;

    ExAcquireFastMutex(&ext->BufferLock);
    copy_len = (requested < ext->BufferValidLength) ? requested : ext->BufferValidLength;
    if (copy_len > 0) {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, ext->Buffer, copy_len);
    }
    DbgPrint("vsdev: READ requested=%lu returned=%lu (loopback buffer had %lu bytes)\n",
             (unsigned long)requested, (unsigned long)copy_len, (unsigned long)ext->BufferValidLength);
    /* Consumed - genuine loopback semantics, not a re-readable log. */
    ext->BufferValidLength = 0;
    ExReleaseFastMutex(&ext->BufferLock);

    return CompleteIrp(Irp, STATUS_SUCCESS, copy_len);
}

NTSTATUS
VsdevDispatchWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PVSDEV_EXTENSION ext = ExtensionOf(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG requested = stack->Parameters.Write.Length;
    ULONG copy_len = (requested < VSDEV_LOOPBACK_BUFFER_SIZE) ? requested : VSDEV_LOOPBACK_BUFFER_SIZE;

    ExAcquireFastMutex(&ext->BufferLock);
    if (copy_len > 0) {
        RtlCopyMemory(ext->Buffer, Irp->AssociatedIrp.SystemBuffer, copy_len);
    }
    ext->BufferValidLength = copy_len;
    DbgPrint("vsdev: WRITE requested=%lu stored=%lu\n", (unsigned long)requested, (unsigned long)copy_len);
    ExReleaseFastMutex(&ext->BufferLock);

    return CompleteIrp(Irp, STATUS_SUCCESS, copy_len);
}

NTSTATUS
VsdevDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PVSDEV_EXTENSION ext = ExtensionOf(DeviceObject);
    PoStartNextPowerIrp(Irp);
    if (ext->IsLegacy || ext->LowerDevice == NULL) {
        /* No lower device stack to forward to (legacy, DriverEntry-
         * created device - see DriverEntry's comment). */
        return CompleteIrp(Irp, STATUS_SUCCESS, 0);
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(ext->LowerDevice, Irp);
}

NTSTATUS
VsdevDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PVSDEV_EXTENSION ext = ExtensionOf(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    if (ext->IsLegacy || ext->LowerDevice == NULL) {
        /* A legacy (DriverEntry-created) device has no PnP manager
         * tracking it and, in practice, never receives IRP_MJ_PNP - but
         * if something ever sends one anyway, there is no lower device
         * stack to forward to, so fail cleanly instead of dereferencing
         * NULL. */
        return CompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);
    }

    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        DbgPrint("vsdev: IRP_MN_START_DEVICE\n");
        IoCopyCurrentIrpStackLocationToNext(Irp);
        status = IoCallDriver(ext->LowerDevice, Irp);
        DbgPrint("vsdev: device started, ready for I/O via \\\\.\\NTLVSER0\n");
        return status;

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
        DbgPrint("vsdev: PnP query stop/remove - allowing\n");
        break;

    case IRP_MN_STOP_DEVICE:
        DbgPrint("vsdev: IRP_MN_STOP_DEVICE\n");
        break;

    case IRP_MN_REMOVE_DEVICE:
        DbgPrint("vsdev: IRP_MN_REMOVE_DEVICE\n");
        IoSkipCurrentIrpStackLocation(Irp);
        status = IoCallDriver(ext->LowerDevice, Irp);
        if (ext->SymlinkCreated) {
            UNICODE_STRING symlink;
            RtlInitUnicodeString(&symlink, VSDEV_SYMLINK_NAME);
            IoDeleteSymbolicLink(&symlink);
        }
        IoDetachDevice(ext->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return status;

    default:
        break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(ext->LowerDevice, Irp);
}
