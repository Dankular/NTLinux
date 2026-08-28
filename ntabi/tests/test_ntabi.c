/*
 * Differential-in-spirit test suite for libntabi/ntd (CLAUDE.md Rule 11:
 * every NT compatibility implementation requires tests). Not literally
 * differential against real Windows/Wine yet - Phase 2 has no ntdll
 * integration to compare against (see ROADMAP.md) - but it does exercise
 * real NT object *semantics* (auto-reset vs manual-reset, semaphore
 * counting, mutant ownership hand-off) against the actual daemon, across
 * real process boundaries, not mocked.
 *
 * Self-contained: starts its own ntd instance, runs every test against
 * it, tears it down. Run with: ./test_ntabi (after `make` in this dir).
 */
#define _GNU_SOURCE
#include "ntabi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <pthread.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; printf("  PASS: " __VA_ARGS__); printf("\n"); } \
    else      { g_fail++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } \
} while (0)

static long elapsed_ms(struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000 + (now.tv_nsec - start->tv_nsec) / 1000000;
}

/* Raw syscall, matching ntd.c/libntabi.c's own gettid()/pidfd_open()
 * pattern (no glibc gettid() wrapper before 2.30). */
static pid_t test_gettid(void) {
    return (pid_t)syscall(SYS_gettid);
}

/* --- ntd lifecycle -------------------------------------------------- */

static pid_t start_ntd(const char *ntd_path) {
    pid_t pid = fork();
    if (pid == 0) {
        execl(ntd_path, "ntd", (char *)NULL);
        perror("execl ntd");
        _exit(127);
    }
    return pid;
}

static ntabi_conn_t *connect_with_retry(void) {
    for (int i = 0; i < 100; i++) {
        ntabi_conn_t *c = ntabi_connect();
        if (c) return c;
        usleep(20 * 1000);
    }
    return NULL;
}

/* --- single-process tests --------------------------------------------- */

static void test_auto_reset_event(ntabi_conn_t *c) {
    printf("test_auto_reset_event:\n");
    int32_t h;
    ntabi_status_t st = ntabi_create_event(c, NULL, 0, 0, &h);
    CHECK(st == NTABI_STATUS_SUCCESS && h > 0, "create unnamed auto-reset event -> handle %d", h);

    st = ntabi_wait_single(c, h, 50);
    CHECK(st == NTABI_STATUS_TIMEOUT, "wait on unsignaled event times out (got %s)", ntabi_status_string(st));

    st = ntabi_set_event(c, h);
    CHECK(st == NTABI_STATUS_SUCCESS, "set_event succeeds");

    st = ntabi_wait_single(c, h, 1000);
    CHECK(st == NTABI_STATUS_SUCCESS, "wait after set_event succeeds immediately");

    st = ntabi_wait_single(c, h, 50);
    CHECK(st == NTABI_STATUS_TIMEOUT, "auto-reset event was consumed by previous wait (got %s)", ntabi_status_string(st));

    ntabi_close_handle(c, h);
}

static void test_manual_reset_event(ntabi_conn_t *c) {
    printf("test_manual_reset_event:\n");
    int32_t h;
    ntabi_create_event(c, NULL, 0, 1 /* manual reset */, &h);
    ntabi_set_event(c, h);

    ntabi_status_t st1 = ntabi_wait_single(c, h, 100);
    ntabi_status_t st2 = ntabi_wait_single(c, h, 100);
    CHECK(st1 == NTABI_STATUS_SUCCESS && st2 == NTABI_STATUS_SUCCESS,
          "manual-reset event stays signaled across multiple waits");

    ntabi_reset_event(c, h);
    ntabi_status_t st3 = ntabi_wait_single(c, h, 50);
    CHECK(st3 == NTABI_STATUS_TIMEOUT, "reset_event clears manual-reset event (got %s)", ntabi_status_string(st3));

    ntabi_close_handle(c, h);
}

static void test_semaphore(ntabi_conn_t *c) {
    printf("test_semaphore:\n");
    int32_t h;
    ntabi_status_t st = ntabi_create_semaphore(c, NULL, 2, 2, &h);
    CHECK(st == NTABI_STATUS_SUCCESS, "create semaphore(initial=2, max=2)");

    ntabi_status_t a = ntabi_wait_single(c, h, 100);
    ntabi_status_t b = ntabi_wait_single(c, h, 100);
    CHECK(a == NTABI_STATUS_SUCCESS && b == NTABI_STATUS_SUCCESS, "two waits succeed (count 2 -> 0)");

    ntabi_status_t timeout = ntabi_wait_single(c, h, 100);
    CHECK(timeout == NTABI_STATUS_TIMEOUT, "third wait times out at count 0 (got %s)", ntabi_status_string(timeout));

    int32_t prev = -1;
    st = ntabi_release_semaphore(c, h, 1, &prev);
    CHECK(st == NTABI_STATUS_SUCCESS && prev == 0, "release_semaphore(1): prev_count=%d", prev);

    st = ntabi_wait_single(c, h, 100);
    CHECK(st == NTABI_STATUS_SUCCESS, "wait succeeds after release");

    st = ntabi_release_semaphore(c, h, 5, &prev);
    CHECK(st == NTABI_STATUS_INVALID_PARAMETER, "release beyond max_count is rejected (got %s)", ntabi_status_string(st));

    ntabi_close_handle(c, h);
}

static void test_naming(ntabi_conn_t *c) {
    printf("test_naming:\n");
    int32_t h1, h2;
    ntabi_status_t st = ntabi_create_event(c, "ntabi-test-named", 0, 0, &h1);
    CHECK(st == NTABI_STATUS_SUCCESS, "create named event");

    st = ntabi_create_event(c, "ntabi-test-named", 0, 0, &h2);
    CHECK(st == NTABI_STATUS_OBJECT_NAME_COLLISION, "re-creating same name collides (got %s)", ntabi_status_string(st));

    /* Per-process handle tables (Phase 3): each Open gets its *own* new
     * handle number, even from the same process re-opening the same name
     * - matching real NT (OpenEvent/DuplicateHandle return a fresh handle
     * value to the same object, not the original one back). Prove
     * "same object" behaviorally instead of by handle-number equality:
     * signal via h1, confirm h2 (a separate handle to the same object)
     * sees it signaled too. */
    st = ntabi_open_event(c, "ntabi-test-named", &h2);
    CHECK(st == NTABI_STATUS_SUCCESS && h2 != h1,
          "open returns a *new* handle number to the same object (h1=%d, h2=%d)", h1, h2);
    ntabi_set_event(c, h1);
    ntabi_status_t waited = ntabi_wait_single(c, h2, 100);
    CHECK(waited == NTABI_STATUS_SUCCESS,
          "signaling via h1 is visible when waiting via h2 - confirms both handles reference the same object");

    int32_t h3;
    st = ntabi_open_event(c, "does-not-exist", &h3);
    CHECK(st == NTABI_STATUS_OBJECT_NAME_NOT_FOUND, "opening a nonexistent name fails correctly (got %s)", ntabi_status_string(st));

    st = ntabi_open_mutant(c, "ntabi-test-named", &h3);
    CHECK(st == NTABI_STATUS_OBJECT_TYPE_MISMATCH, "opening an event as a mutant is rejected (got %s)", ntabi_status_string(st));

    ntabi_close_handle(c, h1);
    ntabi_close_handle(c, h2);
}

/* --- cross-process tests: the actual point of this daemon -------------- */

static void test_cross_process_event(void) {
    printf("test_cross_process_event:\n");
    ntabi_conn_t *c = connect_with_retry();
    int32_t h;
    ntabi_create_event(c, "ntabi-test-xproc-event", 0, 0, &h);

    pid_t child = fork();
    if (child == 0) {
        usleep(200 * 1000);
        ntabi_conn_t *cc = ntabi_connect();
        int32_t ch;
        ntabi_open_event(cc, "ntabi-test-xproc-event", &ch);
        ntabi_set_event(cc, ch);
        ntabi_disconnect(cc);
        _exit(0);
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    ntabi_status_t st = ntabi_wait_single(c, h, 2000);
    long ms = elapsed_ms(&start);

    int wstatus;
    waitpid(child, &wstatus, 0);

    CHECK(st == NTABI_STATUS_SUCCESS, "parent's wait unblocked when child (separate process) set_event'd");
    CHECK(ms >= 150 && ms < 1500, "parent actually blocked (~200ms expected, got %ldms) - not a busy-poll race", ms);

    ntabi_close_handle(c, h);
    ntabi_disconnect(c);
}

static void test_cross_process_mutant(void) {
    printf("test_cross_process_mutant:\n");
    ntabi_conn_t *c = connect_with_retry();
    int32_t h;
    ntabi_create_mutant(c, "ntabi-test-xproc-mutant", 0, &h);
    ntabi_status_t acquired = ntabi_wait_single(c, h, 100); /* parent takes ownership */

    pid_t child = fork();
    if (child == 0) {
        ntabi_conn_t *cc = ntabi_connect();
        int32_t ch;
        ntabi_open_mutant(cc, "ntabi-test-xproc-mutant", &ch);
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        ntabi_status_t st = ntabi_wait_single(cc, ch, 2000); /* blocks until parent releases */
        long ms = elapsed_ms(&start);
        ntabi_disconnect(cc);
        int ok = (st == NTABI_STATUS_SUCCESS) && (ms >= 150 && ms < 1500);
        _exit(ok ? 0 : 1);
    }

    usleep(200 * 1000);
    ntabi_status_t released = ntabi_release_mutant(c, h);

    int wstatus;
    waitpid(child, &wstatus, 0);

    CHECK(acquired == NTABI_STATUS_SUCCESS, "parent acquired the unowned mutant immediately");
    CHECK(released == NTABI_STATUS_SUCCESS, "parent released it");
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "child (blocked waiter) acquired ownership only after parent released, and within the expected window");

    ntabi_close_handle(c, h);
    ntabi_disconnect(c);
}

/* --- Phase 3 tests -------------------------------------------------------*/

static void test_per_process_handle_isolation(void) {
    printf("test_per_process_handle_isolation:\n");
    ntabi_conn_t *c1 = connect_with_retry();
    int32_t h1;
    ntabi_create_event(c1, NULL, 0, 0, &h1);

    pid_t child = fork();
    if (child == 0) {
        ntabi_conn_t *c2 = ntabi_connect();
        int32_t h2;
        ntabi_create_event(c2, NULL, 0, 0, &h2);
        /* Child's own first handle is very likely also "1" (its own table
         * starts fresh) - the real test is that neither process can use
         * the other's handle *number* to reach the other's object. */
        int same_number = (h2 == h1);

        /* Using the parent's handle number (whether or not it happens to
         * equal h2) against an operation must fail unless it also
         * happens to be a valid handle in *this* process's own table. */
        (void)same_number; /* informational only - true or false is a valid outcome, not itself checked */
        ntabi_status_t st = ntabi_set_event(c2, h1 == h2 ? h1 + 1000 : h1);
        int ok = (st == NTABI_STATUS_INVALID_HANDLE);

        ntabi_disconnect(c2);
        _exit(ok ? 0 : 1);
    }

    int wstatus;
    waitpid(child, &wstatus, 0);
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "child using an unrelated/parent-scoped handle number gets INVALID_HANDLE, not the parent's object");

    /* Now the isolation-in-the-other-direction case: parent closes its
     * own handle; a *different* process holding the numerically-same
     * handle for a *different* object must be unaffected. This needs a
     * second live child, so re-run with a synchronization event. */
    int32_t sync_h;
    ntabi_create_event(c1, "ntabi-test-isolation-sync", 0, 0, &sync_h);
    pid_t child2 = fork();
    if (child2 == 0) {
        ntabi_conn_t *c2 = ntabi_connect();
        int32_t h2;
        ntabi_create_event(c2, NULL, 0, 0, &h2); /* independent object, likely same local number as h1 */
        int32_t syncd;
        ntabi_open_event(c2, "ntabi-test-isolation-sync", &syncd);
        ntabi_wait_single(c2, syncd, 2000); /* wait for parent to have closed h1 */
        ntabi_status_t st = ntabi_set_event(c2, h2); /* this process's own handle must still work */
        ntabi_disconnect(c2);
        _exit(st == NTABI_STATUS_SUCCESS ? 0 : 1);
    }
    ntabi_close_handle(c1, h1); /* closing parent's handle must not disturb child2's numerically-same one */
    ntabi_set_event(c1, sync_h);
    waitpid(child2, &wstatus, 0);
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "closing one process's handle doesn't disturb another process's numerically-same handle");

    ntabi_close_handle(c1, sync_h);
    ntabi_disconnect(c1);
}

static void test_wait_any(void) {
    printf("test_wait_any:\n");
    ntabi_conn_t *c = connect_with_retry();
    int32_t h[3];
    ntabi_create_event(c, "ntabi-test-waitany-0", 0, 0, &h[0]);
    ntabi_create_event(c, "ntabi-test-waitany-1", 0, 0, &h[1]);
    ntabi_create_event(c, "ntabi-test-waitany-2", 0, 0, &h[2]);

    pid_t child = fork();
    if (child == 0) {
        ntabi_conn_t *cc = ntabi_connect();
        int32_t ch[3];
        ntabi_open_event(cc, "ntabi-test-waitany-0", &ch[0]);
        ntabi_open_event(cc, "ntabi-test-waitany-1", &ch[1]);
        ntabi_open_event(cc, "ntabi-test-waitany-2", &ch[2]);
        int32_t idx = -1;
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        ntabi_status_t st = ntabi_wait_multiple(cc, ch, 3, 0 /* any */, 2000, &idx);
        long ms = elapsed_ms(&start);
        ntabi_disconnect(cc);
        _exit((st == NTABI_STATUS_SUCCESS && idx == 1 && ms >= 150 && ms < 1500) ? 0 : 1);
    }

    usleep(200 * 1000);
    ntabi_set_event(c, h[1]); /* only the middle one - child must report index 1, not 0 or 2 */

    int wstatus;
    waitpid(child, &wstatus, 0);
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "wait-any unblocks on the specific event signaled (index 1 of 3), after really blocking");

    ntabi_close_handle(c, h[0]); ntabi_close_handle(c, h[1]); ntabi_close_handle(c, h[2]);
    ntabi_disconnect(c);
}

static void test_wait_all(void) {
    printf("test_wait_all:\n");
    ntabi_conn_t *c = connect_with_retry();
    int32_t h[2];
    ntabi_create_event(c, "ntabi-test-waitall-0", 0, 0, &h[0]);
    ntabi_create_event(c, "ntabi-test-waitall-1", 0, 0, &h[1]);

    pid_t child = fork();
    if (child == 0) {
        ntabi_conn_t *cc = ntabi_connect();
        int32_t ch[2];
        ntabi_open_event(cc, "ntabi-test-waitall-0", &ch[0]);
        ntabi_open_event(cc, "ntabi-test-waitall-1", &ch[1]);
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        ntabi_status_t st = ntabi_wait_multiple(cc, ch, 2, 1 /* all */, 2000, NULL);
        long ms = elapsed_ms(&start);
        ntabi_disconnect(cc);
        /* Must not return until *both* are signaled - i.e. not right after
         * the first one at ~0ms, only after the second at ~150ms+. */
        _exit((st == NTABI_STATUS_SUCCESS && ms >= 100 && ms < 1500) ? 0 : 1);
    }

    ntabi_set_event(c, h[0]); /* immediately - wait-all must NOT unblock yet */
    usleep(150 * 1000);
    ntabi_set_event(c, h[1]); /* now both are signaled - wait-all should unblock */

    int wstatus;
    waitpid(child, &wstatus, 0);
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "wait-all blocks until every handle is signaled, not just the first one");

    ntabi_close_handle(c, h[0]); ntabi_close_handle(c, h[1]);
    ntabi_disconnect(c);
}

static void test_sections(void) {
    printf("test_sections:\n");
    ntabi_conn_t *c = connect_with_retry();
    ntabi_section_info_t sec;
    ntabi_status_t st = ntabi_create_section(c, "ntabi-test-section", 4096, &sec);
    CHECK(st == NTABI_STATUS_SUCCESS && sec.size == 4096, "create 4096-byte named section");

    void *ptr;
    st = ntabi_map_view_of_section(&sec, &ptr);
    CHECK(st == NTABI_STATUS_SUCCESS, "map view in creating process");
    memcpy(ptr, "hello from parent", 18);

    int32_t sync_h;
    ntabi_create_event(c, "ntabi-test-section-sync", 0, 0, &sync_h);

    pid_t child = fork();
    if (child == 0) {
        ntabi_conn_t *cc = ntabi_connect();
        int32_t syncd;
        ntabi_open_event(cc, "ntabi-test-section-sync", &syncd);
        ntabi_wait_single(cc, syncd, 2000);

        ntabi_section_info_t sec2;
        ntabi_status_t st2 = ntabi_open_section(cc, "ntabi-test-section", &sec2);
        int ok = (st2 == NTABI_STATUS_SUCCESS && sec2.size == 4096);
        void *ptr2 = NULL;
        if (ok) ok = (ntabi_map_view_of_section(&sec2, &ptr2) == NTABI_STATUS_SUCCESS);
        if (ok) ok = (memcmp(ptr2, "hello from parent", 18) == 0);
        if (ok) memcpy((char *)ptr2 + 100, "reply from child", 17); /* write back, parent checks it */
        ntabi_disconnect(cc);
        _exit(ok ? 0 : 1);
    }

    ntabi_set_event(c, sync_h);
    int wstatus;
    waitpid(child, &wstatus, 0);
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "child opened the same named section and read exactly what the parent wrote");
    CHECK(memcmp((char *)ptr + 100, "reply from child", 17) == 0,
          "child's write-back through the same mapping is visible to the parent - genuinely shared memory");

    ntabi_unmap_view_of_section(ptr, 4096);
    ntabi_close_handle(c, sec.handle); /* also exercised: ntd unlinks the backing shm on last-handle close */
    ntabi_close_handle(c, sync_h);
    ntabi_disconnect(c);
}

static void test_completion_port(void) {
    printf("test_completion_port:\n");
    ntabi_conn_t *c = connect_with_retry();
    int32_t h;
    ntabi_create_completion(c, "ntabi-test-completion", &h);

    ntabi_post_completion(c, h, 100, 1000, 0xaaaa);
    ntabi_post_completion(c, h, 200, 2000, 0xbbbb);
    ntabi_post_completion(c, h, 300, 3000, 0xcccc);

    int32_t key; int64_t bytes; uint64_t ov;
    ntabi_remove_completion(c, h, 100, &key, &bytes, &ov);
    int fifo_ok = (key == 100 && bytes == 1000 && ov == 0xaaaa);
    ntabi_remove_completion(c, h, 100, &key, &bytes, &ov);
    fifo_ok = fifo_ok && (key == 200 && bytes == 2000 && ov == 0xbbbb);
    ntabi_remove_completion(c, h, 100, &key, &bytes, &ov);
    fifo_ok = fifo_ok && (key == 300 && bytes == 3000 && ov == 0xcccc);
    CHECK(fifo_ok, "three posted completions dequeue in FIFO order with correct payloads");

    pid_t child = fork();
    if (child == 0) {
        /* Per-process handle tables (Phase 3): the child needs its own
         * handle to the port, opened by name - it can't reuse the
         * parent's raw handle number (see test_per_process_handle_isolation). */
        ntabi_conn_t *cc = ntabi_connect();
        int32_t hc;
        ntabi_open_completion(cc, "ntabi-test-completion", &hc);
        int32_t k; int64_t b; uint64_t o;
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        ntabi_status_t st = ntabi_remove_completion(cc, hc, 2000, &k, &b, &o);
        long ms = elapsed_ms(&start);
        ntabi_disconnect(cc);
        _exit((st == NTABI_STATUS_SUCCESS && k == 42 && ms >= 150 && ms < 1500) ? 0 : 1);
    }

    usleep(200 * 1000);
    ntabi_post_completion(c, h, 42, 4096, 0xdead);

    int wstatus;
    waitpid(child, &wstatus, 0);
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "blocked RemoveCompletion in another process unblocks when this process posts, with correct payload");

    ntabi_close_handle(c, h);
    ntabi_disconnect(c);
}

static void test_process_object(void) {
    printf("test_process_object:\n");
    /* Target is a child of *this test harness*, not of ntd - proving ntd's
     * pidfd-based exit detection works for an arbitrary permitted process,
     * not just ntd's own children (which plain waitpid() would have been
     * limited to). */
    pid_t target = fork();
    if (target == 0) {
        usleep(200 * 1000);
        _exit(0);
    }

    ntabi_conn_t *c = connect_with_retry();
    int32_t h;
    ntabi_status_t st = ntabi_open_process(c, (int32_t)target, &h);
    CHECK(st == NTABI_STATUS_SUCCESS, "OpenProcess on a real, currently-running, unrelated pid succeeds");

    ntabi_status_t early = ntabi_wait_single(c, h, 50);
    CHECK(early == NTABI_STATUS_TIMEOUT, "process handle not signaled while the target is still alive");

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    ntabi_status_t st2 = ntabi_wait_single(c, h, 2000);
    long ms = elapsed_ms(&start);
    CHECK(st2 == NTABI_STATUS_SUCCESS && ms < 1500,
          "process handle becomes signaled once the target actually exits (ntd detected it via pidfd, got %s, %ldms)",
          ntabi_status_string(st2), ms);

    int32_t bogus_pid = 999999; /* astronomically unlikely to be a real pid */
    int32_t h2;
    ntabi_status_t st3 = ntabi_open_process(c, bogus_pid, &h2);
    CHECK(st3 == NTABI_STATUS_PROCESS_NOT_FOUND, "OpenProcess on a nonexistent pid is rejected correctly (got %s)",
          ntabi_status_string(st3));

    waitpid(target, NULL, 0); /* reap - this test harness is target's real parent */
    ntabi_close_handle(c, h);
    ntabi_disconnect(c);
}

/* --- Thread objects + APCs (Phase 12) ----------------------------------- */

typedef struct { int report_fd; int sleep_ms; } worker_arg_t;

static void *worker_thread(void *arg_) {
    worker_arg_t *arg = arg_;
    pid_t tid = test_gettid();
    /* Report our own kernel tid back to the parent before sleeping, so the
     * parent can OpenThread this *specific* thread without racing its
     * creation. */
    if (write(arg->report_fd, &tid, sizeof(tid)) != sizeof(tid)) { /* best-effort; parent's read() length check catches this */ }
    usleep((useconds_t)arg->sleep_ms * 1000);
    return NULL;
}

/* Proves ntd's Thread-object granularity is real: two threads in the SAME
 * target process, with different lifetimes, each get their own handle
 * that signals only on THAT thread's exit - not the process's, and not
 * its sibling's. A Process object (test_process_object) couldn't tell
 * these apart; that's the entire reason Thread objects exist. */
static void test_thread_object(void) {
    printf("test_thread_object:\n");

    int short_pipe[2], long_pipe[2];
    if (pipe(short_pipe) != 0 || pipe(long_pipe) != 0) { perror("pipe"); return; }

    pid_t target = fork();
    if (target == 0) {
        close(short_pipe[0]);
        close(long_pipe[0]);
        worker_arg_t short_arg = { short_pipe[1], 150 };
        worker_arg_t long_arg  = { long_pipe[1], 600 };
        pthread_t t1, t2;
        pthread_create(&t1, NULL, worker_thread, &short_arg);
        pthread_create(&t2, NULL, worker_thread, &long_arg);
        pthread_join(t1, NULL);
        pthread_join(t2, NULL);
        close(short_pipe[1]);
        close(long_pipe[1]);
        _exit(0);
    }
    close(short_pipe[1]);
    close(long_pipe[1]);

    pid_t short_tid = 0, long_tid = 0;
    ssize_t n1 = read(short_pipe[0], &short_tid, sizeof(short_tid));
    ssize_t n2 = read(long_pipe[0], &long_tid, sizeof(long_tid));
    close(short_pipe[0]);
    close(long_pipe[0]);
    CHECK(n1 == (ssize_t)sizeof(short_tid) && n2 == (ssize_t)sizeof(long_tid) && short_tid != long_tid,
          "got two distinct real kernel tids for the target's two worker threads (%d, %d)",
          (int)short_tid, (int)long_tid);

    ntabi_conn_t *c = connect_with_retry();

    int32_t h_short, h_long;
    ntabi_status_t st = ntabi_open_thread(c, (int32_t)target, (int32_t)short_tid, &h_short);
    CHECK(st == NTABI_STATUS_SUCCESS, "OpenThread on the short-lived worker thread succeeds");
    st = ntabi_open_thread(c, (int32_t)target, (int32_t)long_tid, &h_long);
    CHECK(st == NTABI_STATUS_SUCCESS, "OpenThread on the long-lived worker thread succeeds");

    ntabi_status_t early = ntabi_wait_single(c, h_long, 50);
    CHECK(early == NTABI_STATUS_TIMEOUT, "long-lived thread's handle not signaled while it's still alive");

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    ntabi_status_t st_short = ntabi_wait_single(c, h_short, 2000);
    long ms_short = elapsed_ms(&start);
    CHECK(st_short == NTABI_STATUS_SUCCESS && ms_short < 500,
          "short-lived thread's handle signals once THAT specific thread exits (got %s, %ldms)",
          ntabi_status_string(st_short), ms_short);

    ntabi_status_t still_long = ntabi_wait_single(c, h_long, 50);
    CHECK(still_long == NTABI_STATUS_TIMEOUT,
          "sibling (long-lived) thread's handle is UNAFFECTED by the short thread's exit - real per-thread granularity");

    clock_gettime(CLOCK_MONOTONIC, &start);
    ntabi_status_t st_long = ntabi_wait_single(c, h_long, 2000);
    long ms_long = elapsed_ms(&start);
    CHECK(st_long == NTABI_STATUS_SUCCESS && ms_long < 1000,
          "long-lived thread's handle eventually signals once it too exits (got %s, %ldms)",
          ntabi_status_string(st_long), ms_long);

    int32_t bogus_h;
    ntabi_status_t st_bogus = ntabi_open_thread(c, (int32_t)target, 999999, &bogus_h);
    CHECK(st_bogus == NTABI_STATUS_THREAD_NOT_FOUND, "OpenThread on a nonexistent tid is rejected correctly (got %s)",
          ntabi_status_string(st_bogus));

    waitpid(target, NULL, 0); /* reap - this test harness is target's real parent */
    ntabi_close_handle(c, h_short);
    ntabi_close_handle(c, h_long);
    ntabi_disconnect(c);
}

/* Proves the documented alertable-wait delivery timing precisely: a
 * pending APC short-circuits the NEXT alertable wait immediately (without
 * touching the waited-on object), a plain (non-alertable) wait is never
 * interrupted by one, and an alertable wait with no pending APC behaves
 * exactly like a normal wait. All against this test harness's own thread,
 * which is a legitimate target - "self" is exactly how a real alertable
 * wait discovers its own pending APCs. */
static void test_apc(ntabi_conn_t *c) {
    printf("test_apc:\n");

    pid_t self_pid = getpid();
    pid_t self_tid = test_gettid();
    int32_t th;
    ntabi_status_t st = ntabi_open_thread(c, (int32_t)self_pid, (int32_t)self_tid, &th);
    CHECK(st == NTABI_STATUS_SUCCESS, "OpenThread on this test harness's own (pid,tid) succeeds");

    int32_t ev;
    ntabi_create_event(c, NULL, 0, 0, &ev); /* never signaled by anything in this test */

    st = ntabi_queue_apc(c, th, 0xDEADBEEFULL, 1, 2, 3);
    CHECK(st == NTABI_STATUS_SUCCESS, "QueueApc on own thread handle succeeds");

    ntabi_apc_info_t apc = {0};
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    st = ntabi_wait_single_alertable(c, ev, 2000, &apc);
    long ms = elapsed_ms(&start);
    CHECK(st == NTABI_STATUS_USER_APC && ms < 300,
          "alertable wait short-circuits immediately on a pending APC rather than blocking (got %s, %ldms)",
          ntabi_status_string(st), ms);
    CHECK(apc.routine == 0xDEADBEEFULL && apc.arg1 == 1 && apc.arg2 == 2 && apc.arg3 == 3,
          "delivered APC carries the exact queued routine/args");

    st = ntabi_wait_single(c, ev, 50);
    CHECK(st == NTABI_STATUS_TIMEOUT,
          "the waited-on object itself was never touched by the APC short-circuit (still unsignaled)");

    clock_gettime(CLOCK_MONOTONIC, &start);
    st = ntabi_wait_single_alertable(c, ev, 100, &apc);
    ms = elapsed_ms(&start);
    CHECK(st == NTABI_STATUS_TIMEOUT && ms >= 90,
          "with no pending APC left, an alertable wait behaves like a normal wait (blocked for the real timeout, got %ldms)", ms);

    ntabi_queue_apc(c, th, 0x1234ULL, 0, 0, 0);
    st = ntabi_wait_single(c, ev, 50);
    CHECK(st == NTABI_STATUS_TIMEOUT, "a pending APC does NOT interrupt a non-alertable wait");

    st = ntabi_wait_single_alertable(c, ev, 100, &apc);
    CHECK(st == NTABI_STATUS_USER_APC && apc.routine == 0x1234ULL,
          "the APC skipped by the non-alertable wait is still delivered by the next alertable one");

    ntabi_close_handle(c, ev);
    ntabi_close_handle(c, th);
}

/* Real integer accounting only (see ntd.c's handle_suspend_thread comment)
 * - checks the exact NT return-value convention (report the count BEFORE
 * this call's adjustment) and that ResumeThread clamps at 0. */
static void test_suspend_resume(ntabi_conn_t *c) {
    printf("test_suspend_resume:\n");

    pid_t self_pid = getpid();
    pid_t self_tid = test_gettid();
    int32_t th;
    ntabi_status_t st = ntabi_open_thread(c, (int32_t)self_pid, (int32_t)self_tid, &th);
    CHECK(st == NTABI_STATUS_SUCCESS, "OpenThread for suspend/resume test succeeds");

    int32_t prev;
    st = ntabi_suspend_thread(c, th, &prev);
    CHECK(st == NTABI_STATUS_SUCCESS && prev == 0, "first SuspendThread reports previous count 0 (got %d)", prev);
    st = ntabi_suspend_thread(c, th, &prev);
    CHECK(st == NTABI_STATUS_SUCCESS && prev == 1, "second SuspendThread reports previous count 1, accumulates (got %d)", prev);
    st = ntabi_resume_thread(c, th, &prev);
    CHECK(st == NTABI_STATUS_SUCCESS && prev == 2, "first ResumeThread reports previous count 2 (got %d)", prev);
    st = ntabi_resume_thread(c, th, &prev);
    CHECK(st == NTABI_STATUS_SUCCESS && prev == 1, "second ResumeThread reports previous count 1, net back to 0 (got %d)", prev);
    st = ntabi_resume_thread(c, th, &prev);
    CHECK(st == NTABI_STATUS_SUCCESS && prev == 0,
          "ResumeThread on an already-zero count reports previous 0 and clamps rather than going negative (got %d)", prev);

    ntabi_close_handle(c, th);
}

int main(int argc, char *argv[]) {
    const char *ntd_path = argc > 1 ? argv[1] : "../../ntd/ntd";

    shm_unlink(NTABI_SHM_NAME); /* clean slate in case a prior run crashed */
    pid_t ntd_pid = start_ntd(ntd_path);
    usleep(100 * 1000); /* let ntd create the shm segment before we probe for it */

    ntabi_conn_t *c = connect_with_retry();
    if (!c) {
        fprintf(stderr, "test_ntabi: could not connect to ntd at %s\n", ntd_path);
        kill(ntd_pid, SIGTERM);
        waitpid(ntd_pid, NULL, 0);
        return 2;
    }

    test_auto_reset_event(c);
    test_manual_reset_event(c);
    test_semaphore(c);
    test_naming(c);
    ntabi_disconnect(c);

    test_cross_process_event();
    test_cross_process_mutant();

    test_per_process_handle_isolation();
    test_wait_any();
    test_wait_all();
    test_sections();
    test_completion_port();
    test_process_object();
    test_thread_object();

    ntabi_conn_t *c2 = connect_with_retry();
    test_apc(c2);
    test_suspend_resume(c2);
    ntabi_disconnect(c2);

    kill(ntd_pid, SIGTERM);
    waitpid(ntd_pid, NULL, 0);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
