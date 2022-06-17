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
	const char *smart = "smart";

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
	enc_hal_cfg.ref_buf_shared = cfg->ref_buf_shared;
	enc_hal_cfg.shared_buf = cfg->shared_buf;
	enc_hal_cfg.qpmap_en = cfg->qpmap_en;
	enc_hal_cfg.smart_en = cfg->smart_en;
	enc_hal_cfg.motion_static_switch_en = cfg->motion_static_switch_en;
	enc_hal_cfg.only_smartp = cfg->only_smartp;
	p->ring_buf_size = cfg->buf_size;
	p->max_strm_cnt = cfg->max_strm_cnt;
	p->motion_static_switch_en = cfg->motion_static_switch_en;
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

	rc_init(&p->rc_ctx, coding, cfg->smart_en ? &smart : NULL);

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

		ret = mpp_packet_init(&p->hdr_pkt, p->hdr_buf, size);
		if (ret)
			goto ERR_RET;
		mpp_packet_set_length(p->hdr_pkt, 0);
	}

	/* NOTE: setup configure coding for check */
	p->cfg.codec.coding = coding;
	//	p->cfg.plt_cfg.plt = &p->cfg.plt_data;
	if (mpp_enc_ref_cfg_init(&p->cfg.ref_cfg))
		goto  ERR_RET;

	if (mpp_enc_ref_cfg_copy(p->cfg.ref_cfg, mpp_enc_ref_default()))
		goto ERR_RET;

	if (mpp_enc_refs_set_cfg(p->refs, mpp_enc_ref_default()))
		goto ERR_RET;

	if (mpp_enc_refs_set_rc_igop(p->refs, p->cfg.rc.gop))
		goto ERR_RET;

	sema_init(&p->enc_sem, 1);
	p->stop_flag = 1;
	p->rb_userdata.free_cnt = MAX_USRDATA_CNT;

	if (!get_vsm_ops())
		p->ring_pool = mpp_calloc(ring_buf_pool, 1);
	p->online = cfg->online;
	p->shared_buf = cfg->shared_buf;
	p->qpmap_en = cfg->qpmap_en;
	p->chan_id = cfg->chan_id;
	p->ref_buf_shared = cfg->ref_buf_shared;
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

	if (enc->ring_pool) {
		if (!enc->shared_buf->stream_buf) {
			if (enc->ring_pool->buf)
				mpp_buffer_put(enc->ring_pool->buf);
		}
		MPP_FREE(enc->ring_pool);
	}
	mpp_enc_unref_osd_buf(&enc->cur_osd);

	if (enc->qpmap_en) {
		RK_U32 i;
		if (enc->mv_info)
			mpp_buffer_put(enc->mv_info);
		if (enc->qpmap)
			mpp_buffer_put(enc->qpmap);
		for (i = 0; i < 3; i++) {
			if (enc->mv_flag[i])
				mpp_free(enc->mv_flag[i]);
		}
	}

	MPP_FREE(enc->rc_cfg_info);
	enc->rc_cfg_size = 0;
	enc->rc_cfg_length = 0;
	up(&enc->enc_sem);
	if (enc->strm_pool) {
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
	enc->stop_flag = 1;
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
	MppEncCfgSet *cfg = &enc->cfg;
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
	mpp_enc_proc_rc_update(enc);
	enc->enc_status = ENC_STATUS_CFG_IN;
	if (enc->qpmap_en && !enc->mv_info) {
		RK_U32 mb_w = 0;
		RK_U32 mb_h = 0;
		RK_U32 i;
		if (cfg->codec.coding == MPP_VIDEO_CodingAVC) {
			mb_w = MPP_ALIGN(cfg->prep.max_width, 64) / 16;
			mb_h = MPP_ALIGN(cfg->prep.max_height, 64) / 16;
		} else {
			mb_w = MPP_ALIGN(cfg->prep.max_width, 32) / 16;
			mb_h = MPP_ALIGN(cfg->prep.max_height, 32) / 16;
		}
		if (!enc->mv_info)
			mpp_buffer_get(NULL, &enc->mv_info, mb_w * mb_h * 4);
		if (!enc->qpmap)
			mpp_buffer_get(NULL, &enc->qpmap, mb_w * mb_h * 4);
		for (i = 0; i < 3; i++) {
			enc->mv_flag[i] = (RK_U8 *)mpp_calloc(RK_U8, mb_w * mb_h);
			if (!enc->mv_flag[i])
				mpp_log("alloc mv_flag failed!\n");
		}
	}
	ret = mpp_enc_impl_reg_cfg(ctx, frame);
	enc->enc_status = ENC_STATUS_CFG_DONE;
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
	enc->enc_status = ENC_STATUS_START_IN;
	ret = mpp_enc_impl_hw_start(ctx, jpeg_ctx);
	enc->enc_status = ENC_STATUS_START_DONE;
	if (MPP_OK == ret)
		enc->hw_run = 1;
	up(&enc->enc_sem);
	enc_dbg_func("%p out\n", enc);
	return ret;
}


MPP_RET mpp_enc_int_process(MppEnc ctx, MppEnc jpeg_ctx, MppPacket * packet,
			    MppPacket * jpeg_packet)
{
	MppEncImpl *enc = (MppEncImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == enc) {
		mpp_err_f("found NULL input enc\n");
		return MPP_ERR_NULL_PTR;
	}

	enc_dbg_func("%p in\n", enc);
	enc->enc_status = ENC_STATUS_INT_IN;
	ret = mpp_enc_impl_int(ctx, jpeg_ctx, packet, jpeg_packet);
	enc->enc_status = ENC_STATUS_INT_DONE;
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
	case MPP_ENC_GET_CFG: {
		MppEncCfgImpl *p = (MppEncCfgImpl *) param;
		MppEncCfgSet *cfg = &p->cfg;

		enc_dbg_ctrl("get all config\n");
		memcpy(&p->cfg, &enc->cfg, sizeof(enc->cfg));
		if (cfg->prep.rotation == MPP_ENC_ROT_90 ||
		    cfg->prep.rotation == MPP_ENC_ROT_270) {
			MPP_SWAP(RK_S32, cfg->prep.width, cfg->prep.height);
			MPP_SWAP(RK_S32, cfg->prep.max_width, cfg->prep.max_height);
		}
	}
	break;
	case MPP_ENC_SET_PREP_CFG: {
		enc_dbg_ctrl("set prep config\n");
		memcpy(&enc->cfg.prep, param, sizeof(enc->cfg.prep));
	}
	break;
	case MPP_ENC_SET_CODEC_CFG: {
		enc_dbg_ctrl("set codec config\n");
		memcpy(&enc->cfg.codec, param, sizeof(enc->cfg.codec));
	} break;

	case MPP_ENC_GET_PREP_CFG: {
		enc_dbg_ctrl("get prep config\n");
		memcpy(param, &enc->cfg.prep, sizeof(enc->cfg.prep));
	}
	break;
	case MPP_ENC_GET_RC_CFG: {
		enc_dbg_ctrl("get rc config\n");
		memcpy(param, &enc->cfg.rc, sizeof(enc->cfg.rc));
	}
	break;
	case MPP_ENC_GET_CODEC_CFG: {
		enc_dbg_ctrl("get codec config\n");
		memcpy(param, &enc->cfg.codec, sizeof(enc->cfg.codec));
	}
	break;
	case MPP_ENC_GET_HEADER_MODE: {
		enc_dbg_ctrl("get header mode\n");
		memcpy(param, &enc->hdr_mode, sizeof(enc->hdr_mode));
	}
	break;
	case MPP_ENC_GET_REF_CFG: {
		enc_dbg_ctrl("get ref config\n");
		memcpy(param, &enc->cfg.ref_param, sizeof(enc->cfg.ref_param));
	}
	break;
	case MPP_ENC_GET_ROI_CFG: {
		enc_dbg_ctrl("get roi config\n");
		memcpy(param, &enc->cfg.roi, sizeof(enc->cfg.roi));
	} break;
	default:
		down(&enc->enc_sem);
		mpp_enc_proc_cfg(enc, cmd, param);
		mpp_enc_hal_prepare(enc->enc_hal);
		up(&enc->enc_sem);
		enc_dbg_ctrl("sending cmd %d done\n", cmd);
		break;
	}

	return ret;
}
