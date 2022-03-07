// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_STREAM_RING_BUF_H__
#define __MPP_STREAM_RING_BUF_H__

#include "rk_type.h"
typedef struct addr_info_t {
	MppBuffer buf;
	void *buf_start;
	RK_U32 start_offset;
	RK_U32 use_len;
	RK_U32 size;
} addr_info;

typedef struct ring_buff_t {
	RK_U32 r_pos;
	RK_U32 w_pos;
	RK_U32 len;
	RK_U32 use_len;
	void *buf_base;
	MppBuffer buf;
} ring_buff;

RK_U32 ring_buf_get_use_size(ring_buff *stream_buf);
MPP_RET ring_buf_put_use(ring_buff *ctx, addr_info *info);
MPP_RET ring_buf_put_free(ring_buff *ctx, addr_info *info);
MPP_RET ring_buf_get_free(ring_buff *ctx, addr_info *buf_info, RK_U32 align,
			  RK_U32 min_size, RK_U32 stream_num);

#endif /*__MPP_STREAM_RING_BUF_H__*/
