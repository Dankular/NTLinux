/*
 * ntabi wire protocol - shared between libntabi (ntabi/lib/) and ntd
 * (ntd/), and the one thing both sides must never disagree about
 * (CLAUDE.md Rule 12: cross-boundary protocols must be versioned).
 *
 * docs/ARCHITECTURE.md section 5's suggested early architecture:
 *
 *   ntdll -> libntabi -> shared-memory request queues -> ntd -> Linux
 *
 * This is that shared-memory request queue, concretely. Rule 13 (prefer
 * shared memory over chatty RPC for hot I/O paths) is why this isn't a
 * socket-based RPC: one POSIX shared memory segment, a lock-free-ish
 * submission ring, and a per-request completion semaphore living in the
 * same segment - no per-call socket round trip, no serialization format
 * beyond flat structs.
 *
 * Scope (Phase 2 / ROADMAP.md): object manager subset - Event, Mutant,
 * Semaphore, handles, waits. Sections, processes/threads, completion
 * ports, APCs are explicitly out of scope here (see ROADMAP.md Phase 3).
 * Two Gen2 simplifications, documented rather than silent:
 *   - One flat handle space per ntd instance, not the real per-process
 *     handle table docs/ARCHITECTURE.md section 6 describes.
 *   - One flat name -> object map, not the real \BaseNamedObjects / \Device
 *     / \Sessions namespace hierarchy section 6 describes.
 * Both are real simplifications to revisit, not accidents.
 */
#ifndef NTLINUX_NTABI_PROTOCOL_H
#define NTLINUX_NTABI_PROTOCOL_H

#include <stdint.h>
#include <semaphore.h>
#include <pthread.h>

/* Bump on any wire-incompatible change to ntabi_request_t/ntabi_response_t
 * or the shm layout. libntabi and ntd both check this at connect time and
 * refuse to talk to a mismatched peer rather than silently corrupting
 * memory - see ntabi_connect() / ntd's accept path. */
#define NTABI_PROTOCOL_VERSION 1u

#define NTABI_SHM_NAME    "/ntlinux-ntabi-v1"
#define NTABI_MAX_SLOTS   64u
#define NTABI_MAX_NAME    128

#define NTABI_MAGIC 0x4e544142u /* "NTAB" */

typedef enum {
    NTABI_OP_NONE = 0,
    NTABI_OP_CREATE_EVENT,
    NTABI_OP_OPEN_EVENT,
    NTABI_OP_CREATE_MUTANT,
    NTABI_OP_OPEN_MUTANT,
    NTABI_OP_CREATE_SEMAPHORE,
    NTABI_OP_OPEN_SEMAPHORE,
    NTABI_OP_SET_EVENT,
    NTABI_OP_RESET_EVENT,
    NTABI_OP_RELEASE_MUTANT,
    NTABI_OP_RELEASE_SEMAPHORE,
    NTABI_OP_WAIT_SINGLE,
    NTABI_OP_CLOSE_HANDLE,
} ntabi_opcode_t;

/* Loosely mirrors NTSTATUS's role (0 = success, distinct nonzero codes for
 * distinct failures) without claiming wire-compatibility with real NT
 * status codes - that mapping is Phase-3+ scope, once this sits behind an
 * actual ntdll boundary rather than a standalone test client. */
typedef enum {
    NTABI_STATUS_SUCCESS = 0,
    NTABI_STATUS_TIMEOUT,
    NTABI_STATUS_INVALID_HANDLE,
    NTABI_STATUS_OBJECT_NAME_NOT_FOUND,
    NTABI_STATUS_OBJECT_NAME_COLLISION,
    NTABI_STATUS_OBJECT_TYPE_MISMATCH,
    NTABI_STATUS_INVALID_PARAMETER,
    NTABI_STATUS_LIMIT_REACHED,
    NTABI_STATUS_INTERNAL_ERROR,
} ntabi_status_t;

typedef struct {
    ntabi_opcode_t opcode;
    int32_t  handle;          /* target handle for ops on an existing object */
    int32_t  initial_state;   /* event: initial signaled (0/1); mutant: initial owner (0/1) */
    int32_t  manual_reset;    /* event only: 0 = auto-reset, 1 = manual-reset */
    int32_t  initial_count;   /* semaphore only */
    int32_t  max_count;       /* semaphore only */
    int32_t  release_count;   /* release semaphore: how much; release mutant: ignored */
    int32_t  timeout_ms;      /* wait: -1 = infinite, 0 = poll, >0 = milliseconds */
    char     name[NTABI_MAX_NAME]; /* empty = unnamed object */
} ntabi_request_t;

typedef struct {
    ntabi_status_t status;
    int32_t  handle;      /* result of create/open */
    int32_t  prev_count;  /* release semaphore: previous count */
} ntabi_response_t;

typedef struct {
    _Atomic uint32_t in_use;
    pid_t    client_pid;
    sem_t    ready;        /* client blocks here until ntd posts the response */
    ntabi_request_t  req;
    ntabi_response_t resp;
} ntabi_slot_t;

/* Single shared-memory segment: header + submission ring + slot table.
 * Clients: allocate a free slot (protected by alloc_lock), fill req, push
 * the slot index into the ring, post doorbell, then sem_wait(slot->ready).
 * ntd: sem_wait(doorbell) [with a bounded timeout so pending waits can
 * still time out even with no new traffic], drain the ring, process each
 * request, sem_post(slot->ready) when a response is ready (immediately for
 * most ops; only after a wait condition is satisfied for WAIT_SINGLE). */
typedef struct {
    uint32_t magic;
    uint32_t protocol_version;
    sem_t    doorbell;
    pthread_mutex_t alloc_lock;
    uint32_t ring[NTABI_MAX_SLOTS];
    uint32_t ring_head; /* ntd consumes here (guarded by alloc_lock) */
    uint32_t ring_tail; /* clients produce here (guarded by alloc_lock) */
    ntabi_slot_t slots[NTABI_MAX_SLOTS];
} ntabi_shm_t;

#endif /* NTLINUX_NTABI_PROTOCOL_H */
