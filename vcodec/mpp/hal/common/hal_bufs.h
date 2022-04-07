// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __HAL_BUFS_H__
#define __HAL_BUFS_H__

#include "mpp_buffer.h"

/* HalBuf buffer set allocater */
typedef struct HalBuf_t {
	RK_U32 cnt;
	MppBuffer *buf;
} HalBuf;

typedef void *HalBufs;

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET hal_bufs_init(HalBufs * bufs);
MPP_RET hal_bufs_deinit(HalBufs bufs);

MPP_RET hal_bufs_setup(HalBufs bufs, RK_S32 max_cnt, RK_S32 size_cnt,
		       size_t sizes[]);
HalBuf *hal_bufs_get_buf(HalBufs bufs, RK_S32 buf_idx);

#ifdef __cplusplus
}
#endif
#endif
