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
/****************************************************************************
 * examples/hello/hello_main.c
 *
 *   Copyright (C) 2008, 2011-2012 Gregory Nutt. All rights reserved.
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
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <tinyara/prodconfig.h>
#include <tinyara/fs/ioctl.h>

#define MAX_ALLOCATIONS 100
#define ALLOC_SIZE 1000

/****************************************************************************
 * hello_main
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int hello_main(int argc, char *argv[])
#endif
{
	int fd;
	int num_blocks = 0;
	int ret;
	int i;
	void *user_ptrs[MAX_ALLOCATIONS];
	int kernel_alloc_count = 0;
	int user_alloc_count = 0;

	if (argc > 1) {
		num_blocks = atoi(argv[1]);
		
		if (num_blocks <= 0) {
			printf("Hello, World!! Invalid number of blocks: %s\n", argv[1]);
			printf("Usage: hello <num_blocks> - Allocate and deallocate memory blocks\n");
			return -1;
		}
		
		if (num_blocks > MAX_ALLOCATIONS) {
			printf("Warning: Limiting allocations to %d (max allowed)\n", MAX_ALLOCATIONS);
			num_blocks = MAX_ALLOCATIONS;
		}

		printf("Hello, World!! PID %d\n", getpid());
		printf("Starting memory allocation test for %d blocks...\n", num_blocks);
		printf("Each block: %d bytes (user space) + 1 block (kernel space)\n", ALLOC_SIZE);
		printf("Sleep 2 seconds between each operation.\n");
		printf("NOTE: Pointers are deliberately discarded to create UNREACHABLE leaks.\n");
		printf("Run 'mem_leak' during the 2-second sleep to see the leaks.\n\n");

		/* Initialize pointer array */
		memset(user_ptrs, 0, sizeof(user_ptrs));

		/* Open prodconfig device for kernel allocations */
		fd = open(PRODCONFIG_DRVPATH, O_RDWR);
		if (fd < 0) {
			printf("Failed to open %s device\n", PRODCONFIG_DRVPATH);
			return -1;
		}

		/* ============ ALLOCATION PHASE ============ */
		printf("========== ALLOCATION PHASE ==========\n");
		for (i = 0; i < num_blocks; i++) {
			/* Allocate in user space - then discard the pointer to create
			 * an unreachable leak. The mem_leak checker scans stack, BSS,
			 * and data for references; with no stored pointer, the memory
			 * is unreachable and will be reported as a leak.
			 */
			void *leaked_ptr = malloc(ALLOC_SIZE);
			if (leaked_ptr != NULL) {
				user_alloc_count++;
				printf("[%d/%d] User space: Allocated %d bytes at %p (LEAK: pointer discarded)\n", 
				       i + 1, num_blocks, ALLOC_SIZE, leaked_ptr);
				/* Deliberately lose the pointer - do NOT store it */
				leaked_ptr = NULL;
			} else {
				printf("[%d/%d] User space: Malloc FAILED for block %d\n", 
				       i + 1, num_blocks, i);
			}

			/* Allocate in kernel space with PRODIOC_LEAK - this allocates
			 * kernel memory without storing the pointer, creating an
			 * unreachable leak in the kernel heap.
			 */
			ret = ioctl(fd, PRODIOC_LEAK, 1);
			if (ret >= 0) {
				kernel_alloc_count++;
				printf("[%d/%d] Kernel space: Leaked 1 block via PRODIOC_LEAK (ioctl returned %d)\n", 
				       i + 1, num_blocks, ret);
			} else {
				printf("[%d/%d] Kernel space: ioctl PRODIOC_LEAK FAILED (ret=%d)\n", 
				       i + 1, num_blocks, ret);
			}

			printf("--- Sleeping 2 seconds (run 'mem_leak' now to see leaks) ---\n\n");
			sleep(2);
		}

		close(fd);
		printf("========== ALLOCATION PHASE COMPLETE ==========\n");
		printf("Total user space allocations: %d/%d\n", user_alloc_count, num_blocks);
		printf("Total kernel space allocations: %d/%d\n", kernel_alloc_count, num_blocks);
		printf("\n");

		/* ============ DEALLOCATION PHASE ============ */
		printf("========== DEALLOCATION PHASE ==========\n");
		printf("NOTE: Leaked blocks are NOT freed - they are permanently lost.\n");
		printf("The user-space pointers were discarded, and kernel PRODIOC_LEAK\n");
		printf("blocks were never tracked. Use 'mem_leak' to verify the leaks.\n\n");

		printf("========== DEALLOCATION PHASE COMPLETE ==========\n");
		printf("Memory test completed (leaked blocks remain allocated)!\n");
	} else {
		printf("Hello, World!!\n");
		printf("Usage: hello <num_blocks> - Allocate and deallocate memory blocks\n");
		printf("  num_blocks: Number of times to allocate (max %d)\n", MAX_ALLOCATIONS);
		printf("  Each iteration allocates in both user space and kernel space\n");
		printf("  2 second sleep between each operation\n");
	}

	return 0;
}
