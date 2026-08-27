/*
 * ntbridge_pnp.h — device extension and prototypes for the ReactOS-side
 * (cell) half of ntbridge: a WDM bus driver that maps the ivshmem PCI
 * BAR2 shared-memory region and turns ntbridge_pnp_descriptor_t
 * arrivals into real NT PDOs under this driver's FDO
 * (ARCHITECTURE.md sections 17-19).
 *
 * NOT YET BUILT OR RUN — see this directory's README.md and
 * ROADMAP.md Phase 4's "known gap" for exactly why (no RosBE
 * cross-toolchain reachable in the sandbox that wrote this). Written
 * against real ReactOS/WDK conventions (DRIVER_OBJECT, DEVICE_OBJECT,
 * IRP dispatch, MmMapIoSpace, KeInitializeTimer/Dpc) so it is ready to
 * be dropped into a ReactOS source tree and wired into its build
 * (drivers/ntlinux/ntbridge/ + a CMakeLists.txt entry, or the
 * equivalent .dff line on the branches that still use the old build
 * system) once that toolchain is available — not a design sketch that
 * still needs translating into real driver-model calls.
 *
 * Deliberately #include's the exact same driver/ntbridge/protocol/
 * ntbridge_protocol.h the Linux host side (driver/ntbridge/host/) and
 * the stand-in guest test client (tests/reactos/) use — one wire
 * format, defined once, consumed by all three (the same pattern
 * ntabi_protocol.h uses for libntabi/ntd).
 */

#ifndef NTBRIDGE_PNP_H
#define NTBRIDGE_PNP_H

#include <ntddk.h>
#include "ntbridge_protocol.h"

#define NTBRIDGE_POOL_TAG 'grbN' /* "Nbrg" reversed, per NT tagging convention */
#define NTBRIDGE_MAX_CHILD_PDOS 64

/* One entry per synthetic device this FDO has created a child PDO for,
 * so IRP_MN_QUERY_DEVICE_RELATIONS(BusRelations) can hand back a stable
 * list and IRP_MN_REMOVE_DEVICE on a child can find its way back here.
 */
typedef struct _NTBRIDGE_CHILD_ENTRY {
    BOOLEAN InUse;
    UINT64 DeviceId;
    PDEVICE_OBJECT Pdo;
    ntbridge_pnp_descriptor_t Descriptor;
} NTBRIDGE_CHILD_ENTRY, *PNTBRIDGE_CHILD_ENTRY;

typedef struct _NTBRIDGE_FDO_EXTENSION {
    BOOLEAN IsFdo; /* TRUE for the bus FDO itself, FALSE for a child PDO's extension */

    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;     /* FDO only: result of IoAttachDeviceToDeviceStack */
    PDEVICE_OBJECT PhysicalDeviceObject;

    /* FDO only: the mapped ivshmem BAR2 region. */
    PHYSICAL_ADDRESS ShmPhysicalBase;
    ULONG ShmLength;
    ntbridge_shm_header_t *Shm; /* MmMapIoSpace() result */

    /* FDO only: poll timer standing in for the doorbell interrupt the
     * ivshmem-plain transport doesn't provide (see ntbridge_protocol.h's
     * header comment on the doorbell upgrade path). */
    KTIMER PollTimer;
    KDPC PollDpc;
    BOOLEAN PollTimerStarted;

    /* FDO only: child PDO table for the synthetic bus this driver
     * exposes — one child per ntbridge_pnp_descriptor_t the host side
     * has reported ARRIVED and this driver hasn't seen REMOVED for. */
    NTBRIDGE_CHILD_ENTRY Children[NTBRIDGE_MAX_CHILD_PDOS];
    KSPIN_LOCK ChildrenLock;

    /* Child PDO only: which entry in the parent FDO's Children[] this
     * PDO corresponds to, so IRP_MJ_PNP can find its own descriptor. */
    PDEVICE_OBJECT ParentFdo;
    ULONG ChildIndex;
} NTBRIDGE_FDO_EXTENSION, *PNTBRIDGE_FDO_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

DRIVER_ADD_DEVICE NtBridgeAddDevice;
DRIVER_DISPATCH NtBridgeDispatchPnp;
DRIVER_DISPATCH NtBridgeDispatchPower;
DRIVER_DISPATCH NtBridgeDispatchCreateClose;
DRIVER_UNLOAD NtBridgeUnload;

/* IRP_MN_START_DEVICE for the FDO: parse the translated CM_RESOURCE_LIST
 * for the ivshmem BAR2 memory range, MmMapIoSpace it, verify
 * ntbridge_shm_header_t's magic/version (Rule 12 — refuse a mismatched
 * region exactly like the host and stand-in guest test client do). */
NTSTATUS NtBridgeStartFdo(PNTBRIDGE_FDO_EXTENSION FdoExt, PIRP Irp);

/* IRP_MN_QUERY_DEVICE_RELATIONS(BusRelations): return the current
 * Children[] as a DEVICE_RELATIONS list, referencing each PDO per the
 * IRP contract. */
NTSTATUS NtBridgeQueryBusRelations(PNTBRIDGE_FDO_EXTENSION FdoExt, PIRP Irp);

/* Poll DPC: bumps guest_heartbeat_seq/time, drains pnp_ring into
 * Children[] (creating child PDOs + IoInvalidateDeviceRelations on
 * arrival, marking InUse=FALSE + invalidating relations on removal),
 * pushes accumulated DbgPrint-worthy events onto log_ring for the host
 * side to see. Scheduled by KeSetTimerEx against PollTimer at FDO
 * start; a real interrupt/doorbell would replace this polling with an
 * ISR + DPC pair instead (see the doorbell note in
 * ntbridge_protocol.h) — left as a documented follow-up, not silently
 * assumed away.
 */
VOID NtBridgePollDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);

#endif /* NTBRIDGE_PNP_H */
