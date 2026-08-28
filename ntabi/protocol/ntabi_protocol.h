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
 * --- Version history ---
 * v1 (Phase 2): Event, Mutant, Semaphore. One flat handle space, one flat
 *   name->object map (documented simplifications).
 * v2 (Phase 3): adds wait-any/wait-all, Section (real shared memory,
 *   mapped client-side - see ntabi_map_view_of_section), I/O Completion
 *   ports, Process objects (signaled on exit, via pidfd). Replaces the
 *   flat handle space with real per-process handle tables (keyed by the
 *   client's pid) - the flat name->object map remains (still a documented
 *   simplification; see ntd/namespace/README.md).
 *   Correctness hardening carried into v2: NTABI_SHM_NAME now embeds
 *   NTABI_PROTOCOL_VERSION directly, so a version-mismatched client can't
 *   even successfully mmap a wrong-sized segment before reaching the
 *   explicit version check - v1 changed the *name* only informally
 *   (matching the version by convention, not by construction); a v1
 *   client mmap-ing a v2-sized segment (or vice versa) before the version
 *   field was checked was a latent read-past-the-mapping risk. Threads
 *   and APCs are explicitly not covered - see ROADMAP.md Phase 3's "known
 *   gap" for why.
 * v3 (Phase 12): adds Thread objects (OpenThread, signaled on the
 *   *specific* target thread exiting - not the whole process - detected
 *   by polling /proc/<pid>/task/<tid> existence, since pidfd_open(2) only
 *   works on whole thread-group-leader pids, not arbitrary thread IDs)
 *   and APC queueing/alertable-wait delivery timing (QueueApc + a new
 *   `alertable` flag on WAIT_SINGLE - a pending APC short-circuits an
 *   alertable wait with NTABI_STATUS_USER_APC before the wait is even
 *   attempted against the target object). New `client_tid` on the shared
 *   slot (alongside the existing `client_pid`) identifies which thread
 *   issued a request - needed for APC delivery to know "am I the thread
 *   this APC was queued to". Real, tested semantics; NOT a claim that
 *   ntd actually executes an APC routine in the target thread's context
 *   (needs real ntdll/thread-execution integration this prototype
 *   doesn't have) or that SuspendThread/ResumeThread actually freeze
 *   real CPU execution (accounting only - real per-thread suspend needs
 *   ptrace, a separate, larger, permission-sensitive undertaking not
 *   attempted here). See ntd/README.md and ROADMAP.md Phase 12.
 */
#ifndef NTLINUX_NTABI_PROTOCOL_H
#define NTLINUX_NTABI_PROTOCOL_H

#include <stdint.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/types.h>

/* Bump on any wire-incompatible change to ntabi_request_t/ntabi_response_t
 * or the shm layout. libntabi and ntd both check this at connect time and
 * refuse to talk to a mismatched peer rather than silently corrupting
 * memory - see ntabi_connect() / ntd's accept path. */
/* No 'u' suffix: NTABI_SHM_NAME below stringifies this token directly via
 * the preprocessor (#x stringifies the raw token, not its evaluated
 * value), so a 'u' suffix here would literally end up in the shm object's
 * *name* ("/ntlinux-ntabi-v2u") - caught by actually starting ntd and
 * noticing "v2u" in its own startup banner, not by inspection. The bare
 * decimal constant still converts to uint32_t/ntabi_shm_t.protocol_version
 * without any warning or ambiguity - the suffix bought nothing here. */
#define NTABI_PROTOCOL_VERSION 3

#define NTABI_STRINGIFY_(x) #x
#define NTABI_STRINGIFY(x) NTABI_STRINGIFY_(x)
#define NTABI_SHM_NAME "/ntlinux-ntabi-v" NTABI_STRINGIFY(NTABI_PROTOCOL_VERSION)

#define NTABI_MAX_SLOTS   64u
#define NTABI_MAX_NAME    128
#define NTABI_MAX_WAIT_HANDLES 16 /* real NT's MAXIMUM_WAIT_OBJECTS is 64; 16 is a documented prototype-scoped limit */

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
    /* --- v2 / Phase 3 --- */
    NTABI_OP_WAIT_MULTIPLE,
    NTABI_OP_CREATE_SECTION,
    NTABI_OP_OPEN_SECTION,
    NTABI_OP_CREATE_COMPLETION,
    NTABI_OP_OPEN_COMPLETION,
    NTABI_OP_POST_COMPLETION,
    NTABI_OP_REMOVE_COMPLETION,
    NTABI_OP_OPEN_PROCESS,
    /* --- v3 / Phase 12 --- */
    NTABI_OP_OPEN_THREAD,
    NTABI_OP_QUEUE_APC,
    NTABI_OP_SUSPEND_THREAD,
    NTABI_OP_RESUME_THREAD,
} ntabi_opcode_t;

/* Loosely mirrors NTSTATUS's role (0 = success, distinct nonzero codes for
 * distinct failures) without claiming wire-compatibility with real NT
 * status codes - that mapping is Phase-4+ scope, once this sits behind an
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
    NTABI_STATUS_PROCESS_NOT_FOUND, /* v2: OpenProcess on a pid that doesn't exist (or already reaped) */
    NTABI_STATUS_THREAD_NOT_FOUND,  /* v3: OpenThread on a (pid,tid) that doesn't exist */
    NTABI_STATUS_USER_APC,          /* v3: an alertable wait was interrupted by a pending APC, not satisfied by the waited-on object */
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

    /* --- v2 / Phase 3 --- */
    int32_t  handles[NTABI_MAX_WAIT_HANDLES]; /* WAIT_MULTIPLE */
    int32_t  handle_count;                    /* WAIT_MULTIPLE: how many of handles[] are valid */
    int32_t  wait_all;                        /* WAIT_MULTIPLE: 0 = wait-any, 1 = wait-all */
    int64_t  section_size;                    /* CREATE_SECTION */
    int32_t  completion_key;                  /* POST_COMPLETION */
    int64_t  completion_bytes;                /* POST_COMPLETION */
    uint64_t completion_overlapped;           /* POST_COMPLETION */
    int32_t  target_pid;                      /* OPEN_PROCESS */

    /* --- v3 / Phase 12 --- */
    int32_t  target_tid;      /* OPEN_THREAD: the Linux TID to track, alongside target_pid */
    int32_t  alertable;       /* WAIT_SINGLE: 1 = an alertable wait - a pending APC on the calling (client_pid, client_tid) thread short-circuits this wait with NTABI_STATUS_USER_APC before the wait is attempted */
    uint64_t apc_routine;     /* QUEUE_APC */
    uint64_t apc_arg1;        /* QUEUE_APC */
    uint64_t apc_arg2;        /* QUEUE_APC */
    uint64_t apc_arg3;        /* QUEUE_APC */
} ntabi_request_t;

typedef struct {
    ntabi_status_t status;
    int32_t  handle;      /* result of create/open */
    int32_t  prev_count;  /* release semaphore: previous count */

    /* --- v2 / Phase 3 --- */
    int32_t  satisfied_index;          /* WAIT_MULTIPLE (wait-any): which handles[] index; -1 for wait-all */
    int64_t  section_size;             /* CREATE_SECTION / OPEN_SECTION: actual size */
    char     shm_name[NTABI_MAX_NAME]; /* CREATE_SECTION / OPEN_SECTION: POSIX shm object to map client-side */
    int32_t  completion_key;           /* REMOVE_COMPLETION */
    int64_t  completion_bytes;         /* REMOVE_COMPLETION */
    uint64_t completion_overlapped;    /* REMOVE_COMPLETION */

    /* --- v3 / Phase 12 --- */
    int32_t  prev_suspend_count;       /* SUSPEND_THREAD / RESUME_THREAD: the count *before* this call's adjustment, matching real NT's SuspendThread/ResumeThread return value convention */
    uint64_t apc_routine;              /* WAIT_SINGLE when status == NTABI_STATUS_USER_APC: the delivered APC's routine/args, dequeued from the calling thread's pending list */
    uint64_t apc_arg1;
    uint64_t apc_arg2;
    uint64_t apc_arg3;
} ntabi_response_t;

typedef struct {
    _Atomic uint32_t in_use;
    pid_t    client_pid;
    pid_t    client_tid;   /* v3/Phase 12: which thread of client_pid issued this request - needed for APC delivery ("is this my own pending APC queue?") */
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
 * most ops; only after a wait condition is satisfied for WAIT_SINGLE/
 * WAIT_MULTIPLE/REMOVE_COMPLETION). */
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
