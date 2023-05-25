// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_PACKET_H__
#define __MPP_PACKET_H__

#include "mpp_stream_ring_buf.h"
#include "mpp_device.h"

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET mpp_packet_pool_init(RK_U32 max_cnt);
MPP_RET mpp_packet_pool_deinit(void);
void mpp_packet_pool_info_show(void *seq_file);
/*
 * MppPacket interface
 *
 * mpp_packet_init = mpp_packet_new + mpp_packet_set_data + mpp_packet_set_size
 */
MPP_RET mpp_packet_new(MppPacket *packet);

MPP_RET mpp_packet_new_ring_buf(MppPacket *packet, ring_buf_pool *pool, size_t min_size,
				RK_U32 type, RK_S32 chan_id);

MPP_RET mpp_packet_init(MppPacket *packet, void *data, size_t size);
MPP_RET mpp_packet_init_with_buffer(MppPacket *packet, MppBuffer buffer);
MPP_RET mpp_packet_deinit(MppPacket *packet);
MPP_RET mpp_packet_ring_buf_put_used(MppPacket * packet, RK_S32 chan_id, MppDev dev_ctx);
/*
 * data   : ( R/W ) start address of the whole packet memory
 * size   : ( R/W ) total size of the whole packet memory
 * pos    : ( R/W ) current access position of the whole packet memory, used for buffer read/write
 * length : ( R/W ) the rest length from current position to end of buffer
 *                  NOTE: normally length is updated only by set_pos,
 *                        so set length must be used carefully for special usage
 */
void    mpp_packet_set_data(MppPacket packet, void *data);
void    mpp_packet_set_size(MppPacket packet, size_t size);
void    mpp_packet_set_pos(MppPacket packet, void *pos);
void    mpp_packet_set_length(MppPacket packet, size_t size);

void*   mpp_packet_get_data(const MppPacket packet);
void*   mpp_packet_get_pos(const MppPacket packet);
size_t  mpp_packet_get_size(const MppPacket packet);
size_t  mpp_packet_get_length(const MppPacket packet);


void    mpp_packet_set_pts(MppPacket packet, RK_S64 pts);
RK_S64  mpp_packet_get_pts(const MppPacket packet);
void    mpp_packet_set_dts(MppPacket packet, RK_S64 dts);
RK_S64  mpp_packet_get_dts(const MppPacket packet);

void    mpp_packet_set_flag(MppPacket packet, RK_U32 flag);
RK_U32  mpp_packet_get_flag(const MppPacket packet);

void    mpp_packet_set_temporal_id(MppPacket packet, RK_U32 temporal_id);
RK_U32  mpp_packet_get_temporal_id(const MppPacket packet);

MPP_RET mpp_packet_set_eos(MppPacket packet);
MPP_RET mpp_packet_clr_eos(MppPacket packet);
RK_U32  mpp_packet_get_eos(MppPacket packet);

void        mpp_packet_set_buffer(MppPacket packet, MppBuffer buffer);
MppBuffer   mpp_packet_get_buffer(const MppPacket packet);

/*
 * data access interface
 */
MPP_RET mpp_packet_read(MppPacket packet, size_t offset, void *data, size_t size);
MPP_RET mpp_packet_write(MppPacket packet, size_t offset, void *data, size_t size);

/*
 * multi packet sequence interface for slice/split encoding/decoding
 * partition - the packet is a part of a while image
 * soi - Start Of Image
 * eoi - End Of Image
 */
RK_U32  mpp_packet_is_partition(const MppPacket packet);
RK_U32  mpp_packet_is_soi(const MppPacket packet);
RK_U32  mpp_packet_is_eoi(const MppPacket packet);

#ifdef __cplusplus
}
#endif

#endif /*__MPP_PACKET_H__*/
