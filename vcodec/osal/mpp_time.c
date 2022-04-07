// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#define MODULE_TAG "mpp_time"
#include <linux/clk.h>
//#include <linux/delay.h>
#include "mpp_time.h"

RK_S64 mpp_time(void)
{
	struct timespec64 time;
	ktime_get_real_ts64(&time);
	return (RK_S64)time.tv_sec * 1000000 + (RK_S64)time.tv_nsec / 1000;
}


