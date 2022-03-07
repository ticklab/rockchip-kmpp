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

#include "mpp_log.h"
#include "mpp_err.h"
#include "mpp_mem.h"
#include "mpp_buffer.h"
#include "mpp_stream_ring_buf.h"

MPP_RET ring_buf_init(ring_buff *ctx, MppBuffer buf)
{
	if (!ctx) {
		return MPP_NOK;
	}
	ctx->w_pos = 0;
	ctx->r_pos = 0;
	ctx->len = mpp_buffer_get_size(buf);
	ctx->use_len = 0;
	ctx->buf_base = mpp_buffer_get_ptr(buf);
	ctx->buf = buf;
	return MPP_OK;
}

RK_U32 ring_buf_get_use_size(ring_buff *ctx)
{
	if (!ctx) {
		return 0;
	}

	if (ctx->r_pos > ctx->w_pos) {
		return ctx->len + ctx->w_pos - ctx->r_pos;
	}
	return ctx->w_pos - ctx->r_pos;
}

MPP_RET ring_buf_put_use(ring_buff *ctx, addr_info *info)
{
	RK_U32 w_pos = 0, r_pos = 0;
	RK_U32 start_pos = 0, end_pos = 0;
	RK_U32 use_len = 0;
	if (!ctx || !info || (ctx->buf != info->buf)) {
		return MPP_NOK;
	}
	w_pos = ctx->w_pos;
	r_pos = ctx->r_pos;
	start_pos = info->start_offset;
	end_pos = info->start_offset + info->use_len;
	if (w_pos >= r_pos) {
		if ((start_pos < w_pos && start_pos > r_pos) ||
		    (end_pos >= r_pos && end_pos < w_pos)) {
			mpp_log("INVALID param: r_pos=%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, info->use_len);
		}
	} else {
		if (start_pos >= r_pos || start_pos < w_pos ||
		    end_pos < w_pos || end_pos >= r_pos) {
			mpp_log("INVALID param: r_pos =%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, info->use_len);
		}
	}

	if (end_pos < r_pos)
		use_len = ctx->len + end_pos - r_pos;
	else
		use_len = end_pos - r_pos;

	ctx->w_pos = end_pos;

	if (ctx->use_len < use_len)
		ctx->use_len = use_len;

	return MPP_OK;
}

MPP_RET ring_buf_put_free(ring_buff *ctx, addr_info *info)
{
	RK_U32 w_pos = 0, r_pos = 0;
	RK_U32 start_pos = 0, end_pos = 0;
	if (!ctx || !info || (ctx->buf != info->buf)) {
		return MPP_NOK;
	}
	w_pos = ctx->w_pos;
	r_pos = ctx->r_pos;
	start_pos = info->start_offset;
	end_pos = info->start_offset + info->use_len;
	if (r_pos >= w_pos) {
		if ((end_pos < r_pos && end_pos > w_pos) ||
		    (start_pos >= w_pos && start_pos < r_pos)) {
			mpp_log("INVALID param: r_pos=%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, info->use_len);
		}
	} else {
		if (start_pos < r_pos || start_pos >= w_pos ||
		    end_pos < r_pos || end_pos > w_pos) {
			mpp_log("INVALID param: r_pos=%x, w_pos=%x, start=%x, size=%x\n",
				r_pos, w_pos, start_pos, info->use_len);
		}
	}
	return MPP_OK;
}

MPP_RET ring_buf_get_free(ring_buff *ctx, addr_info *buf_info, RK_U32 align,
			  RK_U32 min_size, RK_U32 stream_num)
{
	RK_U32 align_offset = 0;
	RK_U32 align_w_pos = 0;
	RK_U32 w_pos = 0, r_pos = 0;
	if (!ctx || !buf_info) {
		return MPP_NOK;
	}

	w_pos = ctx->w_pos;
	r_pos = ctx->r_pos;

	if (!stream_num) {
		ctx->w_pos = 0;
		ctx->r_pos = 0;
		if (ctx->len < min_size) {
			return MPP_NOK;
		}
		align_offset = 0;
		align_w_pos = w_pos + align_offset;
		buf_info->buf = ctx->buf;
		buf_info->start_offset = align_w_pos;
		buf_info->buf_start = ctx->buf_base + buf_info->start_offset;
		buf_info->size = ctx->len - align_offset;
	} else {
		if (r_pos > w_pos) {
			align_offset = align - (w_pos & (align - 1));
			if (min_size + w_pos + align_offset >= r_pos)
				return MPP_NOK;
			align_w_pos = w_pos + align_offset;
			buf_info->start_offset = align_w_pos;
			buf_info->buf_start =
				ctx->buf_base + buf_info->start_offset;
			buf_info->size = r_pos - w_pos - align_offset;
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
			buf_info->start_offset = align_w_pos;
			buf_info->buf_start =
				ctx->buf_base + buf_info->start_offset;
			buf_info->size = r_pos - align_w_pos;
			return MPP_NOK;
		}
		align_offset = align - (w_pos & (align - 1));
		align_w_pos = w_pos + align_offset;
		buf_info->start_offset = align_w_pos;
		buf_info->buf_start = ctx->buf_base + buf_info->start_offset;
		buf_info->size = ctx->len - align_w_pos;
	}
	return MPP_OK;
}
