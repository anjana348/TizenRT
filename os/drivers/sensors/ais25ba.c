/****************************************************************************
 * drivers/input/ist415.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <tinyara/fs/fs.h>
#include <tinyara/fs/ioctl.h>

#include <tinyara/i2c.h>
#include <tinyara/audio/audio.h>
#include <tinyara/audio/i2s.h>

#include <tinyara/sensors/sensor.h>
#include <tinyara/sensors/ais25ba.h>

/****************************************************************************
 * Pre-Processor Definitions
 ****************************************************************************/
/****************************************************************************
 * Private Types
 ****************************************************************************/
/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static ssize_t ais25ba_read(FAR struct sensor_upperhalf_s *dev, FAR char *buffer);

/****************************************************************************
 * Private Data
 ****************************************************************************/
struct sensor_ops_s g_ais25ba_ops = {
        .sensor_read = ais25ba_read,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static void ais25ba_verify_sensor(struct i2c_dev_s *i2c, struct i2c_config_s config)
{
        int ret = 0;
        int reg[1];
        uint8_t data[1];
        reg[0] = 0x0F;                                  //WHO_AM_I
        printf("add %8x, freq %d, len%d\n", config.address, config.frequency, config.addrlen);
        ret = i2c_write(i2c, &config, (uint8_t *)reg, 1);
        if (ret == 1) {
                i2c_read(i2c, &config, (uint8_t *)data, 1);
                printf("data read is %8x\n", data[0]); // this should be 0x20
        }
}

static void ais25ba_read_i2c(struct i2c_dev_s *i2c,struct i2c_config_s config, uint8_t reg, int len, uint16_t *data)
{
	lldbg("%d %d %d\n", config.address, config.addrlen, config.frequency);
	uint8_t buffer[3];
    	if (i2c) {
#ifdef CONFIG_I2C_WRITEREAD
		int ret = i2c_writeread(i2c, &config, (uint8_t *)&reg, 1, buffer, len);
                printf("ret %d\n", ret);
#else
		int ret = i2c_write(i2c, &config, (uint8_t *)&reg, 1);
		if (ret != 1) {
			printf("ERROR: i2c write not working\n");
			return;
		}
		ret = i2c_read(i2c, &config, buffer, len);
#endif
		data[0] = (uint16_t)(buffer[0] | (buffer[1] << 8));
		printf("value[0]: %8x | value[1]: %8x | value[2]: %8x | %8x ret: %d\n",
		buffer[0], buffer[1], buffer[2], ret);
	}
}

static void ais25ba_i2s_callback(FAR struct i2s_dev_s *dev, FAR struct ap_buffer_s *apb, FAR void *arg, int result)
{
        lldbg("in callback..........................................\n");
        lldbg("apb=%p nbytes=%d result=%d\n", apb, apb->nbytes, result);

}

static int ais25ba_read_i2s(struct i2s_dev_s *i2s)
{
        struct ap_buffer_s *apb;
        //apb_reference(&apb);
        struct audio_buf_desc_s desc;
        desc.numbytes = 256;
        desc.u.ppBuffer = &apb;

        int ret = apb_alloc(&desc);
        if (ret < 0) {
                printf("alloc fail\n");
                return;
        }
        ret = I2S_RECEIVE(i2s, apb, ais25ba_i2s_callback, NULL, 1000);/* 100 ms timeout for read data */
	printf("i2s receive return %d\n", ret);
        if (ret < 0) {
                printf("ERROR: I2S_RECEIVE returned: %d\n", ret);
        }
	return ret;
}

static ssize_t ais25ba_read(FAR struct sensor_upperhalf_s *dev, FAR char *buffer)
{
	FAR struct ais25ba_dev_s *priv = dev->priv;
	size_t outlen;
	irqstate_t flags;
	int ret;
	uint8_t reg[2];
	uint16_t data[3];
	float Ta_N, To_N;

	struct i2c_dev_s *i2c = priv->i2c;
	struct i2s_dev_s *i2s = priv->i2s;
	struct i2c_config_s config = priv->i2c_config;
	/* Wait for semaphore to prevent concurrent reads */
	ais25ba_verify_sensor(i2c, config);
	ais25ba_read_i2s(i2s);
	
	return OK;
}


int ais25ba_initialize(const char *devpath, struct ais25ba_dev_s *priv)
{
	int ret = 0;

	/* Setup device structure. */

	struct sensor_upperhalf_s *upper = (struct sensor_upperhalf_s *)kmm_zalloc(sizeof(struct sensor_upperhalf_s));
	upper->ops = &g_ais25ba_ops;
	upper->priv = priv;
	priv->upper = upper;

	printf("sensor registered success\n");
	return sensor_register(devpath, upper);
}
