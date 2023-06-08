// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_buffer"

#include <linux/string.h>
#include <linux/dma-buf.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/delay.h>

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_buffer.h"
#include "rk_export_func.h"
#include "mpp_frame.h"
#include "mpp_mem_pool.h"

static const char *module_name = MODULE_TAG;
struct MppBufferImpl {
	MppBufferInfo info;
	struct mpi_buf *mpi_buf;
	struct dma_buf *dmabuf;
	size_t offset;
	RK_U32 cir_flag;
	RK_S32 ref_count;
};

static MppMemPool g_mppbuf_pool = NULL;

MPP_RET mpp_buffer_pool_init(RK_U32 max_cnt)
{
	if (g_mppbuf_pool)
		return MPP_OK;

	g_mppbuf_pool = mpp_mem_pool_init(module_name, sizeof(struct MppBufferImpl), max_cnt);

	return MPP_OK;
}

MPP_RET mpp_buffer_pool_deinit(void)
{
	if (!g_mppbuf_pool)
		return MPP_OK;

	mpp_mem_pool_deinit(g_mppbuf_pool);
	g_mppbuf_pool = NULL;

	return MPP_OK;
}

void mpp_buf_pool_info_show(void *seq_file)
{
	mpp_mem_pool_info_show(seq_file, g_mppbuf_pool);
}

MPP_RET mpp_buffer_import_with_tag(MppBufferGroup group, MppBufferInfo *info,
				   MppBuffer *buffer, const char *tag,
				   const char *caller)
{
	MPP_RET ret = MPP_OK;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}

	if (NULL == info) {
		mpp_err("mpp_buffer_commit invalid input: info NULL from %s\n",
			caller);
		return MPP_ERR_NULL_PTR;
	}
	if (buffer) {
		struct MppBufferImpl *buf = NULL;

		buf = mpp_mem_pool_get(g_mppbuf_pool);
		if (!buf) {
			mpp_err("mpp_buffer_import fail %s\n", caller);
			return MPP_ERR_NULL_PTR;
		}
		buf->info = *info;
		buf->mpi_buf = (struct mpi_buf *)buf->info.hnd;

		if (mpibuf_fn->buf_get_dmabuf)
			buf->dmabuf = mpibuf_fn->buf_get_dmabuf(buf->mpi_buf);

		if (mpibuf_fn->buf_ref)
			mpibuf_fn->buf_ref(buf->mpi_buf);
		buf->ref_count++;
		buf->info.fd = -1;
		*buffer = buf;
	}

	return ret;
}

struct mpi_buf *mpi_buf_alloc_with_tag(size_t size, const char *tag,
				       const char *caller)
{
	struct mpi_buf *buf = NULL;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return NULL;
	}
	if (mpibuf_fn->buf_alloc)
		buf = mpibuf_fn->buf_alloc(size);

	return buf;
}

MPP_RET mpi_buf_ref_with_tag(struct mpi_buf *buf, const char *tag,
			     const char *caller)
{
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}
	if (buf) {
		if (mpibuf_fn->buf_ref)
			mpibuf_fn->buf_ref(buf);
	}

	return MPP_OK;
}

MPP_RET mpi_buf_unref_with_tag(struct mpi_buf *buf, const char *tag,
			       const char *caller)
{
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}
	if (buf) {
		if (mpibuf_fn->buf_unref)
			mpibuf_fn->buf_unref(buf);
	}

	return MPP_OK;
}

struct dma_buf *mpi_buf_get_dma_with_caller(MpiBuf buffer, const char *caller)
{
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	struct dma_buf *dmabuf = NULL;

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return NULL;
	}

	if (!buffer)
		return NULL;

	if (mpibuf_fn->buf_get_dmabuf)
		dmabuf = mpibuf_fn->buf_get_dmabuf(buffer);

	return dmabuf;
}

MPP_RET mpp_buffer_get_with_tag(MppBufferGroup group, MppBuffer *buffer,
				size_t size, const char *tag,
				const char *caller)
{
	struct mpi_buf *mpi_buf = NULL;
	struct MppBufferImpl *buf_impl = NULL;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}
	buf_impl = mpp_mem_pool_get(g_mppbuf_pool);
	if (NULL == buf_impl) {
		mpp_err("buf impl malloc fail : group %p buffer %p size %u from %s\n",
			group, buffer, (RK_U32)size, caller);
		return MPP_ERR_UNKNOW;
	}

	if (mpibuf_fn->buf_alloc)
		mpi_buf = mpibuf_fn->buf_alloc(size);

	if (NULL == mpi_buf || 0 == size) {
		mpp_err("mpp_buffer_get invalid input: group %p buffer %p size %u from %s\n",
			group, buffer, (RK_U32)size, caller);
		mpp_mem_pool_put(g_mppbuf_pool, buf_impl);
		return MPP_ERR_UNKNOW;
	}
	if (mpibuf_fn->buf_get_dmabuf)
		buf_impl->dmabuf = mpibuf_fn->buf_get_dmabuf(mpi_buf);

	buf_impl->mpi_buf = mpi_buf;
	buf_impl->info.size = size;
	buf_impl->info.hnd = mpi_buf;
	buf_impl->info.fd = -1;
	buf_impl->ref_count++;
	*buffer = buf_impl;
	return (buf_impl) ? (MPP_OK) : (MPP_NOK);
}

MPP_RET mpp_ring_buffer_get_with_tag(MppBufferGroup group, MppBuffer *buffer,
				     size_t size, const char *tag,
				     const char *caller)
{
	struct mpi_buf *mpi_buf = NULL;
	struct MppBufferImpl *buf_impl = NULL;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}
	buf_impl = mpp_mem_pool_get(g_mppbuf_pool);
	if (NULL == buf_impl) {
		mpp_err("buf impl malloc fail : group %p buffer %p size %u from %s\n",
			group, buffer, (RK_U32)size, caller);
		return MPP_ERR_UNKNOW;
	}

	if (mpibuf_fn->buf_alloc)
		mpi_buf = mpibuf_fn->buf_alloc(size);

	if (NULL == mpi_buf || 0 == size) {
		mpp_err("mpp_buffer_get invalid input: group %p buffer %p size %u from %s\n",
			group, buffer, (RK_U32)size, caller);
		mpp_mem_pool_put(g_mppbuf_pool, buf_impl);
		return MPP_ERR_UNKNOW;
	}
	if (mpibuf_fn->buf_get_dmabuf)
		buf_impl->dmabuf = mpibuf_fn->buf_get_dmabuf(mpi_buf);

	buf_impl->mpi_buf = mpi_buf;
	buf_impl->info.size = size;
	buf_impl->info.hnd = mpi_buf;
	buf_impl->info.fd = -1;
	buf_impl->ref_count++;
	buf_impl->cir_flag = 1;
	*buffer = buf_impl;

	return (buf_impl) ? (MPP_OK) : (MPP_NOK);
}

MPP_RET mpp_buffer_put_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *buf_impl = (struct MppBufferImpl *)buffer;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}

	if (NULL == buf_impl) {
		mpp_err("mpp_buffer_put invalid input: buffer NULL from %s\n",
			caller);
		return MPP_ERR_UNKNOW;
	}
	buf_impl->ref_count--;
	if (!buf_impl->ref_count) {
		if (buf_impl->cir_flag)
			vunmap(buf_impl->info.ptr);

		else {
			if (mpibuf_fn->buf_unmap)
				mpibuf_fn->buf_unmap(buf_impl->mpi_buf);
		}

		if (mpibuf_fn->buf_unref)
			mpibuf_fn->buf_unref(buf_impl->mpi_buf);

		mpp_mem_pool_put(g_mppbuf_pool, buf_impl);
	}

	return MPP_OK;
}

void * mpp_buffer_map_ring_ptr(struct MppBufferImpl *p)
{
	RK_U32 end = 0, start = 0;
	RK_S32 i = 0;
	struct page **pages;
	RK_S32 page_count;
	RK_S32 phy_addr = mpp_buffer_get_phy(p);

	if (phy_addr == -1)
		phy_addr = mpp_srv_get_phy(p->dmabuf);

	end = phy_addr + p->info.size;
	start = phy_addr;
	end = round_up(end, PAGE_SIZE);

	if (phy_addr & 0xfff) {
		mpp_err("alloc buf start is no 4k align");
		return NULL;
	}

	page_count = (((end - start) >> PAGE_SHIFT) + 1) * 2;
	pages = kmalloc_array(page_count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return NULL;

	i = 0;
	while (start < end) {
		mpp_assert(i < page_count);
		pages[i++] = phys_to_page(start);
		start += PAGE_SIZE;
	}

	start = phy_addr;
	while (start < end) {
		mpp_assert(i < page_count);
		pages[i++] = phys_to_page(start);
		start += PAGE_SIZE;
	}

	p->info.ptr = vmap(pages, i, VM_MAP, PAGE_KERNEL);

	kfree(pages);
	return p->info.ptr;
}

MPP_RET mpp_buffer_map(struct MppBufferImpl *p)
{
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}

	if (NULL == p->info.ptr && mpibuf_fn->buf_map) {
		if (p->cir_flag)
			p->info.ptr = mpp_buffer_map_ring_ptr(p);

		else
			p->info.ptr = mpibuf_fn->buf_map(p->mpi_buf);
	}

	return MPP_OK;
}

MPP_RET mpp_buffer_inc_ref_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *buf_impl = (struct MppBufferImpl *)buffer;

	if (NULL == buf_impl) {
		mpp_err("mpp_buffer_inc_ref invalid input: buffer NULL from %s\n",
			caller);
		return MPP_ERR_UNKNOW;
	}
	buf_impl->ref_count++;

	return MPP_OK;
}

MPP_RET mpp_buffer_read_with_caller(MppBuffer buffer, size_t offset, void *data,
				    size_t size, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	void *src = NULL;

	if (NULL == p || NULL == data) {
		mpp_err("mpp_buffer_read invalid input: buffer %p data %p from %s\n",
			buffer, data, caller);
		return MPP_ERR_UNKNOW;
	}

	if (0 == size)
		return MPP_OK;

	if (mpp_buffer_map(p))
		return MPP_NOK;

	src = p->info.ptr;
	mpp_assert(src != NULL);
	if (src)
		memcpy(data, (char *)src + offset, size);

	return MPP_OK;
}

MPP_RET mpp_buffer_write_with_caller(MppBuffer buffer, size_t offset,
				     void *data, size_t size,
				     const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	void *dst = NULL;

	if (NULL == p || NULL == data) {
		mpp_err("mpp_buffer_write invalid input: buffer %p data %p from %s\n",
			buffer, data, caller);
		return MPP_ERR_UNKNOW;
	}

	if (0 == size)
		return MPP_OK;

	if (offset + size > p->info.size)
		return MPP_ERR_VALUE;

	if (mpp_buffer_map(p))
		return MPP_NOK;

	dst = p->info.ptr;
	mpp_assert(dst != NULL);
	if (dst)
		memcpy((char *)dst + offset, data, size);

	return MPP_OK;
}

void *mpp_buffer_get_ptr_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return NULL;
	}
	if (NULL == p) {
		mpp_err("mpp_buffer_get_ptr invalid NULL input from %s\n",
			caller);
		return NULL;
	}

	if (mpp_buffer_map(p))
		return NULL;

	mpp_assert(p->info.ptr != NULL);
	if (NULL == p->info.ptr)
		mpp_err("mpp_buffer_get_ptr buffer %p ret NULL from %s\n",
			buffer, caller);

	return p->info.ptr;
}

int mpp_buffer_get_fd_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	int fd = -1;

	mpp_log("p->info.fd %d", p->info.fd);
	if (p->info.fd > 0)
		return p->info.fd;
	if (NULL == p) {
		mpp_err("mpp_buffer_get_fd invalid NULL input from %s\n",
			caller);
		return -1;
	}
	fd = dma_buf_fd(p->dmabuf, 0);
	mpp_assert(fd >= 0);
	if (fd < 0)
		mpp_err("mpp_buffer_get_fd buffer %p fd %d from %s\n", buffer,
			fd, caller);

	mpp_log("dma_buf_fd fd %d", fd);
	p->info.fd = fd;

	return fd;
}

struct dma_buf *mpp_buffer_get_dma_with_caller(MppBuffer buffer,
					       const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;

	if (NULL == p) {
		mpp_err("mpp_buffer_get_dma invalid NULL input from %s\n", caller);
		return NULL;
	}
	if (!p->dmabuf)
		mpp_err("mpp_buffer_get_fd buffer %p from %s\n", buffer,
			caller);

	return p->dmabuf;
}

size_t mpp_buffer_get_size_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;

	if (NULL == p) {
		mpp_err("mpp_buffer_get_size invalid NULL input from %s\n", caller);
		return 0;
	}
	if (p->info.size == 0)
		mpp_err("mpp_buffer_get_size buffer %p ret zero size from %s\n",
			buffer, caller);

	return p->info.size;
}

int mpp_buffer_get_index_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;

	if (NULL == p) {
		mpp_err("mpp_buffer_get_index invalid NULL input from %s\n",
			caller);
		return -1;
	}

	return p->info.index;
}

MPP_RET mpp_buffer_set_index_with_caller(MppBuffer buffer, int index,
					 const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;

	if (NULL == p) {
		mpp_err("mpp_buffer_set_index invalid NULL input from %s\n",
			caller);
		return MPP_ERR_UNKNOW;
	}

	p->info.index = index;

	return MPP_OK;
}

size_t mpp_buffer_get_offset_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;

	if (NULL == p) {
		mpp_err("mpp_buffer_get_offset invalid NULL input from %s\n",
			caller);
		return -1;
	}

	return p->offset;
}

MPP_RET mpp_buffer_set_offset_with_caller(MppBuffer buffer, size_t offset,
					  const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;

	if (NULL == p) {
		mpp_err("mpp_buffer_set_offset invalid NULL input from %s\n",
			caller);
		return MPP_ERR_UNKNOW;
	}

	p->offset = offset;

	return MPP_OK;
}

MPP_RET mpp_buffer_info_get_with_caller(MppBuffer buffer, MppBufferInfo *info,
					const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}
	if (NULL == buffer || NULL == info) {
		mpp_err("mpp_buffer_info_get invalid input buffer %p info %p from %s\n",
			buffer, info, caller);
		return MPP_ERR_UNKNOW;
	}

	if (NULL == p->info.ptr && mpibuf_fn->buf_map)
		p->info.ptr = mpibuf_fn->buf_map(p->mpi_buf);

	*info = p->info;

	return MPP_OK;
}

MPP_RET mpp_buffer_flush_for_cpu_with_caller(ring_buf *buf, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buf->buf;

	if (NULL == p) {
		mpp_err("mpp_buffer_set_offset invalid NULL input from %s\n", caller);
		return MPP_ERR_UNKNOW;
	}
	if ( buf->start_offset + buf->use_len >= p->info.size) {
		dma_buf_begin_cpu_access_partial(p->dmabuf, DMA_FROM_DEVICE, buf->start_offset,
						 p->info.size - buf->start_offset);

		dma_buf_begin_cpu_access_partial(p->dmabuf, DMA_FROM_DEVICE, 0,
						 buf->start_offset + buf->use_len - p->info.size);

	} else
		dma_buf_begin_cpu_access_partial(p->dmabuf, DMA_FROM_DEVICE, buf->start_offset, buf->use_len);

	return MPP_OK;
}

MPP_RET mpp_buffer_flush_for_device_with_caller(ring_buf *buf, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buf->buf;

	if (NULL == p) {
		mpp_err("mpp_buffer_set_offset invalid NULL input from %s\n", caller);
		return MPP_ERR_UNKNOW;
	}
	if ( buf->start_offset + buf->use_len >= p->info.size) {
		dma_buf_end_cpu_access_partial(p->dmabuf, DMA_TO_DEVICE, buf->start_offset,
					       p->info.size - buf->start_offset);

		dma_buf_end_cpu_access_partial(p->dmabuf, DMA_TO_DEVICE, 0,
					       buf->start_offset + buf->use_len - p->info.size);

	} else
		dma_buf_end_cpu_access_partial(p->dmabuf, DMA_TO_DEVICE, buf->start_offset, buf->use_len);


	return MPP_OK;
}

RK_S32 mpp_buffer_get_mpi_buf_id_with_caller(MppBuffer buffer,
					     const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	struct mpp_frame_infos frm_info;

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return -1;
	}

	if (NULL == buffer) {
		mpp_err("mpp_buffer_get_mpi_buf_id invalid input buffer %p from %s\n",
			buffer, caller);
		return MPP_ERR_UNKNOW;
	}
	memset(&frm_info, 0, sizeof(frm_info));
	if (mpibuf_fn->get_buf_frm_info) {
		if (mpibuf_fn->get_buf_frm_info(p->mpi_buf, &frm_info, -1))
			return -1;
	} else {
		mpp_err("get buf info fail");
		return -1;
	}

	return frm_info.mpi_buf_id;
}

void mpp_buffer_set_phy_caller(MppBuffer buffer, RK_U32 phy_addr, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;

	if (NULL == p) {
		mpp_err("mpp_buffer_get_offset invalid NULL input from %s\n",
			caller);
		return;
	}
	p->info.phy_flg = 1;
	p->info.phy_addr = phy_addr;

	return;
}
RK_S32 mpp_buffer_get_phy_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	RK_S32 phy_addr = -1;

	if (NULL == p) {
		mpp_err("mpp_buffer_get_offset invalid NULL input from %s\n", caller);
		return -1;
	}

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return -1;
	}

	if (p->info.phy_flg)
		phy_addr = (RK_S32)p->info.phy_addr;
	else if (mpibuf_fn->buf_get_paddr) {
		phy_addr = mpibuf_fn->buf_get_paddr(p->mpi_buf);
		if (phy_addr != -1) {
			p->info.phy_addr = phy_addr;
			p->info.phy_flg = 1;
		}
	}

	return phy_addr;
}

