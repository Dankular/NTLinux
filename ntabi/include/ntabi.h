/*
 * libntabi public API - the client side of the ntabi protocol
 * (ntabi/protocol/ntabi_protocol.h). See that header for the wire format
 * and design rationale; see ntd/ntd.c for the daemon that answers these
 * calls.
 *
 * This is what a future ntdll boundary would call (docs/ARCHITECTURE.md
 * section 5's `ntdll -> libntabi -> ntd` chain) - NtCreateEvent and
 * friends would become thin wrappers over these. That wiring into a real
 * ntdll is Phase-3+ scope (see ROADMAP.md); this header and its
 * implementation are usable standalone today, and are exercised that way
 * by ntabi/tests/test_ntabi.c.
 */
#ifndef NTLINUX_NTABI_H
#define NTLINUX_NTABI_H

#include <stdint.h>
#include "ntabi_protocol.h" /* ntabi_status_t */

typedef struct ntabi_conn ntabi_conn_t;

/* Connects to a running ntd instance's shared-memory segment. Fails
 * (returns NULL) if ntd isn't running, or if its protocol_version doesn't
 * match NTABI_PROTOCOL_VERSION this library was built against - a version
 * mismatch is refused, not papered over (Rule 12). */
ntabi_conn_t *ntabi_connect(void);
void ntabi_disconnect(ntabi_conn_t *conn);

/* name may be NULL/empty for an unnamed object. */
ntabi_status_t ntabi_create_event(ntabi_conn_t *c, const char *name,
                                   int initial_state, int manual_reset,
                                   int32_t *out_handle);
ntabi_status_t ntabi_open_event(ntabi_conn_t *c, const char *name,
                                 int32_t *out_handle);
ntabi_status_t ntabi_set_event(ntabi_conn_t *c, int32_t handle);
ntabi_status_t ntabi_reset_event(ntabi_conn_t *c, int32_t handle);

ntabi_status_t ntabi_create_mutant(ntabi_conn_t *c, const char *name,
                                    int initial_owner, int32_t *out_handle);
ntabi_status_t ntabi_open_mutant(ntabi_conn_t *c, const char *name,
                                  int32_t *out_handle);
ntabi_status_t ntabi_release_mutant(ntabi_conn_t *c, int32_t handle);

ntabi_status_t ntabi_create_semaphore(ntabi_conn_t *c, const char *name,
                                       int32_t initial_count, int32_t max_count,
                                       int32_t *out_handle);
ntabi_status_t ntabi_open_semaphore(ntabi_conn_t *c, const char *name,
                                     int32_t *out_handle);
ntabi_status_t ntabi_release_semaphore(ntabi_conn_t *c, int32_t handle,
                                        int32_t release_count,
                                        int32_t *out_prev_count);

/* timeout_ms: -1 = infinite (real blocking wait, no polling), 0 = poll
 * (return immediately either way), >0 = milliseconds. Returns
 * NTABI_STATUS_SUCCESS or NTABI_STATUS_TIMEOUT (or an error). */
ntabi_status_t ntabi_wait_single(ntabi_conn_t *c, int32_t handle, int32_t timeout_ms);

/* wait_all: 0 = return as soon as any one handle is satisfied (result in
 * *out_satisfied_index); 1 = only return once *all* are simultaneously
 * satisfiable, consumed together (*out_satisfied_index is unspecified). */
ntabi_status_t ntabi_wait_multiple(ntabi_conn_t *c, const int32_t *handles, int32_t count,
                                    int wait_all, int32_t timeout_ms, int32_t *out_satisfied_index);

ntabi_status_t ntabi_close_handle(ntabi_conn_t *c, int32_t handle);

/* --- Sections (Phase 3): real shared memory, mapped client-side ---------
 * CreateSection/OpenSection return the backing POSIX shm object's name;
 * ntabi_map_view_of_section() shm_open()s + mmap()s it directly - no
 * further round-trip to ntd, and no fd-passing machinery needed, since
 * the name alone is enough for any process that has it to map the same
 * memory independently. */
typedef struct {
    int32_t  handle;
    int64_t  size;
    char     shm_name[NTABI_MAX_NAME];
} ntabi_section_info_t;

ntabi_status_t ntabi_create_section(ntabi_conn_t *c, const char *name, int64_t size,
                                     ntabi_section_info_t *out);
ntabi_status_t ntabi_open_section(ntabi_conn_t *c, const char *name, ntabi_section_info_t *out);
/* Pure client-side - no daemon round-trip. */
ntabi_status_t ntabi_map_view_of_section(const ntabi_section_info_t *info, void **out_ptr);
ntabi_status_t ntabi_unmap_view_of_section(void *ptr, int64_t size);

/* --- I/O Completion ports (Phase 3) --------------------------------------
 * No real I/O is routed through these yet (no file/socket I/O exists in
 * this prototype) - ntabi_post_completion is directly callable, standing
 * in for what would normally be driven by completed I/O. */
ntabi_status_t ntabi_create_completion(ntabi_conn_t *c, const char *name, int32_t *out_handle);
ntabi_status_t ntabi_open_completion(ntabi_conn_t *c, const char *name, int32_t *out_handle);
ntabi_status_t ntabi_post_completion(ntabi_conn_t *c, int32_t handle, int32_t key,
                                      int64_t bytes, uint64_t overlapped);
ntabi_status_t ntabi_remove_completion(ntabi_conn_t *c, int32_t handle, int32_t timeout_ms,
                                        int32_t *out_key, int64_t *out_bytes, uint64_t *out_overlapped);

/* --- Process objects (Phase 3) -------------------------------------------
 * Signaled (and stays signaled) once target_pid exits - detected via
 * pidfd, so this works for any permitted pid, not just a child of ntd's
 * own process. Thread objects are NOT implemented - see ROADMAP.md
 * Phase 3's "known gap". */
ntabi_status_t ntabi_open_process(ntabi_conn_t *c, int32_t target_pid, int32_t *out_handle);

const char *ntabi_status_string(ntabi_status_t status);

#endif /* NTLINUX_NTABI_H */
