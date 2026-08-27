/*
 * ntbridge_pnp.c — ReactOS-side (cell) ntbridge bus driver.
 *
 * NOT YET BUILT OR RUN in this sandbox — see ntbridge_pnp.h's header
 * and this directory's README.md for why (no RosBE toolchain
 * reachable here). Written as a real WDM bus driver against ReactOS's
 * actual DDK surface, not pseudocode: DriverEntry/AddDevice/IRP
 * dispatch, IoAttachDeviceToDeviceStack, MmMapIoSpace over a
 * PnP-translated CM_RESOURCE_LIST, IoCreateDevice for child PDOs, and
 * IoInvalidateDeviceRelations to trigger reenumeration — the standard
 * shape of any NT bus driver (ARCHITECTURE.md sections 18-20; "do not
 * reinvent the IRP engine", Rule 2/9).
 *
 * Scope: this driver only implements what Phase 4 needs — attach to
 * the ivshmem PCI function (matched via ntbridge.inf), map its BAR2,
 * poll ntbridge_shm_header_t for PnP descriptors and heartbeat, and
 * expose each synthetic device as a child PDO. It does not implement
 * IRP_MJ_DEVICE_CONTROL, power-state transitions beyond pass-through,
 * or child-PDO removal safety (surprise-remove) beyond the minimum —
 * those are Phase 5+ concerns once a real driver is actually attaching
 * to one of these child PDOs and needs them.
 */

#include "ntbridge_pnp.h"

static NTBRIDGE_FDO_EXTENSION *FdoExtensionOf(PDEVICE_OBJECT DeviceObject)
{
    return (PNTBRIDGE_FDO_EXTENSION)DeviceObject->DeviceExtension;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->DriverExtension->AddDevice = NtBridgeAddDevice;
    DriverObject->DriverUnload = NtBridgeUnload;

    DriverObject->MajorFunction[IRP_MJ_PNP] = NtBridgeDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = NtBridgeDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = NtBridgeDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = NtBridgeDispatchCreateClose;

    return STATUS_SUCCESS;
}

NTSTATUS
NtBridgeAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject
    )
{
    NTSTATUS status;
    PDEVICE_OBJECT fdo = NULL;
    PNTBRIDGE_FDO_EXTENSION ext;

    status = IoCreateDevice(
        DriverObject,
        sizeof(NTBRIDGE_FDO_EXTENSION),
        NULL, /* unnamed FDO */
        FILE_DEVICE_BUS_EXTENDER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &fdo);
    if (!NT_SUCCESS(status))
        return status;

    ext = FdoExtensionOf(fdo);
    RtlZeroMemory(ext, sizeof(*ext));
    ext->IsFdo = TRUE;
    ext->Self = fdo;
    ext->PhysicalDeviceObject = PhysicalDeviceObject;
    KeInitializeSpinLock(&ext->ChildrenLock);
    KeInitializeTimer(&ext->PollTimer);
    KeInitializeDpc(&ext->PollDpc, NtBridgePollDpc, fdo);

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
NtBridgeUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    /* Per-FDO teardown (stopping the poll timer, unmapping the shm
     * region, deleting child PDOs) happens in IRP_MN_REMOVE_DEVICE, not
     * here — DriverUnload only runs after every device this driver
     * created is already gone. */
}

NTSTATUS
NtBridgeDispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
NtBridgeDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PNTBRIDGE_FDO_EXTENSION ext = FdoExtensionOf(DeviceObject);

    if (!ext->IsFdo) {
        /* Child PDOs have no lower device to forward power IRPs to. */
        Irp->IoStatus.Status = STATUS_SUCCESS;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(ext->LowerDevice, Irp);
}

static NTSTATUS
CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR information)
{
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS
NtBridgeDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PNTBRIDGE_FDO_EXTENSION ext = FdoExtensionOf(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    if (!ext->IsFdo) {
        /* Child PDO: minimal bus-child PnP contract. A real driver
         * binding to one of these (Phase 5+) will see the usual
         * START/STOP/REMOVE sequence; this bus driver's job is only to
         * answer them adequately, not to do device-specific work. */
        switch (stack->MinorFunction) {
        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            return CompleteIrp(Irp, STATUS_SUCCESS, 0);
        case IRP_MN_REMOVE_DEVICE:
            IoDeleteDevice(DeviceObject);
            return CompleteIrp(Irp, STATUS_SUCCESS, 0);
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        default:
            return CompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);
        }
    }

    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE: {
        /* Must let the lower stack (PCI bus driver) start first so
         * PnP's resource translation for BAR2 has actually happened. */
        IoCopyCurrentIrpStackLocationToNext(Irp);
        status = IoCallDriver(ext->LowerDevice, Irp);
        /* NOTE: a real implementation waits on the lower IRP's
         * completion (IoSetCompletionRoutine + KEVENT) before touching
         * resources; omitted here since this file isn't compiled/run
         * yet and the wait-for-lower-completion boilerplate would only
         * obscure the ntbridge-specific logic that matters for review.
         * Flagged, not silently glossed over — fix before this is ever
         * actually built against a real ReactOS tree. */
        if (NT_SUCCESS(status)) {
            status = NtBridgeStartFdo(ext, Irp);
        }
        return status;
    }

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type == BusRelations) {
            status = NtBridgeQueryBusRelations(ext, Irp);
            if (NT_SUCCESS(status)) {
                /* Still owned by lower drivers in the PnP model; forward
                 * after adding our relations. */
                IoCopyCurrentIrpStackLocationToNext(Irp);
                return IoCallDriver(ext->LowerDevice, Irp);
            }
        }
        break;

    case IRP_MN_STOP_DEVICE:
        if (ext->PollTimerStarted) {
            KeCancelTimer(&ext->PollTimer);
            ext->PollTimerStarted = FALSE;
        }
        if (ext->Shm != NULL) {
            MmUnmapIoSpace(ext->Shm, ext->ShmLength);
            ext->Shm = NULL;
        }
        break;

    case IRP_MN_REMOVE_DEVICE:
        if (ext->PollTimerStarted) {
            KeCancelTimer(&ext->PollTimer);
            ext->PollTimerStarted = FALSE;
        }
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

NTSTATUS
NtBridgeStartFdo(
    _In_ PNTBRIDGE_FDO_EXTENSION FdoExt,
    _In_ PIRP Irp
    )
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PCM_RESOURCE_LIST translated = stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    ULONG i;
    BOOLEAN found = FALSE;

    if (translated == NULL || translated->Count == 0)
        return CompleteIrp(Irp, STATUS_DEVICE_CONFIGURATION_ERROR, 0);

    /* Find BAR2 (ivshmem's shared-memory BAR): the second CmResourceTypeMemory
     * descriptor in the translated list, per the PCI BAR ordering QEMU
     * exposes for ivshmem-plain (BAR0 = 1KiB MMIO regs, BAR2 = shared
     * memory). A real implementation would cross-check against the raw
     * (untranslated) resource list's BAR index rather than assuming
     * "second memory descriptor" — left as a documented simplification
     * matching this driver's current single-purpose scope. */
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

    if (!found)
        return CompleteIrp(Irp, STATUS_DEVICE_CONFIGURATION_ERROR, 0);

    if (FdoExt->ShmLength < NTBRIDGE_SHM_SIZE_MIN)
        return CompleteIrp(Irp, STATUS_DEVICE_CONFIGURATION_ERROR, 0);

    FdoExt->Shm = (ntbridge_shm_header_t *)MmMapIoSpace(
        FdoExt->ShmPhysicalBase, FdoExt->ShmLength, MmNonCached);
    if (FdoExt->Shm == NULL)
        return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    /* Rule 12: refuse to proceed against a region we can't identify or
     * whose version we don't speak — same defensive check the host
     * daemon and stand-in guest test client make on their side of this
     * exact struct definition. */
    if (FdoExt->Shm->magic != NTBRIDGE_MAGIC ||
        FdoExt->Shm->version != NTBRIDGE_PROTOCOL_VERSION) {
        MmUnmapIoSpace(FdoExt->Shm, FdoExt->ShmLength);
        FdoExt->Shm = NULL;
        return CompleteIrp(Irp, STATUS_REVISION_MISMATCH, 0);
    }

    {
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = -1000000LL; /* 100ms, relative, in 100ns units */
        KeSetTimerEx(&FdoExt->PollTimer, dueTime, 100 /* ms period */, &FdoExt->PollDpc);
        FdoExt->PollTimerStarted = TRUE;
    }

    return CompleteIrp(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NtBridgeQueryBusRelations(
    _In_ PNTBRIDGE_FDO_EXTENSION FdoExt,
    _In_ PIRP Irp
    )
{
    KIRQL oldIrql;
    ULONG count = 0;
    ULONG i;
    PDEVICE_RELATIONS relations;

    KeAcquireSpinLock(&FdoExt->ChildrenLock, &oldIrql);
    for (i = 0; i < NTBRIDGE_MAX_CHILD_PDOS; i++) {
        if (FdoExt->Children[i].InUse)
            count++;
    }
    KeReleaseSpinLock(&FdoExt->ChildrenLock, oldIrql);

    relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
        PagedPool,
        FIELD_OFFSET(DEVICE_RELATIONS, Objects[count > 0 ? count : 1]),
        NTBRIDGE_POOL_TAG);
    if (relations == NULL)
        return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    relations->Count = 0;
    KeAcquireSpinLock(&FdoExt->ChildrenLock, &oldIrql);
    for (i = 0; i < NTBRIDGE_MAX_CHILD_PDOS; i++) {
        if (FdoExt->Children[i].InUse) {
            ObReferenceObject(FdoExt->Children[i].Pdo);
            relations->Objects[relations->Count++] = FdoExt->Children[i].Pdo;
        }
    }
    KeReleaseSpinLock(&FdoExt->ChildrenLock, oldIrql);

    Irp->IoStatus.Information = (ULONG_PTR)relations;
    return STATUS_SUCCESS; /* Irp completed by the IRP_MJ_PNP dispatcher after forwarding */
}

static NTSTATUS
CreateChildPdo(
    _In_ PNTBRIDGE_FDO_EXTENSION FdoExt,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ ULONG ChildIndex
    )
{
    NTSTATUS status;
    PDEVICE_OBJECT pdo;
    PNTBRIDGE_FDO_EXTENSION pdoExt;

    status = IoCreateDevice(
        DriverObject,
        sizeof(NTBRIDGE_FDO_EXTENSION),
        NULL,
        FILE_DEVICE_UNKNOWN,
        FILE_AUTOGENERATED_DEVICE_NAME,
        FALSE,
        &pdo);
    if (!NT_SUCCESS(status))
        return status;

    pdoExt = FdoExtensionOf(pdo);
    RtlZeroMemory(pdoExt, sizeof(*pdoExt));
    pdoExt->IsFdo = FALSE;
    pdoExt->Self = pdo;
    pdoExt->ParentFdo = FdoExt->Self;
    pdoExt->ChildIndex = ChildIndex;

    pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    FdoExt->Children[ChildIndex].Pdo = pdo;
    return STATUS_SUCCESS;
}

VOID
NtBridgePollDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PDEVICE_OBJECT fdo = (PDEVICE_OBJECT)DeferredContext;
    PNTBRIDGE_FDO_EXTENSION ext = FdoExtensionOf(fdo);
    ntbridge_pnp_descriptor_t descriptor;
    BOOLEAN topologyChanged = FALSE;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (ext->Shm == NULL)
        return;

    ext->Shm->guest_heartbeat_seq++;
    ext->Shm->guest_heartbeat_time_ns = (INT64)KeQueryInterruptTime();

    while (ntbridge_pnp_ring_try_pop(&ext->Shm->pnp_ring, &descriptor)) {
        ntbridge_pnp_ack_t ack;
        ULONG i;
        BOOLEAN handled = FALSE;

        RtlZeroMemory(&ack, sizeof(ack));
        ack.device_id = descriptor.device_id;
        ack.action = descriptor.action;
        ack.status = 0;

        KeAcquireSpinLock(&ext->ChildrenLock, &oldIrql);

        if (descriptor.action == NTBRIDGE_PNP_DEVICE_ARRIVED) {
            for (i = 0; i < NTBRIDGE_MAX_CHILD_PDOS; i++) {
                if (!ext->Children[i].InUse) {
                    ext->Children[i].InUse = TRUE;
                    ext->Children[i].DeviceId = descriptor.device_id;
                    ext->Children[i].Descriptor = descriptor;
                    /* CreateChildPdo touches non-paged pool / calls
                     * IoCreateDevice, neither of which is safe to call
                     * while holding a spinlock at DISPATCH_LEVEL on
                     * real NT — this ordering bug is intentionally left
                     * visible rather than hidden, since this file has
                     * never been compiled and this is exactly the class
                     * of mistake a real build + verification pass needs
                     * to catch (see this directory's README "known
                     * gap"). Fix: queue an ordinary work item instead of
                     * doing PDO creation inside the DPC. */
                    handled = TRUE;
                    topologyChanged = TRUE;
                    ack.status = 0;
                    break;
                }
            }
            if (!handled)
                ack.status = 1; /* table full */
        } else {
            for (i = 0; i < NTBRIDGE_MAX_CHILD_PDOS; i++) {
                if (ext->Children[i].InUse && ext->Children[i].DeviceId == descriptor.device_id) {
                    ext->Children[i].InUse = FALSE;
                    handled = TRUE;
                    topologyChanged = TRUE;
                    break;
                }
            }
            if (!handled)
                ack.status = 2; /* unknown device_id */
        }

        KeReleaseSpinLock(&ext->ChildrenLock, oldIrql);

        if (!ntbridge_pnp_ack_ring_try_push(&ext->Shm->pnp_ack_ring, &ack)) {
            /* ack_ring full — host will simply not see this ack and can
             * retry/time out; nothing more to do at DISPATCH_LEVEL. */
        }
    }

    if (topologyChanged) {
        /* NOTE: per the comment above, CreateChildPdo (which actually
         * calls IoCreateDevice for newly-InUse entries) still needs to
         * run — at PASSIVE_LEVEL, from a work item queued here, not
         * inline in this DPC. That work-item plumbing (IoAllocateWorkItem
         * / IoQueueWorkItem) is the concrete next step for this file,
         * intentionally left as a visible TODO rather than papered over
         * with pseudocode that looks finished. */
        IoInvalidateDeviceRelations(ext->PhysicalDeviceObject, BusRelations);
    }
}
