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

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_buffer.h"
#include "rk_export_func.h"

struct MppBufferImpl {
	MppBufferInfo info;
	struct mpi_buf *mpi_buf;
	struct dma_buf *dmabuf;
	size_t offset;
	RK_S32 ref_count;
};

MPP_RET mpp_buffer_import_with_tag(MppBufferGroup group, MppBufferInfo * info,
				   MppBuffer * buffer, const char *tag,
				   const char *caller)
{
	MPP_RET ret = MPP_OK;
    struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
    if (!mpibuf_fn){
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
		buf = mpp_calloc(struct MppBufferImpl, 1);
		if (!buf) {
			mpp_err("mpp_buffer_import fail %s\n", caller);
			return MPP_ERR_NULL_PTR;
		}
		buf->info = *info;
		buf->mpi_buf = (struct mpi_buf *)buf->info.hnd;

		if (mpibuf_fn->buf_get_dmabuf)
			buf->dmabuf =
			    mpibuf_fn->buf_get_dmabuf(buf->mpi_buf);

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
    if (!mpibuf_fn){
        mpp_err_f("mpibuf_ops get fail");
        return NULL;
    }
	if (mpibuf_fn->buf_alloc)
		buf = mpibuf_fn->buf_alloc(size);

	if (mpibuf_fn->buf_ref)
		mpibuf_fn->buf_ref(buf);

	return buf;
}

MPP_RET mpi_buf_ref_with_tag(struct mpi_buf * buf, const char *tag,
			      const char *caller)
{
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	if (!mpibuf_fn){
		mpp_err_f("mpibuf_ops get fail");
		return MPP_NOK;
	}
	if (buf) {
		if (mpibuf_fn->buf_ref)
			mpibuf_fn->buf_ref(buf);
	}
	return MPP_OK;
}


MPP_RET mpi_buf_unref_with_tag(struct mpi_buf * buf, const char *tag,
			      const char *caller)
{
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	if (!mpibuf_fn){
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

    if (!mpibuf_fn){
        mpp_err_f("mpibuf_ops get fail");
        return NULL;
    }

    if (!buffer){
        return NULL;
    }

	if (mpibuf_fn->buf_get_dmabuf)
	    dmabuf = mpibuf_fn->buf_get_dmabuf(buffer);

    return dmabuf;
}

MPP_RET mpp_buffer_get_with_tag(MppBufferGroup group, MppBuffer * buffer,
				size_t size, const char *tag,
				const char *caller)
{
	struct mpi_buf *mpi_buf = NULL;
	struct MppBufferImpl *buf_impl = NULL;
    struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

    if (!mpibuf_fn){
        mpp_err_f("mpibuf_ops get fail");
        return MPP_NOK;
    }
	buf_impl = mpp_calloc(struct MppBufferImpl, 1);
	if (NULL == buf_impl) {
		mpp_err
		    ("buf impl malloc fail : group %p buffer %p size %u from %s\n",
		     group, buffer, (RK_U32) size, caller);
		return MPP_ERR_UNKNOW;
	}

	if (mpibuf_fn->buf_alloc)
		mpi_buf = mpibuf_fn->buf_alloc(size);

	if (NULL == mpi_buf || 0 == size) {
		mpp_err
		    ("mpp_buffer_get invalid input: group %p buffer %p size %u from %s\n",
		     group, buffer, (RK_U32) size, caller);
		return MPP_ERR_UNKNOW;
	}
	if (mpibuf_fn->buf_get_dmabuf)
		buf_impl->dmabuf = mpibuf_fn->buf_get_dmabuf(mpi_buf);

	if (mpibuf_fn->buf_ref)
		mpibuf_fn->buf_ref(mpi_buf);

	buf_impl->mpi_buf = mpi_buf;
	buf_impl->info.size = size;
	buf_impl->info.hnd = mpi_buf;
	buf_impl->info.fd = -1;
	buf_impl->ref_count++;
	*buffer = buf_impl;
	return (buf_impl) ? (MPP_OK) : (MPP_NOK);
}

MPP_RET mpp_buffer_put_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *buf_impl = (struct MppBufferImpl *)buffer;
    struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

    if (!mpibuf_fn){
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
		if (mpibuf_fn->buf_unmap)
			mpibuf_fn->buf_unmap(buf_impl->mpi_buf);

		if (mpibuf_fn->buf_unref)
			mpibuf_fn->buf_unref(buf_impl->mpi_buf);

		mpp_free(buf_impl);
	}
	return MPP_OK;
}

MPP_RET mpp_buffer_inc_ref_with_caller(MppBuffer buffer, const char *caller)
{
	struct MppBufferImpl *buf_impl = (struct MppBufferImpl *)buffer;

	if (NULL == buf_impl) {
		mpp_err
		    ("mpp_buffer_inc_ref invalid input: buffer NULL from %s\n",
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
    struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

    if (!mpibuf_fn){
        mpp_err_f("mpibuf_ops get fail");
        return MPP_NOK;
    }

	if (NULL == p || NULL == data) {
		mpp_err
		    ("mpp_buffer_read invalid input: buffer %p data %p from %s\n",
		     buffer, data, caller);
		return MPP_ERR_UNKNOW;
	}

	if (0 == size)
		return MPP_OK;

	if (NULL == p->info.ptr && mpibuf_fn->buf_map)
		p->info.ptr = mpibuf_fn->buf_map(p->mpi_buf);

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
    struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

    if (!mpibuf_fn){
        mpp_err_f("mpibuf_ops get fail");
        return MPP_NOK;
    }

	if (NULL == p || NULL == data) {
		mpp_err
		    ("mpp_buffer_write invalid input: buffer %p data %p from %s\n",
		     buffer, data, caller);
		return MPP_ERR_UNKNOW;
	}

	if (0 == size)
		return MPP_OK;

	if (offset + size > p->info.size)
		return MPP_ERR_VALUE;

	if (NULL == p->info.ptr && mpibuf_fn->buf_map)
		p->info.ptr = mpibuf_fn->buf_map(p->mpi_buf);

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

    if (!mpibuf_fn){
        mpp_err_f("mpibuf_ops get fail");
        return NULL;
    }
	if (NULL == p) {
		mpp_err("mpp_buffer_get_ptr invalid NULL input from %s\n",
			caller);
		return NULL;
	}

	if (NULL == p->info.ptr && mpibuf_fn->buf_map)
		p->info.ptr = mpibuf_fn->buf_map(p->mpi_buf);

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
	if (NULL == p) {
		mpp_err("mpp_buffer_get_fd invalid NULL input from %s\n",
			caller);
		return -1;
	}

	mpp_assert(fd >= 0);
	if (fd < 0)
		mpp_err("mpp_buffer_get_fd buffer %p fd %d from %s\n", buffer,
			fd, caller);

	return fd;
}

struct dma_buf *mpp_buffer_get_dma_with_caller(MppBuffer buffer,
					       const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
	if (NULL == p) {
		mpp_err("mpp_buffer_get_dma invalid NULL input from %s\n",
			caller);
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
		mpp_err("mpp_buffer_get_size invalid NULL input from %s\n",
			caller);
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

MPP_RET mpp_buffer_info_get_with_caller(MppBuffer buffer, MppBufferInfo * info,
					const char *caller)
{
	struct MppBufferImpl *p = (struct MppBufferImpl *)buffer;
    struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

    if (!mpibuf_fn){
      mpp_err_f("mpibuf_ops get fail");
      return MPP_NOK;
    }
	if (NULL == buffer || NULL == info) {
		mpp_err
		    ("mpp_buffer_info_get invalid input buffer %p info %p from %s\n",
		     buffer, info, caller);
		return MPP_ERR_UNKNOW;
	}

	if (NULL == p->info.ptr && mpibuf_fn->buf_map)
		p->info.ptr = mpibuf_fn->buf_map(p->mpi_buf);

	*info = p->info;
	(void)caller;
	return MPP_OK;
}
