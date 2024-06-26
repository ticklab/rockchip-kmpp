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
#include <linux/wait.h>
#include <linux/sched.h>
#include "rk_type.h"
#include "mpp_vcodec_flow.h"
#include "mpp_vcodec_base.h"
#include "mpp_vcodec_thread.h"
#include "mpp_enc.h"
#include "mpp_log.h"
#include "mpp_buffer.h"
#include "mpp_packet.h"
#include "rk_export_func.h"
#include "mpp_vcodec_debug.h"
#include "mpp_packet_impl.h"
#include "mpp_time.h"
#include <linux/module.h>

int max_packet_num = 100;
module_param(max_packet_num, int, 0644);

static MPP_RET frame_add_osd(MppFrame frame, MppEncOSDData3 *osd_data)
{
	RK_U32 i = 0;

	mpp_frame_add_osd(frame, (MppOsd)osd_data);

	for (i = 0; i < osd_data->num_region; i++) {
		if (osd_data->region[i].osd_buf.buf)
			mpi_buf_unref(osd_data->region[i].osd_buf.buf);

		if (osd_data->region[i].inv_cfg.inv_buf.buf) {
			mpi_buf_unref(
				osd_data->region[i].inv_cfg.inv_buf.buf);
		}
	}

	return MPP_OK;
}

static MPP_RET enc_chan_get_buf_info(struct mpi_buf *buf,
				     struct mpp_frame_infos *frm_info,
				     MppFrame *frame, RK_S32 chan_id)
{
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	memset(frm_info, 0, sizeof(*frm_info));
	if (!mpibuf_fn || !mpibuf_fn->get_buf_frm_info) {
		mpp_err("get buf info fail");
		return MPP_NOK;
	}
	if (mpibuf_fn->get_buf_frm_info(buf, frm_info, chan_id))
		return MPP_NOK;

	mpp_frame_init(frame);
	mpp_frame_set_width(*frame, frm_info->width);
	mpp_frame_set_height(*frame, frm_info->height);
	mpp_frame_set_hor_stride(*frame, frm_info->hor_stride);
	mpp_frame_set_ver_stride(*frame, frm_info->ver_stride);
	mpp_frame_set_pts(*frame, frm_info->pts);
	mpp_frame_set_dts(*frame, frm_info->dts);
	mpp_frame_set_fmt(*frame, frm_info->fmt);
	mpp_frame_set_offset_x(*frame, frm_info->offset_x);
	mpp_frame_set_offset_y(*frame, frm_info->offset_y);
	mpp_frame_set_is_gray(*frame, frm_info->is_gray);
	mpp_frame_set_is_full(*frame, frm_info->is_full);
	mpp_frame_set_phy_addr(*frame, frm_info->phy_addr);
	if (frm_info->osd_buf)
		frame_add_osd(*frame, (MppEncOSDData3 *)frm_info->osd_buf);
	if (frm_info->pp_info)
		mpp_frame_add_ppinfo(*frame, (MppPpInfo*)frm_info->pp_info);

	return MPP_OK;
}

static MPP_RET enc_chan_process_single_chan(RK_U32 chan_id)
{
	struct mpp_chan *chan_entry =
		mpp_vcodec_get_chan_entry(chan_id, MPP_CTX_ENC);
	struct mpp_chan *comb_chan = NULL;
	MppFrame frame = NULL;
	MppFrame comb_frame = NULL;
	MppBuffer frm_buf = NULL;
	struct mpi_buf *buf = NULL;
	struct mpp_frame_infos frm_info;
	unsigned long lock_flag;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	RK_U64 cfg_start = 0, cfg_end = 0;

	if (!chan_entry) {
		mpp_err_f("chan_entry is NULL\n");
		return MPP_NOK;
	}
	if (!mpibuf_fn) {
		mpp_err_f("mpibuf_ops get fail\n");
		return MPP_NOK;
	}

	spin_lock_irqsave(&chan_entry->chan_lock, lock_flag);
	if (chan_entry->state != CHAN_STATE_RUN) {
		mpp_err("cur chnl %d state is no runing\n", chan_id);
		spin_unlock_irqrestore(&chan_entry->chan_lock, lock_flag);
		return MPP_OK;
	}
	spin_unlock_irqrestore(&chan_entry->chan_lock, lock_flag);

	if (atomic_read(&chan_entry->runing) > 0) {
		mpp_vcodec_detail("cur chnl %d state is wating irq\n", chan_id);
		return MPP_OK;
	}

	mpp_vcodec_detail("enc_chan_process_single_chan id %d\n", chan_id);
	if (!chan_entry->reenc) {
		MppBufferInfo info;

		if (!mpp_enc_check_pkt_pool((MppEnc)chan_entry->handle)) {
			mpp_log("cur chnl %d pkt pool non free buf\n", chan_id);
			return MPP_OK;
		}

		if (mpibuf_fn->buf_queue_pop)
			buf = mpibuf_fn->buf_queue_pop(chan_entry->yuv_queue);
		if (!buf)
			return MPP_OK;

		chan_entry->gap_time = (RK_S32)(mpp_time() - chan_entry->last_yuv_time);
		chan_entry->last_yuv_time = mpp_time();

		if (enc_chan_get_buf_info(buf, &frm_info, &frame, chan_entry->chan_id)) {
			mpi_buf_unref(buf);
			return MPP_OK;
		}
		memset(&info, 0, sizeof(info));
		info.hnd = buf;
		mpp_buffer_import(&frm_buf, &info);
		mpp_vcodec_jpegcomb("attach jpeg id %d\n", frm_info.jpeg_chan_id);
		if (frm_info.jpeg_chan_id > 0) {
			comb_chan = mpp_vcodec_get_chan_entry(frm_info.jpeg_chan_id, MPP_CTX_ENC);
			if (comb_chan->state != CHAN_STATE_RUN)
				comb_chan = NULL;

			if (comb_chan && comb_chan->handle) {

				chan_entry->combo_gap_time = (RK_S32)(mpp_time() - chan_entry->last_jeg_combo_start);
				chan_entry->last_jeg_combo_start = mpp_time();
				chan_entry->binder_chan_id = frm_info.jpeg_chan_id;

				mpp_frame_init(&comb_frame);
				mpp_frame_copy(comb_frame, frame);
				if (frm_info.jpg_combo_osd_buf)
					frame_add_osd(comb_frame, (MppEncOSDData3 *)frm_info.jpg_combo_osd_buf);
				mpp_frame_set_buffer(comb_frame, frm_buf);
			}
		}
		mpp_frame_set_buffer(frame, frm_buf);
		if (mpibuf_fn->buf_unref)
			mpibuf_fn->buf_unref(buf);
	}

	if (frame != NULL || chan_entry->reenc) {
		MPP_RET ret = MPP_OK;

		cfg_start = mpp_time();
		atomic_inc(&chan_entry->runing);
		if ((atomic_read(&chan_entry->stream_count) + atomic_read(&chan_entry->str_out_cnt) >
		     max_packet_num) && !chan_entry->reenc) {
			mpp_frame_deinit(&frame);
			ret = MPP_NOK;
		} else
			ret = mpp_enc_cfg_reg((MppEnc)chan_entry->handle, frame);

		if (MPP_OK == ret) {
			if (comb_chan && comb_chan->handle) {
				atomic_inc(&comb_chan->runing);
				atomic_inc(&chan_entry->cfg.comb_runing);
				ret = mpp_enc_cfg_reg((MppEnc)comb_chan->handle, comb_frame);
				if (MPP_OK == ret) {
					ret = mpp_enc_hw_start((MppEnc)chan_entry->handle,
							       (MppEnc)comb_chan->handle);
					if (MPP_OK != ret) {
						mpp_err("combo start fail \n");
						atomic_dec(&chan_entry->cfg.comb_runing);
						atomic_dec(&comb_chan->runing);
						wake_up(&comb_chan->stop_wait);
					} else
						comb_chan->master_chan_id = chan_entry->chan_id;

				} else {
					struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();

					mpp_err("combo cfg fail \n");
					atomic_dec(&comb_chan->runing);
					atomic_dec(&chan_entry->cfg.comb_runing);
					wake_up(&comb_chan->stop_wait);
					if (mpidev_fn && mpidev_fn->notify_drop_frm && comb_chan)
						mpidev_fn->notify_drop_frm(comb_chan->chan_id);
					ret = mpp_enc_hw_start( (MppEnc)chan_entry->handle, NULL);
				}
			} else
				ret = mpp_enc_hw_start((MppEnc)chan_entry->handle, NULL);
		}

		if (MPP_OK != ret) {
			struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
			struct venc_module *venc = NULL;

			venc = mpp_vcodec_get_enc_module_entry();
			atomic_dec(&chan_entry->runing);
			wake_up(&chan_entry->stop_wait);
			if (mpidev_fn && mpidev_fn->notify_drop_frm && chan_entry)
				mpidev_fn->notify_drop_frm(chan_entry->chan_id);
			if (comb_frame) {
				if (mpidev_fn && mpidev_fn->notify_drop_frm && comb_chan)
					mpidev_fn->notify_drop_frm(comb_chan->chan_id);
				mpp_frame_deinit(&comb_frame);
			}
			vcodec_thread_trigger(venc->thd);
		}
		cfg_end = mpp_time();
		chan_entry->last_cfg_time = cfg_end - cfg_start;
	}
	if (frm_buf)
		mpp_buffer_put(frm_buf);

	enc_chan_update_tab_after_enc(chan_id);

	return MPP_OK;
}

void mpp_vcodec_enc_add_packet_list(struct mpp_chan *chan_entry,
				    MppPacket packet)
{
	MppPacketImpl *p = (MppPacketImpl *) packet;
	unsigned long flags;

	mpp_vcodec_detail("packet size %zu\n", mpp_packet_get_length(packet));
	if (!get_vsm_ops()) {
		spin_lock_irqsave(&chan_entry->stream_list_lock, flags);
		list_add_tail(&p->list, &chan_entry->stream_done);
		atomic_inc(&chan_entry->stream_count);
		spin_unlock_irqrestore(&chan_entry->stream_list_lock, flags);
		wake_up(&chan_entry->wait);
	} else
		mpp_packet_deinit(&packet);

	chan_entry->reenc = 0;
}

void mpp_vcodec_enc_int_handle(int chan_id)
{
	MppPacket packet = NULL;
	MppPacket jpeg_packet = NULL;
	MPP_RET ret = MPP_OK;
	struct venc_module *venc = NULL;
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, MPP_CTX_ENC);
	struct mpp_chan *comb_entry = NULL;

	chan_entry->reenc = 1;
	venc = mpp_vcodec_get_enc_module_entry();
	if (!venc) {
		mpp_err_f("get_enc_module_entry fail");
		return;
	}
	if (atomic_read(&chan_entry->cfg.comb_runing)) {
		comb_entry = mpp_vcodec_get_chan_entry(
				     chan_entry->binder_chan_id, MPP_CTX_ENC);
		if (comb_entry && comb_entry->handle) {
			struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();

			chan_entry->last_jeg_combo_end = mpp_time();
			ret = mpp_enc_int_process((MppEnc)chan_entry->handle,
						  comb_entry->handle, &packet,
						  &jpeg_packet);
			if (jpeg_packet)
				mpp_vcodec_enc_add_packet_list(comb_entry, jpeg_packet);
			else if (mpidev_fn && mpidev_fn->notify_drop_frm)
				mpidev_fn->notify_drop_frm(comb_entry->chan_id);
		}
		atomic_dec(&chan_entry->cfg.comb_runing);
		atomic_dec(&comb_entry->runing);
		wake_up(&comb_entry->stop_wait);
		chan_entry->binder_chan_id = -1;
		comb_entry->master_chan_id = -1;
	} else {
		ret = mpp_enc_int_process((MppEnc)chan_entry->handle, NULL,
					  &packet, &jpeg_packet);
	}

	if (packet)
		mpp_vcodec_enc_add_packet_list(chan_entry, packet);

	if (ret) {
		mpp_err("enc %d handle int err", chan_id);
		chan_entry->reenc = 0;
	}
	atomic_dec(&chan_entry->runing);
	wake_up(&chan_entry->stop_wait);
	vcodec_thread_trigger(venc->thd);

	return;
}

int mpp_vcodec_enc_run_task(RK_U32 chan_id)
{
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, MPP_CTX_ENC);
	unsigned long lock_flag;

	if (!chan_entry)
		return -EINVAL;

	spin_lock_irqsave(&chan_entry->chan_lock, lock_flag);
	if (chan_entry->state != CHAN_STATE_RUN) {
		spin_unlock_irqrestore(&chan_entry->chan_lock, lock_flag);
		return MPP_NOK;
	}
	spin_unlock_irqrestore(&chan_entry->chan_lock, lock_flag);

	return mpp_enc_run_task(chan_entry->handle);
}

int mpp_vcodec_enc_routine(void *param)
{
	RK_U32 started_chan_num = 0;
	RK_U32 next_chan;
	unsigned long lock_flag;
	struct venc_module *venc = mpp_vcodec_get_enc_module_entry();

	if (!venc) {
		mpp_err_f("get_enc_module_entry fail\n");
		return MPP_ERR_VALUE;
	}
	spin_lock_irqsave(&venc->enc_lock, lock_flag);
	started_chan_num = venc->started_chan_num;
	venc->chan_pri_tab_index = 0;
	spin_unlock_irqrestore(&venc->enc_lock, lock_flag);
	mpp_vcodec_detail("mpp_vcodec_enc_routine started_chan_num %d\n",
			  started_chan_num);
	while (started_chan_num--) {
		/* get high prior chan id */
		enc_chan_get_high_prior_chan();
		next_chan = venc->curr_high_prior_chan;
		if (next_chan >= MAX_ENC_NUM)
			continue;
		if (enc_chan_process_single_chan(next_chan) != MPP_OK)
			break;
	}
	mpp_vcodec_detail("mpp_vcodec_enc_routine end\n");

	return MPP_OK;
}

void *mpp_vcodec_dec_routine(void *param)
{
	return NULL;
}
