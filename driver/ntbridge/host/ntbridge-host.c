/*
 * ntbridge-host — Linux-side (host) implementation of ntbridge
 * (ARCHITECTURE.md section 17, driver/ntbridge/host/README.md).
 *
 * Owns the ivshmem-plain backing file that QEMU's memory-backend-file
 * maps into the driver cell's PCI BAR2 (see driver/cell/launcher/
 * ntcell), creates and initializes the shared ntbridge_shm_header_t
 * (Rule 12: versioned — refuses to run against a region stamped with a
 * mismatched protocol version rather than guessing at compatibility),
 * and drives three of Phase 4's four concrete deliverables:
 *
 *   - host heartbeat: bumps host_heartbeat_seq/time on a fixed tick,
 *     and reports whether the guest side is answering back.
 *   - logging channel: drains guest-written log lines from log_ring
 *     and prints them, tagged, to stdout.
 *   - device enumeration bridge: seeds pnp_ring with synthetic device
 *     descriptors (the real integration point for genuine sysfs/udev/
 *     netlink discovery — ARCHITECTURE.md section 18 — is a single
 *     function, seed_synthetic_devices() below; swapping it for a real
 *     udev enumerator does not touch anything else in this file) and
 *     tracks which ones the guest side acknowledges via pnp_ack_ring.
 *
 * The fourth deliverable — the actual bootable ReactOS image + KVM/QEMU
 * launcher — is driver/cell/images/ + driver/cell/launcher/ntcell; this
 * daemon doesn't know or care whether the process on the other end of
 * the shared memory is a real ReactOS ntbridge.sys driver (see
 * driver/ntbridge/reactos/, not yet buildable in this sandbox — needs
 * RosBE) or the honest stand-in guest test client in tests/reactos/
 * that exercises this exact wire protocol over a real QEMU VM boundary.
 * Both speak the same struct definitions from ntbridge_protocol.h, so
 * whichever one is on the other end, this code is unchanged.
 *
 * No interrupt/doorbell available on the ivshmem-plain transport, so
 * this is a deliberate poll loop (documented, not accidental — see the
 * doorbell upgrade note in ntbridge_protocol.h and this directory's
 * README).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "ntbridge_protocol.h"

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static const char *log_level_str(uint32_t level)
{
    switch (level) {
    case NTBRIDGE_LOG_DEBUG: return "DEBUG";
    case NTBRIDGE_LOG_INFO: return "INFO";
    case NTBRIDGE_LOG_WARN: return "WARN";
    case NTBRIDGE_LOG_ERROR: return "ERROR";
    default: return "?";
    }
}

/*
 * Device enumeration bridge, seed side. A real deployment replaces this
 * with a walk over /sys/bus/pci/devices and /sys/bus/usb/devices (or a
 * netlink uevent listener for hotplug — ARCHITECTURE.md section 18),
 * translating each Linux device into the NT-flavored
 * ntbridge_pnp_descriptor_t shape (hardware ID string, not a raw sysfs
 * path — section 19: the cell keeps the NT view, this bridge only
 * carries physical facts across). This prototype seeds a small fixed
 * list so the transport, ring protocol, and acknowledgment path can be
 * proven end-to-end without requiring the driver cell to actually be
 * ReactOS (see the file header).
 */
static int seed_synthetic_devices(ntbridge_shm_header_t *shm, int count)
{
    int pushed = 0;
    for (int i = 0; i < count; i++) {
        ntbridge_pnp_descriptor_t d;
        memset(&d, 0, sizeof(d));
        d.device_id = (uint64_t)(1000 + i);
        d.action = NTBRIDGE_PNP_DEVICE_ARRIVED;
        d.bus_type = NTBRIDGE_BUS_SYNTHETIC;
        d.vendor_id = 0x1AF4;
        d.product_id = (uint16_t)(0x1000 + i);
        snprintf(d.hardware_id, sizeof(d.hardware_id),
                 "NTLINUX\\SYNTH_%04X", d.product_id);
        snprintf(d.friendly_name, sizeof(d.friendly_name),
                 "NTLinux Synthetic Test Device %d", i);
        snprintf(d.location, sizeof(d.location),
                 "ntbridge synthetic bus, slot %d", i);
        if (ntbridge_pnp_ring_try_push(&shm->pnp_ring, &d))
            pushed++;
        else
            fprintf(stderr, "ntbridge-host: pnp_ring full, only seeded %d/%d devices\n",
                    pushed, count);
    }
    printf("ntbridge-host: seeded %d synthetic device descriptor(s)\n", pushed);
    return pushed;
}

static ntbridge_shm_header_t *open_shm(const char *path, uint32_t size, int *out_fd)
{
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        fprintf(stderr, "ntbridge-host: open(%s): %s\n", path, strerror(errno));
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "ntbridge-host: fstat: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    int fresh = (st.st_size == 0);
    if (fresh || (uint32_t)st.st_size < size) {
        if (ftruncate(fd, size) != 0) {
            fprintf(stderr, "ntbridge-host: ftruncate: %s\n", strerror(errno));
            close(fd);
            return NULL;
        }
    }

    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "ntbridge-host: mmap: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    ntbridge_shm_header_t *shm = (ntbridge_shm_header_t *)map;

    if (fresh || shm->magic == 0) {
        /* First opener initializes the region. QEMU's memory-backend-file
         * with share=on will map the same inode into the guest cell once
         * it starts, so this must happen before (or race-tolerantly
         * concurrent with) the cell booting — the launcher starts
         * ntbridge-host first for exactly this reason. */
        memset(shm, 0, sizeof(*shm));
        shm->magic = NTBRIDGE_MAGIC;
        shm->version = NTBRIDGE_PROTOCOL_VERSION;
        shm->total_size = (uint32_t)sizeof(*shm);
        __atomic_store_n(&shm->initialized, 1, __ATOMIC_RELEASE);
        printf("ntbridge-host: initialized fresh shm region at %s (%u bytes)\n",
               path, size);
    } else {
        if (shm->magic != NTBRIDGE_MAGIC) {
            fprintf(stderr, "ntbridge-host: bad magic 0x%08x in %s (expected 0x%08x) — "
                    "refusing to attach\n", shm->magic, path, NTBRIDGE_MAGIC);
            munmap(map, size);
            close(fd);
            return NULL;
        }
        if (shm->version != NTBRIDGE_PROTOCOL_VERSION) {
            fprintf(stderr, "ntbridge-host: protocol version mismatch in %s: "
                    "region is v%u, this build is v%u — refusing to attach "
                    "(Rule 12: never guess at wire compatibility)\n",
                    path, shm->version, NTBRIDGE_PROTOCOL_VERSION);
            munmap(map, size);
            close(fd);
            return NULL;
        }
        printf("ntbridge-host: reattached existing v%u shm region at %s\n",
               shm->version, path);
    }

    *out_fd = fd;
    return shm;
}

int main(int argc, char **argv)
{
    const char *shm_path = "/dev/shm/ntlinux-ntbridge-cell0";
    uint32_t shm_size = NTBRIDGE_SHM_DEFAULT_SIZE;
    int duration_s = 15;
    int device_count = 3;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shm") == 0 && i + 1 < argc) {
            shm_path = argv[++i];
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_s = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--devices") == 0 && i + 1 < argc) {
            device_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: %s [--shm PATH] [--duration SECONDS] [--devices N]\n", argv[0]);
            return 0;
        }
    }

    if (sizeof(ntbridge_shm_header_t) > shm_size) {
        fprintf(stderr, "ntbridge-host: header (%zu bytes) exceeds shm size (%u) — "
                "increase --size or NTBRIDGE_SHM_DEFAULT_SIZE\n",
                sizeof(ntbridge_shm_header_t), shm_size);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int fd = -1;
    ntbridge_shm_header_t *shm = open_shm(shm_path, shm_size, &fd);
    if (!shm)
        return 1;

    int devices_seeded = seed_synthetic_devices(shm, device_count);

    /* Track which of our seeded device_ids the guest side has acked, so
     * the exit status can be a real pass/fail oracle for
     * tests/reactos/run-test.sh, not just "the loop finished". */
    int acked_count = 0;
    char acked[4096] = {0}; /* device_id -> acked, indexed sparsely by (id % sizeof) is enough for this prototype's small ids */

    int64_t start = now_ns();
    int64_t deadline = start + (int64_t)duration_s * 1000000000LL;
    int64_t first_guest_heartbeat_ns = 0;
    uint64_t log_lines_seen = 0;

    printf("ntbridge-host: running for %ds, waiting on guest heartbeat + %d device ack(s)...\n",
           duration_s, devices_seeded);

    while (!g_stop && now_ns() < deadline) {
        shm->host_heartbeat_seq++;
        shm->host_heartbeat_time_ns = now_ns();

        ntbridge_log_entry_t log;
        while (ntbridge_log_ring_try_pop(&shm->log_ring, &log)) {
            log_lines_seen++;
            printf("[ntbridge] [%s] [%s] %s\n",
                   log.source == NTBRIDGE_SIDE_GUEST ? "GUEST" : "HOST",
                   log_level_str(log.level), log.text);
        }

        ntbridge_pnp_ack_t ack;
        while (ntbridge_pnp_ack_ring_try_pop(&shm->pnp_ack_ring, &ack)) {
            unsigned idx = (unsigned)(ack.device_id % sizeof(acked));
            if (!acked[idx]) {
                acked[idx] = 1;
                acked_count++;
            }
            printf("ntbridge-host: device %llu %s (status=%u)\n",
                   (unsigned long long)ack.device_id,
                   ack.action == NTBRIDGE_PNP_DEVICE_ARRIVED ? "ACKED arrival" : "ACKED removal",
                   ack.status);
        }

        uint64_t guest_seq = __atomic_load_n(&shm->guest_heartbeat_seq, __ATOMIC_ACQUIRE);
        if (guest_seq > 0 && first_guest_heartbeat_ns == 0) {
            first_guest_heartbeat_ns = now_ns();
            printf("ntbridge-host: guest heartbeat detected (seq=%llu) after %.3fs\n",
                   (unsigned long long)guest_seq,
                   (double)(first_guest_heartbeat_ns - start) / 1e9);
        }

        struct timespec tick = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 }; /* 100ms poll */
        nanosleep(&tick, NULL);
    }

    printf("ntbridge-host: summary — guest heartbeat: %s, log lines received: %llu, "
           "devices acked: %d/%d\n",
           first_guest_heartbeat_ns ? "yes" : "no",
           (unsigned long long)log_lines_seen, acked_count, devices_seeded);

    munmap(shm, shm_size);
    close(fd);

    int ok = (first_guest_heartbeat_ns != 0) && (acked_count == devices_seeded) && (devices_seeded > 0);
    return ok ? 0 : 1;
}
