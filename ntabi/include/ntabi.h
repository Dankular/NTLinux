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
 * own process. */
ntabi_status_t ntabi_open_process(ntabi_conn_t *c, int32_t target_pid, int32_t *out_handle);

/* --- Thread objects + APCs (Phase 12) ------------------------------------
 * Signaled (and stays signaled) once the *specific* (target_pid,
 * target_tid) thread exits - detected via /proc/<pid>/task/<tid>, not
 * pidfd (pidfd_open(2) only accepts thread-group-leader pids). Returns
 * NTABI_STATUS_THREAD_NOT_FOUND for a (pid,tid) that doesn't exist at
 * call time.
 *
 * ntabi_queue_apc appends one APC packet to the target thread's queue.
 * It is only ever observed by that thread's own next call to
 * ntabi_wait_single_alertable - never by interrupting a wait already in
 * progress (Design A, documented in ntd/ntd.c). A plain ntabi_wait_single
 * is never interrupted by a pending APC, alertable or not.
 *
 * ntabi_suspend_thread/ntabi_resume_thread are real integer accounting
 * (matching real NT's "report the count before this call" convention,
 * clamped at 0) - they do NOT freeze the target thread's actual CPU
 * execution (that needs ptrace, not attempted here; stated precisely).
 */
typedef struct {
    uint64_t routine;
    uint64_t arg1, arg2, arg3;
} ntabi_apc_info_t;

ntabi_status_t ntabi_open_thread(ntabi_conn_t *c, int32_t target_pid, int32_t target_tid,
                                  int32_t *out_handle);
ntabi_status_t ntabi_queue_apc(ntabi_conn_t *c, int32_t thread_handle,
                                uint64_t routine, uint64_t arg1, uint64_t arg2, uint64_t arg3);
ntabi_status_t ntabi_suspend_thread(ntabi_conn_t *c, int32_t thread_handle, int32_t *out_prev_count);
ntabi_status_t ntabi_resume_thread(ntabi_conn_t *c, int32_t thread_handle, int32_t *out_prev_count);

/* Like ntabi_wait_single, but alertable: if the calling thread (identified
 * by this process's pid and this OS thread's tid, set on every request -
 * see ntabi_protocol.h's client_tid) has a pending APC, this returns
 * NTABI_STATUS_USER_APC immediately with *out_apc filled in and the
 * target handle's object untouched - it is not consumed, not even if it
 * was simultaneously available. */
ntabi_status_t ntabi_wait_single_alertable(ntabi_conn_t *c, int32_t handle, int32_t timeout_ms,
                                            ntabi_apc_info_t *out_apc);

const char *ntabi_status_string(ntabi_status_t status);

#endif /* NTLINUX_NTABI_H */
