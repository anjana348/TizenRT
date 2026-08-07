/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * apps/examples/sem_post_assert_test/sem_post_assert_test_main.c
 *
 * Test case to reproduce the semaphore assert at
 * os/kernel/semaphore/sem_holder.c:926:
 *
 *   DEBUGASSERT((sem->semcount > 0 && stcb == NULL) ||
 *               (sem->semcount <= 0 && stcb != NULL));
 *
 * The assert fires inside sem_restorebaseprio(), reached via
 * sem_post() -> sem_unblock_task() -> sem_restorebaseprio().
 *
 * It fires when:
 *   - semcount <= 0  (the count says a waiter exists), AND
 *   - stcb == NULL    (but no valid waiter is found on the wait queue)
 *
 * This happens when a task that was blocked on sem_wait() is cleaned up
 * (its waitsem is nulled / its TCB is invalidated) but the semaphore's
 * semcount is not fully restored, AND the TCB is still on
 * g_waitingforsemaphore.  When a subsequent sem_post() (e.g. from an ISR)
 * runs, it sees semcount <= 0, scans the wait queue, finds no TCB whose
 * waitsem matches, and hits the assert.
 *
 * This test directly manipulates the internal semaphore/TCB state to
 * create exactly that condition.
 *
 * Build
 * -----
 *   CONFIG_EXAMPLES_SEM_POST_ASSERT_TEST=y
 *
 * Usage
 * -----
 *   sem_post_assert_test single   - 1 waiter, state mismatch scenario
 *   sem_post_assert_test multi    - 2 waiters, partial recovery scenario
 *   sem_post_assert_test all      - run all scenarios
 *
 * NOTE: The assert only triggers when CONFIG_DEBUG is enabled (DEBUGASSERT
 * is a no-op otherwise).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sched.h>
#include <errno.h>
#include <assert.h>

#include <tinyara/arch.h>
#include <tinyara/sched.h>
#include <tinyara/clock.h>
#include <tinyara/irq.h>

/* Internal kernel headers - needed for sem_canceled(), sched_gettcb(),
 * and access to sem_t / tcb_s internals.
 */
#include "sched/sched.h"
#include "semaphore/semaphore.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WAITER_PRIORITY     100
#define TEST_STACKSIZE      4096

/* ms to let a waiter task reach sem_wait() and block */
#define WAITER_SETTLE_MS    100

/****************************************************************************
 * Private Data
 ****************************************************************************/

static sem_t g_test_sem;
static volatile bool g_waiter_ready;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: waiter_task
 *
 *   Blocks on sem_wait(&g_test_sem).  Sets g_waiter_ready just before
 *   blocking so the main test knows the waiter has entered the wait.
 *   The sem_wait() call decrements semcount; if it goes <= 0 the task
 *   is placed on g_waitingforsemaphore with waitsem = &g_test_sem and
 *   task_state = TSTATE_WAIT_SEM.
 *
 ****************************************************************************/

static int waiter_task(int argc, char *argv[])
{
	g_waiter_ready = true;
	sem_wait(&g_test_sem);
	return 0;
}

/****************************************************************************
 * Name: spawn_waiter
 *
 *   Creates a waiter task and waits until it has blocked on the semaphore.
 *   Returns the PID, or -1 on failure.
 *
 ****************************************************************************/

static pid_t spawn_waiter(void)
{
	g_waiter_ready = false;

	pid_t pid = task_create("sem_waiter", WAITER_PRIORITY, TEST_STACKSIZE,
				(void *)waiter_task, NULL);
	if (pid < 0) {
		printf("  ERROR: task_create failed\n");
		return -1;
	}

	/* Wait for the waiter to set the flag, then give it time to
	 * actually enter the blocked state inside sem_wait.
	 */
	while (!g_waiter_ready) {
		usleep(1000);
	}
	usleep(WAITER_SETTLE_MS * 1000);
	return pid;
}

/****************************************************************************
 * Name: verify_blocked
 *
 *   Confirms that the given PID is in TSTATE_WAIT_SEM and that
 *   g_test_sem.semcount is negative (one or more waiters).
 *
 ****************************************************************************/

static bool verify_blocked(pid_t pid)
{
	struct tcb_s *tcb = sched_gettcb(pid);
	if (!tcb) {
		printf("  ERROR: cannot find TCB for pid %d\n", pid);
		return false;
	}
	if (tcb->task_state != TSTATE_WAIT_SEM) {
		printf("  ERROR: task state = %d, expected %d (TSTATE_WAIT_SEM)\n",
		       tcb->task_state, TSTATE_WAIT_SEM);
		return false;
	}
	if (tcb->waitsem != &g_test_sem) {
		printf("  ERROR: waitsem = %p, expected %p\n",
		       tcb->waitsem, &g_test_sem);
		return false;
	}
	printf("  OK: pid %d blocked, task_state=WAIT_SEM, semcount=%d\n",
	       pid, g_test_sem.semcount);
	return true;
}

/****************************************************************************
 * Name: trigger_sem_post
 *
 *   Calls sem_post() inside a critical section to simulate an ISR
 *   posting the semaphore.  The assert fires inside sem_post -> 
 *   sem_unblock_task -> sem_restorebaseprio when the semaphore state
 *   is inconsistent.
 *
 ****************************************************************************/

static void trigger_sem_post(void)
{
	irqstate_t flags = enter_critical_section();
	printf("  >>> Calling sem_post (simulated ISR) - expect ASSERT <<<\n");
	sem_post(&g_test_sem);
	leave_critical_section(flags);
}

/****************************************************************************
 * Name: test_single_waiter_state_mismatch
 *
 *   Scenario A: One waiter (semcount = -1).
 *
 *   We simulate the condition where sem_recover()'s guard
 *     if (task_state == TSTATE_WAIT_SEM)
 *   would fail (e.g. the state was changed before recovery), so
 *   semcount is never restored.  At the same time, the waiter's
 *   waitsem is nulled (as recovery would do), but the TCB is left
 *   on g_waitingforsemaphore.
 *
 *   State after manipulation:
 *     semcount  = -1  (not restored)
 *     waitsem   = NULL  (cleared by recovery)
 *     TCB still on g_waitingforsemaphore
 *
 *   sem_post() does:
 *     semcount++  -> 0  (still <= 0)
 *     scan g_waitingforsemaphore
 *     TCB found but waitsem == NULL != &g_test_sem -> skip
 *     stcb = NULL
 *     sem_restorebaseprio(stcb=NULL, ..., sem)
 *       -> DEBUGASSERT((0 <= 0 && NULL != NULL)) -> false
 *                  (0 > 0  && NULL == NULL)  -> false
 *       -> ASSERT FIRES
 *
 ****************************************************************************/

static void test_single_waiter_state_mismatch(void)
{
	printf("\n=== Scenario A: single waiter, state mismatch ===\n");

	sem_init(&g_test_sem, 0, 0);

	pid_t pid = spawn_waiter();
	if (pid < 0) {
		goto cleanup;
	}
	if (!verify_blocked(pid)) {
		goto cleanup;
	}

	/* --- Simulate the inconsistent state --- */
	irqstate_t flags = enter_critical_section();

	struct tcb_s *tcb = sched_gettcb(pid);

	/* Null the waitsem (recovery does this), but do NOT increment
	 * semcount (simulating the guard failing so the increment is
	 * skipped).  Leave the TCB on the wait list.
	 */
#ifndef CONFIG_DISABLE_SIGNALS
	sem_canceled(tcb, &g_test_sem);
#endif
	tcb->waitsem = NULL;

	printf("  Manipulated: waitsem=NULL, semcount=%d (unchanged)\n",
	       g_test_sem.semcount);
	printf("  TCB still on g_waitingforsemaphore, no valid waiter\n");

	leave_critical_section(flags);

	/* --- Trigger the assert --- */
	trigger_sem_post();

	/* If we reach here, CONFIG_DEBUG is probably off. */
	printf("  sem_post returned (assert is a no-op without CONFIG_DEBUG)\n");

cleanup:
	/* Clean up: kill the waiter if still alive. */
	if (pid > 0) {
		task_delete(pid);
	}
	sem_destroy(&g_test_sem);
}

/****************************************************************************
 * Name: test_multi_waiter_partial_recovery
 *
 *   Scenario B: Two waiters (semcount = -2).
 *
 *   Simulate a partial recovery that nulls BOTH waitsems but only
 *   increments semcount ONCE (from -2 to -1).  This can happen when
 *   a recovery loop iterates over holders/waiters but a race or bug
 *   causes the count increment to be applied fewer times than the
 *   number of cleaned-up waiters.
 *
 *   State after manipulation:
 *     semcount  = -1  (was -2, incremented once)
 *     both waitsems = NULL
 *     both TCBs still on g_waitingforsemaphore
 *
 *   sem_post() does:
 *     semcount++  -> 0  (still <= 0)
 *     scan g_waitingforsemaphore
 *     both TCBs have waitsem == NULL -> skip both
 *     stcb = NULL
 *     -> ASSERT FIRES (same as Scenario A)
 *
 ****************************************************************************/

static void test_multi_waiter_partial_recovery(void)
{
	int i;
	int num_waiters = 2;
	pid_t pids[2];

	printf("\n=== Scenario B: %d waiters, partial recovery ===\n", num_waiters);

	sem_init(&g_test_sem, 0, 0);

	for (i = 0; i < num_waiters; i++) {
		pids[i] = spawn_waiter();
		if (pids[i] < 0) {
			goto cleanup;
		}
	}

	printf("  %d waiters blocked, semcount=%d\n", num_waiters, g_test_sem.semcount);

	/* --- Simulate partial recovery --- */
	irqstate_t flags = enter_critical_section();

	for (i = 0; i < num_waiters; i++) {
		struct tcb_s *tcb = sched_gettcb(pids[i]);
		if (tcb && tcb->task_state == TSTATE_WAIT_SEM) {
#ifndef CONFIG_DISABLE_SIGNALS
			sem_canceled(tcb, &g_test_sem);
#endif
			tcb->waitsem = NULL;
			printf("  Nulled waitsem for waiter %d (pid %d)\n", i, pids[i]);
		}
	}

	/* Increment only ONCE instead of num_waiters times. */
	g_test_sem.semcount++;
	printf("  Partial recovery: semcount=%d (should be %d if fully recovered)\n",
	       g_test_sem.semcount, 0);
	printf("  All waitsems nulled, TCBs still on wait list\n");

	leave_critical_section(flags);

	/* --- Trigger the assert --- */
	trigger_sem_post();

	printf("  sem_post returned (assert is a no-op without CONFIG_DEBUG)\n");

cleanup:
	for (i = 0; i < num_waiters; i++) {
		if (pids[i] > 0) {
			task_delete(pids[i]);
		}
	}
	sem_destroy(&g_test_sem);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int sem_post_assert_test_main(int argc, char *argv[])
#endif
{
	printf("\n======================================================\n");
	printf(" sem_post_assert_test\n");
	printf(" Reproduce: sem_holder.c:926\n");
	printf("   DEBUGASSERT((semcount > 0 && stcb == NULL) ||\n");
	printf("               (semcount <= 0 && stcb != NULL))\n");
	printf("======================================================\n");

	if (argc < 2) {
		printf("Usage: sem_post_assert_test <single|multi|all>\n");
		printf("  single - 1 waiter, sem_recover guard mismatch\n");
		printf("  multi  - 2 waiters, partial count recovery\n");
		printf("  all    - run all scenarios\n");
		return 0;
	}

	if (strcmp(argv[1], "single") == 0 || strcmp(argv[1], "all") == 0) {
		test_single_waiter_state_mismatch();
	}

	if (strcmp(argv[1], "multi") == 0 || strcmp(argv[1], "all") == 0) {
		test_multi_waiter_partial_recovery();
	}

	printf("\n=== sem_post_assert_test complete ===\n");
	return 0;
}
