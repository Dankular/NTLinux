/*
 * ntbridge-guest-test — honest stand-in for the ReactOS-side ntbridge
 * guest driver (driver/ntbridge/reactos/), used to prove the ntbridge
 * wire protocol and transport work for real, across a genuine QEMU VM
 * boundary, in a sandbox that cannot build actual ReactOS (no RosBE
 * toolchain reachable here — see driver/ntbridge/reactos/README.md and
 * ROADMAP.md Phase 4's "known gap").
 *
 * This is a small statically-linked Linux userspace program, not a
 * ReactOS/WDM driver. It plays the exact same role a real ntbridge.sys
 * would from the transport's point of view: it finds the ivshmem PCI
 * device, maps its BAR2 (the shared ntbridge_shm_header_t region), and
 * speaks the guest side of the protocol defined in ntbridge_protocol.h
 * — the *same* struct definitions the real ReactOS driver source in
 * driver/ntbridge/reactos/ntbridge_pnp.c targets, so this is a genuine
 * protocol-conformance test, not a mock.
 *
 * Runs as the init process inside a minimal busybox-based initramfs
 * booted directly by QEMU (see tests/reactos/build-testguest-initramfs.sh
 * and driver/cell/launcher/ntcell) — no real ReactOS or even a full
 * Linux distro in the guest, on purpose: the point is to exercise the
 * bridge transport, not to stand up another OS.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ntbridge_protocol.h"

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void log_console(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void log_both(ntbridge_shm_header_t *shm, uint32_t level, const char *text)
{
    log_console("[ntbridge-guest-test] %s", text);
    ntbridge_log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.level = level;
    entry.source = NTBRIDGE_SIDE_GUEST;
    static uint64_t seq = 0;
    entry.seq = ++seq;
    snprintf(entry.text, sizeof(entry.text), "%s", text);
    if (!ntbridge_log_ring_try_push(&shm->log_ring, &entry))
        log_console("[ntbridge-guest-test] WARNING: log_ring full, dropped a line");
}

/*
 * Find the ivshmem PCI device's BAR2 resource file by scanning
 * /sys/bus/pci/devices for vendor=1af4 device=1110 (see
 * NTBRIDGE_IVSHMEM_VENDOR_ID/DEVICE_ID in ntbridge_protocol.h). No
 * kernel driver needs to claim this device — sysfs resource files are
 * directly mmap-able by any process with permission, which is exactly
 * why ivshmem-plain needs no in-guest driver at all for a userspace
 * consumer (the real ReactOS driver instead reaches the same BAR
 * through WDM resource translation in IRP_MN_START_DEVICE, a kernel
 * mechanism this userspace stand-in obviously doesn't use).
 */
static int find_ivshmem_resource2(char *out_path, size_t out_len)
{
    const char *pci_dir = "/sys/bus/pci/devices";
    DIR *d = opendir(pci_dir);
    if (!d) {
        fprintf(stderr, "ntbridge-guest-test: opendir(%s): %s\n", pci_dir, strerror(errno));
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s/vendor", pci_dir, ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        unsigned vendor = 0;
        if (fscanf(f, "%x", &vendor) != 1)
            vendor = 0;
        fclose(f);

        snprintf(path, sizeof(path), "%s/%s/device", pci_dir, ent->d_name);
        f = fopen(path, "r");
        if (!f)
            continue;
        unsigned device = 0;
        if (fscanf(f, "%x", &device) != 1)
            device = 0;
        fclose(f);

        if (vendor == NTBRIDGE_IVSHMEM_VENDOR_ID && device == NTBRIDGE_IVSHMEM_DEVICE_ID) {
            snprintf(out_path, out_len, "%s/%s/resource2", pci_dir, ent->d_name);
            closedir(d);
            return 0;
        }
    }

    closedir(d);
    return -1;
}

int main(int argc, char **argv)
{
    int duration_s = 12;
    if (argc > 1)
        duration_s = atoi(argv[1]);

    log_console("ntbridge-guest-test: locating ivshmem device (vendor=0x%04x device=0x%04x)...",
                NTBRIDGE_IVSHMEM_VENDOR_ID, NTBRIDGE_IVSHMEM_DEVICE_ID);

    char res_path[512];
    /* PCI enumeration can lag slightly behind kernel boot; retry briefly
     * rather than fail on the first attempt. */
    int found = -1;
    for (int attempt = 0; attempt < 50 && found != 0; attempt++) {
        found = find_ivshmem_resource2(res_path, sizeof(res_path));
        if (found != 0)
            usleep(100 * 1000);
    }
    if (found != 0) {
        fprintf(stderr, "ntbridge-guest-test: ivshmem PCI device not found — is ntcell "
                "attaching -device ivshmem-plain?\n");
        return 1;
    }
    log_console("ntbridge-guest-test: found ivshmem BAR2 at %s", res_path);

    int fd = open(res_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ntbridge-guest-test: open(%s): %s\n", res_path, strerror(errno));
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        /* PCI resource files often report size 0 via fstat even though
         * mmap works — fall back to the protocol's known default size. */
        st.st_size = NTBRIDGE_SHM_DEFAULT_SIZE;
    }
    size_t map_size = (size_t)st.st_size;
    if (map_size < NTBRIDGE_SHM_DEFAULT_SIZE)
        map_size = NTBRIDGE_SHM_DEFAULT_SIZE;

    void *map = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "ntbridge-guest-test: mmap(%s, %zu): %s\n",
                res_path, map_size, strerror(errno));
        close(fd);
        return 1;
    }

    ntbridge_shm_header_t *shm = (ntbridge_shm_header_t *)map;

    if (shm->magic != NTBRIDGE_MAGIC) {
        fprintf(stderr, "ntbridge-guest-test: bad magic 0x%08x (expected 0x%08x) — "
                "host side hasn't initialized the region yet, or layout mismatch\n",
                shm->magic, NTBRIDGE_MAGIC);
        munmap(map, map_size);
        close(fd);
        return 1;
    }
    if (shm->version != NTBRIDGE_PROTOCOL_VERSION) {
        fprintf(stderr, "ntbridge-guest-test: protocol version mismatch: region is v%u, "
                "this build is v%u — refusing to proceed (Rule 12)\n",
                shm->version, NTBRIDGE_PROTOCOL_VERSION);
        munmap(map, map_size);
        close(fd);
        return 1;
    }

    log_console("ntbridge-guest-test: attached to v%u ntbridge region, running for %ds",
                shm->version, duration_s);
    log_both(shm, NTBRIDGE_LOG_INFO, "guest online: ntbridge-guest-test attached");

    int64_t start = now_ns();
    int64_t deadline = start + (int64_t)duration_s * 1000000000LL;
    int devices_seen = 0;

    while (now_ns() < deadline) {
        shm->guest_heartbeat_seq++;
        shm->guest_heartbeat_time_ns = now_ns();

        ntbridge_pnp_descriptor_t dev;
        while (ntbridge_pnp_ring_try_pop(&shm->pnp_ring, &dev)) {
            devices_seen++;
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "PnP: %s device_id=%llu hwid=%s name=\"%s\" at %s",
                     dev.action == NTBRIDGE_PNP_DEVICE_ARRIVED ? "ARRIVED" : "REMOVED",
                     (unsigned long long)dev.device_id, dev.hardware_id,
                     dev.friendly_name, dev.location);
            log_both(shm, NTBRIDGE_LOG_INFO, msg);

            /* Real ReactOS side: IoReportDetectedDevice / PDO creation
             * happens here before acking. This stand-in just validates
             * the descriptor is well-formed and acks — enough to prove
             * the round trip, not enough to claim PnP semantics. */
            ntbridge_pnp_ack_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.device_id = dev.device_id;
            ack.action = dev.action;
            ack.status = 0;
            if (!ntbridge_pnp_ack_ring_try_push(&shm->pnp_ack_ring, &ack))
                log_console("ntbridge-guest-test: WARNING: pnp_ack_ring full");
        }

        struct timespec tick = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
        nanosleep(&tick, NULL);
    }

    char summary[256];
    snprintf(summary, sizeof(summary), "guest shutting down: saw %d device(s), heartbeat seq=%llu",
             devices_seen, (unsigned long long)shm->guest_heartbeat_seq);
    log_both(shm, NTBRIDGE_LOG_INFO, summary);

    munmap(map, map_size);
    close(fd);
    return 0;
}
