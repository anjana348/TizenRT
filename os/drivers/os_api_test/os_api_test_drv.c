/****************************************************************************
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
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
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <errno.h>
#include <debug.h>
#include <stdlib.h>
#include <string.h>

#include <tinyara/fs/fs.h>
#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>
#include <tinyara/irq.h>
#include <tinyara/mmu.h>
#include "os_api_test_proto.h"
#include "wdog/wdog.h"


#ifdef CONFIG_EXAMPLES_MEM_PROTECT_TEST
#include <tinyara/binfmt/binfmt.h>
#include <tinyara/mem_protect_test.h>
#include "binary_manager/binary_manager_internal.h"
#endif

/****************************************************************************
 * Public variables
 ****************************************************************************/
#ifdef CONFIG_EXAMPLES_MEM_PROTECT_TEST
extern uint32_t _stext_flash;
extern uint32_t _sdata;
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int os_api_test_drv_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static ssize_t os_api_test_drv_read(FAR struct file *filep, FAR char *buffer, size_t len);
static ssize_t os_api_test_drv_write(FAR struct file *filep, FAR const char *buffer, size_t len);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations os_api_test_drv_fops = {
	0,						/* open */
	0,						/* close */
	os_api_test_drv_read,				/* read */
	os_api_test_drv_write,				/* write */
	0,						/* seek */
	os_api_test_drv_ioctl				/* ioctl */
#ifndef CONFIG_DISABLE_POLL
	, 0						/* poll */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/************************************************************************************
 * Name: os_api_test_drv_ioctl
 *
 * Description:  The standard ioctl method.
 *
 ************************************************************************************/

static int os_api_test_drv_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	/* Handle built-in ioctl commands */

	switch (cmd) {
	/* TESTFWIOC_DRIVER_ANALOG - Run the test case for /os/driver/analog module
	 *
	 *   ioctl argument:  An integer value indicating the particular test to be run
	 */

	case TESTIOC_ANALOG:
		break;
#ifndef CONFIG_DISABLE_SIGNALS
	case TESTIOC_GET_SIG_FINDACTION_ADD:
	case TESTIOC_SIGNAL_PAUSE:
	case TESTIOC_GET_TCB_SIGPROCMASK:
		ret = test_signal(cmd, arg);
		break;
#endif
	case TESTIOC_GET_SELF_PID:
	case TESTIOC_IS_ALIVE_THREAD:
	case TESTIOC_GET_TCB_ADJ_STACK_SIZE:
	case TESTIOC_SCHED_FOREACH:
		ret = test_sched(cmd, arg);
		break;
	case TESTIOC_CLOCK_ABSTIME2TICKS_TEST:
		ret = test_clock(cmd, arg);
		break;
	case TESTIOC_TIMER_INITIALIZE_TEST:
		ret = test_timer(cmd, arg);
		break;
	case TESTIOC_SEM_TICK_WAIT_TEST:
		ret = test_sem(cmd, arg);
		break;
#if defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS)
	case TESTIOC_GROUP_ADD_FINED_REMOVE_TEST:
	case TESTIOC_GROUP_ALLOC_FREE_TEST:
	case TESTIOC_GROUP_EXIT_CHILD_TEST:
	case TESTIOC_GROUP_REMOVECHILDREN_TEST:
		ret = test_group(cmd, arg);
		break;
#endif
	case TESTIOC_TASK_REPARENT:
	case TESTIOC_TASK_INIT_TEST:
		ret = test_task(cmd, arg);
		break;
#ifdef CONFIG_TC_COMPRESS_READ
	case TESTIOC_COMPRESSION_TEST:
		ret = test_compress_decompress(cmd, arg);
		break;
#endif

#ifdef CONFIG_EXAMPLES_MEM_PROTECT_TEST
	case TESTIOC_MEM_PROTECTTEST:
		ret = OK;
		struct mem_protecttest_arg_s *obj = (struct mem_protecttest_arg_s*)arg;
		
		if (!obj) {
			return -EINVAL;
		}

		switch (obj->type) {
		case MEM_PROTECTTEST_KERNEL_CODE:
			obj->addr = &_stext_flash;
			break;
		case MEM_PROTECTTEST_KERNEL_DATA:
			obj->addr = &_sdata;
			break;
		case MEM_PROTECTTEST_APP_ADDR:
		{
			/* Find the current executing app and return an address
			* which belongs to any other app in the system. Here,
			* we choose to return the address of the app heap
			*/
			uint32_t binidx = sched_self()->group->tg_binidx;
			binidx = (binidx + 1) % (binary_manager_get_ucount() + 1);
			if (binidx == 0) {
				binidx++;
			}
			struct tcb_s *tcb = (struct tcb_s *)sched_gettcb(BIN_ID(binidx));
			if (tcb) {
				obj->addr = (volatile uint32_t *)tcb->uheap;
			} else {
				ret = -ESRCH;
			}
			break;
		}
		default:
			ret = -EINVAL;
			break;
		}

		break;
#endif

#ifdef CONFIG_ARMV8M_TRUSTZONE
	case TESTIOC_TZ:
		ret = test_tz();
		break;
#endif

#ifdef CONFIG_EXAMPLES_STACK_PROTECTION
	case TESTIOC_KTHREAD_STACK_PROTECTION_TEST:
		ret = test_kthread_stack_overflow_protection(cmd, arg);
		break;
#endif

#ifdef CONFIG_TC_NET_PBUF
	/* Run the test case for pbuf   */
	case TESTIOC_NET_PBUF:
		ret = test_net_pbuf(cmd, arg);
		break;
#endif
#if defined(CONFIG_AUTOMOUNT_USERFS) && defined(CONFIG_EXAMPLES_TESTCASE_FILESYSTEM)
	case TESTIOC_GET_FS_PARTNO:
		ret = test_fs_get_devname();
		break;
#endif
#ifdef CONFIG_EXAMPLES_MMU_PROTECT_TEST
	case TESTIOC_UAF_MMU_PROTECT: {
		/* Use-after-free detection via MMU page protection.
		 *
		 * arg = 0: read from protected page (UAF read)
		 * arg = 1: write to protected page (UAF write)
		 *
		 * 1. Allocate a page-aligned buffer (simulates a kernel allocation).
		 * 2. Write data to it (normal use).
		 * 3. "Free" it: call mmu_set_page_no_access() to make the page
		 *    inaccessible at any privilege level (AP=000).
		 * 4. Read or write the protected page — the MMU triggers a Data Abort.
		 *
		 * The Data Abort handler (arm_dataabort) prints PC/DFAR/DFSR and
		 * calls PANIC().  DFAR contains the freed page address, confirming
		 * a use-after-free was detected.
		 *
		 * This function does NOT return — the Data Abort is fatal.
		 */
		uint8_t *buf;
		uint32_t page_addr;
		volatile uint8_t val;
		int do_write = (arg == 1);

		buf = (uint8_t *)memalign(4096, 4096 * 2);
		if (!buf) {
			lldbg("UAF test: memalign failed\n");
			return -ENOMEM;
		}

		/* Get page-aligned address */
		page_addr = ((uint32_t)buf + 4095) & ~4095u;

		/* Step 1: Normal use — write data */
		memset((void *)page_addr, 0xAA, 4096);
		lldbg("UAF test: allocated buffer at 0x%08x, page 0x%08x\n",
		      (uint32_t)buf, page_addr);
		lldbg("UAF test: wrote data, buf[0] = 0x%02x\n",
		      ((volatile uint8_t *)page_addr)[0]);

		/* Step 2: Simulate free — protect the page via MMU */
		mmu_set_page_no_access(page_addr);
		lldbg("UAF test: page 0x%08x set to No-Access (simulating free)\n",
		      page_addr);

		/* Step 3: Use-after-free — access the protected page.
		 * The MMU sees AP=000 and raises a permission fault (Data Abort).
		 * DFAR will contain page_addr — the freed address that was accessed.
		 */
		if (do_write) {
			lldbg("UAF test: >>> WRITE to freed memory, expect Data Abort <<<\n");
			((volatile uint8_t *)page_addr)[0] = 0xBB;
		} else {
			lldbg("UAF test: >>> READ from freed memory, expect Data Abort <<<\n");
			val = ((volatile uint8_t *)page_addr)[0];
		}

		/* If we reach here, protection failed */
		(void)val;
		lldbg("UAF test: ERROR — access succeeded, Data Abort NOT triggered!\n");
		mmu_restore_page_pte(page_addr, mmu_save_page_pte(page_addr));
		free(buf);
		ret = -EIO;
		break;
	}
	case TESTIOC_UAF_NO_MMU: {
		/* UAF WITHOUT MMU protection — demonstrates what happens when
		 * freed memory is accessed without any protection.
		 *
		 * 1. Allocate a page-aligned buffer.
		 * 2. Write known data to it.
		 * 3. free() the buffer (real free, no MMU protection).
		 * 4. Read from the freed memory — access succeeds silently,
		 *    returning stale data.  No Data Abort, no detection.
		 * 5. Write to the freed memory — also succeeds silently.
		 *
		 * This shows why MMU protection is needed: without it, a
		 * use-after-free goes completely undetected.
		 */
		uint8_t *buf;
		uint32_t page_addr;
		volatile uint8_t val;

		buf = (uint8_t *)memalign(4096, 4096 * 2);
		if (!buf) {
			lldbg("UAF test (no mmu): memalign failed\n");
			return -ENOMEM;
		}

		page_addr = ((uint32_t)buf + 4095) & ~4095u;

		/* Step 1: Normal use — write data */
		memset((void *)page_addr, 0xAA, 4096);
		lldbg("UAF test (no mmu): allocated buffer at 0x%08x, page 0x%08x\n",
		      (uint32_t)buf, page_addr);
		lldbg("UAF test (no mmu): wrote data, buf[0] = 0x%02x\n",
		      ((volatile uint8_t *)page_addr)[0]);

		/* Step 2: Free the buffer — NO MMU protection */
		free(buf);
		lldbg("UAF test (no mmu): buffer freed (no MMU protection)\n");

		/* Step 3: Use-after-free — read from freed memory.
		 * Without MMU protection, this access succeeds silently.
		 * The data may be stale (0xAA) or garbage if the allocator
		 * has already reused the page.
		 */
		lldbg("UAF test (no mmu): >>> reading freed memory (no protection) <<<\n");
		val = ((volatile uint8_t *)page_addr)[0];
		lldbg("UAF test (no mmu): read succeeded! val = 0x%02x (stale data)\n",
		      val);

		/* Step 4: Use-after-free — write to freed memory.
		 * Without MMU protection, this also succeeds silently,
		 * silently corrupting whatever may now be at that address.
		 */
		lldbg("UAF test (no mmu): >>> writing to freed memory (no protection) <<<\n");
		((volatile uint8_t *)page_addr)[0] = 0xCC;
		lldbg("UAF test (no mmu): write succeeded! (silent corruption)\n");

		lldbg("UAF test (no mmu): NO Data Abort — UAF went UNDETECTED!\n");
		lldbg("UAF test (no mmu): This is why MMU protection is needed.\n");

		ret = OK;
		break;
	}
	case TESTIOC_WDOG_MMU_PROTECT_TEST: {
		/* Verify watchdog MMU page protection.
		 *
		 * This test accesses the wdog static data page (g_wdpool) from

		 * OUTSIDE wdog code — i.e. without calling wd_mmu_write_begin().
		 *
		 * Step 1: Read from the wdog page — should succeed (page is
		 *         Read-Only, reads are allowed).
		 * Step 2: Write to the wdog page — if CONFIG_WDOG_MMU_PROTECT
		 *         is enabled, the MMU triggers a Data Abort (permission
		 *         fault).  DFAR will contain the wdog page address.
		 *
		 * If protection is NOT enabled, the write succeeds but we
		 * immediately restore the original value (with interrupts
		 * disabled so no timer interrupt sees the corruption).
		 *
		 * arg = 0: read only (safe, no crash)
		 * arg = 1: write (crash if protection enabled)
		 */
		volatile uint8_t *wdog_page = (volatile uint8_t *)&g_wdpool;


		int do_write = (arg == 1);
		irqstate_t irq_state;

		lldbg("WDOG MMU protect test: wdog data at 0x%08x\n",
		      (uint32_t)wdog_page);


		/* Step 1: Read — should always succeed (Read-Only allows reads) */
		lldbg("WDOG MMU protect test: reading wdog page (should succeed)...\n");
		volatile uint8_t val = wdog_page[0];
		lldbg("WDOG MMU protect test: read OK, val = 0x%02x\n", val);

		if (do_write) {
			/* Step 2: Write — if CONFIG_WDOG_MMU_PROTECT is enabled,
			 * this triggers a Data Abort.  The page is Read-Only and
			 * we are NOT inside wd_mmu_write_begin()/wd_mmu_write_end().
			 *
			 * DFAR will contain &g_wdpool — the protected wdog address.

			 *
			 * If protection is NOT enabled, the write succeeds.  We
			 * disable interrupts and save/restore the original byte
			 * so the wdog data is not permanently corrupted.  Without
			 * interrupts, no timer tick can fire between the write
			 * and the restore.
			 */
			lldbg("WDOG MMU protect test: >>> WRITE to wdog page, expect Data Abort <<<\n");

			irq_state = enter_critical_section();
			uint8_t orig = wdog_page[0];
			wdog_page[0] = 0xBB;

			/* If we reach here, protection is NOT enabled.
			 * Restore the original value to avoid corrupting wdog data.
			 */
			wdog_page[0] = orig;
			leave_critical_section(irq_state);

			lldbg("WDOG MMU protect test: write succeeded (value restored), no Data Abort!\n");
			lldbg("WDOG MMU protect test: CONFIG_WDOG_MMU_PROTECT is NOT enabled.\n");
			lldbg("WDOG MMU protect test: Wdog page is writable — protection is OFF.\n");
			ret = OK;

		} else {
			lldbg("WDOG MMU protect test: read-only test passed.\n");
			lldbg("WDOG MMU protect test: use arg=1 to test write protection (crash).\n");
			ret = OK;
		}
		break;
	}

#endif
	default:

		vdbg("Unrecognized cmd: %d arg: %ld\n", cmd, arg);
		break;
	}

	return ret;
}


static ssize_t os_api_test_drv_read(FAR struct file *filep, FAR char *buffer, size_t len)
{
	return 0;                                       /* Return EOF */
}

static ssize_t os_api_test_drv_write(FAR struct file *filep, FAR const char *buffer, size_t len)
{
	return len;                                     /* Say that everything was written */
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: os_api_test_drv_register
 *
 * Description:
 *   Register /dev/os_api_test
 *
 ****************************************************************************/

void os_api_test_drv_register(void)
{
	(void)register_driver(OS_API_TEST_DRVPATH, &os_api_test_drv_fops, 0666, NULL);
}
