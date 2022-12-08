// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/cdev.h>

#include "mpp_vcodec_base.h"
#include "mpp_vcodec_debug.h"
#include "mpp_vcodec_flow.h"
#include "mpp_log.h"
#include "mpp_vcodec_thread.h"
#include "mpp_vcodec_intf.h"
#include "mpp_buffer.h"
#include "rk_export_func.h"
#include "mpp_packet_impl.h"
#include "mpp_packet_pool.h"
#include "hal_bufs.h"
#include "mpp_maths.h"

RK_U32 mpp_vcodec_debug = 0;

#define CHAN_MAX_YUV_POOL_SIZE  1
#define CHAN_MAX_STREAM_POOL_SIZE  2
#define MAX_STREAM_CNT 512

static struct vcodec_entry g_vcodec_entry;

static void *mpp_vcodec_bind(void *out_param)
{
	struct mpp_chan *entry = NULL;
	int id = -1;
	MppCtxType type = MPP_CTX_ENC;
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	if ( !mpidev_fn) {
		mpp_err("get_mpidev_ops fail");
		return NULL;
	}
	if (mpidev_fn->get_chnl_id)
		id = mpidev_fn->get_chnl_id(out_param);

	if (mpidev_fn->get_chnl_type)
		type = (MppCtxType) mpidev_fn->get_chnl_type(out_param);

	entry = mpp_vcodec_get_chan_entry(id, type);
	if (!entry->handle) {
		mpp_err("type %d chnl %d is no create", type, id);
		return NULL;
	}
	return &entry->yuv_queue;
}

static int mpp_vcodec_unbind(void *ctx)
{
	return 0;
}

static struct vcodec_threads *mpp_vcodec_get_thd_handle(struct mpi_obj *obj)
{
	struct vcodec_threads *thd = NULL;
	void *ctx = NULL;
	struct mpp_chan *entry = NULL;
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	if (!mpidev_fn) {
		mpp_err("get_mpidev_ops fail");
		return NULL;
	}

	if (mpidev_fn->get_chnl_ctx)
		ctx = mpidev_fn->get_chnl_ctx(obj);

	if (ctx)
		entry = container_of(ctx, struct mpp_chan, yuv_queue);

	if (entry) {
		switch (entry->type) {
		case MPP_CTX_ENC: {
			thd = g_vcodec_entry.venc.thd;
		}
		break;
		default: {
			mpp_err("MppCtxType error %d", entry->type);
		}
		break;
		}
	}
	return thd;
}


static int mpp_vcodec_msg_handle(struct mpi_obj *obj, int event, void *args)
{
	int ret = 0;

	struct vcodec_threads *thd;
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	if (!mpidev_fn) {
		mpp_err("get_mpidev_ops fail");
		return -1;
	}
	thd = mpp_vcodec_get_thd_handle(obj);
	if (mpidev_fn->handle_message)
		ret = mpidev_fn->handle_message(obj, event, args);

	if (ret && thd)
		vcodec_thread_trigger(thd);
	return 0;
}

struct vcodec_set_dev_fn gdev_fn = {
	.bind = mpp_vcodec_bind,
	.unbind = mpp_vcodec_unbind,
	.msg_callback = mpp_vcodec_msg_handle,
};

int vcodec_create_mpi_dev(void)
{
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	struct venc_module *venc = &g_vcodec_entry.venc;
	if (mpidev_fn->create_dev && !venc->dev)
		venc->dev = mpidev_fn->create_dev(venc->name, &gdev_fn);

	if (!venc->dev)
		mpp_err("creat mpi dev & register fail \n");
	return 0;
}

static int mpp_enc_module_init(void)
{
	RK_U32 i = 0;
	char *name = "rkv_enc";
	struct venc_module *venc = &g_vcodec_entry.venc;
	struct vcodec_threads *thds = NULL;

	memset(venc, 0, sizeof(*venc));
	venc->name = kstrdup(name, GFP_KERNEL);

	for (i = 0; i < MAX_ENC_NUM; i++) {
		struct mpp_chan *curr_entry = &venc->mpp_enc_chan_entry[i];
		curr_entry->chan_id = i;
		curr_entry->coding_type = -1;
		curr_entry->handle = NULL;
		spin_lock_init(&curr_entry->chan_lock);
	}
	venc->num_enc = 0;
	thds = vcodec_thread_create((struct vcodec_module *)venc);

	vcodec_thread_set_count(thds, 1);
	vcodec_thread_set_callback(thds,
				   (void *)
				   mpp_vcodec_enc_routine,
				   (void *)venc);
	mpp_packet_pool_init(MAX_STREAM_CNT);
	vcodec_thread_start(thds);

	venc->check = &venc;
	venc->thd = thds;
	spin_lock_init(&venc->enc_lock);
	return 0;
}

int mpp_vcodec_get_free_chan(MppCtxType type)
{
	RK_S32 i = 0;
	switch (type) {

	case MPP_CTX_ENC: {
		unsigned long lock_flag;
		struct venc_module *venc = &g_vcodec_entry.venc;
		spin_lock_irqsave(&venc->enc_lock, lock_flag);
		for (i = 0; i < MAX_ENC_NUM; i++) {
			if (!venc->mpp_enc_chan_entry[i].handle)
				break;
		}
		if (i >= MAX_ENC_NUM)
			i = -1;
		spin_unlock_irqrestore(&venc->enc_lock, lock_flag);
	}
	break;
	default: {
		mpp_err("MppCtxType error %d", type);
	}
	break;
	}
	return i;
}

struct mpp_chan *mpp_vcodec_get_chan_entry(RK_S32 chan_id, MppCtxType type)
{
	struct mpp_chan *chan = NULL;
	switch (type) {
	case MPP_CTX_ENC: {
		struct venc_module *venc = &g_vcodec_entry.venc;
		if (chan_id < 0 || chan_id > MAX_ENC_NUM) {
			mpp_err("chan id %d is over, full max is %d \n",
				chan_id, MAX_ENC_NUM);
			return NULL;
		}
		chan = &venc->mpp_enc_chan_entry[chan_id];
	}
	break;
	default: {
		mpp_err("MppCtxType error %d", type);
	}
	break;
	}
	return chan;
}

RK_U32 mpp_vcodec_get_chan_num(MppCtxType type)
{
	RK_U32 num_chan = 0;

	switch (type) {
	case MPP_CTX_ENC: {
		unsigned long lock_flag;
		struct venc_module *venc = &g_vcodec_entry.venc;
		spin_lock_irqsave(&venc->enc_lock, lock_flag);
		num_chan = venc->num_enc;
		spin_unlock_irqrestore(&venc->enc_lock, lock_flag);
	} break;
	default: {
		mpp_err("MppCtxType error %d", type);
	}
	break;
	}
	return num_chan;
}

void mpp_vcodec_inc_chan_num(MppCtxType type)
{
	switch (type) {
	case MPP_CTX_ENC: {
		unsigned long lock_flag;
		struct venc_module *venc = &g_vcodec_entry.venc;
		spin_lock_irqsave(&venc->enc_lock, lock_flag);
		venc->num_enc++;
		spin_unlock_irqrestore(&venc->enc_lock, lock_flag);
	} break;
	default: {
		mpp_err("MppCtxType error %d", type);
	}
	break;
	}
	return;
}

void mpp_vcodec_dec_chan_num(MppCtxType type)
{
	switch (type) {
	case MPP_CTX_ENC: {
		unsigned long lock_flag;
		struct venc_module *venc = &g_vcodec_entry.venc;
		spin_lock_irqsave(&venc->enc_lock, lock_flag);
		venc->num_enc--;
		spin_unlock_irqrestore(&venc->enc_lock, lock_flag);

	} break;
	default: {
		mpp_err("MppCtxType error %d", type);
	}
	break;
	}

	return;
}

#define REF_BODY_SIZE(w, h)			MPP_ALIGN((((w) * (h) * 3 / 2) + 48 * MPP_MAX(w, h)), SZ_4K)
#define REF_WRAP_BODY_EXT_SIZE(w, h)		MPP_ALIGN((240 * MPP_MAX(w, h)), SZ_4K)
#define REF_HEADER_SIZE(w, h)			MPP_ALIGN((((w) * (h) / 64) + MPP_MAX(w, h) / 2), SZ_4K)
#define REF_WRAP_HEADER_EXT_SIZE(w, h)		MPP_ALIGN((3 * MPP_MAX(w, h)), SZ_4K)

static MPP_RET get_wrap_buf(struct hal_shared_buf *ctx, struct vcodec_attr *attr, RK_S32 max_lt_cnt)
{
	RK_S32 alignment = 64;
	RK_S32 aligned_w = MPP_ALIGN(attr->max_width, alignment);
	RK_S32 aligned_h = MPP_ALIGN(attr->max_height, alignment);
	RK_U32 total_wrap_size;
	RK_U32 body_size, body_total_size, hdr_size, hdr_total_size;

	body_size = REF_BODY_SIZE(aligned_w, aligned_h);
	body_total_size = body_size + REF_WRAP_BODY_EXT_SIZE(aligned_w, aligned_h);
	hdr_size = REF_HEADER_SIZE(aligned_w, aligned_h);
	hdr_total_size = hdr_size + REF_WRAP_HEADER_EXT_SIZE(aligned_w, aligned_h);
	total_wrap_size = body_total_size + hdr_total_size;
	if (max_lt_cnt > 0) {
#ifdef ONLY_SMART_P
		total_wrap_size += body_size;
		total_wrap_size += hdr_size;
#else
		total_wrap_size += total_wrap_size;
#endif
	}


	if (ctx->recn_ref_buf)
		mpp_buffer_put(ctx->recn_ref_buf);
	return mpp_buffer_get(NULL, &ctx->recn_ref_buf, total_wrap_size);
}

static void mpp_chan_clear_buf_resource(struct mpp_chan *entry)
{
	struct hal_shared_buf *ctx = &entry->shared_buf;

	if (ctx->dpb_bufs) {
		hal_bufs_deinit(ctx->dpb_bufs);
		ctx->dpb_bufs = NULL;
	}
	if (ctx->recn_ref_buf) {
		mpp_buffer_put(ctx->recn_ref_buf);
		ctx->recn_ref_buf = NULL;
	}

	if (ctx->stream_buf) {
		mpp_buffer_put(ctx->stream_buf);
		ctx->stream_buf = NULL;
	}

	if (ctx->ext_line_buf) {
		mpp_buffer_put(ctx->ext_line_buf);
		ctx->ext_line_buf = NULL;
	}
	entry->max_width = 0;
	entry->max_height = 0;
	entry->max_lt_cnt = 0;
	entry->ring_buf_size = 0;
	return;
}

MPP_RET mpp_vcodec_chan_setup_hal_bufs(struct mpp_chan *entry, struct vcodec_attr *attr)
{

	if (((attr->max_width * attr->max_height > entry->max_width * entry->max_height) ||
	     (attr->max_lt_cnt > entry->max_lt_cnt)) &&
	    (attr->coding != MPP_VIDEO_CodingMJPEG)) {
		RK_S32 alignment = 64;
		RK_S32 aligned_w = MPP_ALIGN(attr->max_width, alignment);
		RK_S32 aligned_h = MPP_ALIGN(attr->max_height, alignment);

		RK_S32 mb_wd64 = aligned_w / 64;
		RK_S32 mb_h64 = aligned_h / 64;

		RK_S32 pixel_buf_fbc_hdr_size = MPP_ALIGN(aligned_w * aligned_h / 64, SZ_8K);
		RK_S32 pixel_buf_fbc_bdy_size = aligned_w * aligned_h * 3 / 2;
		RK_S32 pixel_buf_size = pixel_buf_fbc_hdr_size + pixel_buf_fbc_bdy_size;

		RK_S32 thumb_buf_size = MPP_ALIGN(aligned_w / 64 * aligned_h / 64 * 256, SZ_8K);
		RK_S32 max_buf_cnt = 2 + attr->max_lt_cnt;
		RK_U32 smera_size = 0;
		RK_U32 smera_r_size = 0;
		RK_U32 smera_size_t = 0;
		RK_U32 cmv_size = 0 ;//MPP_ALIGN(mb_wd64 * mb_h64 * 4, 256) * 16;
		RK_S32 max_lt_cnt = attr->max_lt_cnt;
		struct hal_shared_buf *ctx = &entry->shared_buf;
		RK_S32 pixel_buf_size_265 = MPP_ALIGN(((mb_wd64 * mb_h64) << 6), SZ_8K) +
					    ((mb_wd64 * mb_h64) << 12) * 3 / 2;
		RK_U32 i = 0;

		mpp_log("attr->max_width = %d, attr->max_height = %d", attr->max_width, attr->max_height);
		pixel_buf_size = MPP_MAX(pixel_buf_size, pixel_buf_size_265);

		if (1) {
			smera_size_t = MPP_ALIGN(aligned_w, 1024) / 1024 * MPP_ALIGN(aligned_h, 16) / 16 * 16;
			smera_r_size = MPP_ALIGN(aligned_h, 1024) / 1024 * MPP_ALIGN(aligned_w, 16) / 16 * 16;
		} else {
			smera_size_t = MPP_ALIGN(aligned_w, 256) / 256 * MPP_ALIGN(aligned_h, 32) / 32;
			smera_r_size = MPP_ALIGN(aligned_h, 256) / 256 * MPP_ALIGN(aligned_w, 32) / 32;;
		}
		smera_size = MPP_MAX(smera_size_t, smera_r_size);
		if (1) {
			smera_size_t = MPP_ALIGN(aligned_w, 512) / 512 * MPP_ALIGN(aligned_h, 32) / 32 * 16;
			smera_r_size = MPP_ALIGN(aligned_h, 512) / 512 * MPP_ALIGN(aligned_w, 32) / 32 * 16;

		} else {
			smera_size_t = MPP_ALIGN(aligned_w, 256) / 256 * MPP_ALIGN(aligned_h, 32) / 32;
			smera_r_size = MPP_ALIGN(aligned_h, 256) / 256 * MPP_ALIGN(aligned_w, 32) / 32;
		}

		smera_size_t = MPP_MAX(smera_size_t, smera_r_size);
		smera_size = MPP_MAX(smera_size, smera_size_t);

		if (ctx->dpb_bufs) {
			hal_bufs_deinit(ctx->dpb_bufs);
			ctx->dpb_bufs = NULL;
		}

		hal_bufs_init(&ctx->dpb_bufs);

		if (attr->shared_buf_en) {
			size_t sizes[4] = {thumb_buf_size, cmv_size, smera_size, 0};
			hal_bufs_setup(ctx->dpb_bufs, max_buf_cnt, MPP_ARRAY_ELEMS(sizes), sizes);
			if (get_wrap_buf(ctx, attr, max_lt_cnt))
				goto fail;
		} else {
			size_t sizes[4] = {thumb_buf_size, cmv_size, smera_size, pixel_buf_size};
			hal_bufs_setup(ctx->dpb_bufs, max_buf_cnt, MPP_ARRAY_ELEMS(sizes), sizes);
		}

		for (i = 0; i < max_buf_cnt; i++) {
			if (hal_bufs_get_buf(ctx->dpb_bufs, i) == NULL)
				goto fail;
		}

		if (attr->max_width < attr->max_height)
			MPP_SWAP(RK_U32, attr->max_width,  attr->max_height);

		if (attr->max_width  > entry->max_width && attr->max_width > 3072) {
			RK_S32 aligned_w = MPP_ALIGN(attr->max_width, 64);
			RK_S32 h265_buf_size = (aligned_w / 32 - 91) * 26 * 16;
			RK_S32 h264_buf_size = (aligned_w / 64 - 36) * 56 * 16;
			RK_S32 extern_line_size = MPP_MAX(h265_buf_size, h264_buf_size);

			if (ctx->ext_line_buf) {
				mpp_buffer_put(ctx->ext_line_buf);
				ctx->ext_line_buf = NULL;
			}

			if (mpp_buffer_get(NULL, &ctx->ext_line_buf, extern_line_size))
				goto fail;
		}

		entry->max_width = attr->max_width;
		entry->max_height = attr->max_height;
		entry->max_lt_cnt = attr->max_lt_cnt;

	}
	if (!get_vsm_ops()) {
		if (attr->buf_size > entry->ring_buf_size) {
			struct hal_shared_buf *ctx = &entry->shared_buf;
			RK_U32 buf_size = MPP_MAX(attr->buf_size, SZ_16K);
			if (ctx->stream_buf) {
				mpp_buffer_put(ctx->stream_buf);
				ctx->stream_buf = NULL;
			}
			if (mpp_ring_buffer_get(NULL, &ctx->stream_buf, MPP_ALIGN(buf_size, SZ_4K)))
				goto fail;

			entry->ring_buf_size = buf_size;
		}
	}


	return 0;
fail:
	mpp_chan_clear_buf_resource(entry);
	return MPP_NOK;
}


int mpp_vcodec_chan_entry_init(struct mpp_chan *entry, MppCtxType type,
			       MppCodingType coding, void *handle)
{
	unsigned long lock_flag;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return -1;
	}
	spin_lock_irqsave(&entry->chan_lock, lock_flag);
	entry->handle = handle;
	entry->coding_type = coding;
	entry->type = type;
	atomic_set(&entry->stream_count, 0);
	atomic_set(&entry->str_out_cnt, 0);

	atomic_set(&entry->runing, 0);
	atomic_set(&entry->cfg.comb_runing, 0);
	INIT_LIST_HEAD(&entry->stream_done);
	INIT_LIST_HEAD(&entry->stream_remove);
	mutex_init(&entry->stream_done_lock);
	mutex_init(&entry->stream_remove_lock);
	init_waitqueue_head(&entry->wait);
	init_waitqueue_head(&entry->stop_wait);

	if (mpibuf_fn->buf_queue_create && !entry->yuv_queue) {
		entry->yuv_queue =
			mpibuf_fn->buf_queue_create(CHAN_MAX_YUV_POOL_SIZE);
	}

	entry->state = CHAN_STATE_SUSPEND_PENDING;
	spin_unlock_irqrestore(&entry->chan_lock, lock_flag);
	return 0;
}

int mpp_vcodec_chan_entry_deinit(struct mpp_chan *entry)
{
	unsigned long lock_flag;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail");
		return -1;
	}

	spin_lock_irqsave(&entry->chan_lock, lock_flag);
	entry->handle = NULL;
	entry->state = CHAN_STATE_NULL;
	entry->reenc = 0;
	entry->binder_chan_id = -1;
	spin_unlock_irqrestore(&entry->chan_lock, lock_flag);
	atomic_set(&entry->runing, 0);
	if (mpibuf_fn->buf_queue_destroy) {
		mpibuf_fn->buf_queue_destroy(entry->yuv_queue);
		entry->yuv_queue = NULL;
	}

#ifdef CHAN_RELEASE_BUF
	{
		mpp_chan_clear_buf_resource(entry);
	}
#endif
	return 0;
}

void stream_packet_free(struct kref *ref)
{
	MppPacketImpl *packet =
		container_of(ref, MppPacketImpl, ref);

	if (packet)
		mpp_packet_deinit((MppPacket)&packet);
	return;
}

void mpp_vcodec_stream_clear(struct mpp_chan *entry)
{

	MppPacketImpl *packet = NULL, *n;

	mutex_lock(&entry->stream_done_lock);
	list_for_each_entry_safe(packet, n, &entry->stream_done, list) {
		list_del_init(&packet->list);
		kref_put(&packet->ref, stream_packet_free);
		atomic_dec(&entry->stream_count);
	}
	mutex_unlock(&entry->stream_done_lock);

	mutex_lock(&entry->stream_remove_lock);
	list_for_each_entry_safe(packet, n, &entry->stream_remove, list) {
		list_del_init(&packet->list);
		kref_put(&packet->ref, stream_packet_free);
		atomic_dec(&entry->str_out_cnt);
	}
	mutex_unlock(&entry->stream_remove_lock);

}

struct venc_module *mpp_vcodec_get_enc_module_entry(void)
{
	struct venc_module *venc = &g_vcodec_entry.venc;
	return venc;
}

void enc_chan_update_chan_prior_tab(void)
{
	struct mpp_chan *chan = NULL;
	RK_S32 i, j, tmp_pri_chan[2];
	RK_S32 soft_flag = 0;
	unsigned long lock_flags;
	struct venc_module *venc = &g_vcodec_entry.venc;
	spin_lock_irqsave(&venc->enc_lock, lock_flags);
	venc->chan_pri_tab_index = 0;
	/* snap current prior status */
	venc->started_chan_num = 0;
	for (i = 0; i < MAX_ENC_NUM; i++) {
		chan = &venc->mpp_enc_chan_entry[i];

		if (chan->state == CHAN_STATE_RUN) {
			venc->chan_pri_tab[venc->started_chan_num][1] = 1 + chan->cfg.priority;	/* 0 is invalid */
			venc->chan_pri_tab[venc->started_chan_num][0] = i;
			venc->started_chan_num++;
		}
	}

	for (i = venc->started_chan_num; i < MAX_ENC_NUM; i++) {
		venc->chan_pri_tab[i][1] = 0;
		venc->chan_pri_tab[i][0] = 0;
	}

	/* sort prio */
	for (i = 0; i < MAX_ENC_NUM - 1; i++) {
		soft_flag = MPP_OK;
		for (j = i + 1; j < MAX_ENC_NUM; j++) {
			if (venc->chan_pri_tab[i][1] < venc->chan_pri_tab[j][1]) {
				tmp_pri_chan[0] = venc->chan_pri_tab[i][0];
				tmp_pri_chan[1] = venc->chan_pri_tab[i][1];

				venc->chan_pri_tab[i][0] =
					venc->chan_pri_tab[j][0];
				venc->chan_pri_tab[i][1] =
					venc->chan_pri_tab[j][1];

				venc->chan_pri_tab[j][0] = tmp_pri_chan[0];
				venc->chan_pri_tab[j][1] = tmp_pri_chan[1];
				soft_flag = MPP_NOK;
			}
		}
		if (soft_flag == MPP_OK)
			break;
	}
	spin_unlock_irqrestore(&venc->enc_lock, lock_flags);
	return;
}

MPP_RET enc_chan_update_tab_after_enc(RK_U32 curr_chan)
{
	RK_U32 i;
	RK_U32 tmp_index = 0;
	struct venc_module *venc = &g_vcodec_entry.venc;
	unsigned long lock_flags;

	spin_lock_irqsave(&venc->enc_lock, lock_flags);
	venc->chan_pri_tab_index = 0;

	/* snap current prior status */
	for (i = 0; i < venc->started_chan_num; i++) {
		if (venc->chan_pri_tab[i][0] == (RK_S32) curr_chan) {
			tmp_index = i;
			break;
		}
	}

	if (venc->started_chan_num) {
		for (i = tmp_index; i < venc->started_chan_num - 1; i++)
			venc->chan_pri_tab[i][0] = venc->chan_pri_tab[i + 1][0];
		venc->chan_pri_tab[venc->started_chan_num - 1][0] = curr_chan;
	}

	spin_unlock_irqrestore(&venc->enc_lock, lock_flags);
	return MPP_OK;
}

void enc_chan_get_high_prior_chan(void)
{
	struct venc_module *venc = &g_vcodec_entry.venc;
	unsigned long lock_flags;

	spin_lock_irqsave(&venc->enc_lock, lock_flags);
	venc->curr_high_prior_chan =
		venc->chan_pri_tab[venc->chan_pri_tab_index][0];
	venc->chan_pri_tab_index++;
	spin_unlock_irqrestore(&venc->enc_lock, lock_flags);
	return;
}

int mpp_vcodec_init(void)
{

#ifdef SUPPORT_ENC
	mpp_enc_module_init();
#endif

	return 0;
}

int mpp_vcodec_unregister_mpidev(void)
{
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();

	if (!mpidev_fn)
		return -1;

	if (mpidev_fn->destory_dev && g_vcodec_entry.venc.dev) {
		mpidev_fn->destory_dev(g_vcodec_entry.venc.dev);
		g_vcodec_entry.venc.dev = NULL;
	}
	return 0;
}

int mpp_vcodec_deinit(void)
{
	struct venc_module *venc = &g_vcodec_entry.venc;
	int i = 0;

	for (i = 0; i < MAX_ENC_NUM; i++) {
		struct mpp_chan *entry = &venc->mpp_enc_chan_entry[i];
		mpp_chan_clear_buf_resource(entry);
	}

	if (venc->thd) {
		vcodec_thread_stop(venc->thd);
		vcodec_thread_destroy(venc->thd);
		venc->thd = NULL;
	}
	mpp_packet_pool_deinit();
	return 0;
}

int mpp_vcodec_clear_buf_resource(void)
{
	struct venc_module *venc = &g_vcodec_entry.venc;
	int i = 0;

	for (i = 0; i < MAX_ENC_NUM; i++) {
		struct mpp_chan *entry = &venc->mpp_enc_chan_entry[i];
		mpp_chan_clear_buf_resource(entry);
	}
	return 0;
}
EXPORT_SYMBOL(mpp_vcodec_clear_buf_resource);
