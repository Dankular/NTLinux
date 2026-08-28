/*
 * ntbridge_protocol.h — versioned wire protocol for the Linux host <->
 * ReactOS driver-cell transport (ARCHITECTURE.md section 17).
 *
 * Mirrors the pattern established by ntabi/protocol/ntabi_protocol.h
 * (Rule 12: cross-boundary protocols must be versioned; Rule 13: prefer
 * shared memory over chatty RPC for hot I/O paths) but for a genuinely
 * different transport shape: ntabi connects two *processes on the same
 * Linux host* via POSIX shared memory + semaphores. ntbridge connects a
 * Linux host process to a program running inside a *separate virtual
 * machine* (the ReactOS driver cell), so there is no shared kernel to
 * provide semaphores/futexes across the boundary. The transport here is
 * therefore a lock-free single-producer/single-consumer ring per
 * direction, built directly on volatile reads/writes plus C11 atomic
 * fences — the same technique real ivshmem-based drivers use, because
 * it is the only synchronization primitive that actually crosses a VM
 * boundary without hypervisor-mediated interrupts.
 *
 * Backing transport: QEMU `ivshmem-plain` (see driver/cell/launcher/
 * ntcell) — a PCI device whose BAR2 is a plain shared-memory region
 * backed by a host-side file. No interrupt/doorbell support in the
 * "plain" variant, so both sides poll; see the "Known gap" note in
 * driver/ntbridge/host/README.md for the doorbell upgrade path
 * (ivshmem-doorbell + eventfd) once the polling latency actually
 * matters.
 *
 * This header is shared verbatim by the host side (driver/ntbridge/
 * host/) and the ReactOS driver-cell side (driver/ntbridge/reactos/) —
 * same pattern as ntabi_protocol.h being included by both libntabi and
 * ntd. It is also included by the honest stand-in guest test client in
 * tests/reactos/ that exercises this protocol over a real QEMU VM
 * boundary in this sandbox, since a real ReactOS build (RosBE) isn't
 * reachable here — see tests/reactos/README.md.
 */

#ifndef NTBRIDGE_PROTOCOL_H
#define NTBRIDGE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NTBRIDGE_STRINGIFY_(x) #x
#define NTBRIDGE_STRINGIFY(x) NTBRIDGE_STRINGIFY_(x)

/* Bump on any wire-incompatible change to the structs below (Rule 12).
 * No 'u'/'U' suffix — see the ntabi v2u incident in ROADMAP.md Phase 3;
 * preprocessor stringification (#x) includes literal type suffixes
 * verbatim, which would corrupt anything that stringifies this macro.
 *
 * v2 (Phase 7): added net_tx_ring/net_rx_ring for the NDIS bridge
 * (ARCHITECTURE.md section 22) — a real wire-format change, hence the
 * bump; a v1 guest mapping a v2 region (or vice versa) is refused by
 * the magic+version check every side of this protocol already makes,
 * exactly as designed.
 *
 * v3 (Phase 8): added usb_req_ring/usb_resp_ring for the USB bridge
 * (ARCHITECTURE.md section 27's "USB device cell", Phase 8's URB
 * translation/bridging task). Same reasoning as the v1->v2 bump. */
#define NTBRIDGE_PROTOCOL_VERSION 3

#define NTBRIDGE_MAGIC 0x5242544Eu /* "NTBR" little-endian */

/* QEMU ivshmem PCI identity (Red Hat / Qumranet vendor block). Both the
 * host launcher and any guest-side code that enumerates PCI to find the
 * device use these. */
#define NTBRIDGE_IVSHMEM_VENDOR_ID 0x1af4u
#define NTBRIDGE_IVSHMEM_DEVICE_ID 0x1110u

/* ---- ring buffer -------------------------------------------------- */

/* A fixed-slot single-producer/single-consumer ring. One side only ever
 * writes `head`, the other only ever writes `tail` — the classic SPSC
 * invariant that makes this safe without a cross-VM lock. `head`/`tail`
 * are free-running counters (not masked indices) so full-vs-empty is
 * unambiguous without wasting a slot; index into `slots` with
 * `counter % NTBRIDGE_RING_CAPACITY`.
 *
 * Each ring is embedded directly in a shared header — see
 * ntbridge_shm_header below — never allocated separately, so both sides
 * agree on layout purely from struct definitions compiled from this
 * same header, exactly like ntabi's slot array.
 */
#define NTBRIDGE_RING_CAPACITY 64u

#define NTBRIDGE_LOG_TEXT_MAX 128u
#define NTBRIDGE_DEV_STR_MAX 64u

typedef struct ntbridge_log_entry {
    uint32_t level;   /* ntbridge_log_level_t */
    uint32_t source;  /* ntbridge_side_t: who wrote this entry */
    uint64_t seq;     /* monotonically increasing per writer, for gap detection */
    char text[NTBRIDGE_LOG_TEXT_MAX];
} ntbridge_log_entry_t;

typedef enum ntbridge_log_level {
    NTBRIDGE_LOG_DEBUG = 0,
    NTBRIDGE_LOG_INFO = 1,
    NTBRIDGE_LOG_WARN = 2,
    NTBRIDGE_LOG_ERROR = 3
} ntbridge_log_level_t;

typedef enum ntbridge_side {
    NTBRIDGE_SIDE_HOST = 0,
    NTBRIDGE_SIDE_GUEST = 1
} ntbridge_side_t;

/* Device-enumeration bridge (ARCHITECTURE.md section 18): Linux does
 * physical discovery (sysfs/udev/netlink) and hands synthetic device
 * facts across; the guest/cell side is responsible for the NT-visible
 * PnP consequences (PDO creation, IoReportDetectedDevice-equivalent —
 * see driver/ntbridge/reactos/ntbridge_pnp.c). This struct is the wire
 * shape of one such fact, deliberately Windows/NT-flavored (bus type,
 * hardware IDs) rather than a raw copy of a Linux sysfs record, because
 * the far side has to hand it to an NT PnP manager, not interpret udev
 * conventions directly (ARCHITECTURE.md section 19: preserve the NT PnP
 * model, don't leak udev semantics across the bridge).
 */
typedef enum ntbridge_pnp_action {
    NTBRIDGE_PNP_DEVICE_ARRIVED = 0,
    NTBRIDGE_PNP_DEVICE_REMOVED = 1
} ntbridge_pnp_action_t;

typedef enum ntbridge_bus_type {
    NTBRIDGE_BUS_SYNTHETIC = 0, /* test/demo device, no real hardware backing */
    NTBRIDGE_BUS_PCI = 1,
    NTBRIDGE_BUS_USB = 2
} ntbridge_bus_type_t;

typedef struct ntbridge_pnp_descriptor {
    uint64_t device_id;               /* host-assigned, stable for arrived/removed pairing */
    uint32_t action;                  /* ntbridge_pnp_action_t */
    uint32_t bus_type;                /* ntbridge_bus_type_t */
    uint16_t vendor_id;
    uint16_t product_id;
    uint32_t reserved;
    char hardware_id[NTBRIDGE_DEV_STR_MAX];   /* e.g. "PCI\\VEN_1AF4&DEV_1110" */
    char friendly_name[NTBRIDGE_DEV_STR_MAX]; /* e.g. "NTLinux Synthetic Test Device" */
    char location[NTBRIDGE_DEV_STR_MAX];      /* e.g. "PCI bus 0, device 4, function 0" */
} ntbridge_pnp_descriptor_t;

/* Guest's acknowledgment that a PnP descriptor was consumed (real
 * ReactOS side: PDO created and reported; stand-in guest test client:
 * descriptor validated and logged) — lets the host side's test/
 * verification harness prove round-trip delivery, not just one-way
 * send (ARCHITECTURE.md section 18/19, and Phase 4's success criterion:
 * "ReactOS sees synthetic devices supplied by Linux" — "sees" has to be
 * demonstrated, not assumed). */
typedef struct ntbridge_pnp_ack {
    uint64_t device_id;
    uint32_t action;    /* echoes the action that was acted on */
    uint32_t status;    /* 0 = accepted, nonzero = guest-side error code */
} ntbridge_pnp_ack_t;

/* NDIS bridge (Phase 7, ARCHITECTURE.md section 22): raw Ethernet
 * frames, one ring per direction — net_tx_ring carries frames the guest
 * NDIS miniport (driver/net/reactos/) wants sent out (guest -> host,
 * where the host injects them into a Linux TAP device), net_rx_ring
 * carries frames the host read off that same TAP device and wants
 * delivered as "received" to the miniport (host -> guest). Deliberately
 * whole raw frames, not descriptors pointing at frame data elsewhere —
 * simplicity over throughput for a first cut, matching how the PnP/log
 * rings already work; a zero-copy descriptor-ring redesign is a real,
 * separate optimization if this ever needs more than test-scale
 * throughput. */
#define NTBRIDGE_NET_FRAME_MAX 1514u /* standard Ethernet MTU + 14-byte header, no jumbo frames */

typedef struct ntbridge_net_frame {
    uint32_t length; /* actual frame length in data[], <= NTBRIDGE_NET_FRAME_MAX */
    uint8_t data[NTBRIDGE_NET_FRAME_MAX];
} ntbridge_net_frame_t;

/* USB bridge (Phase 8, ARCHITECTURE.md section 27's "USB device cell" /
 * section 51's "Linux owns... USB host stack"): NTLinux's ReactOS-side
 * driver (driver/usb/reactos/ntusb.c) presents a single synthetic
 * vendor-class USB device (one bulk IN endpoint, one bulk OUT endpoint)
 * directly — it is not a virtual USB Host Controller Driver, so it does
 * not (yet) let an arbitrary pre-existing vendor .sys bind to a
 * PnP-enumerated bus the way a real USB stack would; see that driver's
 * README for the real gap this leaves and why (mingw-w64's DDK ships
 * usb.h/usbioctl.h — the URB and IOCTL_INTERNAL_USB_SUBMIT_URB shapes a
 * client driver above a bus uses — but not usbport.h, the internal
 * miniport interface a from-scratch HC driver would need to register
 * with ReactOS's usbport.sys; confirmed absent by direct search, not
 * assumed).
 *
 * Device/configuration/string descriptors for that synthetic device are
 * entirely local to ntusb.c (there is no real hardware backing them, so
 * there is nothing for Linux to be authoritative over) — only the
 * bulk-transfer *data* genuinely needs to cross the bridge, which is
 * what these two rings carry, one per direction, deliberately whole
 * payloads rather than descriptors (same simplicity-over-throughput
 * tradeoff net_tx_ring/net_rx_ring made in Phase 7). `function` is
 * carried for forward documentation even though this first cut only
 * ever sets it to NTBRIDGE_USB_FN_BULK_OR_INTERRUPT_TRANSFER — a
 * genuine URB_FUNCTION_* code from real Windows usb.h, not a
 * bridge-invented one, so ntusb.c's dispatch can use it directly. */
typedef enum ntbridge_usb_function {
    NTBRIDGE_USB_FN_BULK_OR_INTERRUPT_TRANSFER = 0x0009 /* == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER */
} ntbridge_usb_function_t;

#define NTBRIDGE_USB_DATA_MAX 4096u /* one bridged transfer's worth; a
                                      * larger client request is split
                                      * across multiple messages by
                                      * ntusb.c, not grown here — same
                                      * fixed-max-payload tradeoff
                                      * NTBRIDGE_NET_FRAME_MAX made */

typedef struct ntbridge_usb_urb_msg {
    uint64_t request_id; /* guest-assigned, echoed back on the matching
                           * response so the guest can pair a completion
                           * with its pending IRP without depending on
                           * strict ring ordering */
    uint32_t function;   /* ntbridge_usb_function_t */
    uint32_t status;     /* request side: unused (0). response side: 0 =
                           * success, nonzero = a USBD_STATUS-shaped
                           * failure code ntusb.c reports up as the
                           * URB's Hdr.Status */
    uint8_t endpoint;    /* bEndpointAddress (direction bit included) */
    uint8_t reserved[3];
    uint32_t length;     /* valid bytes in data[] */
    uint8_t data[NTBRIDGE_USB_DATA_MAX];
} ntbridge_usb_urb_msg_t;

#define NTBRIDGE_RING_DECL(name, elem_type)                                  \
    typedef struct name {                                                    \
        volatile uint64_t head; /* next slot the producer will write */      \
        volatile uint64_t tail; /* next slot the consumer will read */       \
        elem_type slots[NTBRIDGE_RING_CAPACITY];                             \
    } name##_t

NTBRIDGE_RING_DECL(ntbridge_log_ring, ntbridge_log_entry_t);
NTBRIDGE_RING_DECL(ntbridge_pnp_ring, ntbridge_pnp_descriptor_t);
NTBRIDGE_RING_DECL(ntbridge_pnp_ack_ring, ntbridge_pnp_ack_t);
NTBRIDGE_RING_DECL(ntbridge_net_ring, ntbridge_net_frame_t);
NTBRIDGE_RING_DECL(ntbridge_usb_ring, ntbridge_usb_urb_msg_t);

/* ---- shared header -------------------------------------------------
 *
 * Laid out at offset 0 of the ivshmem-plain shared region. Both sides
 * memory-map the *same* backing (host: the memory-backend-file directly;
 * guest: the ivshmem device's BAR2 via sysfs `resourceN`/PCI, see
 * tests/reactos/ntbridge-guest-test.c) so there is exactly one instance
 * of this struct, not a copy per side.
 */
typedef struct ntbridge_shm_header {
    uint32_t magic;         /* NTBRIDGE_MAGIC */
    uint32_t version;       /* NTBRIDGE_PROTOCOL_VERSION */
    uint32_t total_size;    /* sizeof(ntbridge_shm_header_t), for a guest-side sanity check */
    uint32_t initialized;   /* set to 1 by whichever side maps the region first */

    /* Heartbeat: both sides bump their own counter/timestamp on a
     * timer; each side detects the other as alive by observing the
     * peer's counter advancing, and detects a stall/crash by it not
     * advancing within a deadline. No cross-VM interrupt in the
     * ivshmem-plain transport, so this is deliberately poll-based —
     * documented, not accidental (see the doorbell note above). */
    volatile uint64_t host_heartbeat_seq;
    volatile int64_t host_heartbeat_time_ns;
    volatile uint64_t guest_heartbeat_seq;
    volatile int64_t guest_heartbeat_time_ns;

    ntbridge_log_ring_t log_ring;           /* guest -> host: driver-cell log lines */
    ntbridge_pnp_ring_t pnp_ring;           /* host -> guest: synthetic device facts */
    ntbridge_pnp_ack_ring_t pnp_ack_ring;   /* guest -> host: per-device acknowledgment */
    ntbridge_net_ring_t net_tx_ring;        /* guest -> host: frames the NDIS miniport is sending */
    ntbridge_net_ring_t net_rx_ring;        /* host -> guest: frames read off the host TAP device */
    ntbridge_usb_ring_t usb_req_ring;       /* guest -> host: bulk OUT payloads / bulk IN requests */
    ntbridge_usb_ring_t usb_resp_ring;      /* host -> guest: bulk IN payloads / bulk OUT completions */
} ntbridge_shm_header_t;

/* ---- ring push/pop helpers ------------------------------------------
 *
 * `static inline`, header-only, and libc-independent (plain struct
 * assignment + GCC/Clang __atomic builtins only) so the exact same
 * functions compile in a userspace daemon (host, and the stand-in guest
 * test client) and, later, in the ReactOS kernel-mode driver — no
 * memcpy/malloc dependency to satisfy from a freestanding driver
 * environment.
 *
 * Ordering: the producer publishes the slot's contents with a release
 * fence *before* publishing the new head, so a consumer that observes
 * an advanced head is guaranteed to see a fully-written slot. The
 * consumer issues an acquire fence after reading head, before reading
 * the slot. This is the standard SPSC ring pattern; it is what makes
 * these rings safe to share across the VM boundary with no OS-level
 * lock available to either side.
 */
#define NTBRIDGE_RING_PUSH_DECL(name, elem_type)                             \
    static inline int name##_try_push(name##_t *ring, const elem_type *item) \
    {                                                                        \
        uint64_t head = ring->head;                                          \
        uint64_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);      \
        if (head - tail >= NTBRIDGE_RING_CAPACITY)                           \
            return 0; /* full */                                             \
        ring->slots[head % NTBRIDGE_RING_CAPACITY] = *item;                  \
        __atomic_store_n(&ring->head, head + 1, __ATOMIC_RELEASE);           \
        return 1;                                                            \
    }                                                                        \
    static inline int name##_try_pop(name##_t *ring, elem_type *out)         \
    {                                                                        \
        uint64_t tail = ring->tail;                                          \
        uint64_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);      \
        if (tail == head)                                                    \
            return 0; /* empty */                                            \
        *out = ring->slots[tail % NTBRIDGE_RING_CAPACITY];                   \
        __atomic_store_n(&ring->tail, tail + 1, __ATOMIC_RELEASE);           \
        return 1;                                                            \
    }

NTBRIDGE_RING_PUSH_DECL(ntbridge_log_ring, ntbridge_log_entry_t)
NTBRIDGE_RING_PUSH_DECL(ntbridge_pnp_ring, ntbridge_pnp_descriptor_t)
NTBRIDGE_RING_PUSH_DECL(ntbridge_pnp_ack_ring, ntbridge_pnp_ack_t)
NTBRIDGE_RING_PUSH_DECL(ntbridge_net_ring, ntbridge_net_frame_t)
NTBRIDGE_RING_PUSH_DECL(ntbridge_usb_ring, ntbridge_usb_urb_msg_t)

#define NTBRIDGE_SHM_SIZE_MIN ((uint32_t)sizeof(ntbridge_shm_header_t))

/* Default ivshmem-plain backing size, rounded up to a page-aligned
 * power-of-two the way QEMU's memory-backend-file expects; comfortably
 * larger than NTBRIDGE_SHM_SIZE_MIN so the layout has headroom to grow
 * without an immediate resize. Bumped from 1 MiB to 4 MiB in Phase 7 to
 * fit the two new net_tx_ring/net_rx_ring rings
 * (2 * 64 * ~1518 bytes =~ 194 KiB alone) with real headroom left over,
 * not just barely fitting today's struct size.
 *
 * Phase 8's usb_req_ring/usb_resp_ring add another ~515 KiB
 * (2 * 64 * 4120 bytes). Checked by actually compiling and printing
 * sizeof(ntbridge_shm_header_t) (728.7 KiB total) against this constant
 * before shipping the v3 bump — precisely because Phase 7's own
 * hardcoded-SHM_SIZE_MB bug (driver/cell/launcher/ntcell) was found only
 * by running, not by hand arithmetic. Unlike Phase 7, this still fits
 * inside 4 MiB with headroom, so no size bump was needed here, and
 * ntcell's SHM_SIZE_MB=4 stays correct as-is. */
#define NTBRIDGE_SHM_DEFAULT_SIZE (4u << 20) /* 4 MiB */

#ifdef __cplusplus
}
#endif

#endif /* NTBRIDGE_PROTOCOL_H */
