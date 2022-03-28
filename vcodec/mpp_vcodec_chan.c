// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/dma-buf.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include "mpp_vcodec_chan.h"
#include "mpp_vcodec_base.h"
#include "mpp_vcodec_flow.h"
#include "mpp_log.h"
#include "mpp_enc.h"
#include "mpp_vcodec_thread.h"
#include "rk_venc_cfg.h"
#include "rk_export_func.h"
#include "mpp_packet_impl.h"
#include "mpp_time.h"

int mpp_vcodec_schedule(void)
{
	return 0;
}

int mpp_vcodec_chan_create(struct vcodec_attr *attr)
{
	int chan_id = attr->chan_id;
	MppCtxType type = attr->type;
	MppCodingType coding = attr->coding;
	RK_S32	online = attr->online;
    RK_U32  buf_size = attr->buf_size;
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	MPP_RET ret = MPP_NOK;
	RK_U32 init_done = 1;
	RK_U32 num_chan = mpp_vcodec_get_chan_num(type);
	mpp_log("num_chan = %d", num_chan);
	mpp_assert(chan_entry->chan_id == chan_id);
	if (chan_entry->handle != NULL) {
		chan_id = mpp_vcodec_get_free_chan(type);
		if (chan_id < 0) {
			return -1;
		}
		mpp_log("current chan %d already created get new chan_id %d \n",
			attr->chan_id, chan_id);
		attr->chan_id = chan_id;
		chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	}
	switch (type) {
	case MPP_CTX_DEC:{

		}
		break;
	case MPP_CTX_ENC:{
			MppEnc enc = NULL;
			MppEncInitCfg cfg = {
				coding,
				online,
				buf_size,
				attr->max_strm_cnt,
				attr->shared_buf_en
			};
			if (!num_chan) {
				struct venc_module *venc =
				    mpp_vcodec_get_enc_module_entry();
				struct vcodec_threads *thd = venc->thd;

				mpp_log("start thread \n");
				vcodec_thread_start(thd);
			}
			ret = mpp_enc_init(&enc, &cfg);
			if (ret)
				break;
			mpp_enc_register_chl(enc,
					     (void *)mpp_vcodec_enc_int_handle,
					     chan_id);
			mpp_vcodec_chan_entry_init(chan_entry, type, coding,
						   (void *)enc);
			mpp_vcodec_inc_chan_num(type);

			chan_entry->last_yuv_time = mpp_time();
			chan_entry->last_jeg_combo_start = mpp_time();
			chan_entry->last_jeg_combo_end = mpp_time();
			init_done = 1;
		} break;
	default:{
			mpp_err("create chan error type %d\n", type);
		}
		break;
	}

	if (!init_done) {
		mpp_err("create chan %d fail \n", chan_id);
	}

	return 0;
}

#define VCODEC_WAIT_TIMEOUT_DELAY		(2000)

int mpp_vcodec_chan_destory(int chan_id, MppCtxType type)
{
	int ret;
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
    if(!chan_entry->handle){
        return 0;
    }
	mpp_assert(chan_entry->handle != NULL);
	switch (type) {
	case MPP_CTX_DEC:{

		}
		break;
	case MPP_CTX_ENC:{
			ret = mpp_vcodec_chan_stop(chan_id, type);

    		ret = wait_event_timeout(chan_entry->stop_wait,
    						 !atomic_read(&chan_entry->runing),
    						 msecs_to_jiffies
    						 (VCODEC_WAIT_TIMEOUT_DELAY));

			mpp_vcodec_stream_clear(chan_entry);
			mpp_enc_deinit(chan_entry->handle);
			mpp_vcodec_dec_chan_num(type);
			mpp_vcodec_chan_entry_deinit(chan_entry);
		}
		break;
	default:{
			mpp_err("create chan error type %d\n", type);
		}
		break;
	}
	return 0;
}

int mpp_vcodec_chan_start(int chan_id, MppCtxType type)
{
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	mpp_assert(chan_entry->handle != NULL);
	switch (type) {
	case MPP_CTX_DEC:{

		}
		break;
	case MPP_CTX_ENC:{
			unsigned long lock_flag;
			mpp_enc_start(chan_entry->handle);
			spin_lock_irqsave(&chan_entry->chan_lock, lock_flag);
			chan_entry->state = CHAN_STATE_RUN;
			spin_unlock_irqrestore(&chan_entry->chan_lock,
					       lock_flag);
			enc_chan_update_chan_prior_tab();
		} break;
	default:{
			mpp_err("create chan error type %d\n", type);
		}
		break;
	}
	return 0;
}

int mpp_vcodec_chan_stop(int chan_id, MppCtxType type)
{
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	int ret = 0;
	mpp_assert(chan_entry->handle != NULL);
	switch (type) {
	case MPP_CTX_DEC:{

		}
		break;
	case MPP_CTX_ENC:{
			unsigned long lock_flag;
			ret = mpp_enc_stop(chan_entry->handle);
			spin_lock_irqsave(&chan_entry->chan_lock, lock_flag);
            if (chan_entry->state != CHAN_STATE_RUN) {
                spin_unlock_irqrestore(&chan_entry->chan_lock,
                                           lock_flag);

                return 0;
			}
			chan_entry->state = CHAN_STATE_SUSPEND;
			spin_unlock_irqrestore(&chan_entry->chan_lock,
					       lock_flag);
			enc_chan_update_chan_prior_tab();
		}
		break;
	default:{
			mpp_err("create chan error type %d\n", type);
		}
		break;
	}
	return ret;
}

int mpp_vcodec_chan_pause(int chan_id, MppCtxType type)
{
	return 0;
}

int mpp_vcodec_chan_resume(int chan_id, MppCtxType type)
{
	return 0;
}

int mpp_vcodec_chan_get_stream(int chan_id, MppCtxType type,
			       struct venc_packet *enc_packet)
{
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	RK_U32 count = atomic_read(&chan_entry->stream_count);

	if (count) {
        	MppPacketImpl *packet = NULL;

        	mutex_lock(&chan_entry->stream_done_lock);
		packet = list_first_entry_or_null(&chan_entry->stream_done,
						  MppPacketImpl, list);
		mutex_unlock(&chan_entry->stream_done_lock);

		atomic_dec(&chan_entry->stream_count);

		enc_packet->flag = mpp_packet_get_flag(packet);
		enc_packet->len = mpp_packet_get_length(packet);
		enc_packet->temporal_id = mpp_packet_get_temporal_id(packet);
		enc_packet->u64pts = mpp_packet_get_pts(packet);
		enc_packet->data_num = 1;
		enc_packet->u64priv_data= packet->buf.mpi_buf_id; //get mpp_buffer fd from ring buf
		enc_packet->offset = packet->buf.start_offset;
		enc_packet->u64packet_addr = (uintptr_t )packet;
		enc_packet->buf_size = mpp_buffer_get_size(packet->buf.buf);

		mutex_lock(&chan_entry->stream_done_lock);
		list_del_init(&packet->list);
		mutex_lock(&chan_entry->stream_remove_lock);
	    	list_move_tail(&packet->list, &chan_entry->stream_remove);
		mutex_unlock(&chan_entry->stream_remove_lock);
		mutex_unlock(&chan_entry->stream_done_lock);

        	atomic_inc(&chan_entry->str_out_cnt);
	}
	return 0;
}

int mpp_vcodec_chan_put_stream(int chan_id, MppCtxType type,
			       struct venc_packet *enc_packet)
{

	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	MppPacketImpl *packet = NULL, *n;
    struct venc_module *venc =  NULL;

	mutex_lock(&chan_entry->stream_remove_lock);
	list_for_each_entry_safe(packet, n, &chan_entry->stream_remove, list) {
		if ((uintptr_t)packet == enc_packet->u64packet_addr) {
			list_del_init(&packet->list);
			kref_put(&packet->ref, stream_packet_free);
			atomic_dec(&chan_entry->str_out_cnt);
			venc = mpp_vcodec_get_enc_module_entry();
			vcodec_thread_trigger(venc->thd);
			break;
		}
	}
    mutex_unlock(&chan_entry->stream_remove_lock);


    return 0;
}

int mpp_vcodec_chan_push_frm(int chan_id, void *param)
{
	struct mpp_frame_infos *info = param;
	struct dma_buf *dmabuf = NULL;
	struct venc_module *venc = NULL;
	struct vcodec_threads *thd;
	struct mpp_chan *chan_entry = NULL;
	struct mpi_buf *buf = NULL;
	int ret = 0;
    struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
    if (!mpibuf_fn){
       mpp_err_f("mpibuf_ops get fail");
       return -1;
    }

	chan_entry = mpp_vcodec_get_chan_entry(chan_id, MPP_CTX_ENC);
	venc = mpp_vcodec_get_enc_module_entry();
	thd = venc->thd;

	if (mpibuf_fn->dma_buf_import) {
		dmabuf = dma_buf_get(info->fd);	//add one ref will be free in mpi_buf
        dma_buf_end_cpu_access(dmabuf, DMA_TO_DEVICE); //flush to device
		// mpp_log("import dmabuf %p \n", dmabuf);
		if (IS_ERR(dmabuf)) {
			mpp_err("dma_buf_get fd %d failed\n", info->fd);
			return -1;
		}
		buf = mpibuf_fn->dma_buf_import(dmabuf, info);
		dma_buf_put(dmabuf);
	}

	if (NULL != buf) {
		if (mpibuf_fn->buf_queue_push)
			ret =
			    mpibuf_fn->buf_queue_push(chan_entry->yuv_queue,
							   buf);
		if (ret) {
			vcodec_thread_trigger(thd);
			// mpp_err("push frm to buf queue ok \n");
		}
	} else {
		if (dmabuf)
			dma_buf_put(dmabuf);
		mpp_err("import dma buf to mpi buf fail \n");
	}

	return 0;
}

int mpp_vcodec_chan_control(int chan_id, MppCtxType type, int cmd, void *arg)
{
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	mpp_assert(chan_entry->handle != NULL);

	switch (type) {
	case MPP_CTX_DEC:{
			;
		}
		break;
	case MPP_CTX_ENC:{
			mpp_enc_control(chan_entry->handle, cmd, arg);
		}
		break;
	default:{
			mpp_err("control type %d error\n", type);
		}
		break;
	}
	return 0;
}
