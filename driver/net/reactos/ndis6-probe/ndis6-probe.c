/*
 * ndis6-probe.c -- real, live compile probe for the real Microsoft
 * NDIS 6.x (connectionless miniport model) headers
 * (fetch-ndis6-headers.sh), against this project's existing mingw-w64
 * DDK toolchain - the same toolchain every other driver under driver/
 * builds with.
 *
 * Follow-up to tooling/compat-db/ddkgap/'s own live finding
 * (README.md, ROADMAP.md Phase 9): NdisMRegisterMiniportDriver,
 * NdisMSetMiniportAttributes, NdisMIndicateReceiveNetBufferLists, and
 * NdisAllocateNetBufferListPool are all real import stubs in this
 * toolchain's libndis.a (`nm` confirms it, again, directly, in this
 * pass), but genuinely absent from every header mingw-w64 packages -
 * confirmed with a real recursive grep across this whole toolchain's
 * *entire* include tree, not just ddk/. Same "find the real headers,
 * fetch them narrowly, verify by actually compiling" technique as
 * driver/gpu/wddm-probe.c and driver/kmdf/kmdf-probe.c.
 *
 * Distinct from driver/net/reactos/ntnet.c, which deliberately targets
 * the *older* NDIS 5.1 NdisMRegisterMiniport/NdisMEthIndicateReceive
 * surface (its own header comment explains why - that's genuinely what
 * this toolchain's *packaged* ndis.h declares). This probe is the NDIS
 * 6.x connectionless miniport model's own NET_BUFFER_LIST-based
 * registration and receive-indication path - a real, different, newer
 * API family, not an alternate spelling of the same one.
 *
 * Goes one step further than driver/gpu/wddm-probe.c and driver/kmdf/
 * kmdf-probe.c, which could only compile (no dxgkrnl.sys/Wdf01000
 * import library exists in this toolchain for either): `make link`
 * (this directory's Makefile) links this file's DriverEntry into a
 * real ndis6-probe.sys against this toolchain's actual libndis.a,
 * with the identical LDFLAGS/LDLIBS driver/net/reactos/Makefile
 * already uses for ntnet.sys, real and live-verified (see README.md's
 * "Real, live result" for the exact transcript) - `nm` on the result
 * shows all four target symbols resolved as real imported thunks, not
 * left undefined.
 *
 * What this does NOT prove, stated precisely rather than glossed over:
 * a successful link only confirms the import stub resolves against
 * this toolchain's own libndis.a - not that ReactOS's own ndis.sys
 * genuinely implements the NDIS 6.x connectionless miniport contract
 * end to end, nor that this driver was ever loaded by a running
 * ReactOS kernel (a real, separate, unattempted question - ReactOS's
 * own NDIS implementation maturity for the 6.x model specifically is
 * not evaluated here; same "written against the real API, not yet
 * loaded live" boundary ntnet.c's own header comment already draws
 * for the NDIS 5.1 surface).
 */
#include <ntddk.h>
#include <assert.h>   /* windot11.h (pulled in by ndis.h) uses the C11
                       * static_assert macro form - same class of gap
                       * driver/gpu/wddm-probe.c's dispmprt.h already
                       * needed this for; included here, before
                       * <ndis.h>, so it resolves regardless of what
                       * else pulls windot11.h in. */

/* One real, narrow macro gap, same "define the missing trivial macro
 * locally" technique as driver/gpu/wddm-probe.c's own
 * _Maybenull_/_Pre_opt_bytecap_ no-ops: the real shared/ndis/types.h
 * (and several other fetched headers) open with EXTERN_C_START /
 * close with EXTERN_C_END, which the real WDK's own shared/ntdef.h
 * defines - but this toolchain still resolves <ntdef.h> to
 * mingw-w64's own copy (deliberately not replaced - see
 * fetch-ndis6-headers.sh for why only sal.h/specstrings.h were), which
 * doesn't define these two. Matches shared/ntdef.h's own C
 * (non-C++) branch exactly: both expand to nothing for a plain C
 * compile. */
#define EXTERN_C_START
#define EXTERN_C_END

/* Three more real, narrow gaps - all things the real WDK's own km/
 * wdm.h and km/miniport.h define (confirmed directly by grepping
 * fetch-ndis6-headers.sh's own downloaded copies of those two files),
 * which this probe deliberately does NOT fetch/swap in (unlike
 * ws2def.h/ws2ipdef.h/ifdef.h above) - wdm.h is the base of this
 * toolchain's entire ntddk.h chain, and miniport.h pulls in the
 * legacy NDIS 3/4/5 miniport surface driver/net/reactos/ntnet.c
 * already targets a working copy of; swapping either wholesale is a
 * materially bigger, riskier change than three narrow local
 * declarations for symbols ndis.h's own header text merely assumes
 * some earlier header already provided. */

/* DECLSPEC_DEPRECATED_DDK: real km/miniport.h's own macro definition,
 * for the (real, default - DEPRECATE_DDK_FUNCTIONS undefined) branch
 * this probe is actually in, reduces to nothing - copied verbatim,
 * not guessed. */
#define DECLSPEC_DEPRECATED_DDK

/* POOL_NX_ALLOCATION: real km/wdm.h defines this NTDDI_WIN7+ pool-type
 * flag as literal 512 - copied verbatim from that header. */
#define POOL_NX_ALLOCATION 512

/* KeGetCurrentProcessorIndex: real km/wdm.h declares this FORCEINLINE,
 * NTDDI_WIN7+, with a body reading a fixed KPCR offset via an
 * architecture-specific intrinsic (__readgsdword(0x1a4) on x64) - not
 * reproduced here, since ndis.h's own NdisCurrentProcessorIndex
 * (ndis.h's sole caller of this) is itself FORCEINLINE and never
 * instantiated by this probe (this file never calls
 * NdisCurrentProcessorIndex); a declaration is all a compile-only
 * probe needs to satisfy NdisCurrentProcessorIndex's own body
 * type-checking. */
ULONG NTAPI KeGetCurrentProcessorIndex(VOID);

#define NDIS_MINIPORT_DRIVER 1  /* required together, per ndis.h's own
                                 * header comment - same pairing
                                 * driver/net/reactos/ntnet.c's header
                                 * comment already documents for the
                                 * NDIS 5.1 surface; here paired with
                                 * NDIS630_MINIPORT instead. */
#define NDIS630_MINIPORT 1      /* NDIS 6.30 (Windows 8.1-era) - the
                                 * version tier that first declares
                                 * NdisMRegisterMiniportDriver (NDIS 6.0
                                 * already has it, but 6.30 is this
                                 * header's own recommended baseline
                                 * for new connectionless miniports;
                                 * see NDIS_MINIPORT_DRIVER_CHARACTERISTICS
                                 * below). */
#include <ndis.h>

/* A real NDIS 6.x connectionless miniport's actual DriverEntry -
 * referencing the real NDIS_MINIPORT_DRIVER_CHARACTERISTICS/
 * NdisMRegisterMiniportDriver contract, not a stub that merely
 * #includes the header and does nothing with it. */

static MINIPORT_INITIALIZE Ndis6ProbeInitializeEx;
static MINIPORT_HALT Ndis6ProbeHaltEx;
static MINIPORT_UNLOAD Ndis6ProbeUnload;

static NDIS_STATUS
Ndis6ProbeInitializeEx(
    NDIS_HANDLE NdisMiniportHandle,
    NDIS_HANDLE MiniportDriverContext,
    PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters
    )
{
    /* NDIS_MINIPORT_ADAPTER_ATTRIBUTES is a real union (not a struct
     * of pointers, as the older NDIS 5.x-style attribute-block APIs
     * this project's ntnet.c already targets might suggest) -
     * confirmed directly from the fetched header's own
     * `typedef ... union _NDIS_MINIPORT_ADAPTER_ATTRIBUTES` - one
     * still-zeroed union, its RegistrationAttributes member
     * initialized in place and passed by address, not a separate
     * struct assigned in through a pointer field. */
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES attrs;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    NdisZeroMemory(&attrs, sizeof(attrs));

    attrs.RegistrationAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    attrs.RegistrationAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2;
    attrs.RegistrationAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2;
    attrs.RegistrationAttributes.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
    attrs.RegistrationAttributes.InterfaceType = NdisInterfacePci;

    /* The real Phase 9 gap: this stub call, compiled against the real
     * header's real NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES
     * shape (via NdisMSetMiniportAttributes's own union parameter),
     * is exactly what this DDK's packaged headers could not do before
     * this probe. */
    NdisMSetMiniportAttributes(NdisMiniportHandle, &attrs);

    return NDIS_STATUS_SUCCESS;
}

static void
Ndis6ProbeHaltEx(
    NDIS_HANDLE MiniportAdapterContext,
    NDIS_HALT_ACTION HaltAction
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(HaltAction);
}

static void
Ndis6ProbeUnload(
    PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
}

/* Exercises the real NET_BUFFER_LIST pool + receive-indication path -
 * the data-path half of the NDIS 6.x gap ddkgap.py flagged
 * (NdisAllocateNetBufferListPool, NdisMIndicateReceiveNetBufferLists),
 * not just the registration half. Never actually called by anything in
 * this probe (same "compiles against the real shapes, not exercised
 * live" boundary wddm-probe.c/kmdf-probe.c already draw) - its purpose
 * is to force the compiler to check these two symbols' real prototypes
 * and the real NET_BUFFER_LIST_POOL_PARAMETERS/NDIS_RECEIVE_FLAGS
 * types against genuine call sites. */
static void
Ndis6ProbeReceiveDemo(
    NDIS_HANDLE NdisMiniportHandle,
    PNET_BUFFER_LIST Nbl
    )
{
    NET_BUFFER_LIST_POOL_PARAMETERS poolParams;
    NDIS_HANDLE poolHandle;

    NdisZeroMemory(&poolParams, sizeof(poolParams));
    poolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    poolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    poolParams.Header.Size = sizeof(poolParams);
    poolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    poolParams.fAllocateNetBuffer = TRUE;
    poolParams.PoolTag = 'bp6N'; /* "N6pb" - NDIS6 probe */

    poolHandle = NdisAllocateNetBufferListPool(NdisMiniportHandle, &poolParams);
    if (poolHandle != NULL) {
        NdisMIndicateReceiveNetBufferLists(
            NdisMiniportHandle,
            Nbl,
            NDIS_DEFAULT_PORT_NUMBER,
            1,
            NDIS_RECEIVE_FLAGS_RESOURCES);
    }
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS mChars;
    NDIS_STATUS status;
    NDIS_HANDLE driverHandle = NULL;

    /* Not called (see Ndis6ProbeReceiveDemo's own header comment) -
     * referencing its address here (rather than
     * UNREFERENCED_PARAMETER, which expands to a self-assignment and
     * needs an lvalue - a function designator isn't one) is enough to
     * silence -Wunused-function without actually invoking it. */
    (void)&Ndis6ProbeReceiveDemo;

    NdisZeroMemory(&mChars, sizeof(mChars));
    mChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    mChars.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    mChars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    mChars.MajorNdisVersion = 6;
    mChars.MinorNdisVersion = 30;
    mChars.MajorDriverVersion = 1;
    mChars.MinorDriverVersion = 0;

    mChars.InitializeHandlerEx = Ndis6ProbeInitializeEx;
    mChars.HaltHandlerEx = Ndis6ProbeHaltEx;
    mChars.UnloadHandler = Ndis6ProbeUnload;

    /* Not actually expected to succeed live - NdisMRegisterMiniportDriver
     * needs a real NDIS_WRAPPER_HANDLE only NdisMInitializeWrapper (or
     * the DriverEntry-time NDIS wrapper context, on newer NDIS) can
     * supply, and this probe supplies none. Compilation against the
     * real struct/prototype shapes is what this probe checks -
     * calling it for real against a live NDIS wrapper is a genuine,
     * separate, unattempted follow-up (same boundary ntnet.c itself
     * draws around "written against the real API, not yet loaded
     * live" - see that file's own header comment). */
    status = NdisMRegisterMiniportDriver(
        DriverObject,
        RegistryPath,
        NULL,
        &mChars,
        &driverHandle);

    return (status == NDIS_STATUS_SUCCESS) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
