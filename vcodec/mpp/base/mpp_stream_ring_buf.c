// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_packet"

#include <linux/string.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "mpp_log.h"
#include "mpp_err.h"
#include "mpp_mem.h"
#include "mpp_buffer.h"
#include "mpp_stream_ring_buf.h"

RK_U32 ring_buf_debug = 0;
#define DEFALUT_STRM_CNT_IN_POOL 4
module_param(ring_buf_debug, uint, 0644);
MODULE_PARM_DESC(ring_buf_debug, "bits ring_buf debug information");

MPP_RET ring_buf_init(ring_buf_pool *ctx, MppBuffer buf, RK_U32 max_strm_cnt)
{
	if (!ctx || ctx->init_done) {
		return MPP_NOK;
	}
	if (!max_strm_cnt) {
		max_strm_cnt = DEFALUT_STRM_CNT_IN_POOL;
	}
	ctx->w_pos = 0;
	ctx->r_pos = 0;
	ctx->len = mpp_buffer_get_size(buf);
	ctx->use_len = 0;
	ctx->buf_base = mpp_buffer_get_ptr(buf);
	ctx->buf = buf;
	ctx->mpi_buf_id = mpp_buffer_get_mpi_buf_id(buf);
	ctx->init_done = 1;
	ctx->min_buf_size = (ctx->len / max_strm_cnt + SZ_1K) & (SZ_1K - 1);
	return MPP_OK;
}

RK_U32 ring_buf_get_use_size(ring_buf_pool *ctx)
{
	if (!ctx) {
		return 0;
	}

	if (ctx->r_pos > ctx->w_pos) {
		return ctx->len + ctx->w_pos - ctx->r_pos;
	}
	return ctx->w_pos - ctx->r_pos;
}

MPP_RET ring_buf_put_use(ring_buf_pool *ctx, ring_buf *buf)
{
	RK_U32 w_pos = 0, r_pos = 0;
	RK_U32 start_pos = 0, end_pos = 0;
	RK_U32 use_len = 0;
	if (!ctx || !buf || (ctx->buf != buf->buf)) {
		return MPP_NOK;
	}
	w_pos = ctx->w_pos;
	r_pos = ctx->r_pos;
	start_pos = buf->start_offset;
	end_pos = buf->start_offset + buf->use_len;
	if (w_pos >= r_pos) {
		if ((start_pos < w_pos && start_pos > r_pos) ||
		    (end_pos >= r_pos && end_pos < w_pos)) {
			mpp_err("INVALID param: r_pos=%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, buf->use_len);
		}
	} else {
		if (start_pos >= r_pos || start_pos < w_pos ||
		    end_pos < w_pos || end_pos >= r_pos) {
			mpp_err("INVALID param: r_pos =%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, buf->use_len);
		}
	}

	if (end_pos < r_pos)
		use_len = ctx->len + end_pos - r_pos;
	else
		use_len = end_pos - r_pos;

	ctx->w_pos = end_pos;

	if (ctx->use_len < use_len)
		ctx->use_len = use_len;

	ring_buf_dbg(" pool %p use update ctx->r_pos %d ctx->w_pos %d\n", ctx,
		     ctx->r_pos, ctx->w_pos);

	return MPP_OK;
}

MPP_RET ring_buf_put_free(ring_buf_pool *ctx, ring_buf *buf)
{
	RK_U32 w_pos = 0, r_pos = 0;
	RK_U32 start_pos = 0, end_pos = 0;
	if (!ctx || !buf || (ctx->buf != buf->buf)) {
		return MPP_NOK;
	}
	w_pos = ctx->w_pos;
	r_pos = ctx->r_pos;
	start_pos = buf->start_offset;
	end_pos = buf->start_offset + buf->use_len;
	if (r_pos >= w_pos) {
		if ((end_pos < r_pos && end_pos > w_pos) ||
		    (start_pos >= w_pos && start_pos < r_pos)) {
			mpp_err("INVALID param: r_pos=%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, buf->use_len);
		}
	} else {
		if (start_pos < r_pos || start_pos >= w_pos ||
		    end_pos < r_pos || end_pos > w_pos) {
			mpp_err("INVALID param: r_pos=%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, buf->use_len);
		}
	}
	ctx->r_pos = end_pos;
	ring_buf_dbg(" pool %p free update ctx->r_pos %d ctx->w_pos %d\n", ctx,
		     ctx->r_pos, ctx->w_pos);
	return MPP_OK;
}

MPP_RET ring_buf_get_free(ring_buf_pool *ctx, ring_buf *buf, RK_U32 align,
			  RK_U32 min_size, RK_U32 stream_num)
{
	RK_U32 align_offset = 0;
	RK_U32 align_w_pos = 0;
	RK_U32 w_pos = 0, r_pos = 0;
	if (!ctx || !buf) {
		return MPP_NOK;
	}
	if(min_size < ctx->min_buf_size){
		min_size = ctx->min_buf_size;
	}
	w_pos = ctx->w_pos;
	r_pos = ctx->r_pos;
	buf->mpi_buf_id = ctx->mpi_buf_id;
	ring_buf_dbg("get free pool %p ctx->r_pos %d ctx->w_pos %d\n", ctx,
		     ctx->r_pos, ctx->w_pos);
	if (w_pos == r_pos || !stream_num) {
		ctx->w_pos = 0;
		ctx->r_pos = 0;
		w_pos = r_pos = 0;
		if (ctx->len < min_size) {
			return MPP_NOK;
		}
		align_offset = 0;
		align_w_pos = w_pos + align_offset;
		buf->buf = ctx->buf;
		buf->start_offset = align_w_pos;
		buf->buf_start = ctx->buf_base + buf->start_offset;
		buf->size = ctx->len - align_offset;
	} else {
		if (r_pos > w_pos) {
			align_offset = align - (w_pos & (align - 1));
			if (min_size + w_pos + align_offset >= r_pos)
				return MPP_NOK;
			align_w_pos = w_pos + align_offset;
			buf->start_offset = align_w_pos;
			buf->buf_start = ctx->buf_base + buf->start_offset;

			buf->buf = ctx->buf;

			buf->size = r_pos - w_pos - align_offset;
			return MPP_NOK;
		}

		/* w_pos  > r_pos*/
		if ((min_size + w_pos + align) > ctx->len) {
			/*size no big enough at the tail but r_pos is zero*/
			align_offset = 0;
			if (!r_pos)
				return MPP_NOK;
			/*jumb to start pos*/
			w_pos = 0;
			if (min_size + w_pos + align_offset >= r_pos) {
				return MPP_OK;
			}
			align_w_pos = w_pos + align_offset;

			buf->buf = ctx->buf;
			buf->start_offset = align_w_pos;
			buf->buf_start = ctx->buf_base + buf->start_offset;
			buf->size = r_pos - align_w_pos;
			return MPP_NOK;
		}
		align_offset = align - (w_pos & (align - 1));
		align_w_pos = w_pos + align_offset;
		buf->start_offset = align_w_pos;
		buf->buf_start = ctx->buf_base + buf->start_offset;
		buf->size = ctx->len - align_w_pos;
		buf->buf = ctx->buf;
	}
	return MPP_OK;
}
