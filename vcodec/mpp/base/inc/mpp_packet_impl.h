// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __MPP_PACKET_IMPL_H__
#define __MPP_PACKET_IMPL_H__

#include "rk_type.h"
#include "mpp_stream_ring_buf.h"
#include <linux/slab.h>
#include <linux/of_platform.h>

#define MPP_PACKET_FLAG_EOS             (0x00000001)
#define MPP_PACKET_FLAG_EXTRA_DATA      (0x00000002)
#define MPP_PACKET_FLAG_INTERNAL        (0x00000004)
#define MPP_PACKET_FLAG_EXTERNAL        (0x00000008)
#define MPP_PACKET_FLAG_INTRA           (0x00000010)

typedef union MppPacketStatus_t {
	RK_U32 val;
	struct {
		RK_U32 eos: 1;
		RK_U32 extra_data: 1;
		RK_U32 internal: 1;
		/* packet is inputed on reset mark as discard */
		RK_U32 discard: 1;

		/* for slice input output */
		RK_U32 partition: 1;
		RK_U32 soi: 1;
		RK_U32 eoi: 1;
	};
} MppPacketStatus;

/*
 * mpp_packet_imp structure
 *
 * data     : pointer
 * size     : total buffer size
 * offset   : valid data start offset
 * length   : valid data length
 * pts      : packet pts
 * dts      : packet dts
 */
typedef struct MppPacketImpl_t {
	const char *name;

	struct list_head poo_list;
	struct list_head list;
	struct kref ref;

	void *data;
	void *pos;
	size_t size;
	size_t length;

	RK_S64 pts;
	RK_S64 dts;

	MppPacketStatus status;
	RK_U32 flag;
	RK_U32 temporal_id;

	MppBuffer buffer;

	ring_buf  buf;
	ring_buf_pool *ring_pool;
} MppPacketImpl;

#ifdef __cplusplus
extern "C" {
#endif
/*
 * mpp_packet_reset is only used internelly and should NOT be used outside
 */
MPP_RET mpp_packet_reset(MppPacketImpl * packet);
MPP_RET mpp_packet_copy(MppPacket dst, MppPacket src);
MPP_RET mpp_packet_append(MppPacket dst, MppPacket src);
MPP_RET mpp_packet_set_status(MppPacket packet, MppPacketStatus status);
MPP_RET mpp_packet_get_status(MppPacket packet,
			      MppPacketStatus * status);

/* pointer check function */
MPP_RET check_is_mpp_packet(void *ptr);

#ifdef __cplusplus
}
#endif
#endif /*__MPP_PACKET_IMPL_H__*/
