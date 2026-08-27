/*
 * ntd - NT userspace service daemon (Phase 2 / ROADMAP.md scope: the
 * object-manager subset only - Event, Mutant, Semaphore, handles, waits.
 * Registry (ntd/registry/), services (ntd/services/), security
 * (ntd/security/), RPC (ntd/rpc/) are untouched - see their own READMEs.
 *
 * Owns the ntabi shared-memory segment (ntabi/protocol/ntabi_protocol.h)
 * and answers requests from any number of libntabi clients. Single-
 * threaded, event-driven: one loop, no worker threads, no per-object
 * locking needed because only this one thread ever touches object state.
 *
 * Two Gen2 simplifications carried over from the protocol header, restated
 * here where they're actually implemented:
 *   - One flat handle space, not a real per-process handle table. A
 *     "handle" here is really the object's own identity - two opens of
 *     the same named object return the *same* handle number, not two
 *     distinct handle values referencing one object the way real NT
 *     handle tables work. Correct for testing object *semantics*
 *     (signaling, waiting, naming), not a claim of real handle-table
 *     isolation - that's Phase 3+ scope (docs/ARCHITECTURE.md section 6).
 *   - One flat name -> object map, not the \BaseNamedObjects / \Device /
 *     \Sessions namespace hierarchy section 6 describes.
 */
#define _GNU_SOURCE
#include "ntabi_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef enum { OBJ_EVENT, OBJ_MUTANT, OBJ_SEMAPHORE } obj_type_t;

typedef struct waiter {
    uint32_t slot_idx;
    int has_deadline;
    struct timespec deadline;
    struct waiter *next;
} waiter_t;

typedef struct nt_object {
    int32_t handle;
    obj_type_t type;
    char name[NTABI_MAX_NAME];
    int refcount;

    /* event */
    int signaled;
    int manual_reset;
    /* mutant */
    int owned;
    pid_t owner_pid;
    /* semaphore */
    int count;
    int max_count;

    waiter_t *waiters;
    struct nt_object *next;
} nt_object_t;

static ntabi_shm_t *g_shm;
static nt_object_t *g_objects;
static int32_t g_next_handle = 1;
static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) { (void)sig; g_running = 0; }

static void complete(uint32_t slot_idx, ntabi_status_t status, int32_t handle, int32_t prev_count) {
    ntabi_slot_t *slot = &g_shm->slots[slot_idx];
    slot->resp.status = status;
    slot->resp.handle = handle;
    slot->resp.prev_count = prev_count;
    sem_post(&slot->ready);
}

static nt_object_t *find_by_name(const char *name) {
    if (!name[0]) return NULL;
    for (nt_object_t *o = g_objects; o; o = o->next)
        if (strcmp(o->name, name) == 0) return o;
    return NULL;
}

static nt_object_t *find_by_handle(int32_t handle) {
    for (nt_object_t *o = g_objects; o; o = o->next)
        if (o->handle == handle) return o;
    return NULL;
}

static nt_object_t *create_object(obj_type_t type, const char *name) {
    nt_object_t *o = calloc(1, sizeof(*o));
    o->handle = g_next_handle++;
    o->type = type;
    snprintf(o->name, sizeof(o->name), "%s", name ? name : "");
    o->refcount = 1;
    o->next = g_objects;
    g_objects = o;
    return o;
}

static void deadline_from_timeout(struct timespec *ts, int32_t timeout_ms) {
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += timeout_ms / 1000;
    ts->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec += 1; ts->tv_nsec -= 1000000000L; }
}

static void add_waiter(nt_object_t *o, uint32_t slot_idx, int32_t timeout_ms) {
    waiter_t *w = calloc(1, sizeof(*w));
    w->slot_idx = slot_idx;
    if (timeout_ms >= 0) {
        w->has_deadline = 1;
        deadline_from_timeout(&w->deadline, timeout_ms);
    }
    /* FIFO: append at tail so waiters are satisfied in arrival order. */
    if (!o->waiters) { o->waiters = w; return; }
    waiter_t *p = o->waiters;
    while (p->next) p = p->next;
    p->next = w;
}

/* Tries to satisfy exactly one pending WAIT_SINGLE against this object's
 * *current* state, for object types where satisfying one waiter changes
 * state for the next one (auto-reset event, mutant, semaphore). Manual-
 * reset events are handled separately (wake everyone, state doesn't
 * change per-waiter). Returns 1 if a waiter was satisfied. */
static int satisfy_one_waiter(nt_object_t *o) {
    if (!o->waiters) return 0;
    switch (o->type) {
        case OBJ_EVENT:
            if (!o->signaled) return 0;
            break;
        case OBJ_MUTANT:
            if (o->owned) return 0;
            break;
        case OBJ_SEMAPHORE:
            if (o->count <= 0) return 0;
            break;
    }
    waiter_t *w = o->waiters;
    o->waiters = w->next;

    switch (o->type) {
        case OBJ_EVENT:
            o->signaled = 0; /* auto-reset: consumed by this waiter */
            complete(w->slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
            break;
        case OBJ_MUTANT:
            o->owned = 1;
            o->owner_pid = g_shm->slots[w->slot_idx].client_pid;
            complete(w->slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
            break;
        case OBJ_SEMAPHORE:
            o->count--;
            complete(w->slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
            break;
    }
    free(w);
    return 1;
}

static void wake_all_waiters_success(nt_object_t *o) {
    waiter_t *w = o->waiters;
    o->waiters = NULL;
    while (w) {
        waiter_t *next = w->next;
        complete(w->slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
        free(w);
        w = next;
    }
}

static void handle_create_event(ntabi_request_t *req, uint32_t slot_idx) {
    if (req->name[0] && find_by_name(req->name)) {
        complete(slot_idx, NTABI_STATUS_OBJECT_NAME_COLLISION, 0, 0);
        return;
    }
    nt_object_t *o = create_object(OBJ_EVENT, req->name);
    o->manual_reset = req->manual_reset;
    o->signaled = req->initial_state ? 1 : 0;
    complete(slot_idx, NTABI_STATUS_SUCCESS, o->handle, 0);
}

static void handle_open_typed(ntabi_request_t *req, uint32_t slot_idx, obj_type_t type) {
    nt_object_t *o = find_by_name(req->name);
    if (!o) { complete(slot_idx, NTABI_STATUS_OBJECT_NAME_NOT_FOUND, 0, 0); return; }
    if (o->type != type) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    o->refcount++;
    complete(slot_idx, NTABI_STATUS_SUCCESS, o->handle, 0);
}

static void handle_set_event(ntabi_request_t *req, uint32_t slot_idx) {
    nt_object_t *o = find_by_handle(req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_EVENT) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }

    o->signaled = 1;
    if (o->manual_reset) {
        wake_all_waiters_success(o); /* stays signaled */
    } else {
        satisfy_one_waiter(o); /* consumes the signal if anyone was waiting */
    }
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void handle_reset_event(ntabi_request_t *req, uint32_t slot_idx) {
    nt_object_t *o = find_by_handle(req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_EVENT) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    o->signaled = 0;
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void handle_create_mutant(ntabi_request_t *req, uint32_t slot_idx) {
    if (req->name[0] && find_by_name(req->name)) {
        complete(slot_idx, NTABI_STATUS_OBJECT_NAME_COLLISION, 0, 0);
        return;
    }
    nt_object_t *o = create_object(OBJ_MUTANT, req->name);
    o->owned = req->initial_state ? 1 : 0;
    if (o->owned) o->owner_pid = g_shm->slots[slot_idx].client_pid;
    complete(slot_idx, NTABI_STATUS_SUCCESS, o->handle, 0);
}

static void handle_release_mutant(ntabi_request_t *req, uint32_t slot_idx) {
    nt_object_t *o = find_by_handle(req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_MUTANT) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    /* Gen2 simplification (see file header): no recursion counter: any
     * release fully releases, ownership isn't strictly enforced against
     * the calling pid beyond this check. */
    if (!o->owned) { complete(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0); return; }
    o->owned = 0;
    satisfy_one_waiter(o);
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void handle_create_semaphore(ntabi_request_t *req, uint32_t slot_idx) {
    if (req->max_count <= 0 || req->initial_count < 0 || req->initial_count > req->max_count) {
        complete(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0);
        return;
    }
    if (req->name[0] && find_by_name(req->name)) {
        complete(slot_idx, NTABI_STATUS_OBJECT_NAME_COLLISION, 0, 0);
        return;
    }
    nt_object_t *o = create_object(OBJ_SEMAPHORE, req->name);
    o->count = req->initial_count;
    o->max_count = req->max_count;
    complete(slot_idx, NTABI_STATUS_SUCCESS, o->handle, 0);
}

static void handle_release_semaphore(ntabi_request_t *req, uint32_t slot_idx) {
    nt_object_t *o = find_by_handle(req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_SEMAPHORE) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    if (req->release_count <= 0 || o->count + req->release_count > o->max_count) {
        complete(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0);
        return;
    }
    int32_t prev = o->count;
    o->count += req->release_count;
    while (satisfy_one_waiter(o)) { /* wake as many as the new count allows */ }
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, prev);
}

static void handle_wait_single(ntabi_request_t *req, uint32_t slot_idx) {
    nt_object_t *o = find_by_handle(req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }

    int can_satisfy_now = 0;
    switch (o->type) {
        case OBJ_EVENT:     can_satisfy_now = o->signaled; break;
        case OBJ_MUTANT:    can_satisfy_now = !o->owned; break;
        case OBJ_SEMAPHORE: can_satisfy_now = o->count > 0; break;
    }

    if (can_satisfy_now) {
        switch (o->type) {
            case OBJ_EVENT: if (!o->manual_reset) o->signaled = 0; break;
            case OBJ_MUTANT: o->owned = 1; o->owner_pid = g_shm->slots[slot_idx].client_pid; break;
            case OBJ_SEMAPHORE: o->count--; break;
        }
        complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
        return;
    }

    if (req->timeout_ms == 0) {
        complete(slot_idx, NTABI_STATUS_TIMEOUT, 0, 0);
        return;
    }

    add_waiter(o, slot_idx, req->timeout_ms); /* -1 = no deadline (infinite wait) */
}

static void handle_close_handle(ntabi_request_t *req, uint32_t slot_idx) {
    nt_object_t **pp = &g_objects;
    while (*pp && (*pp)->handle != req->handle) pp = &(*pp)->next;
    if (!*pp) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }

    nt_object_t *o = *pp;
    o->refcount--;
    if (o->refcount <= 0) {
        /* Any lingering waiters at this point mean a client is waiting on
         * a handle nothing will ever signal again - fail them rather than
         * leaking their slots forever. */
        waiter_t *w = o->waiters;
        while (w) { waiter_t *n = w->next; complete(w->slot_idx, NTABI_STATUS_TIMEOUT, 0, 0); free(w); w = n; }
        *pp = o->next;
        free(o);
    }
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void process_request(uint32_t slot_idx) {
    ntabi_request_t req = g_shm->slots[slot_idx].req; /* copy: only ntd touches this after dequeue */
    switch (req.opcode) {
        case NTABI_OP_CREATE_EVENT:     handle_create_event(&req, slot_idx); break;
        case NTABI_OP_OPEN_EVENT:       handle_open_typed(&req, slot_idx, OBJ_EVENT); break;
        case NTABI_OP_SET_EVENT:        handle_set_event(&req, slot_idx); break;
        case NTABI_OP_RESET_EVENT:      handle_reset_event(&req, slot_idx); break;
        case NTABI_OP_CREATE_MUTANT:    handle_create_mutant(&req, slot_idx); break;
        case NTABI_OP_OPEN_MUTANT:      handle_open_typed(&req, slot_idx, OBJ_MUTANT); break;
        case NTABI_OP_RELEASE_MUTANT:   handle_release_mutant(&req, slot_idx); break;
        case NTABI_OP_CREATE_SEMAPHORE: handle_create_semaphore(&req, slot_idx); break;
        case NTABI_OP_OPEN_SEMAPHORE:   handle_open_typed(&req, slot_idx, OBJ_SEMAPHORE); break;
        case NTABI_OP_RELEASE_SEMAPHORE:handle_release_semaphore(&req, slot_idx); break;
        case NTABI_OP_WAIT_SINGLE:      handle_wait_single(&req, slot_idx); break;
        case NTABI_OP_CLOSE_HANDLE:     handle_close_handle(&req, slot_idx); break;
        default:
            complete(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0);
    }
}

static int timespec_past(const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec != deadline->tv_sec) return now.tv_sec > deadline->tv_sec;
    return now.tv_nsec >= deadline->tv_nsec;
}

/* Walks every object's waiter list, timing out anyone whose deadline has
 * passed. This is the reason ntd's main loop wakes periodically even with
 * no new requests (see sem_timedwait below) - without a poll tick,
 * nothing would ever notice a wait's timeout expired. A real
 * implementation would use a timer wheel instead of an O(objects*waiters)
 * sweep; fine for a Phase 2 prototype's object counts. */
static void expire_timeouts(void) {
    for (nt_object_t *o = g_objects; o; o = o->next) {
        waiter_t **pp = &o->waiters;
        while (*pp) {
            waiter_t *w = *pp;
            if (w->has_deadline && timespec_past(&w->deadline)) {
                *pp = w->next;
                complete(w->slot_idx, NTABI_STATUS_TIMEOUT, 0, 0);
                free(w);
            } else {
                pp = &w->next;
            }
        }
    }
}

static ntabi_shm_t *create_shm(void) {
    shm_unlink(NTABI_SHM_NAME); /* clean up a stale segment from a prior crashed run */
    int fd = shm_open(NTABI_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) { perror("ntd: shm_open"); exit(1); }
    if (ftruncate(fd, sizeof(ntabi_shm_t)) != 0) { perror("ntd: ftruncate"); exit(1); }

    ntabi_shm_t *shm = mmap(NULL, sizeof(ntabi_shm_t), PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
    close(fd);
    if (shm == MAP_FAILED) { perror("ntd: mmap"); exit(1); }

    memset(shm, 0, sizeof(*shm));
    shm->magic = NTABI_MAGIC;
    shm->protocol_version = NTABI_PROTOCOL_VERSION;

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->alloc_lock, &mattr);
    pthread_mutexattr_destroy(&mattr);

    sem_init(&shm->doorbell, 1 /* pshared */, 0);
    for (uint32_t i = 0; i < NTABI_MAX_SLOTS; i++)
        sem_init(&shm->slots[i].ready, 1 /* pshared */, 0);

    return shm;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    g_shm = create_shm();
    fprintf(stderr, "ntd: listening on %s (protocol v%u), pid %d\n",
            NTABI_SHM_NAME, NTABI_PROTOCOL_VERSION, getpid());

    while (g_running) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50 * 1000000L; /* 50ms tick, bounds worst-case timeout latency */
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }

        int r = sem_timedwait(&g_shm->doorbell, &ts);
        if (r != 0 && errno != ETIMEDOUT && errno != EINTR) {
            perror("ntd: sem_timedwait");
            break;
        }

        for (;;) {
            pthread_mutex_lock(&g_shm->alloc_lock);
            if (g_shm->ring_head == g_shm->ring_tail) {
                pthread_mutex_unlock(&g_shm->alloc_lock);
                break;
            }
            uint32_t idx = g_shm->ring[g_shm->ring_head % NTABI_MAX_SLOTS];
            g_shm->ring_head++;
            pthread_mutex_unlock(&g_shm->alloc_lock);
            process_request(idx);
        }

        expire_timeouts();
    }

    fprintf(stderr, "ntd: shutting down\n");
    munmap(g_shm, sizeof(ntabi_shm_t));
    shm_unlink(NTABI_SHM_NAME);
    return 0;
}
