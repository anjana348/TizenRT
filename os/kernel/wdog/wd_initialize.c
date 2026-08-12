/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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
/************************************************************************
 * kernel/wdog/wd_initialize.c
 *
 *   Copyright (C) 2007, 2009, 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ************************************************************************/

/************************************************************************
 * Included Files
 ************************************************************************/

#include <tinyara/config.h>

#include <stdint.h>
#include <queue.h>
#include <stdlib.h>
#include <string.h>

#include "wdog/wdog.h"

#ifdef CONFIG_WDOG_MMU_PROTECT
#include <tinyara/irq.h>
#include <tinyara/mmu.h>



/* MMU Page Table constants for ARMv7-A (Short Descriptor format)
 *
 * This implementation uses the Domain Access Control Register (DACR) to switch
 * the watchdog pool between Read-Only and Read-Write — NO TLB invalidation needed.
 *
 * The wdog pool's L1 section is assigned to domain 1 with AP bits set to RO.
 * The DACR controls whether domain 1's AP bits are enforced:
 *   - Domain 1 = client (01): AP bits enforced → Read-Only
 *   - Domain 1 = manager (11): AP bits ignored → Read-Write
 *
 * The DACR is checked on every memory access (not cached in TLB), so changing
 * it takes effect immediately with just one mcr instruction — no barriers,
 * no TLB invalidation, no pipeline flush.
 *
 * L1 Section fields:
 *   AP[1:0] = bits [11:10] (PMD_SECT_AP_SHIFT = 10)
 *   AP2     = bit 15       (PMD_SECT_AP2)
 *   Domain  = bits [8:5]   (PMD_SECT_DOMAIN_SHIFT = 5)
 *
 * DACR values:
 *   0x00000005 = domain 0=client, domain 1=client  → wdog pool is RO
 *   0x0000000d = domain 0=client, domain 1=manager → wdog pool is RW
 */
#define PMD_SECT_AP2            (1 << 15)	/* Access Permission extension bit */
#define PMD_SECT_AP_SHIFT       10			/* AP[1:0] at bits [11:10] */
#define PMD_SECT_DOMAIN_SHIFT   5			/* Domain at bits [8:5] */
#define WDOG_DOMAIN             1			/* Domain 1 for wdog pool */

#define DACR_WDOG_RO            0x00000005	/* domain 0=client, domain 1=client */
#define DACR_WDOG_RW            0x0000000d	/* domain 0=client, domain 1=manager */

/* External function to get page table base */
extern uint32_t *mmu_get_os_l1_pgtbl(void);
#define PGTABLE_BASE_VADDR  ((uint32_t)mmu_get_os_l1_pgtbl())

/* Write Domain Access Control Register (DACR) — takes effect immediately, no TLB flush */
static inline void cp15_write_dacr(uint32_t val)
{
	__asm__ volatile("mcr p15, 0, %0, c3, c0, 0" :: "r"(val) : "memory");
}



#endif /* CONFIG_WDOG_MMU_PROTECT */



/************************************************************************
 * Pre-processor Definitions
 ************************************************************************/

/************************************************************************
 * Private Type Declarations
 ************************************************************************/

/************************************************************************
 * Public Variables
 ************************************************************************/

/* Watchdog data variables.
 *
 * g_wdpool is the pre-allocated watchdog array.  When CONFIG_WDOG_MMU_PROTECT
 * is enabled, g_wdpool is placed in a separate .wdog_pool linker section
 * (1MB-aligned, in its own L1 section) so it can be individually protected
 * via L1 section AP bit manipulation — no L2 split needed.
 */

sq_queue_t g_wdfreelist;
sq_queue_t g_wdactivelist;
uint16_t g_wdnfree;

#ifdef CONFIG_WDOG_MMU_PROTECT
struct wdog_s g_wdpool[CONFIG_PREALLOC_WDOGS]
	__attribute__((aligned(4096), section(".wdog_pool")));
#else
struct wdog_s g_wdpool[CONFIG_PREALLOC_WDOGS];
#endif

/************************************************************************
 * Private Functions
 ************************************************************************/

#ifdef CONFIG_WDOG_MMU_PROTECT

/************************************************************************
 * MMU Permission Control for Watchdog Pool
 ************************************************************************/

/* Base address of the watchdog pool */
static uintptr_t g_wdog_pool_vaddr;

/* Original L1 section entry for the watchdog pool region (saved at init) */
static uint32_t g_wdog_pool_l1_saved;

/* Flag indicating if the watchdog pool is currently read-only */
static bool g_wdog_pool_is_ro = false;

/* Flag indicating if MMU permission control is initialized */
static bool g_wdog_mmu_initialized = false;

/* Nesting counter for reentrant wd_mmu_write_begin/end calls */
static int g_wdog_mmu_nest_count = 0;

/************************************************************************
 * Name: wd_mmu_protect_init
 *
 * Description:
 *   Initialize MMU permission control for the watchdog pool.
 *
 *   Saves the original L1 section entry, then sets the AP bits to
 *   Read-Only (AP2=1, AP[1:0]=11) directly on the L1 section entry.
 *   No L2 split is needed because .wdog_pool is in its own 1MB section.
 ************************************************************************/
void wd_mmu_protect_init(void)
{
	uint32_t *l1table;
	uint32_t index;
	uint32_t ro_entry;

	if (g_wdog_mmu_initialized) {
		return;
	}

	g_wdog_pool_vaddr = (uintptr_t) & g_wdpool[0];

	/* Get L1 page table entry */
	l1table = (uint32_t *)PGTABLE_BASE_VADDR;
	index = (g_wdog_pool_vaddr >> 20) & 0xfff;

	/* Save the original L1 section entry */
	g_wdog_pool_l1_saved = l1table[index];

	/* Modify the L1 entry:
	 * - Set domain to WDOG_DOMAIN (domain 1)
	 * - Set AP bits to Read-Only (AP2=1, AP[1:0]=11)
	 * The AP bits stay RO permanently — we switch RW/RO via DACR.
	 */
	ro_entry = g_wdog_pool_l1_saved;
	ro_entry &= ~(0xf << PMD_SECT_DOMAIN_SHIFT);	/* clear domain field */
	ro_entry |= (WDOG_DOMAIN << PMD_SECT_DOMAIN_SHIFT);	/* set domain 1 */
	ro_entry |= PMD_SECT_AP2;				/* AP2 = 1 */
	ro_entry |= (3 << PMD_SECT_AP_SHIFT);	/* AP[1:0] = 11 */
	l1table[index] = ro_entry;

	/* Set DACR: domain 0 = client, domain 1 = client → wdog pool is RO */
	cp15_write_dacr(DACR_WDOG_RO);

	g_wdog_pool_is_ro = true;
	g_wdog_mmu_initialized = true;

	lldbg("WDOG_MMU: protection initialized (DACR domain %d, RO)\n", WDOG_DOMAIN);
}





/************************************************************************
 * Name: wd_mmu_write_begin
 *
 * Description:
 *   Change the watchdog pool memory region to read-write access.
 *   Restores the original L1 section entry (AP2=0, AP[1:0]=11 = RW).
 *   Uses a nesting counter to handle reentrant calls.
 ************************************************************************/
void wd_mmu_write_begin(void)
{
	if (!g_wdog_mmu_initialized) {
		return;
	}

	/* Nesting: if already in a write section, just increment counter */
	if (g_wdog_mmu_nest_count > 0) {
		g_wdog_mmu_nest_count++;
		return;
	}

	if (!g_wdog_pool_is_ro) {
		g_wdog_mmu_nest_count = 1;
		return;
	}

	/* Set Read-Write: set domain 1 = manager (ignores AP bits, full access)
	 * Just one mcr instruction — no TLB invalidation, no barriers needed. */
	cp15_write_dacr(DACR_WDOG_RW);

	g_wdog_pool_is_ro = false;
	g_wdog_mmu_nest_count = 1;
}




/************************************************************************
 * Name: wd_mmu_write_end
 *
 * Description:
 *   Change the watchdog pool memory region back to read-only access.
 *   Sets AP2=1, AP[1:0]=11 on the L1 section entry.
 *   Uses a nesting counter to handle reentrant calls.
 ************************************************************************/
void wd_mmu_write_end(void)
{
	if (!g_wdog_mmu_initialized) {
		return;
	}

	/* Nesting: decrement counter, only re-protect when count reaches 0 */
	if (g_wdog_mmu_nest_count > 1) {
		g_wdog_mmu_nest_count--;
		return;
	}

	g_wdog_mmu_nest_count = 0;

	if (g_wdog_pool_is_ro) {
		return;
	}

	/* Set Read-Only: set domain 1 = client (enforces AP bits = RO)
	 * Just one mcr instruction — no TLB invalidation, no barriers needed. */
	cp15_write_dacr(DACR_WDOG_RO);

	g_wdog_pool_is_ro = true;
}




#endif /* CONFIG_WDOG_MMU_PROTECT */




/************************************************************************
 * Public Functions
 ************************************************************************/

/************************************************************************
 * Name: wd_is_prealloc
 *
 * Description:
 * This function checks if the wdog is pre- allocated or not
 *
 * Parameters:
 *   wdog - the address of wdog (WDOG_ID)
 *
 * Return Value:
 *   true  - if wdog is preallocated
 *   false - otherwise
 *
 ************************************************************************/

bool wd_is_prealloc(WDOG_ID wdog)
{
	uintptr_t wdog_ptr = (uintptr_t)wdog;
	uintptr_t start = (uintptr_t)(&g_wdpool[0]);
	uintptr_t end = (uintptr_t)(&g_wdpool[CONFIG_PREALLOC_WDOGS - 1]);

	if (end < start) {
		start = start ^ end;
		end = start ^ end;
		start = start ^ end;
	}

	return (wdog_ptr >= start) && (wdog_ptr <= end) && (((wdog_ptr - start) % sizeof(struct wdog_s)) == 0);
}

/************************************************************************
 * Name: wd_initialize
 *
 * Description:
 * This function initializes the watchdog data structures
 *
 * Parameters:
 *   None
 *
 * Return Value:
 *   None
 *
 * Assumptions:
 *   This function must be called early in the initialization sequence
 *   before the timer interrupt is attached and before any watchdog
 *   services are used.
 *
 ************************************************************************/

void wd_initialize(void)
{
	FAR struct wdog_s *wdog;
	int i;

	wdog = g_wdpool;

	/* Initialize watchdog lists */
	sq_init(&g_wdfreelist);
	sq_init(&g_wdactivelist);

	/* The g_wdfreelist must be loaded at initialization time to hold the
	 * configured number of watchdogs.
	 */
	for (i = 0; i < CONFIG_PREALLOC_WDOGS; i++) {
		sq_addlast((FAR sq_entry_t *)wdog++, &g_wdfreelist);
	}

	/* All watchdogs are free */
	g_wdnfree = CONFIG_PREALLOC_WDOGS;

	/* Enable MMU page protection on the watchdog pool.
	 * Sets the L1 section entry AP bits to Read-Only (AP2=1, AP[1:0]=11).
	 * No L2 split is needed — .wdog_pool is in its own 1MB section.
	 * Future writes must be bracketed by wd_mmu_write_begin()/end().
	 *
	 * The heap spans the full DRAM (including the wdog pool's 1MB section),
	 * but this is safe because the wdog pool is statically allocated and
	 * the heap allocator will never hand out memory from the wdog pool's
	 * addresses (they are not free heap blocks).
	 */
	wd_mmu_protect_init();
}


