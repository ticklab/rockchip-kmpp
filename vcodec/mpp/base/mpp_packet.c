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
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/dma-buf.h>

#include "mpp_log.h"
#include "mpp_err.h"
#include "mpp_mem.h"
#include "mpp_packet.h"
#include "mpp_packet_impl.h"
#include "mpp_buffer.h"
#include "mpp_maths.h"
#include "mpp_packet_pool.h"
#include "rk_export_func.h"

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

	p = mpp_packet_mem_alloc();
	if (NULL == p) {
		mpp_err_f("malloc failed\n");
		return MPP_ERR_NULL_PTR;
	}
	*packet = p;

	INIT_LIST_HEAD(&p->list);
	kref_init(&p->ref);

	setup_mpp_packet_name(p);

	return MPP_OK;
}

static MPP_RET mpp_packet_vsm_buf_alloc (MppPacketImpl *p, RK_U32 type, RK_S32 chan_id)
{
	struct vsm_buf buf;
	struct vcodec_vsm_buf_fn *vsm_fn = get_vsm_ops();
	RK_S32 i;
	RK_S32 page_count;
	struct page **pages;
	memset(&buf, 0, sizeof(struct vsm_buf));

	if (!vsm_fn) {
		mpp_err("vcodec_vsm_buf_fn is NULL");
		return MPP_ERR_MALLOC;
	}
	if (vsm_fn->GetBuf) {
		RK_U32 end = 0, start = 0;
		if (vsm_fn->GetBuf(&buf, type, 0, chan_id) < 0)
			return MPP_ERR_MALLOC;
		// mpp_log("vsm_fn GetBuf phy = 0x%x, size %d", buf.phy_addr, buf.buf_size);
		p->buf.start_offset = 0;

		end = buf.phy_addr + buf.buf_size;
		start = buf.phy_addr;
		end = round_up(end, PAGE_SIZE);

		page_count = ((end - start) >> PAGE_SHIFT) + 1;
		pages = kmalloc_array(page_count, sizeof(*pages), GFP_KERNEL);
		if (!pages) {

			if (vsm_fn->FreeBuf)
				vsm_fn->FreeBuf(&buf, chan_id);
			return MPP_ERR_MALLOC;
		}

		i = 0;
		while (start < end) {
			mpp_assert(i < page_count);
			pages[i++] = phys_to_page(start);
			start += PAGE_SIZE;
		}

		p->buf.buf_start = vmap(pages, i, VM_MAP, PAGE_KERNEL);
		p->buf.buf_start += (buf.phy_addr & 0xfff);
		p->buf.mpi_buf_id = buf.phy_addr;
		//mpp_log("vmap addr %p", p->buf.buf_start);
		p->buf.size = buf.buf_size;
		p->flag |= MPP_PACKET_FLAG_EXTERNAL;
		kfree(pages);
	} else
		return MPP_ERR_MALLOC;
	return MPP_OK;
}

static MPP_RET mpp_packet_vsm_buf_free(MppPacketImpl *p, RK_S32 chan_id)
{
	struct vsm_buf buf;
	struct vcodec_vsm_buf_fn *vsm_fn = get_vsm_ops();
	memset(&buf, 0, sizeof(struct vsm_buf));
	if (!vsm_fn)
		return MPP_NOK;

	buf.phy_addr = p->buf.mpi_buf_id;
	buf.buf_size = p->buf.size;

	vunmap(p->buf.buf_start - (buf.phy_addr & 0xfff));

	if (vsm_fn->PutBuf && p->length) {
		struct venc_packet out_packet;
		memset(&out_packet, 0, sizeof(struct venc_packet));
		out_packet.u64pts = p->pts;
		out_packet.temporal_id = p->temporal_id;
		out_packet.len = p->length;
		if (p->flag & MPP_PACKET_FLAG_INTRA)
			out_packet.flag = 1;

		//mpp_log("vsm put buf phy_addr 0x%x, stream len %d", buf.phy_addr, p->length);
		vsm_fn->PutBuf(&buf, &out_packet, 0, chan_id);
	} else if (vsm_fn->FreeBuf) {
		//mpp_err("enc err free put buf phy_addr 0x%x", buf.phy_addr);
		vsm_fn->FreeBuf(&buf, chan_id);
	}
	return MPP_OK;
}

MPP_RET mpp_packet_new_ring_buf(MppPacket *packet, ring_buf_pool *pool, size_t min_size,
				RK_U32 type, RK_S32 chan_id)
{
	MppPacketImpl *p = NULL;

	if (NULL == packet) {
		mpp_err_f("invalid NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = mpp_packet_mem_alloc();

	if (NULL == p)
		return MPP_ERR_NULL_PTR;

	INIT_LIST_HEAD(&p->list);
	kref_init(&p->ref);

	if (min_size)
		min_size = (min_size + SZ_1K) & (SZ_1K - 1);

	if (pool) {
		if (ring_buf_get_free(pool, &p->buf, 128, min_size, 1)) {
			mpp_packet_mem_free(p);
			return MPP_ERR_MALLOC;
		}
	} else {
		if (mpp_packet_vsm_buf_alloc(p, type, chan_id)) {
			mpp_packet_mem_free(p);
			return MPP_ERR_MALLOC;
		}
	}

	p->data = p->pos = p->buf.buf_start;
	p->size = p->buf.size;
	p->length = 0;
	setup_mpp_packet_name(p);
	p->ring_pool = pool;
	*packet = p;
	return MPP_OK;
}

MPP_RET mpp_packet_ring_buf_put_used(MppPacket * packet, RK_S32 chan_id, MppDev dev_ctx)
{
	MppPacketImpl *p = (MppPacketImpl *)packet;

	p->buf.use_len = p->length;
	if (p->ring_pool) {
		if (p->length > p->buf.size) {
			mpp_err("ring_buf used may be error");
			return MPP_NOK;
		}
		ring_buf_put_use(p->ring_pool, &p->buf);
	}

	if (p->flag & MPP_PACKET_FLAG_EXTERNAL) {
		if (p->length) {
			struct device *dev = mpp_get_dev(dev_ctx);

			dma_sync_single_for_device(dev, p->buf.mpi_buf_id, p->length, DMA_FROM_DEVICE);
		}
		mpp_packet_vsm_buf_free(p, chan_id);
	}

	if (p->buf.buf)
		mpp_buffer_flush_for_cpu(&p->buf);

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
	p = (MppPacketImpl *) *packet;
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

	if (p->ring_pool) {
		ring_buf_put_free(p->ring_pool, &p->buf);
		p->ring_pool = NULL;
	}

	if (p->flag & MPP_PACKET_FLAG_EXTERNAL)
		vunmap(p->buf.buf_start);

	mpp_packet_mem_free(p);

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

void mpp_packet_set_flag(MppPacket packet, RK_U32 flag)
{
	MppPacketImpl *s;
	if (check_is_mpp_packet(packet)) {
		mpp_err_f("invalid input: dst %p\n", packet);
		return;
	}
	s = (MppPacketImpl *)packet;
	s->flag |= flag;
	return;
}

RK_U32  mpp_packet_get_flag(const MppPacket packet)
{
	MppPacketImpl *s;
	if (check_is_mpp_packet(packet)) {
		mpp_err_f("invalid input: dst %p\n", packet);
		return 0;
	}
	s = (MppPacketImpl *)packet;
	return  s->flag;
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
MPP_PACKET_ACCESSORS(RK_U32, temporal_id)

