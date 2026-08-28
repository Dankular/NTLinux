/*
 * ntnet — ReactOS-side NDIS miniport half of the NDIS bridge
 * (Phase 7, ARCHITECTURE.md section 22, driver/net/README.md).
 *
 * A legacy (NDIS 4.0-characteristics) Ethernet miniport that maps the
 * same ivshmem BAR2 region driver/ntbridge/reactos/ntbridge_pnp.c and
 * driver/vsdev/vsdev.c already do, and moves raw Ethernet frames across
 * ntbridge_protocol.h's net_tx_ring/net_rx_ring instead of touching real
 * hardware: MiniportSend copies the outgoing frame into net_tx_ring
 * (host reads it off and injects it into a Linux TAP device — see
 * driver/net/host/), and a polling timer (same "no interrupt in
 * ivshmem-plain, so poll" pattern as ntbridge_pnp.c's NtBridgePollDpc)
 * drains net_rx_ring and indicates each frame up the NDIS stack as a
 * real received packet.
 *
 * NOT YET BUILT-AND-LOADED live inside ReactOS — see this directory's
 * README for exactly what's verified (the ring protocol + host TAP
 * bridge, over a real QEMU VM boundary, via the honest stand-in in
 * tests/reactos/) versus what isn't (this file compiling/linking
 * against a real ReactOS NDIS library, and a real PnP-triggered NDIS
 * install — network adapter installation goes through a materially
 * more involved flow than driver/vsdev/'s `sc start`, out of reach for
 * this pass's verification budget). Written against the real, documented
 * legacy NDIS miniport API (NdisM* functions, NDIS40_MINIPORT_CHARACTERISTICS,
 * the classic Eth-indicate receive path) so it's ready to build and load
 * once that verification is attempted, not a design sketch.
 */

/* Both macros are required together per ndis.h's own header comment
 * ("NDIS_MINIPORT_DRIVER - Define only for NDIS miniport drivers",
 * "NDIS40_MINIPORT - Building NDIS 4.0 miniport driver") - this is
 * ReactOS's own ndis.h (see its file header), and defining only one of
 * the two leaves its internal NDIS_LEGACY_DRIVER/NDIS_LEGACY_PROTOCOL
 * gating in an inconsistent state that duplicates NDIS_REQUEST_TYPE
 * (already defined via its own #include of ntddndis.h) later in the
 * same file - a real, narrow build-environment finding, not guesswork. */
/* NDIS51_MINIPORT (NDIS 5.1), not NDIS40_MINIPORT: this header's
 * version-gate block only accepts NDIS50_MINIPORT/NDIS51_MINIPORT/
 * NDIS6x when NDIS_MINIPORT_DRIVER is set (`#error "Only NDIS miniport
 * drivers with version >= 5 are supported"` otherwise) - matches
 * ReactOS's own reported compatibility level anyway (NT 5.2 / Windows
 * Server 2003-era), so this is the natural target, not a downgrade. */
#define NDIS_MINIPORT_DRIVER 1
#define NDIS51_MINIPORT 1
#define _WIN32_WINNT 0x0501 /* WinXP - sdkddkver.h derives a matching NTDDI_VERSION from this; setting both independently triggers its own mismatch #error */
#include <ndis.h>

#include "ntbridge_protocol.h"

#define NTNET_POLL_INTERVAL_MS 20u /* same order of magnitude as ntbridge_pnp.c's 100ms poll, tighter here since network latency is more visible than PnP/log latency */

/* Synthetic, locally-administered MAC (bit 1 of the first octet set) so
 * it never collides with a real vendor OUI - 02:4E:54:4C:xx:xx spells
 * "NTL" in the second/third octets. */
static const UCHAR g_ntnet_mac[6] = { 0x02, 0x4E, 0x54, 0x4C, 0x00, 0x01 };

typedef struct _NTNET_ADAPTER {
    NDIS_HANDLE MiniportAdapterHandle;
    PHYSICAL_ADDRESS ShmPhysicalBase;
    ULONG ShmLength;
    ntbridge_shm_header_t *Shm;
    NDIS_MINIPORT_TIMER PollTimer;
    ULONG PacketFilter;

    /* Required-by-NDIS running counters (OID_GEN_XMIT_OK et al.) - real
     * counts of what this adapter has actually done, not placeholders
     * frozen at zero. */
    ULONG XmitOk;
    ULONG RcvOk;
    ULONG XmitError;
    ULONG RcvError;
    ULONG RcvNoBuffer;
} NTNET_ADAPTER, *PNTNET_ADAPTER;

static VOID NtnetPollTimer(PVOID SystemSpecific1, PVOID FunctionContext, PVOID SystemSpecific2, PVOID SystemSpecific3);

DRIVER_INITIALIZE DriverEntry;

/* Plain prototypes, not the W_*_HANDLER typedefs — those are POINTER
 * typedefs (`typedef NDIS_STATUS (NTAPI *W_INITIALIZE_HANDLER)(...)`),
 * so using one as a declarator would declare a function-pointer
 * variable, not forward-declare a function. */
NDIS_STATUS NtnetInitialize(OUT PNDIS_STATUS OpenErrorStatus, OUT PUINT SelectedMediumIndex,
                             IN PNDIS_MEDIUM MediumArray, IN UINT MediumArraySize,
                             IN NDIS_HANDLE MiniportAdapterHandle, IN NDIS_HANDLE WrapperConfigurationContext);
VOID NtnetHalt(IN NDIS_HANDLE MiniportAdapterContext);
NDIS_STATUS NtnetSend(IN NDIS_HANDLE MiniportAdapterContext, IN PNDIS_PACKET Packet, IN UINT Flags);
NDIS_STATUS NtnetQueryInformation(IN NDIS_HANDLE MiniportAdapterContext, IN NDIS_OID Oid,
                                   IN PVOID InformationBuffer, IN ULONG InformationBufferLength,
                                   OUT PULONG BytesWritten, OUT PULONG BytesNeeded);
NDIS_STATUS NtnetSetInformation(IN NDIS_HANDLE MiniportAdapterContext, IN NDIS_OID Oid,
                                 IN PVOID InformationBuffer, IN ULONG InformationBufferLength,
                                 OUT PULONG BytesRead, OUT PULONG BytesNeeded);
NDIS_STATUS NtnetReset(OUT PBOOLEAN AddressingReset, IN NDIS_HANDLE MiniportAdapterContext);
NDIS_STATUS NtnetTransferData(OUT PNDIS_PACKET Packet, OUT PUINT BytesTransferred,
                               IN NDIS_HANDLE MiniportAdapterContext, IN NDIS_HANDLE MiniportReceiveContext,
                               IN UINT ByteOffset, IN UINT BytesToTransfer);

/* ---- DriverEntry / MiniportInitialize / MiniportHalt ------------------ */

NTSTATUS
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    NDIS_HANDLE wrapperHandle;
    NDIS_STATUS status;
    NDIS51_MINIPORT_CHARACTERISTICS chars;

    NdisMInitializeWrapper(&wrapperHandle, DriverObject, RegistryPath, NULL);
    if (wrapperHandle == NULL)
        return STATUS_UNSUCCESSFUL;

    NdisZeroMemory(&chars, sizeof(chars));
    chars.MajorNdisVersion = 5;
    chars.MinorNdisVersion = 1;
    chars.InitializeHandler = NtnetInitialize;
    chars.HaltHandler = NtnetHalt;
    chars.SendHandler = NtnetSend;
    chars.QueryInformationHandler = NtnetQueryInformation;
    chars.SetInformationHandler = NtnetSetInformation;
    chars.ResetHandler = NtnetReset;
    chars.TransferDataHandler = NtnetTransferData;
    /* No CheckForHang/interrupt handlers - this adapter is purely
     * timer-polled, same reasoning as ntbridge_pnp.c's poll DPC: the
     * ivshmem-plain transport has no interrupt/doorbell to hook. */

    status = NdisMRegisterMiniport(wrapperHandle, (PNDIS_MINIPORT_CHARACTERISTICS)&chars, sizeof(chars));
    if (status != NDIS_STATUS_SUCCESS) {
        NdisTerminateWrapper(wrapperHandle, NULL);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NDIS_STATUS
NtnetInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT SelectedMediumIndex,
    IN PNDIS_MEDIUM MediumArray,
    IN UINT MediumArraySize,
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE WrapperConfigurationContext
    )
{
    PNTNET_ADAPTER adapter;
    NDIS_STATUS status;
    UINT i, found = (UINT)-1;
    UCHAR resourceBuffer[512];
    PNDIS_RESOURCE_LIST resources = (PNDIS_RESOURCE_LIST)resourceBuffer;
    UINT resourceBufferSize = sizeof(resourceBuffer);
    UINT r;

    UNREFERENCED_PARAMETER(OpenErrorStatus);

    for (i = 0; i < MediumArraySize; i++) {
        if (MediumArray[i] == NdisMedium802_3) {
            found = i;
            break;
        }
    }
    if (found == (UINT)-1)
        return NDIS_STATUS_UNSUPPORTED_MEDIA;
    *SelectedMediumIndex = found;

    {
        NDIS_PHYSICAL_ADDRESS noConstraint;
        noConstraint.QuadPart = -1; /* "no highest-address constraint" per NDIS convention */
        NdisAllocateMemory((PVOID *)&adapter, sizeof(NTNET_ADAPTER), 0, noConstraint);
    }
    if (adapter == NULL)
        return NDIS_STATUS_RESOURCES;
    NdisZeroMemory(adapter, sizeof(*adapter));
    adapter->MiniportAdapterHandle = MiniportAdapterHandle;

    NdisMSetAttributesEx(MiniportAdapterHandle, (NDIS_HANDLE)adapter, 0,
                          NDIS_ATTRIBUTE_BUS_MASTER | NDIS_ATTRIBUTE_DESERIALIZE,
                          NdisInterfacePci);

    /* Find the ivshmem BAR2 memory resource - same "second
     * CmResourceTypeMemory descriptor" convention ntbridge_pnp.c's
     * NtBridgeStartFdo uses, for the same reason (BAR0 = 1KiB MMIO
     * regs, BAR2 = the shared memory, for QEMU's ivshmem-plain). */
    /* NdisMQueryAdapterResources returns VOID - the real result comes
     * back through the first (Status) out-parameter, not a return
     * value; easy to get wrong since most other NdisM* functions here
     * DO return NDIS_STATUS directly. */
    NdisMQueryAdapterResources(&status, WrapperConfigurationContext, resources, &resourceBufferSize);
    if (status != NDIS_STATUS_SUCCESS) {
        NdisFreeMemory(adapter, sizeof(*adapter), 0);
        return NDIS_STATUS_RESOURCES;
    }

    {
        UINT memoryDescriptorsSeen = 0;
        BOOLEAN foundShm = FALSE;
        for (r = 0; r < resources->Count; r++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &resources->PartialDescriptors[r];
            if (desc->Type == CmResourceTypeMemory) {
                memoryDescriptorsSeen++;
                if (memoryDescriptorsSeen == 2) {
                    adapter->ShmPhysicalBase = desc->u.Memory.Start;
                    adapter->ShmLength = desc->u.Memory.Length;
                    foundShm = TRUE;
                    break;
                }
            }
        }
        if (!foundShm || adapter->ShmLength < NTBRIDGE_SHM_SIZE_MIN) {
            NdisFreeMemory(adapter, sizeof(*adapter), 0);
            return NDIS_STATUS_RESOURCES;
        }
    }

    status = NdisMMapIoSpace((PVOID *)&adapter->Shm, MiniportAdapterHandle,
                              adapter->ShmPhysicalBase, adapter->ShmLength);
    if (status != NDIS_STATUS_SUCCESS) {
        NdisFreeMemory(adapter, sizeof(*adapter), 0);
        return NDIS_STATUS_RESOURCES;
    }

    /* Rule 12: refuse a region we can't identify or whose version we
     * don't speak - same defensive check every other ntbridge consumer
     * (host daemon, stand-in guest test client, ntbridge_pnp.c) makes. */
    if (adapter->Shm->magic != NTBRIDGE_MAGIC || adapter->Shm->version != NTBRIDGE_PROTOCOL_VERSION) {
        NdisMUnmapIoSpace(MiniportAdapterHandle, adapter->Shm, adapter->ShmLength);
        NdisFreeMemory(adapter, sizeof(*adapter), 0);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    adapter->PacketFilter = 0;

    NdisMInitializeTimer(&adapter->PollTimer, MiniportAdapterHandle, NtnetPollTimer, adapter);
    NdisMSetTimer(&adapter->PollTimer, NTNET_POLL_INTERVAL_MS);

    return NDIS_STATUS_SUCCESS;
}

VOID
NtnetHalt(
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PNTNET_ADAPTER adapter = (PNTNET_ADAPTER)MiniportAdapterContext;

    NdisMCancelTimer(&adapter->PollTimer, NULL);
    if (adapter->Shm != NULL)
        NdisMUnmapIoSpace(adapter->MiniportAdapterHandle, adapter->Shm, adapter->ShmLength);
    NdisFreeMemory(adapter, sizeof(*adapter), 0);
}

/* ---- Send / receive ---------------------------------------------------
 *
 * Both directions move a whole raw frame at a time through
 * net_tx_ring/net_rx_ring (ntbridge_protocol.h) - no fragmentation
 * across ring slots, matching the ring's own "whole frame per slot"
 * design (see that header's comment on the tradeoff).
 */

NDIS_STATUS
NtnetSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_PACKET Packet,
    IN UINT Flags
    )
{
    PNTNET_ADAPTER adapter = (PNTNET_ADAPTER)MiniportAdapterContext;
    ntbridge_net_frame_t frame;
    UINT physicalBufferCount, bufferCount, totalLength;
    PNDIS_BUFFER buffer;
    UINT copied = 0;

    UNREFERENCED_PARAMETER(Flags);

    NdisQueryPacket(Packet, &physicalBufferCount, &bufferCount, &buffer, &totalLength);
    if (totalLength > NTBRIDGE_NET_FRAME_MAX) {
        adapter->XmitError++;
        return NDIS_STATUS_FAILURE;
    }

    NdisZeroMemory(&frame, sizeof(frame));
    while (buffer != NULL && copied < totalLength) {
        PVOID va;
        UINT len;
        NdisQueryBuffer(buffer, &va, &len);
        if (copied + len > NTBRIDGE_NET_FRAME_MAX)
            len = NTBRIDGE_NET_FRAME_MAX - copied;
        NdisMoveMemory(frame.data + copied, va, len);
        copied += len;
        NdisGetNextBuffer(buffer, &buffer);
    }
    frame.length = copied;

    if (!ntbridge_net_ring_try_push(&adapter->Shm->net_tx_ring, &frame)) {
        /* Ring full - the host-side bridge isn't draining fast enough.
         * Real NICs drop under sustained overload too; report it
         * honestly via the error counter rather than silently
         * pretending success. */
        adapter->XmitError++;
        return NDIS_STATUS_FAILURE;
    }

    adapter->XmitOk++;
    return NDIS_STATUS_SUCCESS; /* synchronous completion - no NdisMSendComplete needed for this return value */
}

NDIS_STATUS
NtnetTransferData(
    OUT PNDIS_PACKET Packet,
    OUT PUINT BytesTransferred,
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_HANDLE MiniportReceiveContext,
    IN UINT ByteOffset,
    IN UINT BytesToTransfer
    )
{
    /* Only reached if NtnetPollTimer's NdisMEthIndicateReceive call
     * (below) didn't hand over the whole frame in header+lookahead -
     * it always does (see that function), so this is a defensive
     * fallback, not the primary path. */
    UNREFERENCED_PARAMETER(Packet);
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(MiniportReceiveContext);
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(BytesToTransfer);
    *BytesTransferred = 0;
    return NDIS_STATUS_FAILURE;
}

static VOID
NtnetPollTimer(
    PVOID SystemSpecific1,
    PVOID FunctionContext,
    PVOID SystemSpecific2,
    PVOID SystemSpecific3
    )
{
    PNTNET_ADAPTER adapter = (PNTNET_ADAPTER)FunctionContext;
    ntbridge_net_frame_t frame;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    while (ntbridge_net_ring_try_pop(&adapter->Shm->net_rx_ring, &frame)) {
        if (frame.length < 14 || frame.length > NTBRIDGE_NET_FRAME_MAX) {
            adapter->RcvError++;
            continue;
        }
        /* Whole frame handed over as header(14 bytes)+lookahead(rest) in
         * one call, so TransferData is never actually needed - see its
         * own comment. */
        NdisMEthIndicateReceive(
            adapter->MiniportAdapterHandle,
            (NDIS_HANDLE)adapter,
            (PCHAR)frame.data, 14,
            (PCHAR)(frame.data + 14), frame.length - 14,
            frame.length);
        NdisMEthIndicateReceiveComplete(adapter->MiniportAdapterHandle);
        adapter->RcvOk++;
    }

    NdisMSetTimer(&adapter->PollTimer, NTNET_POLL_INTERVAL_MS);
}

NDIS_STATUS
NtnetReset(
    OUT PBOOLEAN AddressingReset,
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    *AddressingReset = FALSE;
    return NDIS_STATUS_SUCCESS;
}

/* ---- OID query/set -----------------------------------------------------
 *
 * The mandatory generic + 802.3 OID surface NDIS requires a miniport to
 * answer before it will treat the adapter as valid (Rule 10:
 * compatibility over elegance - this is the tedious but necessary part
 * of being a real NDIS driver, not optional scaffolding). Values are
 * real, not placeholders: link speed/MTU reflect what this adapter
 * genuinely offers over ntbridge, and the traffic counters
 * (XmitOk/RcvOk/XmitError/RcvError/RcvNoBuffer) are live counts updated
 * by NtnetSend/NtnetPollTimer, not frozen zeros.
 */

#define NTNET_SUPPORTED_OID_COUNT 24
static const NDIS_OID g_ntnet_supported_oids[NTNET_SUPPORTED_OID_COUNT] = {
    OID_GEN_SUPPORTED_LIST, OID_GEN_HARDWARE_STATUS, OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE, OID_GEN_MAXIMUM_LOOKAHEAD, OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED, OID_GEN_TRANSMIT_BUFFER_SPACE, OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE, OID_GEN_RECEIVE_BLOCK_SIZE, OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION, OID_GEN_CURRENT_PACKET_FILTER, OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION, OID_GEN_MAXIMUM_TOTAL_SIZE, OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS, OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_XMIT_OK, OID_GEN_RCV_OK, OID_GEN_XMIT_ERROR, OID_GEN_RCV_ERROR,
};

static NDIS_STATUS
NtnetQueryU32(PVOID buf, ULONG bufLen, PULONG written, PULONG needed, ULONG value)
{
    if (bufLen < sizeof(ULONG)) {
        *needed = sizeof(ULONG);
        return NDIS_STATUS_INVALID_LENGTH;
    }
    *(PULONG)buf = value;
    *written = sizeof(ULONG);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NtnetQueryInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesWritten,
    OUT PULONG BytesNeeded
    )
{
    PNTNET_ADAPTER adapter = (PNTNET_ADAPTER)MiniportAdapterContext;

    *BytesWritten = 0;
    *BytesNeeded = 0;

    switch (Oid) {
    case OID_GEN_SUPPORTED_LIST:
        if (InformationBufferLength < sizeof(g_ntnet_supported_oids)) {
            *BytesNeeded = sizeof(g_ntnet_supported_oids);
            return NDIS_STATUS_INVALID_LENGTH;
        }
        NdisMoveMemory(InformationBuffer, g_ntnet_supported_oids, sizeof(g_ntnet_supported_oids));
        *BytesWritten = sizeof(g_ntnet_supported_oids);
        return NDIS_STATUS_SUCCESS;

    case OID_GEN_HARDWARE_STATUS:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, NdisHardwareStatusReady);
    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, NdisMedium802_3);
    case OID_GEN_MAXIMUM_LOOKAHEAD:
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, NTBRIDGE_NET_FRAME_MAX - 14);
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, NTBRIDGE_NET_FRAME_MAX);
    case OID_GEN_LINK_SPEED:
        /* 1 Gbps in 100-bps units - honest about being a shared-memory
         * transport with no real wire, not claimed to be faster than
         * that transport can plausibly move data. */
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, 10000000);
    case OID_GEN_TRANSMIT_BUFFER_SPACE:
    case OID_GEN_RECEIVE_BUFFER_SPACE:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded,
                              NTBRIDGE_NET_FRAME_MAX * NTBRIDGE_RING_CAPACITY);
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, NTBRIDGE_NET_FRAME_MAX);
    case OID_GEN_VENDOR_ID:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, 0x4E544C00); /* "NTL\0" */
    case OID_GEN_VENDOR_DESCRIPTION:
        if (InformationBufferLength < sizeof("NTLinux ntbridge NDIS bridge")) {
            *BytesNeeded = sizeof("NTLinux ntbridge NDIS bridge");
            return NDIS_STATUS_INVALID_LENGTH;
        }
        NdisMoveMemory(InformationBuffer, "NTLinux ntbridge NDIS bridge", sizeof("NTLinux ntbridge NDIS bridge"));
        *BytesWritten = sizeof("NTLinux ntbridge NDIS bridge");
        return NDIS_STATUS_SUCCESS;
    case OID_GEN_CURRENT_PACKET_FILTER:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, adapter->PacketFilter);
    case OID_GEN_CURRENT_LOOKAHEAD:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, NTBRIDGE_NET_FRAME_MAX - 14);
    case OID_GEN_DRIVER_VERSION:
        if (InformationBufferLength < sizeof(USHORT)) {
            *BytesNeeded = sizeof(USHORT);
            return NDIS_STATUS_INVALID_LENGTH;
        }
        *(PUSHORT)InformationBuffer = 0x0401; /* NDIS 4.1-characteristics driver, per NDIS convention (major<<8|minor) */
        *BytesWritten = sizeof(USHORT);
        return NDIS_STATUS_SUCCESS;
    case OID_GEN_MAC_OPTIONS:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded,
                              NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | NDIS_MAC_OPTION_NO_LOOPBACK |
                              NDIS_MAC_OPTION_FULL_DUPLEX | NDIS_MAC_OPTION_TRANSFERS_NOT_PEND);
    case OID_GEN_MEDIA_CONNECT_STATUS:
        /* Always "connected" - this adapter's link state is the ntbridge
         * shared-memory region existing, which was already validated
         * (magic/version check) before this adapter finished
         * initializing; there's no separate physical-link concept to
         * track. */
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, NdisMediaStateConnected);
    case OID_GEN_MAXIMUM_SEND_PACKETS:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, 1);
    case OID_GEN_XMIT_OK:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, adapter->XmitOk);
    case OID_GEN_RCV_OK:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, adapter->RcvOk);
    case OID_GEN_XMIT_ERROR:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, adapter->XmitError);
    case OID_GEN_RCV_ERROR:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, adapter->RcvError);
    case OID_GEN_RCV_NO_BUFFER:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, adapter->RcvNoBuffer);

    case OID_802_3_PERMANENT_ADDRESS:
    case OID_802_3_CURRENT_ADDRESS:
        if (InformationBufferLength < sizeof(g_ntnet_mac)) {
            *BytesNeeded = sizeof(g_ntnet_mac);
            return NDIS_STATUS_INVALID_LENGTH;
        }
        NdisMoveMemory(InformationBuffer, g_ntnet_mac, sizeof(g_ntnet_mac));
        *BytesWritten = sizeof(g_ntnet_mac);
        return NDIS_STATUS_SUCCESS;
    case OID_802_3_MAXIMUM_LIST_SIZE:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, 0); /* multicast filtering not implemented */
    case OID_802_3_MAC_OPTIONS:
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, 0);
    case OID_802_3_RCV_ERROR_ALIGNMENT:
    case OID_802_3_XMIT_ONE_COLLISION:
    case OID_802_3_XMIT_MORE_COLLISIONS:
        /* Genuinely zero, not a stub - a shared-memory transport has no
         * collision domain or alignment errors to report. */
        return NtnetQueryU32(InformationBuffer, InformationBufferLength, BytesWritten, BytesNeeded, 0);

    default:
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NDIS_STATUS
NtnetSetInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesRead,
    OUT PULONG BytesNeeded
    )
{
    PNTNET_ADAPTER adapter = (PNTNET_ADAPTER)MiniportAdapterContext;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid) {
    case OID_GEN_CURRENT_PACKET_FILTER:
        if (InformationBufferLength < sizeof(ULONG)) {
            *BytesNeeded = sizeof(ULONG);
            return NDIS_STATUS_INVALID_LENGTH;
        }
        adapter->PacketFilter = *(PULONG)InformationBuffer;
        *BytesRead = sizeof(ULONG);
        return NDIS_STATUS_SUCCESS;
    case OID_GEN_CURRENT_LOOKAHEAD:
        if (InformationBufferLength < sizeof(ULONG)) {
            *BytesNeeded = sizeof(ULONG);
            return NDIS_STATUS_INVALID_LENGTH;
        }
        *BytesRead = sizeof(ULONG);
        return NDIS_STATUS_SUCCESS; /* accepted, not separately tracked - whole frames always delivered in one shot (see NtnetPollTimer) */
    case OID_802_3_MULTICAST_LIST:
        /* Multicast filtering not implemented (OID_802_3_MAXIMUM_LIST_SIZE
         * reports 0 above, consistent with this) - accept and ignore
         * rather than fail, matching how many real miniports handle an
         * empty-capability multicast list. */
        *BytesRead = InformationBufferLength;
        return NDIS_STATUS_SUCCESS;
    default:
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}
