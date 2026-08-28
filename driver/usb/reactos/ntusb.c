/*
 * ntusb — ReactOS-side USB bridge driver (Phase 8, ARCHITECTURE.md
 * section 27's "USB device cell" / section 51's "Linux owns... USB host
 * stack", driver/usb/README.md).
 *
 * NOT a virtual USB Host Controller Driver. mingw-w64's DDK ships
 * usb.h/usbioctl.h (the URB shapes and IOCTL_INTERNAL_USB_SUBMIT_URB a
 * client driver stacked *above* a bus uses) but not usbport.h (the
 * internal miniport interface a from-scratch HC driver would register
 * with ReactOS's usbport.sys) — confirmed absent by direct search, not
 * assumed; see this directory's README. So instead of a HC miniport,
 * this is a WDM bus driver — same shape as driver/ntbridge/reactos/
 * ntbridge_pnp.c — that attaches to the ivshmem PCI function and
 * creates exactly one child PDO representing a single synthetic
 * vendor-class USB device (one bulk IN endpoint, one bulk OUT
 * endpoint). That child PDO's IRP_MJ_INTERNAL_DEVICE_CONTROL handler
 * answers IOCTL_INTERNAL_USB_SUBMIT_URB directly using real struct
 * _URB shapes from usb.h — the same request a real vendor USB client
 * driver bound on top of it would send — which is what "a vendor
 * Windows USB driver operates a device unavailable through a native
 * Linux driver" mechanically needs, short of a real HC miniport.
 *
 * Device/configuration/string descriptors are entirely local/canned —
 * there is no real hardware behind them, so Linux has nothing
 * authoritative to say about them. Only bulk-transfer *data* crosses
 * the bridge, via ntbridge_protocol.h's usb_req_ring/usb_resp_ring
 * (Phase 8's protocol v3 addition) — same simplicity-over-throughput
 * tradeoff net_tx_ring/net_rx_ring made in Phase 7.
 *
 * Transfers complete synchronously against the ring's current state
 * (push-and-return for OUT, immediate try-pop for IN) rather than
 * blocking/pending an IRP for data to arrive later — a deliberate
 * scope reduction (Rule 2) flagged here and in the README, not a bug
 * found later: implementing IoMarkIrpPending + a deferred completion
 * path is real follow-up work, not required for the one thing this
 * pass actually verifies (see "What's verified" in the README).
 *
 * NOT YET LOADED live inside a booted ReactOS kernel — same category
 * of gap ROADMAP.md Phase 14 already tracks for ntbridge_pnp.c and
 * vsdev.c's PnP path; this driver's live-PnP-load gap belongs there
 * too. Its child PDO's IRP_MJ_INTERNAL_DEVICE_CONTROL / URB dispatch
 * (NtusbProcessUrb) *is* exercised live in this sandbox, via the
 * test-only IOCTL_NTUSB_TEST_BULK_TRANSFER path (ntusb_test.c) — see
 * the README for exactly what that does and does not prove.
 */

#include <ntddk.h>
#include <usbioctl.h> /* IOCTL_INTERNAL_USB_SUBMIT_URB - not pulled in by usb.h itself */
#include <usb.h>

#include "ntbridge_protocol.h"
#include "ntusb.h"

_Static_assert(NTUSB_MAX_TRANSFER <= NTBRIDGE_USB_DATA_MAX,
               "NTUSB_MAX_TRANSFER must fit in one ntbridge usb ring slot");

#define NTUSB_POOL_TAG (((ULONG)'b') | ((ULONG)'s' << 8) | ((ULONG)'u' << 16) | ((ULONG)'N' << 24)) /* "Nusb" */

typedef struct _NTUSB_EXTENSION {
    BOOLEAN IsFdo;
    PDEVICE_OBJECT Self;

    /* FDO fields */
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT ChildPdo; /* the one synthetic device, once created */
    PHYSICAL_ADDRESS ShmPhysicalBase;
    ULONG ShmLength;
    ntbridge_shm_header_t *Shm;
    volatile LONG NextRequestId;

    /* PDO fields */
    PDEVICE_OBJECT ParentFdo;
    BOOLEAN SymlinkCreated;

    /* USBD_PIPE_HANDLE values this driver hands back from
     * URB_FUNCTION_SELECT_CONFIGURATION — since this driver plays the
     * role a real bus/HCD normally plays for pipe-handle assignment, it
     * gets to pick the encoding: just the endpoint address itself,
     * round-tripped unchanged. Set once SELECT_CONFIGURATION has run;
     * a client driver that skips straight to BULK_OR_INTERRUPT_TRANSFER
     * without selecting a configuration first is out of spec and gets
     * USBD_STATUS_INTERFACE_NOT_FOUND, same as a real bus would. */
    BOOLEAN ConfigurationSelected;
} NTUSB_EXTENSION, *PNTUSB_EXTENSION;

static PNTUSB_EXTENSION ExtensionOf(PDEVICE_OBJECT DeviceObject) {
    return (PNTUSB_EXTENSION)DeviceObject->DeviceExtension;
}

static NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR information) {
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NtusbProcessUrb(PNTUSB_EXTENSION PdoExt, PURB Urb);
static NTSTATUS NtusbCreateChildPdo(PDRIVER_OBJECT DriverObject, PNTUSB_EXTENSION FdoExt);

DRIVER_INITIALIZE DriverEntry;
DRIVER_ADD_DEVICE NtusbAddDevice;
DRIVER_UNLOAD NtusbUnload;
DRIVER_DISPATCH NtusbDispatchPnp;
DRIVER_DISPATCH NtusbDispatchPower;
DRIVER_DISPATCH NtusbDispatchCreateClose;
DRIVER_DISPATCH NtusbDispatchDeviceControl;
DRIVER_DISPATCH NtusbDispatchInternalDeviceControl;

/* ---- DriverEntry / AddDevice ------------------------------------------ */

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("ntusb: DriverEntry - NTLinux Phase 8 USB bridge loading\n");

    DriverObject->DriverExtension->AddDevice = NtusbAddDevice;
    DriverObject->DriverUnload = NtusbUnload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = NtusbDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = NtusbDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = NtusbDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = NtusbDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = NtusbDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = NtusbDispatchInternalDeviceControl;

    return STATUS_SUCCESS;
}

NTSTATUS
NtusbAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject
    )
{
    NTSTATUS status;
    PDEVICE_OBJECT fdo = NULL;
    PNTUSB_EXTENSION ext;

    /* Unnamed FDO on the ivshmem PCI function — same hardware ID
     * driver/ntbridge/reactos/ntbridge.inf and driver/net/reactos/
     * ntnet.inf already match (PCI\VEN_1AF4&DEV_1110). That collision
     * is pre-existing (ntnet.inf's own header comment already flags it
     * for the ntbridge/ntnet pair) and this driver makes it a three-way
     * collision, not a new problem introduced here — see ntusb.inf and
     * this directory's README for the real fix (a distinct ivshmem
     * instance per bridge) as documented follow-up, and why it wasn't
     * done in this pass (each phase's driver is verified independently,
     * never loaded against the same cell simultaneously, so it hasn't
     * blocked anything yet). */
    status = IoCreateDevice(
        DriverObject,
        sizeof(NTUSB_EXTENSION),
        NULL,
        FILE_DEVICE_BUS_EXTENDER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &fdo);
    if (!NT_SUCCESS(status))
        return status;

    ext = ExtensionOf(fdo);
    RtlZeroMemory(ext, sizeof(*ext));
    ext->IsFdo = TRUE;
    ext->Self = fdo;
    ext->PhysicalDeviceObject = PhysicalDeviceObject;

    ext->LowerDevice = IoAttachDeviceToDeviceStack(fdo, PhysicalDeviceObject);
    if (ext->LowerDevice == NULL) {
        IoDeleteDevice(fdo);
        return STATUS_DEVICE_REMOVED;
    }

    fdo->Flags |= DO_BUFFERED_IO;
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

VOID
NtusbUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    /* Per-FDO teardown happens in IRP_MN_REMOVE_DEVICE, same as
     * ntbridge_pnp.c - DriverUnload only runs after every device this
     * driver created is already gone. */
}

/* ---- Create/Close, Device Control (test-only), Power ------------------ */

NTSTATUS
NtusbDispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return CompleteIrp(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NtusbDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PNTUSB_EXTENSION ext = ExtensionOf(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

    if (ext->IsFdo || stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_NTUSB_TEST_BULK_TRANSFER)
        return CompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);

    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ntusb_test_bulk_io_t) ||
        stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ntusb_test_bulk_io_t)) {
        return CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
    }

    {
        /* METHOD_BUFFERED: SystemBuffer holds the request on entry, gets
         * overwritten with the response before completion — same buffer,
         * used both ways, exactly like a real URB's in-place Status/
         * TransferBufferLength update. */
        ntusb_test_bulk_io_t *io = (ntusb_test_bulk_io_t *)Irp->AssociatedIrp.SystemBuffer;
        UCHAR endpoint = io->endpoint;
        ULONG requestedLength = io->length;
        UCHAR localData[NTUSB_MAX_TRANSFER];
        struct _URB urb;

        if (requestedLength > NTUSB_MAX_TRANSFER)
            requestedLength = NTUSB_MAX_TRANSFER;
        RtlCopyMemory(localData, io->data, requestedLength);

        /* Build the exact URB a real client driver would submit for this
         * transfer, and hand it to the SAME dispatch function
         * IRP_MJ_INTERNAL_DEVICE_CONTROL uses below — this is what makes
         * this test-only path a genuine exercise of the real bridging
         * logic, not a separate reimplementation of it. */
        RtlZeroMemory(&urb, sizeof(urb));
        urb.UrbHeader.Length = sizeof(urb.UrbBulkOrInterruptTransfer);
        urb.UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
        urb.UrbBulkOrInterruptTransfer.PipeHandle = (USBD_PIPE_HANDLE)(ULONG_PTR)endpoint;
        urb.UrbBulkOrInterruptTransfer.TransferBufferLength = requestedLength;
        urb.UrbBulkOrInterruptTransfer.TransferBuffer = localData;
        ext->ConfigurationSelected = TRUE; /* test path skips SELECT_CONFIGURATION on purpose - see README */

        NtusbProcessUrb(ext, &urb);

        io->status = USBD_SUCCESS(urb.UrbHeader.Status) ? 0 : (ULONG)urb.UrbHeader.Status;
        io->length = urb.UrbBulkOrInterruptTransfer.TransferBufferLength;
        if (io->length > NTUSB_MAX_TRANSFER)
            io->length = NTUSB_MAX_TRANSFER;
        RtlCopyMemory(io->data, localData, io->length);

        return CompleteIrp(Irp, STATUS_SUCCESS, sizeof(*io));
    }
}

NTSTATUS
NtusbDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PNTUSB_EXTENSION ext = ExtensionOf(DeviceObject);

    if (!ext->IsFdo) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(ext->LowerDevice, Irp);
}

/* ---- The real client-driver-facing entry point ------------------------- */

NTSTATUS
NtusbDispatchInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PNTUSB_EXTENSION ext = ExtensionOf(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

    if (ext->IsFdo || stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_INTERNAL_USB_SUBMIT_URB) {
        return CompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);
    }

    {
        /* Real WDK convention for this internal, METHOD_NEITHER IOCTL:
         * the URB pointer travels in Argument1, not SystemBuffer. */
        PURB urb = (PURB)stack->Parameters.Others.Argument1;
        if (urb == NULL)
            return CompleteIrp(Irp, STATUS_INVALID_PARAMETER, 0);

        NtusbProcessUrb(ext, urb);
        return CompleteIrp(Irp, USBD_SUCCESS(urb->UrbHeader.Status) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL, 0);
    }
}

/* ---- URB dispatch: the code both the real and test-only paths share --- */

static VOID
NtusbBuildDeviceDescriptor(PUSB_DEVICE_DESCRIPTOR desc)
{
    RtlZeroMemory(desc, sizeof(*desc));
    desc->bLength = sizeof(*desc);
    desc->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
    desc->bcdUSB = 0x0200;
    desc->bDeviceClass = 0xFF; /* vendor-specific */
    desc->bMaxPacketSize0 = 64;
    desc->idVendor = (USHORT)NTUSB_USB_VENDOR_ID;
    desc->idProduct = (USHORT)NTUSB_USB_PRODUCT_ID;
    desc->bcdDevice = 0x0100;
    desc->bNumConfigurations = 1;
}

static NTSTATUS
NtusbProcessUrb(
    _In_ PNTUSB_EXTENSION PdoExt,
    _Inout_ PURB Urb
    )
{
    switch (Urb->UrbHeader.Function) {

    case URB_FUNCTION_SELECT_CONFIGURATION: {
        /* Local only - one configuration, one interface, two bulk
         * endpoints. Hands back a PipeHandle for each pipe the caller
         * described, encoded as the endpoint address itself (see the
         * ConfigurationSelected field's comment) - a real bus would
         * validate the caller's requested pipe count/types against the
         * config descriptor it already handed out via
         * GET_DESCRIPTOR_FROM_DEVICE; skipped here since there is
         * exactly one legal shape to validate against and getting it
         * wrong just means BULK_OR_INTERRUPT_TRANSFER rejects an
         * unrecognized PipeHandle later anyway. */
        PUSBD_INTERFACE_INFORMATION iface = &Urb->UrbSelectConfiguration.Interface;
        ULONG i;
        for (i = 0; i < iface->NumberOfPipes; i++) {
            UCHAR ep = (i == 0) ? (UCHAR)NTUSB_BULK_IN_ENDPOINT : (UCHAR)NTUSB_BULK_OUT_ENDPOINT;
            iface->Pipes[i].PipeType = UsbdPipeTypeBulk;
            iface->Pipes[i].EndpointAddress = ep;
            iface->Pipes[i].MaximumPacketSize = 64;
            iface->Pipes[i].PipeHandle = (USBD_PIPE_HANDLE)(ULONG_PTR)ep;
        }
        Urb->UrbSelectConfiguration.ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)(ULONG_PTR)1;
        PdoExt->ConfigurationSelected = TRUE;
        Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE: {
        /* Local only, same reasoning as SELECT_CONFIGURATION - only
         * DescriptorType == device is implemented (what any client needs
         * first); config/string descriptor bytes are real follow-up
         * work, not required by anything this pass actually tests (see
         * README - no real descriptor-fetching client driver exists to
         * test against). */
        struct _URB_CONTROL_DESCRIPTOR_REQUEST *req = &Urb->UrbControlDescriptorRequest;
        if (req->DescriptorType == USB_DEVICE_DESCRIPTOR_TYPE &&
            req->TransferBufferLength >= sizeof(USB_DEVICE_DESCRIPTOR) &&
            req->TransferBuffer != NULL) {
            NtusbBuildDeviceDescriptor((PUSB_DEVICE_DESCRIPTOR)req->TransferBuffer);
            req->TransferBufferLength = sizeof(USB_DEVICE_DESCRIPTOR);
            Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        } else {
            Urb->UrbHeader.Status = USBD_STATUS_BAD_DESCRIPTOR_TYPE;
        }
        return STATUS_SUCCESS;
    }

    case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER: {
        /* The one path this pass actually bridges to Linux, and the one
         * exercised live via IOCTL_NTUSB_TEST_BULK_TRANSFER (see
         * ntusb_test.c / README). */
        struct _URB_BULK_OR_INTERRUPT_TRANSFER *xfer = &Urb->UrbBulkOrInterruptTransfer;
        UCHAR endpoint = (UCHAR)(ULONG_PTR)xfer->PipeHandle;
        BOOLEAN isIn = (endpoint & USB_ENDPOINT_DIRECTION_MASK) != 0;

        if (!PdoExt->ConfigurationSelected) {
            Urb->UrbHeader.Status = USBD_STATUS_INTERFACE_NOT_FOUND;
            return STATUS_UNSUCCESSFUL;
        }
        if (endpoint != NTUSB_BULK_IN_ENDPOINT && endpoint != NTUSB_BULK_OUT_ENDPOINT) {
            Urb->UrbHeader.Status = USBD_STATUS_INVALID_PIPE_HANDLE;
            return STATUS_UNSUCCESSFUL;
        }

        if (!isIn) {
            /* OUT: push the payload to Linux via usb_req_ring and treat
             * it as complete immediately once queued - same "push and
             * return success synchronously" tradeoff NtnetSend made for
             * net_tx_ring in Phase 7; the far side's actual consumption
             * happens asynchronously. */
            ntbridge_usb_urb_msg_t msg;
            ULONG len = xfer->TransferBufferLength;
            if (len > NTBRIDGE_USB_DATA_MAX)
                len = NTBRIDGE_USB_DATA_MAX;

            RtlZeroMemory(&msg, sizeof(msg));
            msg.request_id = (uint64_t)InterlockedIncrement(&((PNTUSB_EXTENSION)PdoExt->ParentFdo->DeviceExtension)->NextRequestId);
            msg.function = NTBRIDGE_USB_FN_BULK_OR_INTERRUPT_TRANSFER;
            msg.endpoint = endpoint;
            msg.length = len;
            if (len > 0 && xfer->TransferBuffer != NULL)
                RtlCopyMemory(msg.data, xfer->TransferBuffer, len);

            {
                PNTUSB_EXTENSION fdoExt = ExtensionOf(PdoExt->ParentFdo);
                if (fdoExt->Shm == NULL || !ntbridge_usb_ring_try_push(&fdoExt->Shm->usb_req_ring, &msg)) {
                    Urb->UrbHeader.Status = USBD_STATUS_DEVICE_GONE;
                    return STATUS_UNSUCCESSFUL;
                }
            }
            xfer->TransferBufferLength = len;
            Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
            return STATUS_SUCCESS;
        } else {
            /* IN: try to pop whatever the host has already made
             * available on usb_resp_ring. Synchronous, non-blocking - if
             * nothing is queued yet, this completes as a real, honest
             * zero-length success rather than blocking the IRP (see this
             * file's header comment on why pending-IRP support is out of
             * scope for this pass). */
            ntbridge_usb_urb_msg_t msg;
            PNTUSB_EXTENSION fdoExt = ExtensionOf(PdoExt->ParentFdo);
            ULONG copyLen = 0;

            if (fdoExt->Shm != NULL && ntbridge_usb_ring_try_pop(&fdoExt->Shm->usb_resp_ring, &msg)) {
                copyLen = msg.length;
                if (copyLen > xfer->TransferBufferLength)
                    copyLen = xfer->TransferBufferLength;
                if (copyLen > 0 && xfer->TransferBuffer != NULL)
                    RtlCopyMemory(xfer->TransferBuffer, msg.data, copyLen);
            }
            xfer->TransferBufferLength = copyLen;
            Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
    }

    default:
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_URB_FUNCTION;
        return STATUS_NOT_SUPPORTED;
    }
}

/* ---- PnP -----------------------------------------------------------------
 *
 * FDO: attaches to the ivshmem PCI function, maps BAR2, creates the one
 * child PDO. Child PDO: minimal bus-child contract (same shape as
 * ntbridge_pnp.c's) plus IRP_MN_QUERY_ID (so a real client driver's INF
 * can match this PDO's hardware ID) and a minimal IRP_MN_QUERY_
 * CAPABILITIES.
 */

static NTSTATUS
NtusbLowerCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
NtusbStartFdo(
    _In_ PNTUSB_EXTENSION FdoExt,
    _In_ PIRP Irp
    )
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PCM_RESOURCE_LIST translated = stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    ULONG i;
    BOOLEAN found = FALSE;

    if (translated == NULL || translated->Count == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    {
        PCM_PARTIAL_RESOURCE_LIST list = &translated->List[0].PartialResourceList;
        ULONG memoryDescriptorsSeen = 0;

        for (i = 0; i < list->Count; i++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &list->PartialDescriptors[i];
            if (desc->Type == CmResourceTypeMemory) {
                memoryDescriptorsSeen++;
                if (memoryDescriptorsSeen == 2) {
                    FdoExt->ShmPhysicalBase = desc->u.Memory.Start;
                    FdoExt->ShmLength = desc->u.Memory.Length;
                    found = TRUE;
                    break;
                }
            }
        }
    }

    if (!found || FdoExt->ShmLength < NTBRIDGE_SHM_SIZE_MIN)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    FdoExt->Shm = (ntbridge_shm_header_t *)MmMapIoSpace(
        FdoExt->ShmPhysicalBase, FdoExt->ShmLength, MmNonCached);
    if (FdoExt->Shm == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (FdoExt->Shm->magic != NTBRIDGE_MAGIC || FdoExt->Shm->version != NTBRIDGE_PROTOCOL_VERSION) {
        MmUnmapIoSpace(FdoExt->Shm, FdoExt->ShmLength);
        FdoExt->Shm = NULL;
        return STATUS_REVISION_MISMATCH;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtusbDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PNTUSB_EXTENSION ext = ExtensionOf(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    if (!ext->IsFdo) {
        switch (stack->MinorFunction) {
        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            return CompleteIrp(Irp, STATUS_SUCCESS, 0);

        case IRP_MN_REMOVE_DEVICE:
            if (ext->SymlinkCreated) {
                UNICODE_STRING symlink;
                RtlInitUnicodeString(&symlink, NTUSB_SYMLINK_NAME);
                IoDeleteSymbolicLink(&symlink);
            }
            IoDeleteDevice(DeviceObject);
            return CompleteIrp(Irp, STATUS_SUCCESS, 0);

        case IRP_MN_QUERY_CAPABILITIES: {
            PDEVICE_CAPABILITIES caps = stack->Parameters.DeviceCapabilities.Capabilities;
            if (caps != NULL) {
                caps->DeviceD1 = FALSE;
                caps->DeviceD2 = FALSE;
                caps->LockSupported = FALSE;
                caps->EjectSupported = FALSE;
                caps->Removable = TRUE; /* synthetic, "removable" like any USB device */
                caps->SurpriseRemovalOK = TRUE;
                caps->Address = 0;
                caps->UINumber = 0;
            }
            return CompleteIrp(Irp, STATUS_SUCCESS, 0);
        }

        case IRP_MN_QUERY_ID: {
            /* Multi-sz (HardwareIDs/CompatibleIDs) or single string
             * (DeviceID/InstanceID), double-null-terminated where
             * multi-sz — the standard NT bus-driver contract for letting
             * a real vendor driver's INF match this PDO. Rule 1: reuse
             * that existing contract, don't invent a new matching
             * scheme. */
            static const WCHAR hardwareId[] = L"USB\\VID_1AF4&PID_1005&REV_0100\0USB\\VID_1AF4&PID_1005\0";
            static const WCHAR compatId[] = L"USB\\Class_FF\0";
            static const WCHAR deviceId[] = L"USB\\VID_1AF4&PID_1005";
            static const WCHAR instanceId[] = L"0";
            PCWSTR src;
            SIZE_T bytes;

            switch (stack->Parameters.QueryId.IdType) {
            case BusQueryDeviceID:      src = deviceId;   bytes = sizeof(deviceId); break;
            case BusQueryInstanceID:    src = instanceId; bytes = sizeof(instanceId); break;
            case BusQueryHardwareIDs:   src = hardwareId; bytes = sizeof(hardwareId); break;
            case BusQueryCompatibleIDs: src = compatId;   bytes = sizeof(compatId); break;
            default:
                return CompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);
            }

            {
                PWCHAR buf = (PWCHAR)ExAllocatePoolWithTag(PagedPool, bytes, NTUSB_POOL_TAG);
                if (buf == NULL)
                    return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
                RtlCopyMemory(buf, src, bytes);
                return CompleteIrp(Irp, STATUS_SUCCESS, (ULONG_PTR)buf);
            }
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        default:
            return CompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);
        }
    }

    /* FDO */
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE: {
        KEVENT event;
        KeInitializeEvent(&event, NotificationEvent, FALSE);
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, NtusbLowerCompletion, &event, TRUE, TRUE, TRUE);
        status = IoCallDriver(ext->LowerDevice, Irp);
        if (status == STATUS_PENDING) {
            KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
            status = Irp->IoStatus.Status;
        }
        /* Fixed here, unlike ntbridge_pnp.c's still-open bug (Phase 14):
         * wait for the lower stack's IRP_MN_START_DEVICE to actually
         * complete before touching PnP-translated resources below. */
        if (!NT_SUCCESS(status))
            return CompleteIrp(Irp, status, 0);

        status = NtusbStartFdo(ext, Irp);
        if (NT_SUCCESS(status) && ext->ChildPdo == NULL) {
            /* PASSIVE_LEVEL here (IRP_MN_START_DEVICE always is), so
             * unlike ntbridge_pnp.c's DPC-time CreateChildPdo (flagged
             * there as unsafe - IoCreateDevice from DISPATCH_LEVEL),
             * creating the one child PDO synchronously right here is
             * safe - no deferred work item needed for this driver's
             * simpler, static (one device, never hot-plugged) shape. */
            NTSTATUS pdoStatus = NtusbCreateChildPdo(DeviceObject->DriverObject, ext);
            if (NT_SUCCESS(pdoStatus))
                IoInvalidateDeviceRelations(ext->PhysicalDeviceObject, BusRelations);
            else
                DbgPrint("ntusb: child PDO creation failed: 0x%08lx (FDO still started)\n", (unsigned long)pdoStatus);
        }
        return CompleteIrp(Irp, status, 0);
    }

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type == BusRelations) {
            PDEVICE_RELATIONS relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
                PagedPool, FIELD_OFFSET(DEVICE_RELATIONS, Objects[1]), NTUSB_POOL_TAG);
            if (relations == NULL)
                return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            relations->Count = 0;
            if (ext->ChildPdo != NULL) {
                ObReferenceObject(ext->ChildPdo);
                relations->Objects[relations->Count++] = ext->ChildPdo;
            }
            Irp->IoStatus.Information = (ULONG_PTR)relations;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCopyCurrentIrpStackLocationToNext(Irp);
            return IoCallDriver(ext->LowerDevice, Irp);
        }
        break;

    case IRP_MN_STOP_DEVICE:
        if (ext->Shm != NULL) {
            MmUnmapIoSpace(ext->Shm, ext->ShmLength);
            ext->Shm = NULL;
        }
        break;

    case IRP_MN_REMOVE_DEVICE:
        if (ext->Shm != NULL) {
            MmUnmapIoSpace(ext->Shm, ext->ShmLength);
            ext->Shm = NULL;
        }
        IoSkipCurrentIrpStackLocation(Irp);
        status = IoCallDriver(ext->LowerDevice, Irp);
        IoDetachDevice(ext->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return status;

    default:
        break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(ext->LowerDevice, Irp);
}

static NTSTATUS
NtusbCreateChildPdo(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PNTUSB_EXTENSION FdoExt
    )
{
    NTSTATUS status;
    PDEVICE_OBJECT pdo;
    PNTUSB_EXTENSION pdoExt;
    UNICODE_STRING devname, symlink;

    RtlInitUnicodeString(&devname, NTUSB_DEVICE_NAME);
    status = IoCreateDevice(
        DriverObject,
        sizeof(NTUSB_EXTENSION),
        &devname,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &pdo);
    if (!NT_SUCCESS(status))
        return status;

    pdoExt = ExtensionOf(pdo);
    RtlZeroMemory(pdoExt, sizeof(*pdoExt));
    pdoExt->IsFdo = FALSE;
    pdoExt->Self = pdo;
    pdoExt->ParentFdo = FdoExt->Self;

    RtlInitUnicodeString(&symlink, NTUSB_SYMLINK_NAME);
    if (NT_SUCCESS(IoCreateSymbolicLink(&symlink, &devname)))
        pdoExt->SymlinkCreated = TRUE;

    pdo->Flags |= DO_BUS_ENUMERATED_DEVICE | DO_BUFFERED_IO;
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    FdoExt->ChildPdo = pdo;
    DbgPrint("ntusb: child PDO created, open test path via \\\\.\\NTLUSB0\n");
    return STATUS_SUCCESS;
}
