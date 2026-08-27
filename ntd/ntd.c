/*
 * ntd - NT userspace service daemon.
 *
 * Phase 2 scope: Event, Mutant, Semaphore, single-object waits, one flat
 * handle space (documented simplification).
 * Phase 3 scope (this version, protocol v2): adds wait-any/wait-all,
 * Section (real POSIX-shared-memory-backed, mapped client-side), I/O
 * Completion ports, Process objects (signaled on exit via pidfd) - and
 * replaces the flat handle space with real per-process handle tables.
 *
 * Registry (ntd/registry/), services (ntd/services/), security
 * (ntd/security/), RPC (ntd/rpc/) remain untouched - see their READMEs.
 * Thread objects and APCs are NOT implemented - see ROADMAP.md Phase 3's
 * "known gap" for why (both need real per-thread execution-context
 * integration this prototype has no way to provide without real ntdll
 * integration, which is Phase 2's own still-open gap).
 *
 * Single-threaded, event-driven: one loop, no worker threads. Object
 * state, handle tables, and multi-wait bookkeeping need no locking beyond
 * what's already needed for the shm ring (alloc_lock) because only this
 * one thread ever touches any of it.
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
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

/* pidfd_open() has a glibc wrapper only since glibc 2.36; call the raw
 * syscall instead so this builds against older glibc too (ADR-0001: the
 * NT Runtime stays host-agnostic, not tied to whatever this dev sandbox
 * happens to ship). */
static int ntd_pidfd_open(pid_t pid, unsigned int flags) {
    return (int)syscall(SYS_pidfd_open, pid, flags);
}

typedef enum { OBJ_EVENT, OBJ_MUTANT, OBJ_SEMAPHORE, OBJ_SECTION, OBJ_COMPLETION, OBJ_PROCESS } obj_type_t;

typedef struct multiwait {
    uint32_t slot_idx;
    int wait_all;
    int32_t handles[NTABI_MAX_WAIT_HANDLES]; /* per-pid handle values, for satisfied_index reporting */
    struct nt_object *objects[NTABI_MAX_WAIT_HANDLES];
    int handle_count;
    int pending_refs;   /* how many objects still hold a waiter_t pointing at this multiwait */
    int completed;      /* guards against responding/consuming twice when several objects change at once */
} multiwait_t;

typedef struct waiter {
    uint32_t slot_idx;         /* meaningful only when multiwait == NULL */
    multiwait_t *multiwait;    /* NULL for a plain single-object WAIT_SINGLE waiter */
    int has_deadline;
    struct timespec deadline;
    struct waiter *next;
} waiter_t;

typedef struct completion_packet {
    int32_t key;
    int64_t bytes;
    uint64_t overlapped;
    struct completion_packet *next;
} completion_packet_t;

typedef struct nt_object {
    int32_t id; /* internal identity, never sent on the wire directly - see handle_entry_t */
    obj_type_t type;
    char name[NTABI_MAX_NAME];
    int refcount; /* one unit per outstanding (pid, handle) referencing this object */

    /* event */
    int signaled;
    int manual_reset;
    /* mutant */
    int owned;
    pid_t owner_pid;
    /* semaphore */
    int count;
    int max_count;
    /* section */
    int64_t section_size;
    char shm_name[NTABI_MAX_NAME];
    /* completion port */
    completion_packet_t *queue_head, *queue_tail;
    /* process */
    pid_t target_pid;
    int pidfd;
    int process_exited;

    waiter_t *waiters;
    struct nt_object *next;
} nt_object_t;

/* Real per-process handle table (Phase 3 - replaces Phase 2's flat handle
 * space): a (owner_pid, local_handle) pair identifies exactly one
 * reference to one object. Two different pids can each hold "handle 1"
 * referring to two completely different objects; closing one pid's
 * handle never affects another pid's numerically-identical handle. */
typedef struct handle_entry {
    pid_t owner_pid;
    int32_t local_handle;
    nt_object_t *object;
    struct handle_entry *next;
} handle_entry_t;

static ntabi_shm_t *g_shm;
static nt_object_t *g_objects;
static handle_entry_t *g_handles;
static int32_t g_next_object_id = 1;
static int32_t g_next_section_id = 1;
static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) { (void)sig; g_running = 0; }

static void complete(uint32_t slot_idx, ntabi_status_t status, int32_t handle, int32_t prev_count) {
    ntabi_slot_t *slot = &g_shm->slots[slot_idx];
    slot->resp.status = status;
    slot->resp.handle = handle;
    slot->resp.prev_count = prev_count;
    sem_post(&slot->ready);
}

static void complete_wait_multiple(uint32_t slot_idx, ntabi_status_t status, int32_t satisfied_index) {
    ntabi_slot_t *slot = &g_shm->slots[slot_idx];
    slot->resp.status = status;
    slot->resp.satisfied_index = satisfied_index;
    sem_post(&slot->ready);
}

static void complete_section(uint32_t slot_idx, ntabi_status_t status, int32_t handle,
                              int64_t size, const char *shm_name) {
    ntabi_slot_t *slot = &g_shm->slots[slot_idx];
    slot->resp.status = status;
    slot->resp.handle = handle;
    slot->resp.section_size = size;
    if (shm_name) snprintf(slot->resp.shm_name, sizeof(slot->resp.shm_name), "%s", shm_name);
    sem_post(&slot->ready);
}

static void complete_io(uint32_t slot_idx, ntabi_status_t status, int32_t key, int64_t bytes, uint64_t overlapped) {
    ntabi_slot_t *slot = &g_shm->slots[slot_idx];
    slot->resp.status = status;
    slot->resp.completion_key = key;
    slot->resp.completion_bytes = bytes;
    slot->resp.completion_overlapped = overlapped;
    sem_post(&slot->ready);
}

/* --- handle table ------------------------------------------------------- */

static int32_t alloc_local_handle(pid_t pid) {
    int32_t max_seen = 0;
    for (handle_entry_t *h = g_handles; h; h = h->next)
        if (h->owner_pid == pid && h->local_handle > max_seen) max_seen = h->local_handle;
    return max_seen + 1;
}

static int32_t register_handle(pid_t pid, nt_object_t *obj) {
    handle_entry_t *h = calloc(1, sizeof(*h));
    h->owner_pid = pid;
    h->local_handle = alloc_local_handle(pid);
    h->object = obj;
    h->next = g_handles;
    g_handles = h;
    obj->refcount++;
    return h->local_handle;
}

static nt_object_t *find_handle(pid_t pid, int32_t local_handle) {
    for (handle_entry_t *h = g_handles; h; h = h->next)
        if (h->owner_pid == pid && h->local_handle == local_handle) return h->object;
    return NULL;
}

/* Removes the (pid, local_handle) entry and returns the object it pointed
 * at (NULL if no such entry - i.e. an invalid handle, possibly one that
 * belongs to a *different* pid, which is exactly the isolation this table
 * exists to enforce). Does not itself free the object - caller decides. */
static nt_object_t *remove_handle_entry(pid_t pid, int32_t local_handle) {
    handle_entry_t **pp = &g_handles;
    while (*pp) {
        if ((*pp)->owner_pid == pid && (*pp)->local_handle == local_handle) {
            handle_entry_t *victim = *pp;
            nt_object_t *obj = victim->object;
            *pp = victim->next;
            free(victim);
            return obj;
        }
        pp = &(*pp)->next;
    }
    return NULL;
}

/* --- object table --------------------------------------------------------*/

static nt_object_t *find_by_name(const char *name) {
    if (!name[0]) return NULL;
    for (nt_object_t *o = g_objects; o; o = o->next)
        if (strcmp(o->name, name) == 0) return o;
    return NULL;
}

static nt_object_t *create_object(obj_type_t type, const char *name) {
    nt_object_t *o = calloc(1, sizeof(*o));
    o->id = g_next_object_id++;
    o->type = type;
    o->pidfd = -1;
    snprintf(o->name, sizeof(o->name), "%s", name ? name : "");
    o->next = g_objects;
    g_objects = o;
    return o;
}

static void free_object(nt_object_t *o) {
    nt_object_t **pp = &g_objects;
    while (*pp && *pp != o) pp = &(*pp)->next;
    if (*pp) *pp = o->next;

    if (o->type == OBJ_SECTION && o->shm_name[0]) shm_unlink(o->shm_name);
    if (o->type == OBJ_PROCESS && o->pidfd >= 0) close(o->pidfd);
    if (o->type == OBJ_COMPLETION) {
        completion_packet_t *p = o->queue_head;
        while (p) { completion_packet_t *n = p->next; free(p); p = n; }
    }
    free(o);
}

/* --- timeouts & multi-wait bookkeeping ----------------------------------*/

static void deadline_from_timeout(struct timespec *ts, int32_t timeout_ms) {
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += timeout_ms / 1000;
    ts->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec += 1; ts->tv_nsec -= 1000000000L; }
}

static int timespec_past(const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec != deadline->tv_sec) return now.tv_sec > deadline->tv_sec;
    return now.tv_nsec >= deadline->tv_nsec;
}

static void add_waiter(nt_object_t *o, uint32_t slot_idx, multiwait_t *mw, int32_t timeout_ms) {
    waiter_t *w = calloc(1, sizeof(*w));
    w->slot_idx = slot_idx;
    w->multiwait = mw;
    if (timeout_ms >= 0) { w->has_deadline = 1; deadline_from_timeout(&w->deadline, timeout_ms); }
    if (!o->waiters) { o->waiters = w; return; }
    waiter_t *p = o->waiters;
    while (p->next) p = p->next;
    p->next = w;
}

static void remove_waiter_for_multiwait(nt_object_t *o, multiwait_t *mw) {
    waiter_t **pp = &o->waiters;
    while (*pp) {
        if ((*pp)->multiwait == mw) {
            waiter_t *victim = *pp;
            *pp = victim->next;
            free(victim);
            return; /* at most one waiter per multiwait per object, by construction */
        }
        pp = &(*pp)->next;
    }
}

/* --- per-type "is it available right now" / "consume it" ---------------- */

static int object_available(nt_object_t *o) {
    switch (o->type) {
        case OBJ_EVENT:      return o->signaled;
        case OBJ_MUTANT:     return !o->owned;
        case OBJ_SEMAPHORE:  return o->count > 0;
        case OBJ_PROCESS:    return o->process_exited;
        case OBJ_SECTION:    return 0; /* not waitable */
        case OBJ_COMPLETION: return o->queue_head != NULL;
    }
    return 0;
}

static void consume_object(nt_object_t *o, pid_t requester_pid) {
    switch (o->type) {
        case OBJ_EVENT:      if (!o->manual_reset) o->signaled = 0; break;
        case OBJ_MUTANT:     o->owned = 1; o->owner_pid = requester_pid; break;
        case OBJ_SEMAPHORE:  o->count--; break;
        case OBJ_PROCESS:    /* manual-reset-like: stays signaled, nothing to consume */ break;
        case OBJ_SECTION:    break;
        case OBJ_COMPLETION: break; /* completion dequeue is handled directly by its own opcode, not via this generic path */
    }
    (void)requester_pid;
}

/* Attempts to satisfy a registered multiwait right now. On success,
 * removes its waiter_t from every object it was registered on and frees
 * it once fully cleaned up. Safe to call speculatively (e.g. right after
 * registering, or from an object's state-change notification). */
static void try_satisfy_multiwait(multiwait_t *mw) {
    if (mw->completed) return;

    int satisfied_idx = -1;
    if (mw->wait_all) {
        int all_ok = 1;
        for (int i = 0; i < mw->handle_count; i++)
            if (!object_available(mw->objects[i])) { all_ok = 0; break; }
        if (!all_ok) return;
        for (int i = 0; i < mw->handle_count; i++)
            consume_object(mw->objects[i], g_shm->slots[mw->slot_idx].client_pid);
    } else {
        for (int i = 0; i < mw->handle_count; i++) {
            if (object_available(mw->objects[i])) { satisfied_idx = i; break; }
        }
        if (satisfied_idx < 0) return;
        consume_object(mw->objects[satisfied_idx], g_shm->slots[mw->slot_idx].client_pid);
    }

    mw->completed = 1;
    complete_wait_multiple(mw->slot_idx, NTABI_STATUS_SUCCESS, satisfied_idx);

    for (int i = 0; i < mw->handle_count; i++)
        remove_waiter_for_multiwait(mw->objects[i], mw);
    /* every registration removed above (pending_refs implicitly reaches 0
     * since handle_count == initial pending_refs and we just removed one
     * per object) - free directly rather than tracking pending_refs down
     * to zero via a separate counter, since this path always cleans up
     * every object in one pass. */
    free(mw);
}

/* Called after any state change that could newly satisfy waiters
 * (SetEvent, ReleaseMutant, ReleaseSemaphore, a process exiting, a
 * completion posted). Single-object waiters are handled by the caller
 * (satisfy_one_waiter / wake_all_waiters_success, unchanged from Phase 2);
 * this handles the multiwait ones registered on the same object, via a
 * snapshot so try_satisfy_multiwait's list mutations (on this object AND
 * others) can't corrupt an in-progress scan of this object's own list. */
static void notify_multiwaiters(nt_object_t *o) {
    multiwait_t *pending[NTABI_MAX_SLOTS];
    int n = 0;
    for (waiter_t *w = o->waiters; w && (uint32_t)n < NTABI_MAX_SLOTS; w = w->next)
        if (w->multiwait) pending[n++] = w->multiwait;
    for (int i = 0; i < n; i++)
        try_satisfy_multiwait(pending[i]);
}

static int satisfy_one_waiter(nt_object_t *o) {
    if (!o->waiters || !object_available(o)) return 0;
    /* skip past any multiwait-tagged entries at the head - those are
     * handled by notify_multiwaiters, not consumed here, since consuming
     * for a multiwait might need *other* objects too (wait-all). */
    waiter_t **pp = &o->waiters;
    while (*pp && (*pp)->multiwait) pp = &(*pp)->next;
    if (!*pp) return 0;

    waiter_t *w = *pp;
    *pp = w->next;
    consume_object(o, g_shm->slots[w->slot_idx].client_pid);
    complete(w->slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
    free(w);
    return 1;
}

static void wake_all_waiters_success(nt_object_t *o) {
    waiter_t **pp = &o->waiters;
    while (*pp) {
        if ((*pp)->multiwait) { pp = &(*pp)->next; continue; }
        waiter_t *w = *pp;
        *pp = w->next;
        complete(w->slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
        free(w);
    }
}

/* --- request handlers: Event / Mutant / Semaphore (mostly Phase 2, now
 * routed through the per-pid handle table) ------------------------------ */

static void handle_create_event(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    if (req->name[0] && find_by_name(req->name)) {
        complete(slot_idx, NTABI_STATUS_OBJECT_NAME_COLLISION, 0, 0);
        return;
    }
    nt_object_t *o = create_object(OBJ_EVENT, req->name);
    o->manual_reset = req->manual_reset;
    o->signaled = req->initial_state ? 1 : 0;
    complete(slot_idx, NTABI_STATUS_SUCCESS, register_handle(pid, o), 0);
}

static void handle_open_typed(ntabi_request_t *req, uint32_t slot_idx, pid_t pid, obj_type_t type) {
    nt_object_t *o = find_by_name(req->name);
    if (!o) { complete(slot_idx, NTABI_STATUS_OBJECT_NAME_NOT_FOUND, 0, 0); return; }
    if (o->type != type) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    complete(slot_idx, NTABI_STATUS_SUCCESS, register_handle(pid, o), 0);
}

static void handle_set_event(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_handle(pid, req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_EVENT) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }

    o->signaled = 1;
    if (o->manual_reset) wake_all_waiters_success(o);
    else satisfy_one_waiter(o);
    notify_multiwaiters(o);
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void handle_reset_event(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_handle(pid, req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_EVENT) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    o->signaled = 0;
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void handle_create_mutant(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    if (req->name[0] && find_by_name(req->name)) {
        complete(slot_idx, NTABI_STATUS_OBJECT_NAME_COLLISION, 0, 0);
        return;
    }
    nt_object_t *o = create_object(OBJ_MUTANT, req->name);
    o->owned = req->initial_state ? 1 : 0;
    if (o->owned) o->owner_pid = pid;
    complete(slot_idx, NTABI_STATUS_SUCCESS, register_handle(pid, o), 0);
}

static void handle_release_mutant(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_handle(pid, req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_MUTANT) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    if (!o->owned) { complete(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0); return; }
    o->owned = 0;
    satisfy_one_waiter(o);
    notify_multiwaiters(o);
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void handle_create_semaphore(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
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
    complete(slot_idx, NTABI_STATUS_SUCCESS, register_handle(pid, o), 0);
}

static void handle_release_semaphore(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_handle(pid, req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_SEMAPHORE) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }
    if (req->release_count <= 0 || o->count + req->release_count > o->max_count) {
        complete(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0);
        return;
    }
    int32_t prev = o->count;
    o->count += req->release_count;
    while (satisfy_one_waiter(o)) { /* wake as many as the new count allows */ }
    notify_multiwaiters(o);
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, prev);
}

static void handle_wait_single(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_handle(pid, req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type == OBJ_SECTION) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }

    if (object_available(o)) {
        consume_object(o, pid);
        complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
        return;
    }
    if (req->timeout_ms == 0) { complete(slot_idx, NTABI_STATUS_TIMEOUT, 0, 0); return; }
    add_waiter(o, slot_idx, NULL, req->timeout_ms);
}

static void handle_wait_multiple(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    if (req->handle_count <= 0 || req->handle_count > NTABI_MAX_WAIT_HANDLES) {
        complete_wait_multiple(slot_idx, NTABI_STATUS_INVALID_PARAMETER, -1);
        return;
    }

    multiwait_t *mw = calloc(1, sizeof(*mw));
    mw->slot_idx = slot_idx;
    mw->wait_all = req->wait_all;
    mw->handle_count = req->handle_count;

    for (int i = 0; i < req->handle_count; i++) {
        nt_object_t *o = find_handle(pid, req->handles[i]);
        if (!o || o->type == OBJ_SECTION) {
            free(mw);
            complete_wait_multiple(slot_idx, NTABI_STATUS_INVALID_HANDLE, -1);
            return;
        }
        mw->handles[i] = req->handles[i];
        mw->objects[i] = o;
    }

    /* Try immediately before registering anywhere - covers the common
     * case (already satisfiable) without touching any waiter lists. */
    if (mw->wait_all) {
        int all_ok = 1;
        for (int i = 0; i < mw->handle_count; i++)
            if (!object_available(mw->objects[i])) { all_ok = 0; break; }
        if (all_ok) {
            for (int i = 0; i < mw->handle_count; i++) consume_object(mw->objects[i], pid);
            complete_wait_multiple(slot_idx, NTABI_STATUS_SUCCESS, -1);
            free(mw);
            return;
        }
    } else {
        for (int i = 0; i < mw->handle_count; i++) {
            if (object_available(mw->objects[i])) {
                consume_object(mw->objects[i], pid);
                complete_wait_multiple(slot_idx, NTABI_STATUS_SUCCESS, i);
                free(mw);
                return;
            }
        }
    }

    if (req->timeout_ms == 0) {
        complete_wait_multiple(slot_idx, NTABI_STATUS_TIMEOUT, -1);
        free(mw);
        return;
    }

    mw->pending_refs = mw->handle_count;
    for (int i = 0; i < mw->handle_count; i++)
        add_waiter(mw->objects[i], slot_idx, mw, req->timeout_ms);
}

static void handle_close_handle(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = remove_handle_entry(pid, req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }

    o->refcount--;
    if (o->refcount <= 0) {
        /* Lingering waiters here mean some other client is waiting on a
         * handle nothing will ever signal again - fail them (and any
         * multiwaits they're part of) rather than leaking their slots. */
        waiter_t *w = o->waiters;
        while (w) {
            waiter_t *n = w->next;
            if (w->multiwait) {
                multiwait_t *mw = w->multiwait;
                if (!mw->completed) {
                    mw->completed = 1;
                    complete_wait_multiple(mw->slot_idx, NTABI_STATUS_TIMEOUT, -1);
                }
                for (int i = 0; i < mw->handle_count; i++)
                    if (mw->objects[i] != o) remove_waiter_for_multiwait(mw->objects[i], mw);
                free(mw);
            } else {
                complete(w->slot_idx, NTABI_STATUS_TIMEOUT, 0, 0);
            }
            free(w);
            w = n;
        }
        o->waiters = NULL;
        free_object(o);
    }
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

/* --- Section (Phase 3): real POSIX-shared-memory-backed object, mapped
 * client-side. ntd only creates/tracks the backing shm object and hands
 * out its name; it never maps it itself. ------------------------------- */

static void handle_create_section(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    if (req->section_size <= 0) {
        complete_section(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0, NULL);
        return;
    }
    if (req->name[0] && find_by_name(req->name)) {
        complete_section(slot_idx, NTABI_STATUS_OBJECT_NAME_COLLISION, 0, 0, NULL);
        return;
    }

    nt_object_t *o = create_object(OBJ_SECTION, req->name);
    o->section_size = req->section_size;
    snprintf(o->shm_name, sizeof(o->shm_name), "/ntlinux-sect-%d", g_next_section_id++);

    int fd = shm_open(o->shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0 || ftruncate(fd, o->section_size) != 0) {
        if (fd >= 0) { close(fd); shm_unlink(o->shm_name); }
        free_object(o);
        complete_section(slot_idx, NTABI_STATUS_INTERNAL_ERROR, 0, 0, NULL);
        return;
    }
    close(fd);

    int32_t handle = register_handle(pid, o);
    complete_section(slot_idx, NTABI_STATUS_SUCCESS, handle, o->section_size, o->shm_name);
}

static void handle_open_section(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_by_name(req->name);
    if (!o) { complete_section(slot_idx, NTABI_STATUS_OBJECT_NAME_NOT_FOUND, 0, 0, NULL); return; }
    if (o->type != OBJ_SECTION) { complete_section(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0, NULL); return; }
    int32_t handle = register_handle(pid, o);
    complete_section(slot_idx, NTABI_STATUS_SUCCESS, handle, o->section_size, o->shm_name);
}

/* --- I/O Completion port (Phase 3): a wait-capable FIFO queue. No real
 * I/O is routed through it yet (no file/socket I/O exists in this
 * prototype) - PostCompletion is callable directly, standing in for what
 * would normally be driven by completed I/O. -----------------------------*/

static void handle_create_completion(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    if (req->name[0] && find_by_name(req->name)) {
        complete(slot_idx, NTABI_STATUS_OBJECT_NAME_COLLISION, 0, 0);
        return;
    }
    nt_object_t *o = create_object(OBJ_COMPLETION, req->name);
    complete(slot_idx, NTABI_STATUS_SUCCESS, register_handle(pid, o), 0);
}

static void handle_post_completion(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_handle(pid, req->handle);
    if (!o) { complete(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0); return; }
    if (o->type != OBJ_COMPLETION) { complete(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0); return; }

    /* Direct hand-off to the oldest waiter if there is one (skipping past
     * any multiwait entries, same rule as satisfy_one_waiter), else queue. */
    waiter_t **pp = &o->waiters;
    while (*pp && (*pp)->multiwait) pp = &(*pp)->next;
    if (*pp) {
        waiter_t *w = *pp;
        *pp = w->next;
        complete_io(w->slot_idx, NTABI_STATUS_SUCCESS, req->completion_key,
                    req->completion_bytes, req->completion_overlapped);
        free(w);
    } else {
        completion_packet_t *p = calloc(1, sizeof(*p));
        p->key = req->completion_key;
        p->bytes = req->completion_bytes;
        p->overlapped = req->completion_overlapped;
        if (o->queue_tail) { o->queue_tail->next = p; o->queue_tail = p; }
        else { o->queue_head = o->queue_tail = p; }
    }
    notify_multiwaiters(o);
    complete(slot_idx, NTABI_STATUS_SUCCESS, 0, 0);
}

static void handle_remove_completion(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    nt_object_t *o = find_handle(pid, req->handle);
    if (!o) { complete_io(slot_idx, NTABI_STATUS_INVALID_HANDLE, 0, 0, 0); return; }
    if (o->type != OBJ_COMPLETION) { complete_io(slot_idx, NTABI_STATUS_OBJECT_TYPE_MISMATCH, 0, 0, 0); return; }

    if (o->queue_head) {
        completion_packet_t *p = o->queue_head;
        o->queue_head = p->next;
        if (!o->queue_head) o->queue_tail = NULL;
        complete_io(slot_idx, NTABI_STATUS_SUCCESS, p->key, p->bytes, p->overlapped);
        free(p);
        return;
    }
    if (req->timeout_ms == 0) { complete_io(slot_idx, NTABI_STATUS_TIMEOUT, 0, 0, 0); return; }
    add_waiter(o, slot_idx, NULL, req->timeout_ms);
}

/* --- Process object (Phase 3): signaled when the target pid exits,
 * detected via pidfd polling on the daemon's regular tick (no dedicated
 * thread, consistent with the rest of ntd). pidfd polling for readability
 * works for *any* permitted pid, not just this process's own children -
 * unlike waitid()/wait(), which is why pidfd is used here at all. ------- */

static void handle_open_process(ntabi_request_t *req, uint32_t slot_idx, pid_t pid) {
    for (nt_object_t *o = g_objects; o; o = o->next) {
        if (o->type == OBJ_PROCESS && o->target_pid == req->target_pid) {
            complete(slot_idx, NTABI_STATUS_SUCCESS, register_handle(pid, o), 0);
            return;
        }
    }

    int pidfd = ntd_pidfd_open(req->target_pid, 0);
    if (pidfd < 0) {
        complete(slot_idx, NTABI_STATUS_PROCESS_NOT_FOUND, 0, 0);
        return;
    }

    nt_object_t *o = create_object(OBJ_PROCESS, "");
    o->target_pid = req->target_pid;
    o->pidfd = pidfd;

    struct pollfd pfd = { .fd = pidfd, .events = POLLIN };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) o->process_exited = 1;

    complete(slot_idx, NTABI_STATUS_SUCCESS, register_handle(pid, o), 0);
}

static void check_process_exits(void) {
    for (nt_object_t *o = g_objects; o; o = o->next) {
        if (o->type != OBJ_PROCESS || o->process_exited) continue;
        struct pollfd pfd = { .fd = o->pidfd, .events = POLLIN };
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            o->process_exited = 1;
            wake_all_waiters_success(o);
            notify_multiwaiters(o);
        }
    }
}

/* --- dispatch + timeouts -------------------------------------------------*/

static void process_request(uint32_t slot_idx) {
    ntabi_request_t req = g_shm->slots[slot_idx].req; /* copy: only ntd touches this after dequeue */
    pid_t pid = g_shm->slots[slot_idx].client_pid;
    switch (req.opcode) {
        case NTABI_OP_CREATE_EVENT:      handle_create_event(&req, slot_idx, pid); break;
        case NTABI_OP_OPEN_EVENT:        handle_open_typed(&req, slot_idx, pid, OBJ_EVENT); break;
        case NTABI_OP_SET_EVENT:         handle_set_event(&req, slot_idx, pid); break;
        case NTABI_OP_RESET_EVENT:       handle_reset_event(&req, slot_idx, pid); break;
        case NTABI_OP_CREATE_MUTANT:     handle_create_mutant(&req, slot_idx, pid); break;
        case NTABI_OP_OPEN_MUTANT:       handle_open_typed(&req, slot_idx, pid, OBJ_MUTANT); break;
        case NTABI_OP_RELEASE_MUTANT:    handle_release_mutant(&req, slot_idx, pid); break;
        case NTABI_OP_CREATE_SEMAPHORE:  handle_create_semaphore(&req, slot_idx, pid); break;
        case NTABI_OP_OPEN_SEMAPHORE:    handle_open_typed(&req, slot_idx, pid, OBJ_SEMAPHORE); break;
        case NTABI_OP_RELEASE_SEMAPHORE: handle_release_semaphore(&req, slot_idx, pid); break;
        case NTABI_OP_WAIT_SINGLE:       handle_wait_single(&req, slot_idx, pid); break;
        case NTABI_OP_WAIT_MULTIPLE:     handle_wait_multiple(&req, slot_idx, pid); break;
        case NTABI_OP_CLOSE_HANDLE:      handle_close_handle(&req, slot_idx, pid); break;
        case NTABI_OP_CREATE_SECTION:    handle_create_section(&req, slot_idx, pid); break;
        case NTABI_OP_OPEN_SECTION:      handle_open_section(&req, slot_idx, pid); break;
        case NTABI_OP_CREATE_COMPLETION: handle_create_completion(&req, slot_idx, pid); break;
        case NTABI_OP_OPEN_COMPLETION:   handle_open_typed(&req, slot_idx, pid, OBJ_COMPLETION); break;
        case NTABI_OP_POST_COMPLETION:   handle_post_completion(&req, slot_idx, pid); break;
        case NTABI_OP_REMOVE_COMPLETION: handle_remove_completion(&req, slot_idx, pid); break;
        case NTABI_OP_OPEN_PROCESS:      handle_open_process(&req, slot_idx, pid); break;
        default:
            complete(slot_idx, NTABI_STATUS_INVALID_PARAMETER, 0, 0);
    }
}

/* Walks every object's waiter list, timing out anyone whose deadline has
 * passed - including multiwait waiters, coordinated via ->completed so a
 * multiwait registered on several objects only responds once even though
 * several of those objects may notice the same expired deadline in the
 * same sweep. O(objects*waiters); fine at prototype scale, a real
 * implementation would use a timer wheel instead. */
static void expire_timeouts(void) {
    for (nt_object_t *o = g_objects; o; o = o->next) {
        waiter_t **pp = &o->waiters;
        while (*pp) {
            waiter_t *w = *pp;
            if (w->has_deadline && timespec_past(&w->deadline)) {
                *pp = w->next;
                if (w->multiwait) {
                    multiwait_t *mw = w->multiwait;
                    if (!mw->completed) {
                        mw->completed = 1;
                        complete_wait_multiple(mw->slot_idx, NTABI_STATUS_TIMEOUT, -1);
                    }
                } else {
                    complete(w->slot_idx, NTABI_STATUS_TIMEOUT, 0, 0);
                }
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
        ts.tv_nsec += 50 * 1000000L; /* 50ms tick, bounds worst-case timeout/process-exit detection latency */
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
        check_process_exits();
    }

    fprintf(stderr, "ntd: shutting down\n");
    /* Sweep remaining objects before exiting - found by actually noticing
     * a leftover /dev/shm/ntlinux-sect-N after a clean test run: a
     * Section's backing shm segment is real OS state (unlike every other
     * object type here, which is purely in ntd's own memory), so it
     * outlives ntd unless something explicitly unlinks it. Nothing in the
     * protocol requires a client to close every handle before a graceful
     * shutdown, so ntd must not assume they did. */
    while (g_objects) free_object(g_objects);
    munmap(g_shm, sizeof(ntabi_shm_t));
    shm_unlink(NTABI_SHM_NAME);
    return 0;
}
