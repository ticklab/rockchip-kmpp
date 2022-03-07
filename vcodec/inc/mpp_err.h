// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_ERR_H__
#define __MPP_ERR_H__

#define RK_OK                   0
#define RK_SUCCESS              0

typedef enum {
	MPP_SUCCESS                 = RK_SUCCESS,
	MPP_OK                      = RK_OK,

	MPP_NOK                     = -1,
	MPP_ERR_UNKNOW              = -2,
	MPP_ERR_NULL_PTR            = -3,
	MPP_ERR_MALLOC              = -4,
	MPP_ERR_OPEN_FILE           = -5,
	MPP_ERR_VALUE               = -6,
	MPP_ERR_READ_BIT            = -7,
	MPP_ERR_TIMEOUT             = -8,
	MPP_ERR_PERM                = -9,

	MPP_ERR_BASE                = -1000,

	/* The error in stream processing */
	MPP_ERR_LIST_STREAM         = MPP_ERR_BASE - 1,
	MPP_ERR_INIT                = MPP_ERR_BASE - 2,
	MPP_ERR_VPU_CODEC_INIT      = MPP_ERR_BASE - 3,
	MPP_ERR_STREAM              = MPP_ERR_BASE - 4,
	MPP_ERR_FATAL_THREAD        = MPP_ERR_BASE - 5,
	MPP_ERR_NOMEM               = MPP_ERR_BASE - 6,
	MPP_ERR_PROTOL              = MPP_ERR_BASE - 7,
	MPP_FAIL_SPLIT_FRAME        = MPP_ERR_BASE - 8,
	MPP_ERR_VPUHW               = MPP_ERR_BASE - 9,
	MPP_EOS_STREAM_REACHED      = MPP_ERR_BASE - 11,
	MPP_ERR_BUFFER_FULL         = MPP_ERR_BASE - 12,
	MPP_ERR_DISPLAY_FULL        = MPP_ERR_BASE - 13,
} MPP_RET;

#endif /*__MPP_ERR_H__*/
