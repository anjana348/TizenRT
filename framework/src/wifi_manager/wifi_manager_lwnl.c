/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
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
#include <tinyara/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <net/if.h>
#include <tinyara/lwnl/lwnl.h>
#include <tinyara/net/if/wifi.h>
#include <tinyara/net/netlog.h>

#define WU_INTF_NAME "wlan0"
#define WU_INTF_NAME_1 "wlan1"
#define TAG "[WM]"

static inline int _send_msg(lwnl_msg *msg)
{
	int fd = socket(AF_LWNL, SOCK_RAW, LWNL_ROUTE);
	if (fd < 0) {
		NET_LOGE(TAG, "send lwnl msg open error\n");
		return -1;
	}

	int res = write(fd, msg, sizeof(*msg));
	close(fd);
	if (res < 0) {
		NET_LOGE(TAG, "send lwnl msg write error\n");
		return -2;
	}
	return 0;
}

trwifi_result_e wifi_utils_init(void)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_INIT}, 0, NULL, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
#if defined(CONFIG_ENABLE_HOMELYNK) && (CONFIG_ENABLE_HOMELYNK == 1)
	trwifi_result_e res1 = TRWIFI_SUCCESS;
	lwnl_msg msg1 = {WU_INTF_NAME_1, {LWNL_REQ_WIFI_INIT}, 0, NULL, (void *)&res1};
	if (_send_msg(&msg1) < 0) {
		return TRWIFI_FAIL;
	}
	if (res1 != TRWIFI_SUCCESS) {
		return res1;
	}
#endif
	return res;
}

trwifi_result_e wifi_utils_deinit(void)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_DEINIT}, 0, NULL, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
#if defined(CONFIG_ENABLE_HOMELYNK) && (CONFIG_ENABLE_HOMELYNK == 1)
	trwifi_result_e res1 = TRWIFI_SUCCESS;
	lwnl_msg msg1 = {WU_INTF_NAME_1, {LWNL_REQ_WIFI_DEINIT}, 0, NULL, (void *)&res1};
	if (_send_msg(&msg1) < 0) {
		return TRWIFI_FAIL;
	}
	if (res1 != TRWIFI_SUCCESS) {
		return res1;
	}
#endif
	return res;
}

trwifi_result_e wifi_utils_scan_ap(void *arg)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	trwifi_ap_config_s *config = NULL;
	if (arg) {
		config = (trwifi_ap_config_s *)arg;
	}
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_SCANAP}, sizeof(trwifi_ap_config_s), (void *)config, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_connect_ap(trwifi_ap_config_s *ap_connect_config, void *arg)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_CONNECTAP}, sizeof(trwifi_ap_config_s), (void *)ap_connect_config, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_disconnect_ap(void *arg)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_DISCONNECTAP}, 0, NULL, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_start_softap(trwifi_softap_config_s *softap_config)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
#if defined(CONFIG_ENABLE_HOMELYNK) && (CONFIG_ENABLE_HOMELYNK == 1)
	lwnl_msg msg = {WU_INTF_NAME_1, {LWNL_REQ_WIFI_STARTSOFTAP},
				sizeof(trwifi_softap_config_s),
				(void *)softap_config, (void *)&res};
#else
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_STARTSOFTAP},
					sizeof(trwifi_softap_config_s),
					(void *)softap_config, (void *)&res};
#endif
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_start_sta(void)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_STARTSTA}, 0, NULL, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_stop_softap(void)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
#if defined(CONFIG_ENABLE_HOMELYNK) && (CONFIG_ENABLE_HOMELYNK == 1)
		lwnl_msg msg = {WU_INTF_NAME_1, {LWNL_REQ_WIFI_STOPSOFTAP}, 0, NULL, (void *)&res};
#else
		lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_STOPSOFTAP}, 0, NULL, (void *)&res};
#endif
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_set_autoconnect(uint8_t check)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	uint8_t *chk = &check;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_SETAUTOCONNECT}, sizeof(uint8_t), (void *)chk, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_ioctl(trwifi_msg_s *dmsg)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_IOCTL}, sizeof(trwifi_msg_s), (void *)dmsg, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_scan_multi_aps(void *arg)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	trwifi_scan_multi_configs_s *configs = NULL;
	if (arg) {
		configs = (trwifi_scan_multi_configs_s *)arg;
		if (configs->scan_ap_config_count == 0) {
			NET_LOGE(TAG, "scan multi aps MUST at least 1 AP config when the AP configs was included.\n");
			return TRWIFI_FAIL;
		}
	}
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_SCAN_MULTI_APS}, sizeof(trwifi_scan_multi_configs_s), (void *)configs, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_get_info(trwifi_info *wifi_info)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_GETINFO}, sizeof(trwifi_info), (void *)wifi_info, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_set_channel_plan(uint8_t channel_plan)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	uint8_t *plan = &channel_plan;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_SET_CHANNEL_PLAN}, sizeof(uint8_t), (void *)plan, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}

trwifi_result_e wifi_utils_get_signal_quality(trwifi_signal_quality *signal_quality)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_GET_SIGNAL_QUALITY}, sizeof(trwifi_signal_quality), (void *)signal_quality, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}

	return res;
}

trwifi_result_e wifi_utils_get_disconnect_reason(int *disconnect_reason)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_GET_DISCONNECT_REASON}, sizeof(int), (void *)disconnect_reason, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}

	return res;
}

trwifi_result_e wifi_utils_get_driver_info(trwifi_driver_info *driver_info)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_GET_DRIVER_INFO}, sizeof(trwifi_driver_info), (void *)driver_info, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}

	return res;
}

trwifi_result_e wifi_utils_get_wpa_supplicant_state(trwifi_wpa_states *wpa_state)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	lwnl_msg msg = {WU_INTF_NAME, {LWNL_REQ_WIFI_GET_WPA_SUPPLICANT_STATE}, sizeof(trwifi_wpa_states), (void *)wpa_state, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}

	return res;
}

#if defined(CONFIG_ENABLE_HOMELYNK) && (CONFIG_ENABLE_HOMELYNK == 1)
trwifi_result_e wifi_utils_control_bridge(uint8_t enable)
{
	trwifi_result_e res = TRWIFI_SUCCESS;
	uint8_t *control = &enable;
	lwnl_msg msg = {WU_INTF_NAME_1, {LWNL_REQ_WIFI_SETBRIDGE},
					sizeof(uint8_t), (void *)control, (void *)&res};
	if (_send_msg(&msg) < 0) {
		return TRWIFI_FAIL;
	}
	return res;
}
#endif
