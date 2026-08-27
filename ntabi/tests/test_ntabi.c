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

    st = ntabi_open_event(c, "ntabi-test-named", &h2);
    CHECK(st == NTABI_STATUS_SUCCESS && h2 == h1, "open returns same object (handle %d == %d)", h2, h1);

    int32_t h3;
    st = ntabi_open_event(c, "does-not-exist", &h3);
    CHECK(st == NTABI_STATUS_OBJECT_NAME_NOT_FOUND, "opening a nonexistent name fails correctly (got %s)", ntabi_status_string(st));

    st = ntabi_open_mutant(c, "ntabi-test-named", &h3);
    CHECK(st == NTABI_STATUS_OBJECT_TYPE_MISMATCH, "opening an event as a mutant is rejected (got %s)", ntabi_status_string(st));

    ntabi_close_handle(c, h1);
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

    kill(ntd_pid, SIGTERM);
    waitpid(ntd_pid, NULL, 0);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
