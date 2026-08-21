/****************************************************************************
 *
 * Copyright 2021 Samsung Electronics All Rights Reserved.
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
#include <sys/types.h>
#include <tinyara/prodconfig.h>
#include <tinyara/fs/fs.h>
#include <tinyara/fs/ioctl.h>
#include <tinyara/kmalloc.h>
#include <tinyara/sched.h>
#include <stdio.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static int prodconfig_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static ssize_t prodconfig_read(FAR struct file *filep, FAR char *buffer, size_t len);
static ssize_t prodconfig_write(FAR struct file *filep, FAR const char *buffer, size_t len);

/****************************************************************************
 * Private Data
 ****************************************************************************/
static void *g_alloc_blocks[100];  /* Track allocated blocks */
static int g_alloc_count = 0;

static const struct file_operations prodconfig_fops = {
	0,                          /* open */
	0,                          /* close */
	prodconfig_read,               /* read */
	prodconfig_write,              /* write */
	0,                          /* seek */
	prodconfig_ioctl               /* ioctl */
#ifndef CONFIG_DISABLE_POLL
	, 0                         /* poll */
#endif
};
/****************************************************************************
 * Private Functions
 ****************************************************************************/
static ssize_t prodconfig_read(FAR struct file *filep, FAR char *buffer, size_t len)
{
	char config = 0;
	int ret;
	ret = up_check_prodswd();
	if (ret == OK) {
		config |= SWD_ENABLED;
	}
	ret = up_check_proddownload();
	if (ret == OK) {
		config |= DOWNLOAD_ENABLED;
	}

	*buffer = config;

	return 0;
}

static ssize_t prodconfig_write(FAR struct file *filep, FAR const char *buffer, size_t len)
{
	return 0;
}

/************************************************************************************
 * Name: prodconfig_ioctl
 *
 * Description: The ioctl method for prodconfig.
 *
 ************************************************************************************/
static int prodconfig_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
	switch (cmd) {
	case PRODIOC_ALLOC:
		{
			/* Allocate kernel memory blocks */
			int num_blocks = (int)arg;
			int i;
			
			if (num_blocks <= 0) {
				num_blocks = 1;
			}
			if (num_blocks > 100) {
				num_blocks = 100;
			}
			
			for (i = 0; i < num_blocks && g_alloc_count < 100; i++) {
				g_alloc_blocks[g_alloc_count] = kmm_malloc(1024);  /* 1KB per block from kernel heap */
				if (g_alloc_blocks[g_alloc_count] != NULL) {
					g_alloc_count++;
				}
			}
			printf("prodconfig: Allocated %d blocks for PID %d (total tracked: %d)\n", i, getpid(), g_alloc_count);
			return OK;
		}
	case PRODIOC_FREE:
		{
			/* Free kernel memory blocks */
			int num_blocks = (int)arg;
			int i;
			
			if (num_blocks <= 0) {
				num_blocks = 1;
			}
			if (num_blocks > g_alloc_count) {
				num_blocks = g_alloc_count;
			}
			
			for (i = 0; i < num_blocks && g_alloc_count > 0; i++) {
				g_alloc_count--;
				if (g_alloc_blocks[g_alloc_count] != NULL) {
					kmm_free(g_alloc_blocks[g_alloc_count]);
					g_alloc_blocks[g_alloc_count] = NULL;
				}
			}
			printf("prodconfig: Freed %d blocks for PID %d (remaining tracked: %d)\n", i, getpid(), g_alloc_count);
			return OK;
		}
	case PRODIOC_LEAK:
		{
			/* Allocate kernel memory without storing the pointer.
			 * This creates an unreachable allocation that the mem_leak
			 * checker will report as a leak (no reference exists in
			 * data, BSS, stack, or heap).
			 */
			int num_blocks = (int)arg;
			int i;

			if (num_blocks <= 0) {
				num_blocks = 1;
			}

			for (i = 0; i < num_blocks; i++) {
				void *leaked = kmm_malloc(1024);
				if (leaked != NULL) {
					/* Deliberately discard the pointer to create a leak */
					leaked = NULL;
				}
			}
			printf("prodconfig: Leaked %d blocks for PID %d (pointer discarded)\n", num_blocks, getpid());
			return OK;
		}
	default:
		return -ENOTTY;
	}
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: prodconfig_register
 *
 * Description:
 *   Register prodconfig driver path, PRODCONFIG_DRVPATH
 *
 ****************************************************************************/

void prodconfig_register(void)
{
	(void)register_driver(PRODCONFIG_DRVPATH, &prodconfig_fops, 0666, NULL);
}
