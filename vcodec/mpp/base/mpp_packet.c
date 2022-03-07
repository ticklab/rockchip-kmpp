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
#include "mpp_packet.h"
//#include "mpp_mem_pool.h"
#include "mpp_packet_impl.h"
#include "mpp_buffer.h"
//#include "mpp_meta_impl.h"

static const char *module_name = MODULE_TAG;
//static MppMemPool mpp_packet_pool = mpp_mem_pool_init(sizeof(MppPacketImpl));

#define setup_mpp_packet_name(packet) \
    ((MppPacketImpl*)packet)->name = module_name;

MPP_RET check_is_mpp_packet(void *packet)
{
	if (packet && ((MppPacketImpl *) packet)->name == module_name)
		return MPP_OK;

	mpp_err_f("pointer %p failed on check\n", packet);
	mpp_abort();
	return MPP_NOK;
}

MPP_RET mpp_packet_new(MppPacket * packet)
{
	MppPacketImpl *p = NULL;

	if (NULL == packet) {
		mpp_err_f("invalid NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (MppPacketImpl *) mpp_calloc(MppPacketImpl, 1);
	*packet = p;
	if (NULL == p) {
		mpp_err_f("malloc failed\n");
		return MPP_ERR_NULL_PTR;
	}
	setup_mpp_packet_name(p);

	return MPP_OK;
}

MPP_RET mpp_packet_init(MppPacket * packet, void *data, size_t size)
{
	MppPacketImpl *p = NULL;
	MPP_RET ret = MPP_OK;
	if (NULL == packet) {
		mpp_err_f("invalid NULL input packet\n");
		return MPP_ERR_NULL_PTR;
	}

	ret = mpp_packet_new(packet);
	if (ret) {
		mpp_err_f("new packet failed\n");
		return ret;
	}
	p = (MppPacketImpl *) * packet;
	p->data = p->pos = data;
	p->size = p->length = size;

	return MPP_OK;
}

MPP_RET mpp_packet_init_with_buffer(MppPacket * packet, MppBuffer buffer)
{
	MppPacketImpl *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == packet || NULL == buffer) {
		mpp_err_f("invalid input packet %p buffer %p\n", packet,
			  buffer);
		return MPP_ERR_NULL_PTR;
	}

	ret = mpp_packet_new(packet);
	if (ret) {
		mpp_err_f("new packet failed\n");
		return ret;
	}

	p = (MppPacketImpl *) * packet;
	p->data = p->pos = mpp_buffer_get_ptr(buffer);
	p->size = p->length = mpp_buffer_get_size(buffer);
	p->buffer = buffer;
	mpp_buffer_inc_ref(buffer);

	return MPP_OK;
}

MPP_RET mpp_packet_copy_init(MppPacket * packet, const MppPacket src)
{
	MppPacketImpl *src_impl = NULL;
	MPP_RET ret = MPP_OK;
	MppPacket pkt;

	if (NULL == packet || check_is_mpp_packet(src)) {
		mpp_err_f("found invalid input %p %p\n", packet, src);
		return MPP_ERR_UNKNOW;
	}

	*packet = NULL;

	src_impl = (MppPacketImpl *) src;
	ret = mpp_packet_new(&pkt);
	if (ret)
		return ret;

	/* copy the source data */
	memcpy(pkt, src_impl, sizeof(*src_impl));

	/* increase reference of meta data */
//    if (src_impl->meta)
	//      mpp_meta_inc_ref(src_impl->meta);
#if 0
	if (src_impl->buffer) {
		/* if source packet has buffer just create a new reference to buffer */
		mpp_buffer_inc_ref(src_impl->buffer);
	} else
#endif
	{
		/*
		 * NOTE: only copy valid data
		 */
		size_t length = mpp_packet_get_length(src);
		/*
		 * due to parser may be read 32 bit interface so we must alloc more size
		 * then real size to avoid read carsh
		 */
		MppPacketImpl *p = NULL;
		void *pos = mpp_malloc_size(void, length + 256);
		if (NULL == pos) {
			mpp_err_f("malloc failed, size %d\n", (RK_U32) length);
			mpp_packet_deinit(&pkt);
			return MPP_ERR_MALLOC;
		}

		p = (MppPacketImpl *) pkt;
		p->data = p->pos = pos;
		p->size = p->length = length;
		p->flag |= MPP_PACKET_FLAG_INTERNAL;

		if (length) {
			memcpy(pos, src_impl->pos, length);
			/*
			 * clean more alloc byte to zero
			 */
			memset((RK_U8 *) pos + length, 0, 256);
		}
	}

	*packet = pkt;
	return MPP_OK;
}

MPP_RET mpp_packet_deinit(MppPacket * packet)
{
	MppPacketImpl *p = NULL;

	if (NULL == packet || check_is_mpp_packet(*packet)) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (MppPacketImpl *) (*packet);

	/* release buffer reference */
	if (p->buffer)
		mpp_buffer_put(p->buffer);

	if (p->flag & MPP_PACKET_FLAG_INTERNAL)
		mpp_free(p->data);

	// if (p->meta)
	// mpp_meta_put(p->meta);

	// mpp_mem_pool_put(mpp_packet_pool, *packet);
	mpp_free(*packet);
	*packet = NULL;
	return MPP_OK;
}

void mpp_packet_set_pos(MppPacket packet, void *pos)
{
	MppPacketImpl *p = NULL;
	size_t offset = 0;
	size_t diff = 0;

	if (check_is_mpp_packet(packet))
		return;

	p = (MppPacketImpl *) packet;
	offset = (RK_U8 *) pos - (RK_U8 *) p->data;
	diff = (RK_U8 *) pos - (RK_U8 *) p->pos;

	/*
	 * If set pos is a simple update on original buffer update the length
	 * If set pos setup a new buffer reset length to size - offset
	 * This will avoid assert on change "data" in mpp_packet
	 */
	if (diff <= p->length)
		p->length -= diff;
	else
		p->length = p->size - offset;

	p->pos = pos;
	mpp_assert(p->data <= p->pos);
	mpp_assert(p->size >= p->length);
}

void *mpp_packet_get_pos(const MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return NULL;

	p = (MppPacketImpl *) packet;
	return p->pos;
}

MPP_RET mpp_packet_set_eos(MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return MPP_ERR_UNKNOW;

	p = (MppPacketImpl *) packet;
	p->flag |= MPP_PACKET_FLAG_EOS;
	return MPP_OK;
}

MPP_RET mpp_packet_clr_eos(MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return MPP_ERR_UNKNOW;

	p = (MppPacketImpl *) packet;
	p->flag &= ~MPP_PACKET_FLAG_EOS;
	return MPP_OK;
}

RK_U32 mpp_packet_get_eos(MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return 0;

	p = (MppPacketImpl *) packet;
	return (p->flag & MPP_PACKET_FLAG_EOS) ? (1) : (0);
}

MPP_RET mpp_packet_set_extra_data(MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return MPP_ERR_UNKNOW;

	p = (MppPacketImpl *) packet;
	p->flag |= MPP_PACKET_FLAG_EXTRA_DATA;
	return MPP_OK;
}

MPP_RET mpp_packet_reset(MppPacketImpl * packet)
{
	void *data;
	size_t size;

	if (check_is_mpp_packet(packet))
		return MPP_ERR_UNKNOW;

	data = packet->data;
	size = packet->size;

	memset(packet, 0, sizeof(*packet));

	packet->data = data;
	packet->pos = data;
	packet->size = size;
	setup_mpp_packet_name(packet);
	return MPP_OK;
}

void mpp_packet_set_buffer(MppPacket packet, MppBuffer buffer)
{

//    MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return;
#if 0
	p = (MppPacketImpl *) packet;
	if (p->buffer != buffer) {
		if (buffer)
			mpp_buffer_inc_ref(buffer);

		if (p->buffer)
			mpp_buffer_put(p->buffer);

		p->buffer = buffer;
	}
#endif
}

MppBuffer mpp_packet_get_buffer(const MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return NULL;

	p = (MppPacketImpl *) packet;
	return p->buffer;
}

RK_S32 mpp_packet_has_meta(const MppPacket packet)
{
	if (check_is_mpp_packet(packet))
		return 0;

	//MppPacketImpl *p = (MppPacketImpl *)packet;

	return 0;		//(NULL != p->meta);
}

MppMeta mpp_packet_get_meta(const MppPacket packet)
{
	// MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return NULL;

	// MppPacketImpl *p = (MppPacketImpl *)packet;
	//  if (NULL == p->meta)
	//    mpp_meta_get(&p->meta);

	return NULL;		//p->meta;
}

MPP_RET mpp_packet_set_status(MppPacket packet, MppPacketStatus status)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return MPP_ERR_UNKNOW;

	p = (MppPacketImpl *) packet;

	p->status.val = status.val;
	return MPP_OK;
}

MPP_RET mpp_packet_get_status(MppPacket packet, MppPacketStatus * status)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet)) {
		status->val = 0;
		return MPP_ERR_UNKNOW;
	}

	p = (MppPacketImpl *) packet;

	status->val = p->status.val;
	return MPP_OK;
}

RK_U32 mpp_packet_is_partition(const MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return 0;

	p = (MppPacketImpl *) packet;

	return p->status.partition;
}

RK_U32 mpp_packet_is_soi(const MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return 0;

	p = (MppPacketImpl *) packet;

	return p->status.soi;
}

RK_U32 mpp_packet_is_eoi(const MppPacket packet)
{
	MppPacketImpl *p = NULL;
	if (check_is_mpp_packet(packet))
		return 0;

	p = (MppPacketImpl *) packet;

	return p->status.eoi;
}

MPP_RET mpp_packet_read(MppPacket packet, size_t offset, void *data,
			size_t size)
{
	void *src;
	if (check_is_mpp_packet(packet) || NULL == data) {
		mpp_err_f("invalid input: packet %p data %p\n", packet, data);
		return MPP_ERR_UNKNOW;
	}

	if (0 == size)
		return MPP_OK;

	src = mpp_packet_get_data(packet);
	mpp_assert(src != NULL);
	memcpy(data, (char *)src + offset, size);
	return MPP_OK;
}

MPP_RET mpp_packet_write(MppPacket packet, size_t offset, void *data,
			 size_t size)
{
	void *dst;
	if (check_is_mpp_packet(packet) || NULL == data) {
		mpp_err_f("invalid input: packet %p data %p\n", packet, data);
		return MPP_ERR_UNKNOW;
	}

	if (0 == size)
		return MPP_OK;

	dst = mpp_packet_get_data(packet);
	mpp_assert(dst != NULL);
	memcpy((char *)dst + offset, data, size);
	return MPP_OK;
}

MPP_RET mpp_packet_copy(MppPacket dst, MppPacket src)
{
	MppPacketImpl *dst_impl;
	MppPacketImpl *src_impl;

	if (check_is_mpp_packet(dst) || check_is_mpp_packet(src)) {
		mpp_err_f("invalid input: dst %p src %p\n", dst, src);
		return MPP_ERR_UNKNOW;
	}

	dst_impl = (MppPacketImpl *) dst;
	src_impl = (MppPacketImpl *) src;

	memcpy(dst_impl->pos, src_impl->pos, src_impl->length);
	dst_impl->length = src_impl->length;
	return MPP_OK;
}

MPP_RET mpp_packet_append(MppPacket dst, MppPacket src)
{
	MppPacketImpl *dst_impl;
	MppPacketImpl *src_impl;

	if (check_is_mpp_packet(dst) || check_is_mpp_packet(src)) {
		mpp_err_f("invalid input: dst %p src %p\n", dst, src);
		return MPP_ERR_UNKNOW;
	}

	dst_impl = (MppPacketImpl *) dst;
	src_impl = (MppPacketImpl *) src;

	memcpy((RK_U8 *) dst_impl->pos + dst_impl->length, src_impl->pos,
	       src_impl->length);
	dst_impl->length += src_impl->length;
	return MPP_OK;
}

/*
 * object access function macro
 */
#define MPP_PACKET_ACCESSORS(type, field) \
    type mpp_packet_get_##field(const MppPacket s) \
    { \
        check_is_mpp_packet(s); \
        return ((MppPacketImpl*)s)->field; \
    } \
    void mpp_packet_set_##field(MppPacket s, type v) \
    { \
        check_is_mpp_packet(s); \
        ((MppPacketImpl*)s)->field = v; \
    }

MPP_PACKET_ACCESSORS(void *, data)
MPP_PACKET_ACCESSORS(size_t, size)
MPP_PACKET_ACCESSORS(size_t, length)
MPP_PACKET_ACCESSORS(RK_S64, pts)
MPP_PACKET_ACCESSORS(RK_S64, dts)
MPP_PACKET_ACCESSORS(RK_U32, flag)
MPP_PACKET_ACCESSORS(RK_U32, temporal_id)

