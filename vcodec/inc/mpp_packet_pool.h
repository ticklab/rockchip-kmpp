// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_PACKET_POOL_H__
#define __MPP_PACKET_POOL_H__
#include "mpp_packet_impl.h"

#ifdef __cplusplus
extern "C" {
#endif
MPP_RET mpp_packet_pool_init(RK_U32 max_cnt);
MppPacketImpl *mpp_packet_mem_alloc(void);
MPP_RET mpp_packet_mem_free(MppPacketImpl * p);
MPP_RET mpp_packet_pool_deinit(void);
MPP_RET mpp_packet_pool_proc(void *seq_file);
#ifdef __cplusplus
}
#endif
#endif /*__MPP_PACKET_POOL_H__*/
