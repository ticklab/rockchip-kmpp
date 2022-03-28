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

extern RK_U32 ring_buf_debug;

#define RING_BUF_DBG           (0x00000001)

#define ring_buf_dbg_f(flag, fmt, ...) _mpp_dbg_f(ring_buf_debug, flag, fmt, ## __VA_ARGS__)
#define ring_buf_dbg(fmt, ...)    ring_buf_dbg_f(RING_BUF_DBG, fmt, ## __VA_ARGS__)

typedef struct ring_buf_t {
	MppBuffer buf;
    RK_S32 mpi_buf_id;
	void *buf_start;
	RK_U32 start_offset;
	RK_U32 use_len;
	RK_U32 size;
} ring_buf;

typedef struct ring_buf_pool_t {
	RK_U32 r_pos;
	RK_U32 w_pos;
	RK_U32 len;
	RK_U32 use_len;
	void *buf_base;
	MppBuffer buf;
    RK_S32 mpi_buf_id;
    RK_U32 init_done;
	RK_U32 min_buf_size;
	RK_U32 l_r_pos;
	RK_U32 l_w_pos;
} ring_buf_pool;

MPP_RET ring_buf_init(ring_buf_pool *ctx, MppBuffer buf, RK_U32 max_strm_cnt);
RK_U32 ring_buf_get_use_size(ring_buf_pool *stream_buf);
MPP_RET ring_buf_put_use(ring_buf_pool *ctx,  ring_buf *buf);
MPP_RET ring_buf_put_free(ring_buf_pool *ctx, ring_buf *buf);
MPP_RET ring_buf_get_free(ring_buf_pool *ctx, ring_buf *buf, RK_U32 align,
			  RK_U32 min_size, RK_U32 stream_num);

#endif /*__MPP_STREAM_RING_BUF_H__*/
