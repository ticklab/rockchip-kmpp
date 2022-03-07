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

RK_U32 mpp_vcodec_debug = 0;

#define CHAN_MAX_YUV_POOL_SIZE  5
#define CHAN_MAX_STREAM_POOL_SIZE  2

static struct vcodec_entry g_vcodec_entry;

static void *mpp_vcodec_bind(void *in_param, void *out_param)
{
	struct mpp_chan *entry = NULL;
	int id = -1;
	MppCtxType type = MPP_CTX_ENC;
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	if( !mpidev_fn){
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

static int mpp_vcodec_unbind(void *ctx, int discard)
{
	return 0;
}

static struct vcodec_threads *mpp_vcodec_get_thd_handle(struct mpi_obj *obj)
{
	struct vcodec_threads *thd = NULL;
	void *ctx = NULL;
	struct mpp_chan *entry = NULL;
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	if (!mpidev_fn){
		mpp_err("get_mpidev_ops fail");
		return NULL;
	}

	if (mpidev_fn->get_chnl_ctx)
		ctx = mpidev_fn->get_chnl_ctx(obj);

	if (ctx)
		entry = container_of(ctx, struct mpp_chan, yuv_queue);

	if (entry) {
		switch (entry->type) {
		case MPP_CTX_ENC:{
				thd = g_vcodec_entry.venc.thd;
			}
			break;
		default:{
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
	if (!mpidev_fn){
		mpp_err("get_mpidev_ops fail");
		return -1;
	}
	thd = mpp_vcodec_get_thd_handle(obj);
	if (mpidev_fn->handle_message)
		ret = mpidev_fn->handle_message(obj, event, args);

	if (ret && thd) {
		vcodec_thread_trigger(thd);
	}
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
		venc->dev = mpidev_fn->create_dev(venc->name , &gdev_fn);

	if (!venc->dev) {
		mpp_err("creat mpi dev & register fail \n");
	}
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

	venc->check = &venc;
	venc->thd = thds;
	spin_lock_init(&venc->enc_lock);
	return 0;
}

int mpp_vcodec_get_free_chan(MppCtxType type)
{
	RK_S32 i = 0;
	switch (type) {

	case MPP_CTX_ENC:{
			unsigned long lock_flag;
			struct venc_module *venc = &g_vcodec_entry.venc;
			spin_lock_irqsave(&venc->enc_lock, lock_flag);
			for (i = 0; i < MAX_ENC_NUM; i++) {
				if (!venc->mpp_enc_chan_entry[i].handle) {
					break;
				}
			}
			if (i >= MAX_ENC_NUM) {
				i = -1;
			}
			spin_unlock_irqrestore(&venc->enc_lock, lock_flag);
		}
		break;
	default:{
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
	case MPP_CTX_ENC:{
			struct venc_module *venc = &g_vcodec_entry.venc;
			if (chan_id < 0 || chan_id > MAX_ENC_NUM) {
				mpp_err("chan id %d is over, full max is %d \n",
					chan_id, MAX_ENC_NUM);
				return NULL;
			}
			chan = &venc->mpp_enc_chan_entry[chan_id];
		}
		break;
	default:{
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
	case MPP_CTX_ENC:{
			unsigned long lock_flag;
			struct venc_module *venc = &g_vcodec_entry.venc;
			spin_lock_irqsave(&venc->enc_lock, lock_flag);
			num_chan = venc->num_enc;
			spin_unlock_irqrestore(&venc->enc_lock, lock_flag);
		} break;
	default:{
			mpp_err("MppCtxType error %d", type);
		}
		break;
	}
	return num_chan;
}

void mpp_vcodec_inc_chan_num(MppCtxType type)
{
	switch (type) {
	case MPP_CTX_ENC:{
			unsigned long lock_flag;
			struct venc_module *venc = &g_vcodec_entry.venc;
			spin_lock_irqsave(&venc->enc_lock, lock_flag);
			venc->num_enc++;
			spin_unlock_irqrestore(&venc->enc_lock, lock_flag);
		} break;
	default:{
			mpp_err("MppCtxType error %d", type);
		}
		break;
	}
	return;
}

void mpp_vcodec_dec_chan_num(MppCtxType type)
{
	switch (type) {
	case MPP_CTX_ENC:{
			unsigned long lock_flag;
			struct venc_module *venc = &g_vcodec_entry.venc;
			spin_lock_irqsave(&venc->enc_lock, lock_flag);
			venc->num_enc--;
			spin_unlock_irqrestore(&venc->enc_lock, lock_flag);

			if(!venc->num_enc){
				vcodec_thread_stop(venc->thd);
			}

		} break;
	default:{
			mpp_err("MppCtxType error %d", type);
		}
		break;
	}

	return;
}

int mpp_vcodec_chan_entry_init(struct mpp_chan *entry, MppCtxType type,
			       MppCodingType coding, void *handle)
{
	unsigned long lock_flag;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	if (!mpibuf_fn){
		mpp_err_f("mpibuf_ops get fail");
		return -1;
	}
	spin_lock_irqsave(&entry->chan_lock, lock_flag);
	entry->handle = handle;
	entry->coding_type = coding;
	entry->type = type;
	atomic_set(&entry->stream_count, 0);

	atomic_set(&entry->runing, 0);
	atomic_set(&entry->cfg.comb_runing, 0);
	INIT_LIST_HEAD(&entry->stream_done);
	INIT_LIST_HEAD(&entry->stream_remove);
	mutex_init(&entry->stream_done_lock);
	mutex_init(&entry->stream_remove_lock);
	init_waitqueue_head(&entry->wait);
	init_waitqueue_head(&entry->stop_wait);

	if (mpibuf_fn->buf_queue_create) {
		entry->yuv_queue =
			mpibuf_fn->buf_queue_create(CHAN_MAX_YUV_POOL_SIZE);
	}

	entry->state = CHAN_STATE_SUSPEND;
	spin_unlock_irqrestore(&entry->chan_lock, lock_flag);
	return 0;
}

int mpp_vcodec_chan_entry_deinit(struct mpp_chan *entry)
{
	unsigned long lock_flag;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	if (!mpibuf_fn){
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
	}
	return 0;
}

struct stream_packet *stream_packet_alloc(void)
{
	struct stream_packet *packet = NULL;
	packet = kzalloc(sizeof(*packet), GFP_KERNEL);
	if (!packet) {
		mpp_err("alloc stream packet fail \n");
		return NULL;
	}

	INIT_LIST_HEAD(&packet->list);
	kref_init(&packet->ref);
	return packet;
}

void stream_packet_free(struct kref *ref)
{
	struct stream_packet *packet =
	container_of(ref, struct stream_packet, ref);

	if (packet->src) {
		mpp_packet_deinit((MppPacket) & packet->src);
	}

	kfree(packet);
	return;
}

void mpp_vcodec_stream_clear(struct mpp_chan *entry)
{
	struct stream_packet *packet = NULL, *n;

	mutex_lock(&entry->stream_done_lock);
	list_for_each_entry_safe(packet, n, &entry->stream_done, list) {
		list_del_init(&packet->list);
		kref_put(&packet->ref, stream_packet_free);
	}
	mutex_unlock(&entry->stream_done_lock);

	mutex_lock(&entry->stream_remove_lock);
	list_for_each_entry_safe(packet, n, &entry->stream_remove, list) {
		list_del_init(&packet->list);
		kref_put(&packet->ref, stream_packet_free);
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
		if (soft_flag == MPP_OK) {
			break;
		}
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
		for (i = tmp_index; i < venc->started_chan_num - 1; i++) {
			venc->chan_pri_tab[i][0] = venc->chan_pri_tab[i + 1][0];
		}
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

int mpp_vcodec_deinit(void)
{
    struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	struct venc_module *venc = &g_vcodec_entry.venc;
    if (!mpidev_fn){
		mpp_err("mpp_vcodec_deinit get_mpidev_ops fail");
		return -1;
	}

	if (mpidev_fn->destory_dev && g_vcodec_entry.venc.dev)
		mpidev_fn->destory_dev(g_vcodec_entry.venc.dev);

    if (venc->thd){
        vcodec_thread_stop(venc->thd);
        vcodec_thread_destroy(venc->thd);
        venc->thd = NULL;
    }

	return 0;
}
