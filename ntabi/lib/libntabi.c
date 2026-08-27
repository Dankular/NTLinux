/*
 * libntabi client implementation. See ntabi/include/ntabi.h for the API
 * and ntabi/protocol/ntabi_protocol.h for the wire format this speaks.
 */
#define _GNU_SOURCE
#include "ntabi.h"
#include "ntabi_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>

struct ntabi_conn {
    ntabi_shm_t *shm;
};

ntabi_conn_t *ntabi_connect(void) {
    int fd = shm_open(NTABI_SHM_NAME, O_RDWR, 0);
    if (fd < 0) return NULL;

    ntabi_shm_t *shm = mmap(NULL, sizeof(ntabi_shm_t), PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
    close(fd); /* mapping keeps the segment alive; fd itself isn't needed after mmap */
    if (shm == MAP_FAILED) return NULL;

    if (shm->magic != NTABI_MAGIC) {
        munmap(shm, sizeof(ntabi_shm_t));
        return NULL;
    }
    if (shm->protocol_version != NTABI_PROTOCOL_VERSION) {
        fprintf(stderr, "libntabi: protocol version mismatch: ntd speaks %u, "
                "this client speaks %u - refusing to connect (Rule 12)\n",
                shm->protocol_version, NTABI_PROTOCOL_VERSION);
        munmap(shm, sizeof(ntabi_shm_t));
        return NULL;
    }

    ntabi_conn_t *conn = malloc(sizeof(*conn));
    if (!conn) { munmap(shm, sizeof(ntabi_shm_t)); return NULL; }
    conn->shm = shm;
    return conn;
}

void ntabi_disconnect(ntabi_conn_t *conn) {
    if (!conn) return;
    munmap(conn->shm, sizeof(ntabi_shm_t));
    free(conn);
}

/* Allocates a slot, submits req, blocks for the response, frees the slot,
 * and hands the response back. This is the only place that touches the
 * shared-memory ring/slot machinery - every public API call below is a
 * thin wrapper filling in a request and calling this. */
static ntabi_status_t submit(ntabi_conn_t *conn, ntabi_request_t *req,
                              ntabi_response_t *out_resp) {
    ntabi_shm_t *shm = conn->shm;

    pthread_mutex_lock(&shm->alloc_lock);
    uint32_t idx = NTABI_MAX_SLOTS;
    for (uint32_t i = 0; i < NTABI_MAX_SLOTS; i++) {
        if (!shm->slots[i].in_use) { idx = i; break; }
    }
    if (idx == NTABI_MAX_SLOTS) {
        pthread_mutex_unlock(&shm->alloc_lock);
        return NTABI_STATUS_LIMIT_REACHED;
    }
    ntabi_slot_t *slot = &shm->slots[idx];
    slot->in_use = 1;
    slot->client_pid = getpid();
    slot->req = *req;
    memset(&slot->resp, 0, sizeof(slot->resp));

    shm->ring[shm->ring_tail % NTABI_MAX_SLOTS] = idx;
    shm->ring_tail++;
    pthread_mutex_unlock(&shm->alloc_lock);

    sem_post(&shm->doorbell);

    /* Genuinely blocks here - no polling. ntd posts slot->ready exactly
     * once, either when the request completes immediately or later when a
     * waited-on object becomes signaled (or its deadline passes). */
    while (sem_wait(&slot->ready) != 0) {
        if (errno != EINTR) { pthread_mutex_lock(&shm->alloc_lock); slot->in_use = 0; pthread_mutex_unlock(&shm->alloc_lock); return NTABI_STATUS_INTERNAL_ERROR; }
    }

    *out_resp = slot->resp;

    pthread_mutex_lock(&shm->alloc_lock);
    slot->in_use = 0;
    pthread_mutex_unlock(&shm->alloc_lock);

    return out_resp->status;
}

static void fill_name(ntabi_request_t *req, const char *name) {
    if (name && *name) {
        snprintf(req->name, sizeof(req->name), "%s", name);
    } else {
        req->name[0] = '\0';
    }
}

ntabi_status_t ntabi_create_event(ntabi_conn_t *c, const char *name,
                                   int initial_state, int manual_reset,
                                   int32_t *out_handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_CREATE_EVENT;
    req.initial_state = initial_state;
    req.manual_reset = manual_reset;
    fill_name(&req, name);
    ntabi_response_t resp;
    ntabi_status_t st = submit(c, &req, &resp);
    if (out_handle) *out_handle = resp.handle;
    return st;
}

ntabi_status_t ntabi_open_event(ntabi_conn_t *c, const char *name, int32_t *out_handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_OPEN_EVENT;
    fill_name(&req, name);
    ntabi_response_t resp;
    ntabi_status_t st = submit(c, &req, &resp);
    if (out_handle) *out_handle = resp.handle;
    return st;
}

ntabi_status_t ntabi_set_event(ntabi_conn_t *c, int32_t handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_SET_EVENT;
    req.handle = handle;
    ntabi_response_t resp;
    return submit(c, &req, &resp);
}

ntabi_status_t ntabi_reset_event(ntabi_conn_t *c, int32_t handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_RESET_EVENT;
    req.handle = handle;
    ntabi_response_t resp;
    return submit(c, &req, &resp);
}

ntabi_status_t ntabi_create_mutant(ntabi_conn_t *c, const char *name,
                                    int initial_owner, int32_t *out_handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_CREATE_MUTANT;
    req.initial_state = initial_owner;
    fill_name(&req, name);
    ntabi_response_t resp;
    ntabi_status_t st = submit(c, &req, &resp);
    if (out_handle) *out_handle = resp.handle;
    return st;
}

ntabi_status_t ntabi_open_mutant(ntabi_conn_t *c, const char *name, int32_t *out_handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_OPEN_MUTANT;
    fill_name(&req, name);
    ntabi_response_t resp;
    ntabi_status_t st = submit(c, &req, &resp);
    if (out_handle) *out_handle = resp.handle;
    return st;
}

ntabi_status_t ntabi_release_mutant(ntabi_conn_t *c, int32_t handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_RELEASE_MUTANT;
    req.handle = handle;
    ntabi_response_t resp;
    return submit(c, &req, &resp);
}

ntabi_status_t ntabi_create_semaphore(ntabi_conn_t *c, const char *name,
                                       int32_t initial_count, int32_t max_count,
                                       int32_t *out_handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_CREATE_SEMAPHORE;
    req.initial_count = initial_count;
    req.max_count = max_count;
    fill_name(&req, name);
    ntabi_response_t resp;
    ntabi_status_t st = submit(c, &req, &resp);
    if (out_handle) *out_handle = resp.handle;
    return st;
}

ntabi_status_t ntabi_open_semaphore(ntabi_conn_t *c, const char *name, int32_t *out_handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_OPEN_SEMAPHORE;
    fill_name(&req, name);
    ntabi_response_t resp;
    ntabi_status_t st = submit(c, &req, &resp);
    if (out_handle) *out_handle = resp.handle;
    return st;
}

ntabi_status_t ntabi_release_semaphore(ntabi_conn_t *c, int32_t handle,
                                        int32_t release_count, int32_t *out_prev_count) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_RELEASE_SEMAPHORE;
    req.handle = handle;
    req.release_count = release_count;
    ntabi_response_t resp;
    ntabi_status_t st = submit(c, &req, &resp);
    if (out_prev_count) *out_prev_count = resp.prev_count;
    return st;
}

ntabi_status_t ntabi_wait_single(ntabi_conn_t *c, int32_t handle, int32_t timeout_ms) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_WAIT_SINGLE;
    req.handle = handle;
    req.timeout_ms = timeout_ms;
    ntabi_response_t resp;
    return submit(c, &req, &resp);
}

ntabi_status_t ntabi_close_handle(ntabi_conn_t *c, int32_t handle) {
    ntabi_request_t req = {0};
    req.opcode = NTABI_OP_CLOSE_HANDLE;
    req.handle = handle;
    ntabi_response_t resp;
    return submit(c, &req, &resp);
}

const char *ntabi_status_string(ntabi_status_t status) {
    switch (status) {
        case NTABI_STATUS_SUCCESS: return "SUCCESS";
        case NTABI_STATUS_TIMEOUT: return "TIMEOUT";
        case NTABI_STATUS_INVALID_HANDLE: return "INVALID_HANDLE";
        case NTABI_STATUS_OBJECT_NAME_NOT_FOUND: return "OBJECT_NAME_NOT_FOUND";
        case NTABI_STATUS_OBJECT_NAME_COLLISION: return "OBJECT_NAME_COLLISION";
        case NTABI_STATUS_OBJECT_TYPE_MISMATCH: return "OBJECT_TYPE_MISMATCH";
        case NTABI_STATUS_INVALID_PARAMETER: return "INVALID_PARAMETER";
        case NTABI_STATUS_LIMIT_REACHED: return "LIMIT_REACHED";
        case NTABI_STATUS_INTERNAL_ERROR: return "INTERNAL_ERROR";
        default: return "UNKNOWN";
    }
}
