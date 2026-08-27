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

ntabi_status_t ntabi_close_handle(ntabi_conn_t *c, int32_t handle);

const char *ntabi_status_string(ntabi_status_t status);

#endif /* NTLINUX_NTABI_H */
