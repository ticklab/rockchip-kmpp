// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define  MODULE_TAG "mpp_enc"

#include <linux/string.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "mpp_mem.h"
#include "mpp_maths.h"
#include "mpp_packet.h"
#include "mpp_enc_debug.h"
#include "mpp_enc_cfg_impl.h"
#include "mpp_enc_impl.h"
#include "mpp_enc.h"
#include "rk_export_func.h"

RK_U32 mpp_enc_debug = 0;
module_param(mpp_enc_debug, uint, 0644);
MODULE_PARM_DESC(mpp_enc_debug, "bits mpp_enc debug information");

MPP_RET mpp_enc_init(MppEnc * enc, MppEncInitCfg * cfg)
{
	MPP_RET ret;
	MppCodingType coding = cfg->coding;
	EncImpl impl = NULL;
	MppEncImpl *p = NULL;
	MppEncHal enc_hal = NULL;
	MppEncHalCfg enc_hal_cfg;
	EncImplCfg ctrl_cfg;

	//  mpp_env_get_u32("mpp_enc_debug", &mpp_enc_debug, 0);

	if (NULL == enc) {
		mpp_err_f("failed to malloc context\n");
		return MPP_ERR_NULL_PTR;
	}

	*enc = NULL;

	p = mpp_calloc(MppEncImpl, 1);
	if (NULL == p) {
		mpp_err_f("failed to malloc context\n");
		return MPP_ERR_MALLOC;
	}

	ret = mpp_enc_refs_init(&p->refs);
	if (ret) {
		mpp_err_f("could not init enc refs\n");
		goto ERR_RET;
	}
	// H.264 encoder use mpp_enc_hal path
	// create hal first
	enc_hal_cfg.coding = coding;
	enc_hal_cfg.cfg = &p->cfg;
	enc_hal_cfg.type = VPU_CLIENT_BUTT;
	enc_hal_cfg.dev = NULL;
	enc_hal_cfg.online = cfg->online;

	ctrl_cfg.coding = coding;
	ctrl_cfg.type = VPU_CLIENT_BUTT;
	ctrl_cfg.cfg = &p->cfg;
	ctrl_cfg.refs = p->refs;
	ctrl_cfg.task_count = 2;

	ret = mpp_enc_hal_init(&enc_hal, &enc_hal_cfg);
	if (ret) {
		mpp_err_f("could not init enc hal\n");
		goto ERR_RET;
	}

	ctrl_cfg.type = enc_hal_cfg.type;
	ctrl_cfg.task_count = -1;

	ret = enc_impl_init(&impl, &ctrl_cfg);
	if (ret) {
		mpp_err_f("could not init impl\n");
		goto ERR_RET;
	}
	mpp_enc_impl_alloc_task(p);

	rc_init(&p->rc_ctx, coding, NULL);

	/*  ret = hal_info_init(&p->hal_info, MPP_CTX_ENC, coding);
	   if (ret) {
	   mpp_err_f("could not init hal info\n");
	   goto ERR_RET;
	   } */

	p->coding = coding;
	p->impl = impl;
	p->enc_hal = enc_hal;
	p->dev = enc_hal_cfg.dev;
//    p->mpp      = cfg->mpp;
	p->sei_mode = MPP_ENC_SEI_MODE_DISABLE;
    p->version_info = VCODEC_VERSION;
    p->version_length = strlen(p->version_info);
	p->rc_cfg_size = SZ_1K;
	p->rc_cfg_info = mpp_calloc_size(char, p->rc_cfg_size);

	{
		// create header packet storage
		size_t size = SZ_1K;
		p->hdr_buf = mpp_calloc_size(void, size);

		mpp_packet_init(&p->hdr_pkt, p->hdr_buf, size);
		mpp_packet_set_length(p->hdr_pkt, 0);
	}

	/* NOTE: setup configure coding for check */
	p->cfg.codec.coding = coding;
//	p->cfg.plt_cfg.plt = &p->cfg.plt_data;
	mpp_enc_ref_cfg_init(&p->cfg.ref_cfg);
	ret = mpp_enc_ref_cfg_copy(p->cfg.ref_cfg, mpp_enc_ref_default());
	ret = mpp_enc_refs_set_cfg(p->refs, mpp_enc_ref_default());
	sema_init(&p->enc_sem, 1);
	p->stop_flag = 1;
	p->rb_userdata.free_cnt = MAX_USRDATA_CNT;
	*enc = p;
	return ret;
ERR_RET:
	mpp_enc_deinit(p);
	return ret;
}

MPP_RET mpp_enc_deinit(MppEnc ctx)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	down(&enc->enc_sem);
	if (NULL == enc) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}
#if 0
	if (enc->hal_info) {
		hal_info_deinit(enc->hal_info);
		enc->hal_info = NULL;
	}
#endif
	mpp_enc_impl_free_task(enc);

	if (enc->impl) {
		enc_impl_deinit(enc->impl);
		enc->impl = NULL;
	}
	if (enc->enc_hal) {
		mpp_enc_hal_deinit(enc->enc_hal);
		enc->enc_hal = NULL;
	}

	if (enc->hdr_pkt)
		mpp_packet_deinit(&enc->hdr_pkt);

	MPP_FREE(enc->hdr_buf);

	if (enc->cfg.ref_cfg) {
		mpp_enc_ref_cfg_deinit(&enc->cfg.ref_cfg);
		enc->cfg.ref_cfg = NULL;
	}

	if (enc->refs) {
		mpp_enc_refs_deinit(&enc->refs);
		enc->refs = NULL;
	}

	if (enc->rc_ctx) {
		rc_deinit(enc->rc_ctx);
		enc->rc_ctx = NULL;
	}

	MPP_FREE(enc->rc_cfg_info);
	enc->rc_cfg_size = 0;
	enc->rc_cfg_length = 0;
	up(&enc->enc_sem);
	if (enc->strm_pool){
		mpp_log("buf_pool_destroy in");
		mpibuf_fn->buf_pool_destroy(enc->strm_pool);
		mpp_log("buf_pool_destroy out");
		enc->strm_pool = NULL;
	}
	mpp_free(enc);
	return ret;
}

MPP_RET mpp_enc_start(MppEnc ctx)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;

	enc_dbg_func("%p in\n", enc);

	down(&enc->enc_sem);
	// snprintf(name, sizeof(name) - 1, "mpp_%se_%d",
	//   strof_coding_type(enc->coding), getpid());
	enc->stop_flag = 0;
	up(&enc->enc_sem);
	enc_dbg_func("%p out\n", enc);

	return MPP_OK;
}

MPP_RET mpp_enc_stop(MppEnc ctx)
{
	MPP_RET ret = MPP_OK;
	MppEncImpl *enc = (MppEncImpl *) ctx;
	down(&enc->enc_sem);
	enc_dbg_func("%p in\n", enc);
	enc->stop_flag = 0;
	ret = enc->hw_run;
	enc_dbg_func("%p out\n", enc);
	up(&enc->enc_sem);
	return ret;

}

RK_S32 mpp_enc_check_pkt_pool(MppEnc ctx)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	RK_S32 num = 0;
	if (mpibuf_fn && mpibuf_fn->buf_pool_get_free_num)
		num =  mpibuf_fn->buf_pool_get_free_num(enc->strm_pool);
	return num;
}


MPP_RET mpp_enc_reset(MppEnc ctx)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;

	enc_dbg_func("%p in\n", enc);
	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return MPP_ERR_NULL_PTR;
	}

	return MPP_OK;
}

MPP_RET mpp_enc_oneframe(MppEnc ctx, MppFrame frame, MppPacket * packet)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return MPP_ERR_NULL_PTR;
	}

	enc_dbg_func("%p in\n", enc);
	//ret = mpp_enc_impl_oneframe(ctx, frame, packet);
	enc_dbg_func("%p out\n", enc);
	return ret;
}

MPP_RET mpp_enc_cfg_reg(MppEnc ctx, MppFrame frame)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return MPP_ERR_NULL_PTR;
	}

	enc_dbg_func("%p in\n", enc);
	down(&enc->enc_sem);
	if (enc->stop_flag) {
		up(&enc->enc_sem);
		return MPP_NOK;
	}
	ret = mpp_enc_impl_reg_cfg(ctx, frame);
	up(&enc->enc_sem);
	enc_dbg_func("%p out\n", enc);
	return ret;
}

MPP_RET mpp_enc_hw_start(MppEnc ctx, MppEnc jpeg_ctx)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return MPP_ERR_NULL_PTR;
	}

	enc_dbg_func("%p in\n", enc);
	down(&enc->enc_sem);
	if (enc->stop_flag) {
		up(&enc->enc_sem);
		return MPP_NOK;
	}
	ret = mpp_enc_impl_hw_start(ctx, jpeg_ctx);
	if (MPP_OK == ret)
		enc->hw_run = 1;
	up(&enc->enc_sem);
	enc_dbg_func("%p out\n", enc);
	return ret;
}


MPP_RET mpp_enc_int_process(MppEnc ctx, MppEnc jpeg_ctx, MppPacket * packet, MppPacket * jpeg_packet)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return MPP_ERR_NULL_PTR;
	}

	enc_dbg_func("%p in\n", enc);

	ret = mpp_enc_impl_int(ctx, jpeg_ctx, packet, jpeg_packet);
	down(&enc->enc_sem);
	enc->hw_run = 0;
	up(&enc->enc_sem);
	enc_dbg_func("%p out\n", enc);
	return ret;
}

void mpp_enc_proc_debug(void *seq_file, MppEnc ctx, RK_U32 chl_id)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return;
	}

	enc_dbg_func("%p in\n", enc);

	mpp_enc_impl_poc_debug_info(seq_file, ctx, chl_id);

	enc_dbg_func("%p out\n", enc);
	return;
}


MPP_RET mpp_enc_register_chl(MppEnc ctx, void *func, RK_S32 chan_id)
{

	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return MPP_ERR_NULL_PTR;
	}

	mpp_dev_chnl_register(enc->dev, func, chan_id);

	return ret;
}

MPP_RET mpp_enc_notify(MppEnc ctx, RK_U32 flag)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;

	enc_dbg_func("%p in flag %08x\n", enc, flag);

	enc_dbg_func("%p out\n", enc);
	return MPP_OK;
}

/*
 * preprocess config and rate-control config is common config then they will
 * be done in mpp_enc layer
 *
 * codec related config will be set in each hal component
 */
MPP_RET mpp_enc_control(MppEnc ctx, MpiCmd cmd, void *param)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == enc) {
		mpp_err_f("found NULL enc\n");
		return MPP_ERR_NULL_PTR;
	}

	if (NULL == param && cmd != MPP_ENC_SET_IDR_FRAME) {
		mpp_err_f("found NULL param enc %p cmd %x\n", enc, cmd);
		return MPP_ERR_NULL_PTR;
	}

	enc_dbg_ctrl("sending cmd %d param %p\n", cmd, param);
	switch (cmd) {
	case MPP_ENC_GET_CFG:{
			MppEncCfgImpl *p = (MppEncCfgImpl *) param;

			enc_dbg_ctrl("get all config\n");
			if (copy_to_user(&p->cfg, &enc->cfg, sizeof(enc->cfg)))
				ret = -EFAULT;
		}
		break;
	case MPP_ENC_GET_PREP_CFG:{
			enc_dbg_ctrl("get prep config\n");
			if (copy_to_user(param, &enc->cfg.prep, sizeof(enc->cfg.prep)))
				ret = -EFAULT;
		}
		break;
	case MPP_ENC_GET_RC_CFG:{
			enc_dbg_ctrl("get rc config\n");
			if (copy_to_user(param, &enc->cfg.rc, sizeof(enc->cfg.rc)))
				ret = -EFAULT;
		}
		break;
	case MPP_ENC_GET_CODEC_CFG:{
			enc_dbg_ctrl("get codec config\n");
			if (copy_to_user(param, &enc->cfg.codec, sizeof(enc->cfg.codec)))
				ret = -EFAULT;
		}
		break;
	case MPP_ENC_GET_HEADER_MODE:{
			enc_dbg_ctrl("get header mode\n");
			if (copy_to_user(param, &enc->hdr_mode, sizeof(enc->hdr_mode)))
				ret = -EFAULT;
		}
		break;
	default:
		down(&enc->enc_sem);
		mpp_enc_proc_cfg(enc, cmd, param);
		mpp_enc_hal_prepare(enc->enc_hal);
		mpp_enc_proc_rc_update(enc);
		up(&enc->enc_sem);
		enc_dbg_ctrl("sending cmd %d done\n", cmd);
		break;
	}

	return ret;
}
