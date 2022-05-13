// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#define MODULE_TAG "hal_h264e_vepu540c"

#include <linux/string.h>
#include <linux/dma-buf.h>

#include "mpp_mem.h"
#include "mpp_maths.h"
#include "mpp_frame_impl.h"

#include "h264e_sps.h"
#include "h264e_pps.h"
#include "h264e_slice.h"

#include "hal_h264e_debug.h"
#include "hal_bufs.h"
#include "mpp_enc_hal.h"
#include "vepu541_common.h"
#include "vepu540c_common.h"

#include "hal_h264e_vepu540c_reg.h"
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
#include <soc/rockchip/rockchip_dvbm.h>
#endif

#define DUMP_REG 0
#define MAX_CORE_NUM 2

typedef struct vepu540c_h264e_reg_ctx_t {
	void *reg;
	RK_U32 used;
} Vepu540cH264eRegCtx;

const static RefType ref_type_map[2][2] = {
	/* ref_lt = 0	ref_lt = 1 */
	/* cur_lt = 0 */{ST_REF_TO_ST, ST_REF_TO_LT},
	/* cur_lt = 1 */{LT_REF_TO_ST, LT_REF_TO_LT},
};

typedef struct HalH264eVepu540cCtx_t {
	MppEncCfgSet *cfg;

	MppDev dev;
	RK_S32 frame_cnt;

	/* buffers management */
	HalBufs hw_recn;
	RK_S32 pixel_buf_fbc_hdr_size;
	RK_S32 pixel_buf_fbc_bdy_size;
	RK_S32 pixel_buf_fbc_bdy_offset;
	RK_S32 pixel_buf_size;
	RK_S32 thumb_buf_size;
	RK_S32 smera_size;
	RK_S32 max_buf_cnt;
	/* recn and ref buffer offset */
	RK_U32                  recn_ref_wrap;
	MppBuffer               recn_ref_buf;
	WrapBufInfo		wrap_infos;

	/* external line buffer over 4K */
	RK_S32 ext_line_buf_size;

	/* two-pass deflicker */
	MppBuffer  buf_pass1;

	/* syntax for input from enc_impl */
	RK_U32 updated;
	H264eSps *sps;
	H264ePps *pps;
	H264eSlice *slice;
	H264eFrmInfo *frms;
	H264eReorderInfo *reorder;
	H264eMarkingInfo *marking;
	H264ePrefixNal *prefix;

	/* syntax for output to enc_impl */
	EncRcTaskInfo hal_rc_cfg;

	RK_U32 mb_num;
	RK_U32 smear_cnt[5];

	/* roi */
	void *roi_data;

	/* osd */
	Vepu540cOsdCfg osd_cfg;

	/* register */
	HalVepu540cRegSet *regs_set;

	MppBuffer ext_line_buf;
	RK_S32	online;
	struct hal_shared_buf *shared_buf;
	RK_S32	qpmap_en;
	RK_S32	smart_en;
} HalH264eVepu540cCtx;

static RK_U32 dump_l1_reg = 0;
static RK_U32 dump_l2_reg = 0;

static RK_S32 h264_aq_tthd_default[16] = {
	0, 0, 0, 0,
	3, 3, 5, 5,
	8, 8, 8, 15,
	15, 20, 25, 25,
};

static RK_S32 h264_P_aq_step_default[16] = {
	-8, -7, -6, -5,
	-4, -3, -2, -1,
	0, 1, 2, 3,
	4, 5, 7, 8,
};

static RK_S32 h264_I_aq_step_default[16] = {
	-8, -7, -6, -5,
	-4, -3, -2, -1,
	0, 1, 3, 3,
	4, 5, 8, 8,
};

static MPP_RET hal_h264e_vepu540c_deinit(void *hal)
{
	HalH264eVepu540cCtx *p = (HalH264eVepu540cCtx *) hal;
	hal_h264e_dbg_func("enter %p\n", p);

	if (p->dev) {
		mpp_dev_deinit(p->dev);
		p->dev = NULL;
	}

	if (p->ext_line_buf) {
		mpp_buffer_put(p->ext_line_buf);
		p->ext_line_buf = NULL;
	}

	if (!p->shared_buf->dpb_bufs && p->hw_recn) {
		hal_bufs_deinit(p->hw_recn);
		p->hw_recn = NULL;
	}
	if (p->buf_pass1) {
		mpp_buffer_put(p->buf_pass1);
		p->buf_pass1 = NULL;
	}

	if (!p->shared_buf->recn_ref_buf && p->recn_ref_buf) {
		mpp_buffer_put(p->recn_ref_buf);
		p->recn_ref_buf = NULL;
	}

	MPP_FREE(p->regs_set);

	hal_h264e_dbg_func("leave %p\n", p);

	return MPP_OK;
}

static MPP_RET hal_h264e_vepu540c_init(void *hal, MppEncHalCfg *cfg)
{
	HalH264eVepu540cCtx *p = (HalH264eVepu540cCtx *) hal;
	MPP_RET ret = MPP_OK;
	hal_h264e_dbg_func("enter %p\n", p);

	p->cfg = cfg->cfg;
	p->online = cfg->online;
	p->recn_ref_wrap = cfg->ref_buf_shared;
	p->shared_buf = cfg->shared_buf;
	p->qpmap_en = cfg->qpmap_en;
	p->smart_en = cfg->smart_en;
	/* update output to MppEnc */
	cfg->type = VPU_CLIENT_RKVENC;
	ret = mpp_dev_init(&cfg->dev, cfg->type);
	if (ret) {
		mpp_err_f("mpp_dev_init failed. ret: %d\n", ret);
		goto DONE;
	}
	p->dev = cfg->dev;

	/*ret = hal_bufs_init(&p->hw_recn);
	if (ret) {
		mpp_err_f("init vepu buffer failed ret: %d\n", ret);
		goto DONE;
	}*/
	//
	p->osd_cfg.dev = p->dev;

	{                   /* setup default hardware config */
		MppEncHwCfg *hw = &cfg->cfg->hw;

		hw->qp_delta_row_i = 1;
		hw->qp_delta_row = 2;

		memcpy(hw->aq_thrd_i, h264_aq_tthd_default,
		       sizeof(hw->aq_thrd_i));
		memcpy(hw->aq_thrd_p, h264_aq_tthd_default,
		       sizeof(hw->aq_thrd_p));
		memcpy(hw->aq_step_i, h264_I_aq_step_default,
		       sizeof(hw->aq_step_i));
		memcpy(hw->aq_step_p, h264_P_aq_step_default,
		       sizeof(hw->aq_step_p));
	}

	p->regs_set = mpp_calloc(HalVepu540cRegSet, 1);

	if (!p->regs_set) {
		mpp_err("HalVepu540cRegSet alloc fail");
		return MPP_ERR_MALLOC;
	}
DONE:
	if (ret)
		hal_h264e_vepu540c_deinit(hal);

	hal_h264e_dbg_func("leave %p\n", p);
	return ret;
}

static void get_wrap_buf(HalH264eVepu540cCtx *ctx, RK_S32 max_lt_cnt)
{
	MppEncCfgSet *cfg = ctx->cfg;
	MppEncPrepCfg *prep = &cfg->prep;
	RK_S32 alignment = 64;
	RK_S32 aligned_w = MPP_ALIGN(prep->max_width, alignment);
	RK_S32 aligned_h = MPP_ALIGN(prep->max_height, alignment);
	RK_U32 total_wrap_size;
	WrapInfo *body = &ctx->wrap_infos.body;
	WrapInfo *hdr = &ctx->wrap_infos.hdr;

	body->size = REF_BODY_SIZE(aligned_w, aligned_h);
	body->total_size = body->size + REF_WRAP_BODY_EXT_SIZE(aligned_w);
	hdr->size = REF_HEADER_SIZE(aligned_w, aligned_h);
	hdr->total_size = hdr->size + REF_WRAP_HEADER_EXT_SIZE(aligned_w);
	total_wrap_size = body->total_size + hdr->total_size;

	if (max_lt_cnt > 0) {
		WrapInfo *body_lt = &ctx->wrap_infos.body_lt;
		WrapInfo *hdr_lt = &ctx->wrap_infos.hdr_lt;

		body_lt->size = body->size;
		body_lt->total_size = body->total_size;

		hdr_lt->size = hdr->size;
		hdr_lt->total_size = hdr->total_size;
#ifdef ONLY_SMART_P
		body_lt->total_size = body->size;
		hdr_lt->total_size =  hdr->size;
#endif

		total_wrap_size += (body_lt->total_size + hdr_lt->total_size);
	}

	/*
	 * bottom                      top
	 * ┌──────────────────────────►
	 * │
	 * ├──────┬───────┬──────┬─────┐
	 * │hdr_lt│  hdr  │bdy_lt│ bdy │
	 * └──────┴───────┴──────┴─────┘
	 */

	if (max_lt_cnt > 0) {
		WrapInfo *body_lt = &ctx->wrap_infos.body_lt;
		WrapInfo *hdr_lt = &ctx->wrap_infos.hdr_lt;

		hdr_lt->bottom = 0;
		hdr_lt->top = hdr_lt->bottom + hdr_lt->total_size;
		hdr_lt->cur_off = hdr_lt->bottom;

		hdr->bottom = hdr_lt->top;
		hdr->top = hdr->bottom + hdr->total_size;
		hdr->cur_off = hdr->bottom;

		body_lt->bottom = hdr->top;
		body_lt->top = body_lt->bottom + body_lt->total_size;
		body_lt->cur_off = body_lt->bottom;

		body->bottom = body_lt->top;
		body->top = body->bottom + body->total_size;
		body->cur_off = body->bottom;
	} else {
		hdr->bottom = 0;
		hdr->top = hdr->bottom + hdr->total_size;
		hdr->cur_off = hdr->bottom;

		body->bottom = hdr->top;
		body->top = body->bottom + body->total_size;
		body->cur_off = body->bottom;
	}

	if (!ctx->shared_buf->recn_ref_buf) {
		if (ctx->recn_ref_buf)
			mpp_buffer_put(ctx->recn_ref_buf);
		mpp_buffer_get(NULL, &ctx->recn_ref_buf, total_wrap_size);
	} else
		ctx->recn_ref_buf = ctx->shared_buf->recn_ref_buf;
}

static void setup_recn_refr_wrap(HalH264eVepu540cCtx *ctx, HalVepu540cRegSet *regs)
{
	MppDev dev = ctx->dev;
	H264eFrmInfo *frms = ctx->frms;
	RK_U32 recn_ref_wrap = ctx->recn_ref_wrap;
	RK_U32 ref_iova;
	RK_U32 cur_is_lt = frms->curr_is_lt;
	RK_U32 refr_is_lt = frms->refr_is_lt;
	RK_U32 cur_is_non_ref = frms->curr_is_non_ref;
	RK_U32 rfpw_h_off;
	RK_U32 rfpw_b_off;
	RK_U32 rfpr_h_off;
	RK_U32 rfpr_b_off;
	RK_U32 rfp_h_bot;
	RK_U32 rfp_b_bot;
	RK_U32 rfp_h_top;
	RK_U32 rfp_b_top;
	WrapInfo *bdy_lt = &ctx->wrap_infos.body_lt;
	WrapInfo *hdr_lt = &ctx->wrap_infos.hdr_lt;
	WrapInfo *bdy = &ctx->wrap_infos.body;
	WrapInfo *hdr = &ctx->wrap_infos.hdr;

	if (recn_ref_wrap)
		ref_iova = mpp_dev_get_iova_address(dev, ctx->recn_ref_buf, 163);

	if (frms->curr_is_idr && frms->curr_idx == frms->refr_idx) {
		hal_h264e_dbg_wrap("cur is idr  lt %d\n", cur_is_lt);
		if (cur_is_lt) {
			rfpr_h_off = hdr_lt->cur_off;
			rfpr_b_off = bdy_lt->cur_off;
			rfpw_h_off = hdr_lt->cur_off;
			rfpw_b_off = bdy_lt->cur_off;

			rfp_h_bot = hdr->bottom < hdr_lt->bottom ? hdr->bottom : hdr_lt->bottom;
			rfp_h_top = hdr->top > hdr_lt->top ? hdr->top : hdr_lt->top;
			rfp_b_bot = bdy->bottom < bdy_lt->bottom ? bdy->bottom : bdy_lt->bottom;
			rfp_b_top = bdy->top > bdy_lt->top ? bdy->top : bdy_lt->top;
		} else {
			rfpr_h_off = hdr->cur_off;
			rfpr_b_off = bdy->cur_off;
			rfpw_h_off = hdr->cur_off;
			rfpw_b_off = bdy->cur_off;

			rfp_h_bot = hdr->bottom;
			rfp_h_top = hdr->top;
			rfp_b_bot = bdy->bottom;
			rfp_b_top = bdy->top;
		}
	} else {
		RefType type = ref_type_map[cur_is_lt][refr_is_lt];

		hal_h264e_dbg_wrap("ref type %d\n", type);
		switch (type) {
		case ST_REF_TO_ST: {
			/* refr */
			rfpr_h_off = hdr->pre_off;
			rfpr_b_off = bdy->pre_off;
			/* recn */
			rfpw_h_off = hdr->cur_off;
			rfpw_b_off = bdy->cur_off;

			rfp_h_bot = hdr->bottom;
			rfp_h_top = hdr->top;
			rfp_b_bot = bdy->bottom;
			rfp_b_top = bdy->top;
		} break;
		case ST_REF_TO_LT: {
			/* refr */
			rfpr_h_off = hdr_lt->cur_off;
			rfpr_b_off = bdy_lt->cur_off;
			/* recn */
			hdr->cur_off = hdr->bottom;
			bdy->cur_off = bdy->bottom;
			rfpw_h_off = hdr->cur_off;
			rfpw_b_off = bdy->cur_off;

			rfp_h_bot = hdr->bottom < hdr_lt->bottom ? hdr->bottom : hdr_lt->bottom;
			rfp_h_top = hdr->top > hdr_lt->top ? hdr->top : hdr_lt->top;
			rfp_b_bot = bdy->bottom < bdy_lt->bottom ? bdy->bottom : bdy_lt->bottom;
			rfp_b_top = bdy->top > bdy_lt->top ? bdy->top : bdy_lt->top;
		} break;
		case LT_REF_TO_ST: {
			/* not support this case */
			mpp_err("WARNING: not support lt ref to st when buf is wrap");
		} break;
		case LT_REF_TO_LT: {
#ifndef ONLY_SMART_P
			WrapInfo tmp;
			/* the case is hard to implement */
			rfpr_h_off = hdr_lt->cur_off;
			rfpr_b_off = bdy_lt->cur_off;

			hdr->cur_off = hdr->bottom;
			bdy->cur_off = bdy->bottom;
			rfpw_h_off = hdr->cur_off;
			rfpw_b_off = bdy->cur_off;

			rfp_h_bot = hdr->bottom < hdr_lt->bottom ? hdr->bottom : hdr_lt->bottom;
			rfp_h_top = hdr->top > hdr_lt->top ? hdr->top : hdr_lt->top;
			rfp_b_bot = bdy->bottom < bdy_lt->bottom ? bdy->bottom : bdy_lt->bottom;
			rfp_b_top = bdy->top > bdy_lt->top ? bdy->top : bdy_lt->top;

			/* swap */
			memcpy(&tmp, hdr, sizeof(WrapInfo));
			memcpy(hdr, hdr_lt, sizeof(WrapInfo));
			memcpy(hdr_lt, &tmp, sizeof(WrapInfo));

			memcpy(&tmp, bdy, sizeof(WrapInfo));
			memcpy(bdy, bdy_lt, sizeof(WrapInfo));
			memcpy(bdy_lt, &tmp, sizeof(WrapInfo));
#else
			mpp_err("WARNING: not support lt ref to lt when buf is wrap");
#endif
		} break;
		default: {
		} break;
		}
	}


	regs->reg_base.rfpw_h_addr = ref_iova + rfpw_h_off;
	regs->reg_base.rfpw_b_addr = ref_iova + rfpw_b_off;

	regs->reg_base.rfpr_h_addr = ref_iova + rfpr_h_off;
	regs->reg_base.rfpr_b_addr = ref_iova + rfpr_b_off;

	regs->reg_base.rfpt_h_addr = ref_iova + rfp_h_top;
	regs->reg_base.rfpb_h_addr = ref_iova + rfp_h_bot;
	regs->reg_base.rfpt_b_addr = ref_iova + rfp_b_top;
	regs->reg_base.rfpb_b_addr = ref_iova + rfp_b_bot;
	regs->reg_base.enc_pic.cur_frm_ref = !cur_is_non_ref;

	if (recn_ref_wrap) {
		RK_U32 cur_hdr_off;
		RK_U32 cur_bdy_off;

		hal_h264e_dbg_wrap("cur_is_ref %d\n", !cur_is_non_ref);
		hal_h264e_dbg_wrap("hdr[size %d top %d bot %d cur %d pre %d]\n",
				   hdr->size, hdr->top, hdr->bottom, hdr->cur_off, hdr->pre_off);
		hal_h264e_dbg_wrap("bdy [size %d top %d bot %d cur %d pre %d]\n",
				   bdy->size, bdy->top, bdy->bottom, bdy->cur_off, bdy->pre_off);
		if (!cur_is_non_ref) {
			hdr->pre_off = hdr->cur_off;
			cur_hdr_off = hdr->pre_off + hdr->size;
			cur_hdr_off = cur_hdr_off >= hdr->top ?
				      (cur_hdr_off - hdr->top + hdr->bottom) : cur_hdr_off;
			hdr->cur_off = cur_hdr_off;

			bdy->pre_off = bdy->cur_off;
			cur_bdy_off = bdy->pre_off + bdy->size;
			cur_bdy_off = cur_bdy_off >= bdy->top ?
				      (cur_bdy_off - bdy->top + bdy->bottom) : cur_bdy_off;
			bdy->cur_off = cur_bdy_off;
		}
	}
}

static void setup_hal_bufs(HalH264eVepu540cCtx *ctx)
{
	MppEncCfgSet *cfg = ctx->cfg;
	MppEncPrepCfg *prep = &cfg->prep;
	RK_S32 alignment = 64;
	RK_S32 aligned_w = MPP_ALIGN(prep->max_width, alignment);
	RK_S32 aligned_h = MPP_ALIGN(prep->max_height, alignment);
	RK_S32 pixel_buf_fbc_hdr_size = MPP_ALIGN(aligned_w * aligned_h / 64, SZ_8K);
	RK_S32 pixel_buf_fbc_bdy_size = aligned_w * aligned_h * 3 / 2;
	RK_S32 pixel_buf_size = pixel_buf_fbc_hdr_size + pixel_buf_fbc_bdy_size;
	RK_S32 thumb_buf_size = MPP_ALIGN(aligned_w / 64 * aligned_h / 64 * 256, SZ_8K);
	RK_S32 old_max_cnt = ctx->max_buf_cnt;
	RK_S32 new_max_cnt = 2;
	RK_U32 smera_size = 0;
	RK_U32 smera_r_size = 0;
	MppEncRefCfg ref_cfg = cfg->ref_cfg;
	RK_S32 max_lt_cnt;

	if (ref_cfg) {
		MppEncCpbInfo *info = mpp_enc_ref_cfg_get_cpb_info(ref_cfg);
		if (new_max_cnt < MPP_MAX(new_max_cnt, info->dpb_size + 1))
			new_max_cnt = MPP_MAX(new_max_cnt, info->dpb_size + 1);
		max_lt_cnt = info->max_lt_cnt;
	}

	if (aligned_w > SZ_4K) {
		/* 480 bytes for each ctu above 3072 */
		RK_S32 ext_line_buf_size = (aligned_w - 3 * SZ_1K) / 64 * 480;

		if (ext_line_buf_size != ctx->ext_line_buf_size) {
			mpp_buffer_put(ctx->ext_line_buf);
			ctx->ext_line_buf = NULL;
		}

		if (NULL == ctx->ext_line_buf)
			mpp_buffer_get(NULL, &ctx->ext_line_buf,
				       ext_line_buf_size);

		ctx->ext_line_buf_size = ext_line_buf_size;
	} else {
		if (ctx->ext_line_buf) {
			mpp_buffer_put(ctx->ext_line_buf);
			ctx->ext_line_buf = NULL;
		}
		ctx->ext_line_buf_size = 0;
	}

	if (1) {
		smera_size = MPP_ALIGN(aligned_w, 1024) / 1024 * MPP_ALIGN(aligned_h, 16) / 16 * 16;
		smera_r_size = MPP_ALIGN(aligned_h, 1024) / 1024 * MPP_ALIGN(aligned_w, 16) / 16 * 16;
	} else {
		smera_size = MPP_ALIGN(aligned_w, 256) / 256 * MPP_ALIGN(aligned_h, 32) / 32;
		smera_r_size = MPP_ALIGN(aligned_h, 256) / 256 * MPP_ALIGN(aligned_w, 32) / 32;;
	}
	smera_size = MPP_MAX(smera_size, smera_r_size);
	if ((ctx->pixel_buf_fbc_hdr_size != pixel_buf_fbc_hdr_size) ||
	    (ctx->pixel_buf_fbc_bdy_size != pixel_buf_fbc_bdy_size) ||
	    (ctx->pixel_buf_size != pixel_buf_size) ||
	    (ctx->thumb_buf_size != thumb_buf_size) ||
	    (ctx->smera_size != smera_size) ||
	    (new_max_cnt > old_max_cnt)) {

		hal_h264e_dbg_detail("frame size %d -> %d max count %d -> %d\n",
				     ctx->pixel_buf_size, pixel_buf_size,
				     old_max_cnt, new_max_cnt);

		new_max_cnt = MPP_MAX(new_max_cnt, old_max_cnt);

		ctx->pixel_buf_fbc_hdr_size = pixel_buf_fbc_hdr_size;
		ctx->pixel_buf_fbc_bdy_size = pixel_buf_fbc_bdy_size;

		if (!ctx->shared_buf->dpb_bufs)
			hal_bufs_init(&ctx->hw_recn);

		if (ctx->recn_ref_wrap) {
			size_t sizes[4] = {thumb_buf_size, 0, smera_size, 0};

			if (!ctx->shared_buf->dpb_bufs)
				hal_bufs_setup(ctx->hw_recn, new_max_cnt, MPP_ARRAY_ELEMS(sizes), sizes);
			get_wrap_buf(ctx, max_lt_cnt);
		} else {
			size_t sizes[4] = {thumb_buf_size, 0, smera_size, pixel_buf_size};

			if (!ctx->shared_buf->dpb_bufs)
				hal_bufs_setup(ctx->hw_recn, new_max_cnt, MPP_ARRAY_ELEMS(sizes), sizes);
			ctx->pixel_buf_fbc_bdy_offset = pixel_buf_fbc_hdr_size;
			ctx->pixel_buf_size = pixel_buf_size;
		}

		if (ctx->shared_buf->dpb_bufs)
			ctx->hw_recn = ctx->shared_buf->dpb_bufs;

		ctx->pixel_buf_size = pixel_buf_size;
		ctx->thumb_buf_size = thumb_buf_size;
		ctx->smera_size = smera_size;
		ctx->max_buf_cnt = new_max_cnt;
	}
}

static MPP_RET hal_h264e_vepu540c_prepare(void *hal)
{
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;
	MppEncPrepCfg *prep = &ctx->cfg->prep;

	hal_h264e_dbg_func("enter %p\n", hal);

	if (prep->
	    change & (MPP_ENC_PREP_CFG_CHANGE_INPUT |
		      MPP_ENC_PREP_CFG_CHANGE_FORMAT)) {
		//RK_S32 i;

		// pre-alloc required buffers to reduce first frame delay
		/*setup_hal_bufs(ctx);
		for (i = 0; i < ctx->max_buf_cnt; i++)
			hal_bufs_get_buf(ctx->hw_recn, i);*/

		prep->change = 0;
	}

	hal_h264e_dbg_func("leave %p\n", hal);

	return MPP_OK;
}

static RK_U32 update_vepu540c_syntax(HalH264eVepu540cCtx *ctx,
				     MppSyntax *syntax)
{
	H264eSyntaxDesc *desc = syntax->data;
	RK_S32 syn_num = syntax->number;
	RK_U32 updated = 0;
	RK_S32 i;

	for (i = 0; i < syn_num; i++, desc++) {
		switch (desc->type) {
		case H264E_SYN_CFG: {
			hal_h264e_dbg_detail("update cfg");
			ctx->cfg = desc->p;
		}
		break;
		case H264E_SYN_SPS: {
			hal_h264e_dbg_detail("update sps");
			ctx->sps = desc->p;
		}
		break;
		case H264E_SYN_PPS: {
			hal_h264e_dbg_detail("update pps");
			ctx->pps = desc->p;
		}
		break;
		case H264E_SYN_SLICE: {
			hal_h264e_dbg_detail("update slice");
			ctx->slice = desc->p;
		}
		break;
		case H264E_SYN_FRAME: {
			hal_h264e_dbg_detail("update frames");
			ctx->frms = desc->p;
		}
		break;
		case H264E_SYN_PREFIX: {
			hal_h264e_dbg_detail("update prefix nal");
			ctx->prefix = desc->p;
		}
		break;
		default: {
			mpp_log_f("invalid syntax type %d\n",
				  desc->type);
		}
		break;
		}

		updated |= SYN_TYPE_FLAG(desc->type);
	}

	return updated;
}

static MPP_RET hal_h264e_vepu540c_get_task(void *hal, HalEncTask *task)
{
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;
	RK_U32 updated = update_vepu540c_syntax(ctx, &task->syntax);
	//    EncFrmStatus *frm_status = &task->rc_task->frm;
	hal_h264e_dbg_func("enter %p\n", hal);

	if (updated & SYN_TYPE_FLAG(H264E_SYN_CFG))
		setup_hal_bufs(ctx);

	ctx->osd_cfg.reg_base = &ctx->regs_set->reg_osd_cfg.osd_comb_cfg;

	ctx->roi_data = mpp_frame_get_roi(task->frame);
	ctx->osd_cfg.osd_data3 = mpp_frame_get_osd(task->frame);

	hal_h264e_dbg_func("leave %p\n", hal);

	return MPP_OK;
}

static void setup_vepu540c_normal(HalVepu540cRegSet *regs)
{
	hal_h264e_dbg_func("enter\n");
	/* reg000 VERSION is read only */

	/* reg001 ENC_STRT */
	regs->reg_ctl.enc_strt.lkt_num = 0;
	regs->reg_ctl.enc_strt.vepu_cmd = 1;

	regs->reg_ctl.func_en.cke = 1;
	regs->reg_ctl.func_en.resetn_hw_en = 1;
	regs->reg_ctl.func_en.enc_done_tmvp_en = 1;

	/* reg002 ENC_CLR */
	regs->reg_ctl.enc_clr.safe_clr = 0;
	regs->reg_ctl.enc_clr.force_clr = 0;

	/* reg003 LKT_ADDR */
	// regs->reg_ctl.lkt_addr           = 0;

	/* reg004 INT_EN */
	regs->reg_ctl.int_en.enc_done_en = 1;
	regs->reg_ctl.int_en.lkt_node_done_en = 1;
	regs->reg_ctl.int_en.sclr_done_en = 1;
	regs->reg_ctl.int_en.vslc_done_en = 1;
	regs->reg_ctl.int_en.vbsf_oflw_en = 1;
	regs->reg_ctl.int_en.vbuf_lens_en = 1;
	regs->reg_ctl.int_en.enc_err_en = 1;
	regs->reg_ctl.int_en.dvbm_fcfg_en = 1;
	regs->reg_ctl.int_en.wdg_en = 1;
	regs->reg_ctl.int_en.lkt_err_int_en = 1;
	regs->reg_ctl.int_en.lkt_err_stop_en = 1;
	regs->reg_ctl.int_en.lkt_force_stop_en = 1;
	regs->reg_ctl.int_en.jslc_done_en = 1;
	regs->reg_ctl.int_en.jbsf_oflw_en = 1;
	regs->reg_ctl.int_en.jbuf_lens_en = 1;
	regs->reg_ctl.int_en.dvbm_dcnt_en = 1;

	/* reg005 INT_MSK */
	regs->reg_ctl.int_msk.enc_done_msk = 0;
	regs->reg_ctl.int_msk.lkt_node_done_msk = 0;
	regs->reg_ctl.int_msk.sclr_done_msk = 0;
	regs->reg_ctl.int_msk.vslc_done_msk = 0;
	regs->reg_ctl.int_msk.vbsf_oflw_msk = 0;
	regs->reg_ctl.int_msk.vbuf_lens_msk = 0;
	regs->reg_ctl.int_msk.enc_err_msk = 0;
	regs->reg_ctl.int_msk.dvbm_fcfg_msk = 0;
	regs->reg_ctl.int_msk.wdg_msk = 0;
	regs->reg_ctl.int_msk.lkt_err_int_msk = 0;
	regs->reg_ctl.int_msk.lkt_err_stop_msk = 0;
	regs->reg_ctl.int_msk.lkt_force_stop_msk = 0;
	regs->reg_ctl.int_msk.jslc_done_msk = 0;
	regs->reg_ctl.int_msk.jbsf_oflw_msk = 0;
	regs->reg_ctl.int_msk.jbuf_lens_msk = 0;
	regs->reg_ctl.int_msk.dvbm_dcnt_msk = 0;

	/* reg006 INT_CLR is not set */
	/* reg007 INT_STA is read only */
	/* reg008 ~ reg0011 gap */
	regs->reg_ctl.enc_wdg.vs_load_thd = 0xffff;
	regs->reg_ctl.enc_wdg.rfp_load_thd = 0; //xff;

	/* reg015 DTRNS_MAP */
	regs->reg_ctl.dtrns_map.jpeg_bus_edin = 0;
	regs->reg_ctl.dtrns_map.src_bus_edin = 0;
	regs->reg_ctl.dtrns_map.meiw_bus_edin = 0;
	regs->reg_ctl.dtrns_map.bsw_bus_edin = 7;
	regs->reg_ctl.dtrns_map.lktr_bus_edin = 0;
	regs->reg_ctl.dtrns_map.roir_bus_edin = 0;
	regs->reg_ctl.dtrns_map.lktw_bus_edin = 0;
	regs->reg_ctl.dtrns_map.rec_nfbc_bus_edin = 0;

	regs->reg_ctl.dtrns_cfg.axi_brsp_cke = 0;
	/* enable rdo clk gating */
	{
		RK_U32 *rdo_ckg = (RK_U32*)&regs->reg_ctl.rdo_ckg;

		*rdo_ckg = 0xffffffff;
	}
	hal_h264e_dbg_func("leave\n");
}

static MPP_RET setup_vepu540c_prep(HalVepu540cRegSet *regs,
				   MppEncPrepCfg *prep)
{
	VepuFmtCfg cfg;
	MppFrameFormat fmt = prep->format;
	MPP_RET ret = vepu541_set_fmt(&cfg, fmt);
	RK_U32 hw_fmt = cfg.format;
	RK_S32 y_stride;
	RK_S32 c_stride;

	hal_h264e_dbg_func("enter\n");

	/* do nothing when color format is not supported */
	if (ret)
		return ret;

	regs->reg_base.enc_rsl.pic_wd8_m1 = MPP_ALIGN(prep->width, 16) / 8 - 1;
	regs->reg_base.src_fill.pic_wfill =
		MPP_ALIGN(prep->width, 16) - prep->width;
	regs->reg_base.enc_rsl.pic_hd8_m1 = MPP_ALIGN(prep->height, 16) / 8 - 1;
	regs->reg_base.src_fill.pic_hfill =
		MPP_ALIGN(prep->height, 16) - prep->height;

	regs->reg_ctl.dtrns_map.src_bus_edin = cfg.src_endian;

	regs->reg_base.src_fmt.src_cfmt = hw_fmt;
	regs->reg_base.src_fmt.alpha_swap = cfg.alpha_swap;
	regs->reg_base.src_fmt.rbuv_swap = cfg.rbuv_swap;
	//    regs->reg_base.src_fmt.src_range  = cfg.src_range;
	regs->reg_base.src_fmt.out_fmt = 1;

	if (MPP_FRAME_FMT_IS_FBC(fmt))
		y_stride = MPP_ALIGN(prep->width, 16);

	else if (prep->hor_stride)
		y_stride = prep->hor_stride;

	else {
		if (hw_fmt == VEPU541_FMT_BGRA8888 )
			y_stride = prep->width * 4;
		else if (hw_fmt == VEPU541_FMT_BGR888 )
			y_stride = prep->width * 3;
		else if (hw_fmt == VEPU541_FMT_BGR565 ||
			 hw_fmt == VEPU541_FMT_YUYV422 ||
			 hw_fmt == VEPU541_FMT_UYVY422)
			y_stride = prep->width * 2;
		else
			y_stride = prep->width;
	}

	switch (hw_fmt) {
	case VEPU540C_FMT_YUV444SP : {
		c_stride = y_stride * 2;
	} break;
	case VEPU541_FMT_YUV422SP :
	case VEPU541_FMT_YUV420SP :
	case VEPU540C_FMT_YUV444P : {
		c_stride = y_stride;
	} break;
	default : {
		c_stride = y_stride / 2;
	} break;
	}

	if (hw_fmt < VEPU541_FMT_ARGB1555) {
		regs->reg_base.src_udfy.csc_wgt_b2y = 29;
		regs->reg_base.src_udfy.csc_wgt_g2y = 150;
		regs->reg_base.src_udfy.csc_wgt_r2y = 77;

		regs->reg_base.src_udfu.csc_wgt_b2u = 128;
		regs->reg_base.src_udfu.csc_wgt_g2u = -85;
		regs->reg_base.src_udfu.csc_wgt_r2u = -43;

		regs->reg_base.src_udfv.csc_wgt_b2v = -21;
		regs->reg_base.src_udfv.csc_wgt_g2v = -107;
		regs->reg_base.src_udfv.csc_wgt_r2v = 128;

		regs->reg_base.src_udfo.csc_ofst_y = 0;
		regs->reg_base.src_udfo.csc_ofst_u = 128;
		regs->reg_base.src_udfo.csc_ofst_v = 128;
	} else {
		regs->reg_base.src_udfy.csc_wgt_b2y = cfg.weight[0];
		regs->reg_base.src_udfy.csc_wgt_g2y = cfg.weight[1];
		regs->reg_base.src_udfy.csc_wgt_r2y = cfg.weight[2];

		regs->reg_base.src_udfu.csc_wgt_b2u = cfg.weight[3];
		regs->reg_base.src_udfu.csc_wgt_g2u = cfg.weight[4];
		regs->reg_base.src_udfu.csc_wgt_r2u = cfg.weight[5];

		regs->reg_base.src_udfv.csc_wgt_b2v = cfg.weight[6];
		regs->reg_base.src_udfv.csc_wgt_g2v = cfg.weight[7];
		regs->reg_base.src_udfv.csc_wgt_r2v = cfg.weight[8];

		regs->reg_base.src_udfo.csc_ofst_y = cfg.offset[0];
		regs->reg_base.src_udfo.csc_ofst_u = cfg.offset[1];
		regs->reg_base.src_udfo.csc_ofst_v = cfg.offset[2];
	}

	regs->reg_base.src_strd0.src_strd0 = y_stride;
	regs->reg_base.src_strd1.src_strd1 = c_stride;

	regs->reg_base.src_proc.src_mirr = prep->mirroring > 0;
	regs->reg_base.src_proc.src_rot = prep->rotation;
	//  regs->reg_base.src_proc.txa_en     = 0;

	regs->reg_base.sli_cfg.mv_v_lmt_thd = 0;
	regs->reg_base.sli_cfg.mv_v_lmt_en = 0;

	regs->reg_base.pic_ofst.pic_ofst_y = 0;
	regs->reg_base.pic_ofst.pic_ofst_x = 0;

	hal_h264e_dbg_func("leave\n");

	return ret;
}

static void setup_vepu540c_codec(HalVepu540cRegSet *regs, H264eSps *sps,
				 H264ePps *pps, H264eSlice *slice)
{
	hal_h264e_dbg_func("enter\n");

	regs->reg_base.enc_pic.enc_stnd = 0;
	regs->reg_base.enc_pic.cur_frm_ref = slice->nal_reference_idc > 0;
	regs->reg_base.enc_pic.bs_scp = 1;
	//regs->reg013.lamb_mod_sel   = (slice->slice_type == H264_I_SLICE) ? 0 : 1;
	//regs->reg013.atr_thd_sel    = 0;
	// regs->reg_ctl.lkt_node_cfg.node_int       = 0;

	regs->reg_base.synt_nal.nal_ref_idc = slice->nal_reference_idc;
	regs->reg_base.synt_nal.nal_unit_type = slice->nalu_type;

	regs->reg_base.synt_sps.max_fnum = sps->log2_max_frame_num_minus4;
	regs->reg_base.synt_sps.drct_8x8 = sps->direct8x8_inference;
	regs->reg_base.synt_sps.mpoc_lm4 = sps->log2_max_poc_lsb_minus4;

	regs->reg_base.synt_pps.etpy_mode = pps->entropy_coding_mode;
	regs->reg_base.synt_pps.trns_8x8 = pps->transform_8x8_mode;
	regs->reg_base.synt_pps.csip_flag = pps->constrained_intra_pred;
	regs->reg_base.synt_pps.num_ref0_idx =
		pps->num_ref_idx_l0_default_active - 1;
	regs->reg_base.synt_pps.num_ref1_idx =
		pps->num_ref_idx_l1_default_active - 1;
	regs->reg_base.synt_pps.pic_init_qp = pps->pic_init_qp;
	regs->reg_base.synt_pps.cb_ofst = pps->chroma_qp_index_offset;
	regs->reg_base.synt_pps.cr_ofst = pps->second_chroma_qp_index_offset;
	//    regs->reg_base.synt_pps.wght_pred      = pps->weighted_pred;
	regs->reg_base.synt_pps.dbf_cp_flg = pps->deblocking_filter_control;

	regs->reg_base.synt_sli0.sli_type =
		(slice->slice_type == H264_I_SLICE) ? (2) : (0);
	regs->reg_base.synt_sli0.pps_id = slice->pic_parameter_set_id;
	regs->reg_base.synt_sli0.drct_smvp = 0;
	regs->reg_base.synt_sli0.num_ref_ovrd = slice->num_ref_idx_override;
	regs->reg_base.synt_sli0.cbc_init_idc = slice->cabac_init_idc;
	regs->reg_base.synt_sli0.frm_num = slice->frame_num;

	regs->reg_base.synt_sli1.idr_pid =
		(slice->slice_type ==
		 H264_I_SLICE) ? slice->idr_pic_id : (RK_U32) (-1);
	regs->reg_base.synt_sli1.poc_lsb = slice->pic_order_cnt_lsb;

	regs->reg_base.synt_sli2.dis_dblk_idc =
		slice->disable_deblocking_filter_idc;
	regs->reg_base.synt_sli2.sli_alph_ofst =
		slice->slice_alpha_c0_offset_div2;

	h264e_reorder_rd_rewind(slice->reorder);
	{                   /* reorder process */
		H264eRplmo rplmo;
		MPP_RET ret = h264e_reorder_rd_op(slice->reorder, &rplmo);

		if (MPP_OK == ret) {
			regs->reg_base.synt_sli2.ref_list0_rodr = 1;
			regs->reg_base.synt_sli2.rodr_pic_idx =
				rplmo.modification_of_pic_nums_idc;

			switch (rplmo.modification_of_pic_nums_idc) {
			case 0:
			case 1: {
				regs->reg_base.synt_sli2.rodr_pic_num =
					rplmo.abs_diff_pic_num_minus1;
			}
			break;
			case 2: {
				regs->reg_base.synt_sli2.rodr_pic_num =
					rplmo.long_term_pic_idx;
			}
			break;
			default: {
				mpp_err_f
				("invalid modification_of_pic_nums_idc %d\n",
				 rplmo.
				 modification_of_pic_nums_idc);
			}
			break;
			}
		} else {
			// slice->ref_pic_list_modification_flag;
			regs->reg_base.synt_sli2.ref_list0_rodr = 0;
			regs->reg_base.synt_sli2.rodr_pic_idx = 0;
			regs->reg_base.synt_sli2.rodr_pic_num = 0;
		}
	}

	/* clear all mmco arg first */
	regs->reg_base.synt_refm0.nopp_flg = 0;
	regs->reg_base.synt_refm0.ltrf_flg = 0;
	regs->reg_base.synt_refm0.arpm_flg = 0;
	regs->reg_base.synt_refm0.mmco4_pre = 0;
	regs->reg_base.synt_refm0.mmco_type0 = 0;
	regs->reg_base.synt_refm0.mmco_parm0 = 0;
	regs->reg_base.synt_refm0.mmco_type1 = 0;
	regs->reg_base.synt_refm1.mmco_parm1 = 0;
	regs->reg_base.synt_refm0.mmco_type2 = 0;
	regs->reg_base.synt_refm1.mmco_parm2 = 0;
	regs->reg_base.synt_refm2.long_term_frame_idx0 = 0;
	regs->reg_base.synt_refm2.long_term_frame_idx1 = 0;
	regs->reg_base.synt_refm2.long_term_frame_idx2 = 0;

	h264e_marking_rd_rewind(slice->marking);

	/* only update used parameter */
	if (slice->slice_type == H264_I_SLICE) {
		regs->reg_base.synt_refm0.nopp_flg =
			slice->no_output_of_prior_pics;
		regs->reg_base.synt_refm0.ltrf_flg =
			slice->long_term_reference_flag;
	} else {
		if (!h264e_marking_is_empty(slice->marking)) {
			H264eMmco mmco;

			regs->reg_base.synt_refm0.arpm_flg = 1;

			/* max 3 mmco */
			do {
				RK_S32 type = 0;
				RK_S32 param_0 = 0;
				RK_S32 param_1 = 0;

				h264e_marking_rd_op(slice->marking, &mmco);
				type = mmco.mmco;
				switch (type) {
				case 1: {
					param_0 =
						mmco.
						difference_of_pic_nums_minus1;
				}
				break;
				case 2: {
					param_0 =
						mmco.long_term_pic_num;
				}
				break;
				case 3: {
					param_0 =
						mmco.
						difference_of_pic_nums_minus1;
					param_1 =
						mmco.long_term_frame_idx;
				}
				break;
				case 4: {
					param_0 =
						mmco.
						max_long_term_frame_idx_plus1;
				}
				break;
				case 5: {
				}
				break;
				case 6: {
					param_0 =
						mmco.long_term_frame_idx;
				}
				break;
				default: {
					mpp_err_f
					("unsupported mmco 0 %d\n",
					 type);
					type = 0;
				}
				break;
				}

				regs->reg_base.synt_refm0.mmco_type0 = type;
				regs->reg_base.synt_refm0.mmco_parm0 = param_0;
				regs->reg_base.synt_refm2.long_term_frame_idx0 =
					param_1;

				if (h264e_marking_is_empty(slice->marking))
					break;

				h264e_marking_rd_op(slice->marking, &mmco);
				type = mmco.mmco;
				param_0 = 0;
				param_1 = 0;
				switch (type) {
				case 1: {
					param_0 =
						mmco.
						difference_of_pic_nums_minus1;
				}
				break;
				case 2: {
					param_0 =
						mmco.long_term_pic_num;
				}
				break;
				case 3: {
					param_0 =
						mmco.
						difference_of_pic_nums_minus1;
					param_1 =
						mmco.long_term_frame_idx;
				}
				break;
				case 4: {
					param_0 =
						mmco.
						max_long_term_frame_idx_plus1;
				}
				break;
				case 5: {
				}
				break;
				case 6: {
					param_0 =
						mmco.long_term_frame_idx;
				}
				break;
				default: {
					mpp_err_f
					("unsupported mmco 0 %d\n",
					 type);
					type = 0;
				}
				break;
				}

				regs->reg_base.synt_refm0.mmco_type1 = type;
				regs->reg_base.synt_refm1.mmco_parm1 = param_0;
				regs->reg_base.synt_refm2.long_term_frame_idx1 = param_1;

				if (h264e_marking_is_empty(slice->marking))
					break;

				h264e_marking_rd_op(slice->marking, &mmco);
				type = mmco.mmco;
				param_0 = 0;
				param_1 = 0;
				switch (type) {
				case 1: {
					param_0 =
						mmco.
						difference_of_pic_nums_minus1;
				}
				break;
				case 2: {
					param_0 =
						mmco.long_term_pic_num;
				}
				break;
				case 3: {
					param_0 =
						mmco.
						difference_of_pic_nums_minus1;
					param_1 =
						mmco.long_term_frame_idx;
				}
				break;
				case 4: {
					param_0 =
						mmco.
						max_long_term_frame_idx_plus1;
				}
				break;
				case 5: {
				}
				break;
				case 6: {
					param_0 =
						mmco.long_term_frame_idx;
				}
				break;
				default: {
					mpp_err_f
					("unsupported mmco 0 %d\n",
					 type);
					type = 0;
				}
				break;
				}

				regs->reg_base.synt_refm0.mmco_type2 = type;
				regs->reg_base.synt_refm1.mmco_parm2 = param_0;
				regs->reg_base.synt_refm2.long_term_frame_idx2 =
					param_1;
			} while (0);
		}
	}

	hal_h264e_dbg_func("leave\n");
}

static void setup_vepu540c_rdo_pred(HalVepu540cRegSet *regs, H264eSps *sps,
				    H264ePps *pps, H264eSlice *slice)
{
	hal_h264e_dbg_func("enter\n");

	if (slice->slice_type == H264_I_SLICE)
		regs->reg_rc_roi.klut_ofst.chrm_klut_ofst = 6;

	else
		regs->reg_rc_roi.klut_ofst.chrm_klut_ofst = 9;

	regs->reg_base.rdo_cfg.rect_size =
		(sps->profile_idc == H264_PROFILE_BASELINE
		 && sps->level_idc <= H264_LEVEL_3_0) ? 1 : 0;
	regs->reg_base.rdo_cfg.vlc_lmt = (sps->profile_idc < H264_PROFILE_MAIN)
					 && !pps->entropy_coding_mode;
	regs->reg_base.rdo_cfg.chrm_spcl = 1;
	regs->reg_base.rdo_cfg.ccwa_e = 1;
	regs->reg_base.rdo_cfg.scl_lst_sel = pps->pic_scaling_matrix_present;
	regs->reg_base.rdo_cfg.atf_e = 1;
	regs->reg_base.rdo_cfg.atr_e = 1;
	regs->reg_base.rdo_cfg.intra_mode_cost_e = 1;

	regs->reg_base.iprd_csts.rdo_mark_mode = 0;

	hal_h264e_dbg_func("leave\n");
}

static void setup_vepu540c_rdo_cfg(HalH264eVepu540cCtx *ctx)
{
	rdo_skip_par *p_rdo_skip = NULL;
	rdo_noskip_par *p_rdo_noskip = NULL;
	vepu540c_rdo_cfg *reg = &ctx->regs_set->reg_rdo;
	H264eSlice *slice = ctx->slice;
	RK_S32 delta_qp = 0;
	RK_U32 *smear_cnt = ctx->smear_cnt;
	RK_U32 mb_cnt = ctx->mb_num;
	hal_h264e_dbg_func("enter\n");

	reg->rdo_smear_cfg_comb.rdo_smear_en = 1;
	reg->rdo_smear_cfg_comb.rdo_smear_lvl16_multi = 9;

	if (smear_cnt[2] + smear_cnt[3] > smear_cnt[4] / 2)
		delta_qp = 1;
	if (smear_cnt[4] < (mb_cnt >> 8))
		delta_qp -= 8;
	else if (smear_cnt[4] < (mb_cnt >> 7))
		delta_qp -= 6;
	else if (smear_cnt[4] < (mb_cnt >> 6))
		delta_qp -= 4;
	else
		delta_qp -= 1;
	reg->rdo_smear_cfg_comb.rdo_smear_dlt_qp = delta_qp;

	reg->rdo_smear_cfg_comb.rdo_smear_order_state = 0;
	if (H264_I_SLICE == slice->slice_type)
		reg->rdo_smear_cfg_comb.stated_mode = 1;
	else if (H264_I_SLICE == slice->last_slice_type)
		reg->rdo_smear_cfg_comb.stated_mode = 1;
	else
		reg->rdo_smear_cfg_comb.stated_mode = 2;

	if (1) {
		reg->rdo_smear_cfg_comb.online_en = 1;
		reg->rdo_smear_cfg_comb.smear_stride = 0;
	} else {
		reg->rdo_smear_cfg_comb.online_en = 0;
		reg->rdo_smear_cfg_comb.smear_stride = (ctx->cfg->prep.width + 255) / 256;
	}

	reg->rdo_smear_madp_thd0_comb.rdo_smear_madp_cur_thd0 = 0;
	reg->rdo_smear_madp_thd0_comb.rdo_smear_madp_cur_thd1 = 24;
	reg->rdo_smear_madp_thd1_comb.rdo_smear_madp_cur_thd2 = 48;
	reg->rdo_smear_madp_thd1_comb.rdo_smear_madp_cur_thd3 = 64;
	reg->rdo_smear_madp_thd2_comb.rdo_smear_madp_around_thd0 = 16;
	reg->rdo_smear_madp_thd2_comb.rdo_smear_madp_around_thd1 = 32;
	reg->rdo_smear_madp_thd3_comb.rdo_smear_madp_around_thd2 = 48;
	reg->rdo_smear_madp_thd3_comb.rdo_smear_madp_around_thd3 = 96;
	reg->rdo_smear_madp_thd4_comb.rdo_smear_madp_around_thd4 = 48;
	reg->rdo_smear_madp_thd4_comb.rdo_smear_madp_around_thd5 = 24;
	reg->rdo_smear_madp_thd5_comb.rdo_smear_madp_ref_thd0 = 96;
	reg->rdo_smear_madp_thd5_comb.rdo_smear_madp_ref_thd1 = 48;
	reg->rdo_smear_cnt_thd0_comb.rdo_smear_cnt_cur_thd0 = 1;
	reg->rdo_smear_cnt_thd0_comb.rdo_smear_cnt_cur_thd1 = 3;
	reg->rdo_smear_cnt_thd0_comb.rdo_smear_cnt_cur_thd2 = 1;
	reg->rdo_smear_cnt_thd0_comb.rdo_smear_cnt_cur_thd3 = 3;
	reg->rdo_smear_cnt_thd1_comb.rdo_smear_cnt_around_thd0 = 1;
	reg->rdo_smear_cnt_thd1_comb.rdo_smear_cnt_around_thd1 = 4;
	reg->rdo_smear_cnt_thd1_comb.rdo_smear_cnt_around_thd2 = 1;
	reg->rdo_smear_cnt_thd1_comb.rdo_smear_cnt_around_thd3 = 4;
	reg->rdo_smear_cnt_thd2_comb.rdo_smear_cnt_around_thd4 = 0;
	reg->rdo_smear_cnt_thd2_comb.rdo_smear_cnt_around_thd5 = 3;
	reg->rdo_smear_cnt_thd2_comb.rdo_smear_cnt_around_thd6 = 0;
	reg->rdo_smear_cnt_thd2_comb.rdo_smear_cnt_around_thd7 = 3;
	reg->rdo_smear_cnt_thd3_comb.rdo_smear_cnt_ref_thd0 = 1;
	reg->rdo_smear_cnt_thd3_comb.rdo_smear_cnt_ref_thd1 = 3;
	reg->rdo_smear_resi_thd0_comb.rdo_smear_resi_small_cur_th0 = 6;
	reg->rdo_smear_resi_thd0_comb.rdo_smear_resi_big_cur_th0 = 9;
	reg->rdo_smear_resi_thd0_comb.rdo_smear_resi_small_cur_th1 = 6;
	reg->rdo_smear_resi_thd0_comb.rdo_smear_resi_big_cur_th1 = 9;
	reg->rdo_smear_resi_thd1_comb.rdo_smear_resi_small_around_th0 = 6;
	reg->rdo_smear_resi_thd1_comb.rdo_smear_resi_big_around_th0 = 11;
	reg->rdo_smear_resi_thd1_comb.rdo_smear_resi_small_around_th1 = 6;
	reg->rdo_smear_resi_thd1_comb.rdo_smear_resi_big_around_th1 = 8;
	reg->rdo_smear_resi_thd2_comb.rdo_smear_resi_small_around_th2 = 9;
	reg->rdo_smear_resi_thd2_comb.rdo_smear_resi_big_around_th2 = 20;
	reg->rdo_smear_resi_thd2_comb.rdo_smear_resi_small_around_th3 = 6;
	reg->rdo_smear_resi_thd2_comb.rdo_smear_resi_big_around_th3 = 20;
	reg->rdo_smear_resi_thd3_comb.rdo_smear_resi_small_ref_th0 = 7;
	reg->rdo_smear_resi_thd3_comb.rdo_smear_resi_big_ref_th0 = 16;
	reg->rdo_smear_st_thd0_comb.rdo_smear_resi_th0 = 10;
	reg->rdo_smear_st_thd0_comb.rdo_smear_resi_th1 = 6;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th0 = 1;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th1 = 5;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th2 = 1;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th3 = 3;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th4 = 9;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th5 = 10;

	p_rdo_skip = &reg->rdo_b16_skip;
	p_rdo_skip->atf_thd0.madp_thd0 = 1;
	p_rdo_skip->atf_thd0.madp_thd1 = 10;
	p_rdo_skip->atf_thd1.madp_thd2 = 15;
	p_rdo_skip->atf_thd1.madp_thd3 = 25;
	p_rdo_skip->atf_wgt0.wgt0 = 20;
	p_rdo_skip->atf_wgt0.wgt1 = 16;
	p_rdo_skip->atf_wgt0.wgt2 = 16;
	p_rdo_skip->atf_wgt0.wgt3 = 16;
	p_rdo_skip->atf_wgt1.wgt4 = 16;

	p_rdo_noskip = &reg->rdo_b16_inter;
	p_rdo_noskip->ratf_thd0.madp_thd0 = 20;
	p_rdo_noskip->ratf_thd0.madp_thd1 = 40;
	p_rdo_noskip->ratf_thd1.madp_thd2 = 72;
	p_rdo_noskip->atf_wgt.wgt0 = 16;
	p_rdo_noskip->atf_wgt.wgt1 = 16;
	p_rdo_noskip->atf_wgt.wgt2 = 16;
	p_rdo_noskip->atf_wgt.wgt3 = 16;

	p_rdo_noskip = &reg->rdo_b16_intra;
	p_rdo_noskip->ratf_thd0.madp_thd0 = 20;
	p_rdo_noskip->ratf_thd0.madp_thd1 = 40;
	p_rdo_noskip->ratf_thd1.madp_thd2 = 72;
	p_rdo_noskip->atf_wgt.wgt0 = 27;
	p_rdo_noskip->atf_wgt.wgt1 = 25;
	p_rdo_noskip->atf_wgt.wgt2 = 20;
	p_rdo_noskip->atf_wgt.wgt3 = 16;

	reg->rdo_b16_intra_atf_cnt_thd_comb.thd0 = 1;
	reg->rdo_b16_intra_atf_cnt_thd_comb.thd1 = 4;
	reg->rdo_b16_intra_atf_cnt_thd_comb.thd2 = 1;
	reg->rdo_b16_intra_atf_cnt_thd_comb.thd3 = 4;
	reg->rdo_atf_resi_thd_comb.big_th0 = 16;
	reg->rdo_atf_resi_thd_comb.big_th1 = 16;
	reg->rdo_atf_resi_thd_comb.small_th0 = 8;
	reg->rdo_atf_resi_thd_comb.small_th1 = 8;

	hal_h264e_dbg_func("leave\n");
}

static MPP_RET vepu540c_h264e_save_pass1_patch(HalVepu540cRegSet *regs, HalH264eVepu540cCtx *ctx)
{
	RK_S32 width_align = MPP_ALIGN(ctx->cfg->prep.width, 16);
	RK_S32 height_align = MPP_ALIGN(ctx->cfg->prep.height, 16);

	if (NULL == ctx->buf_pass1) {
		mpp_buffer_get(NULL, &ctx->buf_pass1, width_align * height_align * 3 / 2);
		if (!ctx->buf_pass1) {
			mpp_err("buf_pass1 maSlloc fail, debreath invaild");
			return MPP_NOK;
		}
	}

	regs->reg_base.enc_pic.cur_frm_ref = 1;
	regs->reg_base.rfpw_h_addr = 0;
	regs->reg_base.rfpw_b_addr = mpp_dev_get_iova_address(ctx->dev, ctx->buf_pass1, 163);
	regs->reg_base.enc_pic.rec_fbc_dis = 1;

	return MPP_OK;
}

static MPP_RET vepu540c_h264e_use_pass1_patch(HalVepu540cRegSet *regs, HalH264eVepu540cCtx *ctx)
{
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	RK_S32 y_stride;
	RK_S32 c_stride;

	hal_h264e_dbg_func("enter\n");

	regs->reg_base.src_fmt.src_cfmt   = VEPU541_FMT_YUV420SP;
	regs->reg_base.src_fmt.alpha_swap = 0;
	regs->reg_base.src_fmt.rbuv_swap  = 0;
	regs->reg_base.src_fmt.out_fmt    = 1;
	regs->reg_base.src_fmt.src_rcne = 1;

	y_stride = MPP_ALIGN(prep->width, 16);
	c_stride = y_stride;

	regs->reg_base.src_strd0.src_strd0  = y_stride;
	regs->reg_base.src_strd1.src_strd1  = 3 * c_stride;

	regs->reg_base.src_proc.src_mirr   = 0;
	regs->reg_base.src_proc.src_rot    = 0;

	regs->reg_base.pic_ofst.pic_ofst_y = 0;
	regs->reg_base.pic_ofst.pic_ofst_x = 0;


	regs->reg_base.adr_src0   = mpp_dev_get_iova_address(ctx->dev, ctx->buf_pass1, 160);
	regs->reg_base.adr_src1   = regs->reg_base.adr_src0 + 2 * y_stride;
	regs->reg_base.adr_src2   = 0;

	hal_h264e_dbg_func("leave\n");
	return MPP_OK;
}
static void setup_vepu540c_scl_cfg(vepu540c_scl_cfg *regs)
{
	static RK_U32 vepu540c_h264_scl_tab[] = {
		/* 0x2200 */
		0x2fbe3333, 0x2fbe4189, 0x2fbe3333, 0x2fbe4189, 0x2ca42fbe,
		0x2ca43c79, 0x2ca42fbe, 0x2ca43c79,
		0x3c794189, 0x3c7951ec, 0x3c794189, 0x3c7951ec, 0x2ca42fbe,
		0x2ca43c79, 0x2ca42fbe, 0x2ca43c79,
		0x2fbe3333, 0x2fbe4189, 0x2fbe3333, 0x2fbe4189, 0x2ca42fbe,
		0x2ca43c79, 0x2ca42fbe, 0x2ca43c79,
		0x3c794189, 0x3c7951ec, 0x3c794189, 0x3c7951ec, 0x2ca42fbe,
		0x2ca43c79, 0x2ca42fbe, 0x2ca43c79,
		0x2b322e8c, 0x2b323a84, 0x2b322e8c, 0x2b323a84, 0x2a4a2b32,
		0x2a4a37d2, 0x2a4a2b32, 0x2a4a37d2,
		0x37d23a84, 0x37d24ae6, 0x37d23a84, 0x37d24ae6, 0x2a4a2b32,
		0x2a4a37d2, 0x2a4a2b32, 0x2a4a37d2,
		0x2b322e8c, 0x2b323a84, 0x2b322e8c, 0x2b323a84, 0x2a4a2b32,
		0x2a4a37d2, 0x2a4a2b32, 0x2a4a37d2,
		0x37d23a84, 0x37d24ae6, 0x37d23a84, 0x37d24ae6, 0x2a4a2b32,
		0x2a4a37d2, 0x2a4a2b32, 0x2a4a37d2,
		0x25cb2762, 0x25cb31a6, 0x25cb2762, 0x25cb31a6, 0x22ef25cb,
		0x22ef2ed1, 0x22ef25cb, 0x22ef2ed1,
		0x2ed131a6, 0x2ed13e6a, 0x2ed131a6, 0x2ed13e6a, 0x22ef25cb,
		0x22ef2ed1, 0x22ef25cb, 0x22ef2ed1,
		0x25cb2762, 0x25cb31a6, 0x25cb2762, 0x25cb31a6, 0x22ef25cb,
		0x22ef2ed1, 0x22ef25cb, 0x22ef2ed1,
		0x2ed131a6, 0x2ed13e6a, 0x2ed131a6, 0x2ed13e6a, 0x22ef25cb,
		0x22ef2ed1, 0x22ef25cb, 0x22ef2ed1,
		0x22e32492, 0x22e32ed0, 0x22e32492, 0x22e32ed0, 0x202422e3,
		0x20242bfb, 0x202422e3, 0x20242bfb,
		0x2bfb2ed0, 0x2bfb3a41, 0x2bfb2ed0, 0x2bfb3a41, 0x202422e3,
		0x20242bfb, 0x202422e3, 0x20242bfb,
		0x22e32492, 0x22e32ed0, 0x22e32492, 0x22e32ed0, 0x202422e3,
		0x20242bfb, 0x202422e3, 0x20242bfb,
		0x2bfb2ed0, 0x2bfb3a41, 0x2bfb2ed0, 0x2bfb3a41, 0x202422e3,
		0x20242bfb, 0x202422e3, 0x20242bfb,
		0x1e3c2000, 0x1e3c28f6, 0x1e3c2000, 0x1e3c28f6, 0x1cb21e3c,
		0x1cb22631, 0x1cb21e3c, 0x1cb22631,
		0x263128f6, 0x26313367, 0x263128f6, 0x26313367, 0x1cb21e3c,
		0x1cb22631, 0x1cb21e3c, 0x1cb22631,
		0x1e3c2000, 0x1e3c28f6, 0x1e3c2000, 0x1e3c28f6, 0x1cb21e3c,
		0x1cb22631, 0x1cb21e3c, 0x1cb22631,
		0x263128f6, 0x26313367, 0x263128f6, 0x26313367, 0x1cb21e3c,
		0x1cb22631, 0x1cb21e3c, 0x1cb22631,
		0x1aae1c72, 0x1aae239e, 0x1aae1c72, 0x1aae239e, 0x191c1aae,
		0x191c21c0, 0x191c1aae, 0x191c21c0,
		0x21c0239e, 0x21c02d32, 0x21c0239e, 0x21c02d32, 0x191c1aae,
		0x191c21c0, 0x191c1aae, 0x191c21c0,
		0x1aae1c72, 0x1aae239e, 0x1aae1c72, 0x1aae239e, 0x191c1aae,
		0x191c21c0, 0x191c1aae, 0x191c21c0,
		0x21c0239e, 0x21c02d32, 0x21c0239e, 0x21c02d32, 0x191c1aae,
		0x191c21c0, 0x191c1aae, 0x191c21c0,
		0x00be0033, 0x00be0089, 0x00be0033, 0x00be0089, 0x00a400be,
		0x00a40079, 0x00a400be, 0x00a40079,
		0x00790089, 0x007900ec, 0x00790089, 0x007900ec, 0x00a400be,
		0x00a40079, 0x00a400be, 0x00a40079,
		0x00be0033, 0x00be0089, 0x00be0033, 0x00be0089, 0x00a400be,
		0x00a40079, 0x00a400be, 0x00a40079,
		0x00790089, 0x007900ec, 0x00790089, 0x007900ec, 0x00a400be,
		0x00a40079, 0x00a400be, 0x00a40079,
		0x0032008c, 0x00320084, 0x0032008c, 0x00320084, 0x004a0032,
		0x004a00d2, 0x004a0032, 0x004a00d2,
		0x00d20084, 0x00d200e6, 0x00d20084, 0x00d200e6, 0x004a0032,
		0x004a00d2, 0x004a0032, 0x004a00d2,
		0x0032008c, 0x00320084, 0x0032008c, 0x00320084, 0x004a0032,
		0x004a00d2, 0x004a0032, 0x004a00d2,
		0x00d20084, 0x00d200e6, 0x00d20084, 0x00d200e6, 0x004a0032,
		0x004a00d2, 0x004a0032, 0x004a00d2,
		0x00cb0062, 0x00cb00a6, 0x00cb0062, 0x00cb00a6, 0x00ef00cb,
		0x00ef00d1, 0x00ef00cb, 0x00ef00d1,
		0x00d100a6, 0x00d1006a, 0x00d100a6, 0x00d1006a, 0x00ef00cb,
		0x00ef00d1, 0x00ef00cb, 0x00ef00d1,
		0x00cb0062, 0x00cb00a6, 0x00cb0062, 0x00cb00a6, 0x00ef00cb,
		0x00ef00d1, 0x00ef00cb, 0x00ef00d1,
		0x00d100a6, 0x00d1006a, 0x00d100a6, 0x00d1006a, 0x00ef00cb,
		0x00ef00d1, 0x00ef00cb, 0x00ef00d1,
		0x00e30092, 0x00e300d0, 0x00e30092, 0x00e300d0, 0x002400e3,
		0x002400fb, 0x002400e3, 0x002400fb,
		0x00fb00d0, 0x00fb0041, 0x00fb00d0, 0x00fb0041, 0x002400e3,
		0x002400fb, 0x002400e3, 0x002400fb,
		0x00e30092, 0x00e300d0, 0x00e30092, 0x00e300d0, 0x002400e3,
		0x002400fb, 0x002400e3, 0x002400fb,
		0x00fb00d0, 0x00fb0041, 0x00fb00d0, 0x00fb0041, 0x002400e3,
		0x002400fb, 0x002400e3, 0x002400fb,
		0x003c0000, 0x003c00f6, 0x003c0000, 0x003c00f6, 0x00b2003c,
		0x00b20031, 0x00b2003c, 0x00b20031,
		0x003100f6, 0x00310067, 0x003100f6, 0x00310067, 0x00b2003c,
		0x00b20031, 0x00b2003c, 0x00b20031,
		0x003c0000, 0x003c00f6, 0x003c0000, 0x003c00f6, 0x00b2003c,
		0x00b20031, 0x00b2003c, 0x00b20031,
		0x003100f6, 0x00310067, 0x003100f6, 0x00310067, 0x00b2003c,
		0x00b20031, 0x00b2003c, 0x00b20031,
		0x00ae0072, 0x00ae009e, 0x00ae0072, 0x00ae009e, 0x001c00ae,
		0x001c00c0, 0x001c00ae, 0x001c00c0,
		0x00c0009e, 0x00c00032, 0x00c0009e, 0x00c00032, 0x001c00ae,
		0x001c00c0, 0x001c00ae, 0x001c00c0,
		0x00ae0072, 0x00ae009e, 0x00ae0072, 0x00ae009e, 0x001c00ae,
		0x001c00c0, 0x001c00ae, 0x001c00c0,
		0x00c0009e, 0x00c00032, 0x00c0009e, 0x00c00032, 0x001c00ae,
		0x001c00c0, 0x001c00ae, 0x001c00c0,
		0x002f0033, 0x002f0041, 0x002f0033, 0x002f0041, 0x002c002f,
		0x002c003c, 0x002c002f, 0x002c003c,
		0x003c0041, 0x003c0051, 0x003c0041, 0x003c0051, 0x002c002f,
		0x002c003c, 0x002c002f, 0x002c003c,
		0x002f0033, 0x002f0041, 0x002f0033, 0x002f0041, 0x002c002f,
		0x002c003c, 0x002c002f, 0x002c003c,
		0x003c0041, 0x003c0051, 0x003c0041, 0x003c0051, 0x002c002f,
		0x002c003c, 0x002c002f, 0x002c003c,
		0x002b002e, 0x002b003a, 0x002b002e, 0x002b003a, 0x002a002b,
		0x002a0037, 0x002a002b, 0x002a0037,
		0x0037003a, 0x0037004a, 0x0037003a, 0x0037004a, 0x002a002b,
		0x002a0037, 0x002a002b, 0x002a0037,
		0x002b002e, 0x002b003a, 0x002b002e, 0x002b003a, 0x002a002b,
		0x002a0037, 0x002a002b, 0x002a0037,
		0x0037003a, 0x0037004a, 0x0037003a, 0x0037004a, 0x002a002b,
		0x002a0037, 0x002a002b, 0x002a0037,
		0x01250127, 0x01250131, 0x01250127, 0x01250131, 0x01220125,
		0x0122012e, 0x01220125, 0x0122012e,
		0x012e0131, 0x012e013e, 0x012e0131, 0x012e013e, 0x01220125,
		0x0122012e, 0x01220125, 0x0122012e,
		0x01250127, 0x01250131, 0x01250127, 0x01250131, 0x01220125,
		0x0122012e, 0x01220125, 0x0122012e,
		0x012e0131, 0x012e013e, 0x012e0131, 0x012e013e, 0x01220125,
		0x0122012e, 0x01220125, 0x0122012e,
		0x01220124, 0x0122012e, 0x01220124, 0x0122012e, 0x01200122,
		0x0120012b, 0x01200122, 0x0120012b,
		0x012b012e, 0x012b013a, 0x012b012e, 0x012b013a, 0x01200122,
		0x0120012b, 0x01200122, 0x0120012b,
		0x01220124, 0x0122012e, 0x01220124, 0x0122012e, 0x01200122,
		0x0120012b, 0x01200122, 0x0120012b,
		0x012b012e, 0x012b013a, 0x012b012e, 0x012b013a, 0x01200122,
		0x0120012b, 0x01200122, 0x0120012b,
		0x001e0020, 0x001e0028, 0x001e0020, 0x001e0028, 0x001c001e,
		0x001c0026, 0x001c001e, 0x001c0026,
		0x00260028, 0x00260033, 0x00260028, 0x00260033, 0x001c001e,
		0x001c0026, 0x001c001e, 0x001c0026,
		0x001e0020, 0x001e0028, 0x001e0020, 0x001e0028, 0x001c001e,
		0x001c0026, 0x001c001e, 0x001c0026,
		0x00260028, 0x00260033, 0x00260028, 0x00260033, 0x001c001e,
		0x001c0026, 0x001c001e, 0x001c0026,
		0x001a001c, 0x001a0023, 0x001a001c, 0x001a0023, 0x0019001a,
		0x00190021, 0x0019001a, 0x00190021,
		0x00210023, 0x0021002d, 0x00210023, 0x0021002d, 0x0019001a,
		0x00190021, 0x0019001a, 0x00190021,
		0x001a001c, 0x001a0023, 0x001a001c, 0x001a0023, 0x0019001a,
		0x00190021, 0x0019001a, 0x00190021,
		0x00210023, 0x0021002d, 0x00210023, 0x0021002d, 0x0019001a,
		0x00190021, 0x0019001a, 0x00190021,
	};

	hal_h264e_dbg_func("enter\n");

	memcpy(&regs->q_intra_y8, vepu540c_h264_scl_tab,
	       sizeof(vepu540c_h264_scl_tab));

	hal_h264e_dbg_func("leave\n");
}

static void setup_vepu540c_rc_base(HalVepu540cRegSet *regs, H264eSps *sps,
				   H264eSlice *slice, MppEncHwCfg *hw,
				   EncRcTask *rc_task)
{
	EncRcTaskInfo *rc_info = &rc_task->info;
	RK_S32 mb_w = sps->pic_width_in_mbs;
	RK_S32 mb_h = sps->pic_height_in_mbs;
	RK_U32 qp_target = rc_info->quality_target;
	RK_U32 qp_min = rc_info->quality_min;
	RK_U32 qp_max = rc_info->quality_max;
	RK_U32 qpmap_mode = 1;
	RK_S32 mb_target_bits_mul_16 =
		(rc_info->bit_target << 4) / (mb_w * mb_h);
	RK_S32 mb_target_bits;
	RK_S32 negative_bits_thd;
	RK_S32 positive_bits_thd;

	hal_h264e_dbg_rc("bittarget %d qp [%d %d %d]\n", rc_info->bit_target,
			 qp_min, qp_target, qp_max);

	if (mb_target_bits_mul_16 >= 0x100000)
		mb_target_bits_mul_16 = 0x50000;

	mb_target_bits = (mb_target_bits_mul_16 * mb_w) >> 4;
	negative_bits_thd = 0 - 5 * mb_target_bits / 16;
	positive_bits_thd = 5 * mb_target_bits / 16;

	hal_h264e_dbg_func("enter\n");

	regs->reg_base.enc_pic.pic_qp = qp_target;

	regs->reg_base.rc_cfg.rc_en = 1;
	regs->reg_base.rc_cfg.aq_en = 1;
	regs->reg_base.rc_cfg.aq_mode = 0;
	regs->reg_base.rc_cfg.rc_ctu_num = mb_w;

	regs->reg_base.rc_qp.rc_qp_range = (slice->slice_type == H264_I_SLICE) ?
					   hw->qp_delta_row_i : hw->qp_delta_row;
	regs->reg_base.rc_qp.rc_max_qp = qp_max;
	regs->reg_base.rc_qp.rc_min_qp = qp_min;

	regs->reg_base.rc_tgt.ctu_ebit = mb_target_bits_mul_16;

	regs->reg_rc_roi.rc_adj0.qp_adj0 = -2;
	regs->reg_rc_roi.rc_adj0.qp_adj1 = -1;
	regs->reg_rc_roi.rc_adj0.qp_adj2 = 0;
	regs->reg_rc_roi.rc_adj0.qp_adj3 = 1;
	regs->reg_rc_roi.rc_adj0.qp_adj4 = 2;
	regs->reg_rc_roi.rc_adj1.qp_adj5 = 0;
	regs->reg_rc_roi.rc_adj1.qp_adj6 = 0;
	regs->reg_rc_roi.rc_adj1.qp_adj7 = 0;
	regs->reg_rc_roi.rc_adj1.qp_adj8 = 0;

	regs->reg_rc_roi.rc_dthd_0_8[0] = 4 * negative_bits_thd;
	regs->reg_rc_roi.rc_dthd_0_8[1] = negative_bits_thd;
	regs->reg_rc_roi.rc_dthd_0_8[2] = positive_bits_thd;
	regs->reg_rc_roi.rc_dthd_0_8[3] = 4 * positive_bits_thd;
	regs->reg_rc_roi.rc_dthd_0_8[4] = 0x7FFFFFFF;
	regs->reg_rc_roi.rc_dthd_0_8[5] = 0x7FFFFFFF;
	regs->reg_rc_roi.rc_dthd_0_8[6] = 0x7FFFFFFF;
	regs->reg_rc_roi.rc_dthd_0_8[7] = 0x7FFFFFFF;
	regs->reg_rc_roi.rc_dthd_0_8[8] = 0x7FFFFFFF;

	regs->reg_rc_roi.roi_qthd0.qpmin_area0 = qp_min;
	regs->reg_rc_roi.roi_qthd0.qpmax_area0 = qp_max;
	regs->reg_rc_roi.roi_qthd0.qpmin_area1 = qp_min;
	regs->reg_rc_roi.roi_qthd0.qpmax_area1 = qp_max;
	regs->reg_rc_roi.roi_qthd0.qpmin_area2 = qp_min;

	regs->reg_rc_roi.roi_qthd1.qpmax_area2 = qp_max;
	regs->reg_rc_roi.roi_qthd1.qpmin_area3 = qp_min;
	regs->reg_rc_roi.roi_qthd1.qpmax_area3 = qp_max;
	regs->reg_rc_roi.roi_qthd1.qpmin_area4 = qp_min;
	regs->reg_rc_roi.roi_qthd1.qpmax_area4 = qp_max;

	regs->reg_rc_roi.roi_qthd2.qpmin_area5 = qp_min;
	regs->reg_rc_roi.roi_qthd2.qpmax_area5 = qp_max;
	regs->reg_rc_roi.roi_qthd2.qpmin_area6 = qp_min;
	regs->reg_rc_roi.roi_qthd2.qpmax_area6 = qp_max;
	regs->reg_rc_roi.roi_qthd2.qpmin_area7 = qp_min;

	regs->reg_rc_roi.roi_qthd3.qpmax_area7 = qp_max;
	regs->reg_rc_roi.roi_qthd3.qpmap_mode = qpmap_mode;

	hal_h264e_dbg_func("leave\n");
}

static void setup_vepu540c_io_buf(HalH264eVepu540cCtx *ctx,
				  HalEncTask *task)
{
	HalVepu540cRegSet *regs = ctx->regs_set;
	MppDev dev = ctx->dev;
	MppFrame frm = task->frame;
	MppPacket pkt = task->packet;
	MppBuffer buf_in = mpp_frame_get_buffer(frm);
	ring_buf *buf_out = task->output;
	MppFrameFormat fmt = mpp_frame_get_fmt(frm);
	RK_S32 hor_stride = mpp_frame_get_hor_stride(frm);
	RK_S32 ver_stride = mpp_frame_get_ver_stride(frm);
	RK_U32 off_in[2] = { 0 };
	RK_U32 off_out = mpp_packet_get_length(pkt);
	size_t siz_out = buf_out->size;

	hal_h264e_dbg_func("enter\n");

	if (MPP_FRAME_FMT_IS_FBC(fmt)) {
		off_in[0] = mpp_frame_get_fbc_offset(frm);;
		off_in[1] = 0;
	} else if (MPP_FRAME_FMT_IS_YUV(fmt)) {
		VepuFmtCfg cfg;

		vepu541_set_fmt(&cfg, fmt);
		switch (cfg.format) {
		case VEPU541_FMT_BGRA8888:
		case VEPU541_FMT_BGR888:
		case VEPU541_FMT_BGR565: {
			off_in[0] = 0;
			off_in[1] = 0;
		}
		break;
		case VEPU541_FMT_YUV420SP:
		case VEPU541_FMT_YUV422SP: {
			off_in[0] = hor_stride * ver_stride;
			off_in[1] = hor_stride * ver_stride;
		}
		break;
		case VEPU541_FMT_YUV422P: {
			off_in[0] = hor_stride * ver_stride;
			off_in[1] = hor_stride * ver_stride * 3 / 2;
		}
		break;
		case VEPU541_FMT_YUV420P: {
			off_in[0] = hor_stride * ver_stride;
			off_in[1] = hor_stride * ver_stride * 5 / 4;
		}
		break;
		case VEPU541_FMT_YUYV422:
		case VEPU541_FMT_UYVY422: {
			off_in[0] = 0;
			off_in[1] = 0;
		}
		break;
		case VEPU540C_FMT_YUV444SP : {
			off_in[0] = hor_stride * ver_stride;
			off_in[1] = hor_stride * ver_stride;
		} break;
		case VEPU540C_FMT_YUV444P : {
			off_in[0] = hor_stride * ver_stride;
			off_in[1] = hor_stride * ver_stride * 2;
		} break;
		case VEPU540C_FMT_BUTT:
		default: {
			off_in[0] = 0;
			off_in[1] = 0;
		}
		break;
		}
	}

	if (ctx->online) {
		regs->reg_base.adr_src0 = 0;
		regs->reg_base.adr_src1 = 0;
		regs->reg_base.adr_src2 = 0;
	} else {
		regs->reg_base.adr_src0 = mpp_dev_get_iova_address(dev, buf_in, 160);
		regs->reg_base.adr_src1 = regs->reg_base.adr_src0;
		regs->reg_base.adr_src2 = regs->reg_base.adr_src0;
		regs->reg_base.adr_src1 += off_in[0];
		regs->reg_base.adr_src2 += off_in[1];
	}

	regs->reg_base.bsbb_addr = mpp_dev_get_iova_address(dev, buf_out->buf, 173);
	regs->reg_base.bsbb_addr += buf_out->start_offset;
	regs->reg_base.bsbr_addr = regs->reg_base.bsbb_addr;
	regs->reg_base.adr_bsbs = regs->reg_base.bsbb_addr;
	regs->reg_base.bsbt_addr = regs->reg_base.bsbb_addr;

	regs->reg_base.bsbt_addr += (siz_out - 1);
	regs->reg_base.adr_bsbs += off_out;
	if (off_out) {
		dma_buf_end_cpu_access_partial(mpp_buffer_get_dma(task->output->buf),
					       DMA_TO_DEVICE, task->output->start_offset, off_out);
	}
	hal_h264e_dbg_func("leave\n");
}

static void setup_vepu540c_recn_refr(HalH264eVepu540cCtx *ctx,
				     HalVepu540cRegSet *regs)
{
	MppDev dev = ctx->dev;
	H264eFrmInfo *frms = ctx->frms;
	HalBufs bufs = ctx->hw_recn;
	RK_S32 fbc_hdr_size = ctx->pixel_buf_fbc_hdr_size;
	RK_U32 recn_ref_wrap = ctx->recn_ref_wrap;
	HalBuf *curr = hal_bufs_get_buf(bufs, frms->curr_idx);
	HalBuf *refr = hal_bufs_get_buf(bufs, frms->refr_idx);

	hal_h264e_dbg_func("enter\n");

	if (curr && curr->cnt) {
		MppBuffer buf_thumb = curr->buf[THUMB_TYPE];
		mpp_assert(buf_thumb);
		regs->reg_base.dspw_addr = mpp_dev_get_iova_address(dev, buf_thumb, 169);
		if (!recn_ref_wrap) {
			MppBuffer buf_pixel = curr->buf[RECREF_TYPE];

			mpp_assert(buf_pixel);
			regs->reg_base.rfpw_h_addr =
				mpp_dev_get_iova_address(dev, buf_pixel, 163);
			regs->reg_base.rfpw_b_addr =
				regs->reg_base.rfpw_h_addr + fbc_hdr_size;
			regs->reg_base.dspw_addr =
				mpp_dev_get_iova_address(dev, buf_thumb, 169);
		}
	}

	if (refr && refr->cnt) {
		MppBuffer buf_thumb = refr->buf[THUMB_TYPE];

		mpp_assert(buf_thumb);
		regs->reg_base.dspr_addr = mpp_dev_get_iova_address(dev, buf_thumb, 170);
		if (!recn_ref_wrap) {
			MppBuffer buf_pixel = refr->buf[RECREF_TYPE];

			mpp_assert(buf_pixel);
			regs->reg_base.rfpr_h_addr =
				mpp_dev_get_iova_address(dev, buf_pixel, 165);
			regs->reg_base.rfpr_b_addr =
				regs->reg_base.rfpr_h_addr + fbc_hdr_size;
		}
	}

	if (ctx->recn_ref_wrap)
		setup_recn_refr_wrap(ctx, regs);

	else {
		regs->reg_base.rfpt_h_addr = 0xffffffff;
		regs->reg_base.rfpb_h_addr = 0;
		regs->reg_base.rfpt_b_addr = 0xffffffff;
		regs->reg_base.rfpb_b_addr  = 0;
	}
	regs->reg_base.adr_smear_wr =  mpp_dev_get_iova_address(dev, curr->buf[SMEAR_TYPE], 185);
	regs->reg_base.adr_smear_rd =  mpp_dev_get_iova_address(dev, refr->buf[SMEAR_TYPE], 184);
	hal_h264e_dbg_func("leave\n");
}

static void setup_vepu540c_split(HalVepu540cRegSet *regs,
				 MppEncSliceSplit *cfg)
{
	hal_h264e_dbg_func("enter\n");

	switch (cfg->split_mode) {
	case MPP_ENC_SPLIT_NONE: {
		regs->reg_base.sli_splt.sli_splt = 0;
		regs->reg_base.sli_splt.sli_splt_mode = 0;
		regs->reg_base.sli_splt.sli_splt_cpst = 0;
		regs->reg_base.sli_splt.sli_max_num_m1 = 0;
		regs->reg_base.sli_splt.sli_flsh = 0;
		regs->reg_base.sli_cnum.sli_splt_cnum_m1 = 0;

		regs->reg_base.sli_byte.sli_splt_byte = 0;
		regs->reg_base.enc_pic.slen_fifo = 0;
	}
	break;
	case MPP_ENC_SPLIT_BY_BYTE: {
		regs->reg_base.sli_splt.sli_splt = 1;
		regs->reg_base.sli_splt.sli_splt_mode = 0;
		regs->reg_base.sli_splt.sli_splt_cpst = 0;
		regs->reg_base.sli_splt.sli_max_num_m1 = 500;
		regs->reg_base.sli_splt.sli_flsh = 1;
		regs->reg_base.sli_cnum.sli_splt_cnum_m1 = 0;

		regs->reg_base.sli_byte.sli_splt_byte = cfg->split_arg;
		regs->reg_base.enc_pic.slen_fifo = 0;
	}
	break;
	case MPP_ENC_SPLIT_BY_CTU: {
		regs->reg_base.sli_splt.sli_splt = 1;
		regs->reg_base.sli_splt.sli_splt_mode = 1;
		regs->reg_base.sli_splt.sli_splt_cpst = 0;
		regs->reg_base.sli_splt.sli_max_num_m1 = 500;
		regs->reg_base.sli_splt.sli_flsh = 1;
		regs->reg_base.sli_cnum.sli_splt_cnum_m1 =
			cfg->split_arg - 1;

		regs->reg_base.sli_byte.sli_splt_byte = 0;
		regs->reg_base.enc_pic.slen_fifo = 0;
	}
	break;
	default: {
		mpp_log_f("invalide slice split mode %d\n",
			  cfg->split_mode);
	}
	break;
	}

	cfg->change = 0;

	hal_h264e_dbg_func("leave\n");
}

static void calc_cime_parameter(HalVepu540cRegSet *regs)
{
	Vepu540cBaseCfg *base_regs = &regs->reg_base;
	RK_S32 x_gmv = 0;
	RK_S32 y_gmv = 0;
	RK_S32 srch_lftw, srch_rgtw, srch_uph, srch_dwnh;
	RK_S32 frm_sta = 0, frm_end = 0, pic_w = 0;
	RK_S32 pic_wdt_align =
		((base_regs->enc_rsl.pic_wd8_m1 + 1) * 8 + 63) / 64 * 2;

	srch_lftw = base_regs->me_rnge.cime_srch_lftw * 4;
	srch_rgtw = base_regs->me_rnge.cime_srch_rgtw * 4;
	srch_uph = base_regs->me_rnge.cime_srch_uph * 2;
	srch_dwnh = base_regs->me_rnge.cime_srch_dwnh * 2;

	// calc cime_linebuf_w
	{
		{
			if (x_gmv - srch_lftw < 0)
				frm_sta = 0;

			else
				frm_sta = (x_gmv - srch_lftw) / 16;
			if (x_gmv + srch_rgtw < 0) {
				frm_end =
					pic_wdt_align - 1 + (x_gmv +
							     srch_rgtw) / 16;
			} else {
				frm_end =
					pic_wdt_align - 1 + (x_gmv + srch_rgtw +
							     15) / 16;
			}
		}
		if (frm_sta < 0)
			frm_sta = 0;

		else if (frm_sta > pic_wdt_align - 1)
			frm_sta = pic_wdt_align - 1;
		frm_end = mpp_clip(frm_end, 0, pic_wdt_align - 1);
		pic_w = (frm_end - frm_sta + 1) * 32;
		base_regs->me_cach.cme_linebuf_w = pic_w / 32;
	}

	// calc cime_rama_h and cime_rama_max
	{
		RK_U32 rama_size = 1796;
		RK_U32 ramb_h;
		RK_U32 ctu_2_h = 2;
		RK_U32 cur_srch_8_w, cur_srch_2_h, cur_srch_h;

		if ((y_gmv % 4 - srch_uph % 4) < 0) {
			cur_srch_2_h =
				(4 + (y_gmv % 4 - srch_uph % 4) % 4 + srch_uph +
				 srch_dwnh) / 2 + ctu_2_h;
		} else {
			cur_srch_2_h =
				((y_gmv % 4 - srch_uph % 4) % 4 + srch_uph +
				 srch_dwnh) / 2 + ctu_2_h;
		}
		base_regs->me_cach.cime_size_rama = (cur_srch_2_h + 1) / 2 * 2;

		if ((x_gmv % 16 - srch_lftw % 16) < 0) {
			cur_srch_8_w =
				((16 + (x_gmv % 16 - srch_lftw % 16) % 16 +
				  srch_lftw + srch_rgtw + 15) / 16 + 1) * 2;
		} else {
			cur_srch_8_w =
				(((x_gmv % 16 - srch_lftw % 16) % 16 + srch_lftw +
				  srch_rgtw + 15) / 16 + 1) * 2;
		}

		cur_srch_h = ctu_2_h;
		ramb_h = cur_srch_2_h;
		while ((rama_size >
			((cur_srch_h -
			  ctu_2_h) * base_regs->me_cach.cme_linebuf_w +
			 (ramb_h * cur_srch_8_w)))
		       && (cur_srch_h < base_regs->me_cach.cime_size_rama)) {
			cur_srch_h = cur_srch_h + ctu_2_h;
			if (ramb_h > ctu_2_h * 2)
				ramb_h = ramb_h - ctu_2_h;

			else
				ramb_h = ctu_2_h;
		}

		if (cur_srch_2_h == ctu_2_h * 2) {
			cur_srch_h = cur_srch_h + ctu_2_h;
			ramb_h = ctu_2_h;
		}
		if (rama_size <
		    ((cur_srch_h - ctu_2_h) * base_regs->me_cach.cme_linebuf_w +
		     (ramb_h * cur_srch_8_w)))
			cur_srch_h = cur_srch_h - ctu_2_h;
		base_regs->me_cach.cime_size_rama =
			((cur_srch_h - ctu_2_h) * base_regs->me_cach.cme_linebuf_w +
			 ctu_2_h * cur_srch_8_w) / 2;
		base_regs->me_cach.cime_hgt_rama = cur_srch_h / 2;
		base_regs->me_cach.fme_prefsu_en = 0;
	}

}

static void setup_vepu540c_me(HalVepu540cRegSet *regs, H264eSps *sps,
			      H264eSlice *slice)
{
	(void)sps;
	(void)slice;
	regs->reg_base.me_rnge.cime_srch_dwnh = 15;
	regs->reg_base.me_rnge.cime_srch_uph = 14;
	regs->reg_base.me_rnge.cime_srch_rgtw = 12;
	regs->reg_base.me_rnge.cime_srch_lftw = 12;
	regs->reg_base.me_cfg.rme_srch_h = 3;
	regs->reg_base.me_cfg.rme_srch_v = 3;

	regs->reg_base.me_cfg.srgn_max_num = 72;
	regs->reg_base.me_cfg.cime_dist_thre = 1024;
	regs->reg_base.me_cfg.rme_dis = 0;
	regs->reg_base.me_cfg.fme_dis = 0;
	regs->reg_base.me_rnge.dlt_frm_num = 0x0;
	calc_cime_parameter(regs);
	hal_h264e_dbg_func("leave\n");
}

#define H264E_LAMBDA_TAB_SIZE       (52 * sizeof(RK_U32))

static RK_U32 h264e_lambda_default[58] = {
	0x00000003, 0x00000005, 0x00000006, 0x00000007,
	0x00000009, 0x0000000b, 0x0000000e, 0x00000012,
	0x00000016, 0x0000001c, 0x00000024, 0x0000002d,
	0x00000039, 0x00000048, 0x0000005b, 0x00000073,
	0x00000091, 0x000000b6, 0x000000e6, 0x00000122,
	0x0000016d, 0x000001cc, 0x00000244, 0x000002db,
	0x00000399, 0x00000489, 0x000005b6, 0x00000733,
	0x00000912, 0x00000b6d, 0x00000e66, 0x00001224,
	0x000016db, 0x00001ccc, 0x00002449, 0x00002db7,
	0x00003999, 0x00004892, 0x00005b6f, 0x00007333,
	0x00009124, 0x0000b6de, 0x0000e666, 0x00012249,
	0x00016dbc, 0x0001cccc, 0x00024492, 0x0002db79,
	0x00039999, 0x00048924, 0x0005b6f2, 0x00073333,
	0x00091249, 0x000b6de5, 0x000e6666, 0x00122492,
	0x0016dbcb, 0x001ccccc,
};

static void setup_vepu540c_l2(HalVepu540cRegSet *regs, H264eSlice *slice,
			      MppEncHwCfg *hw)
{
	RK_U32 i;

	hal_h264e_dbg_func("enter\n");

	memcpy(regs->reg_s3.rdo_wgta_qp_grpa_0_51, &h264e_lambda_default[6],
	       H264E_LAMBDA_TAB_SIZE);

	regs->reg_s3.RDO_QUANT.quant_f_bias_I = 683;
	regs->reg_s3.RDO_QUANT.quant_f_bias_P = 341;

	regs->reg_s3.iprd_tthdy4_0.iprd_tthdy4_0 = 1;
	regs->reg_s3.iprd_tthdy4_0.iprd_tthdy4_1 = 3;
	regs->reg_s3.iprd_tthdy4_1.iprd_tthdy4_2 = 6;
	regs->reg_s3.iprd_tthdy4_1.iprd_tthdy4_3 = 8;
	regs->reg_s3.iprd_tthdc8_0.iprd_tthdc8_0 = 1;
	regs->reg_s3.iprd_tthdc8_0.iprd_tthdc8_1 = 3;
	regs->reg_s3.iprd_tthdc8_1.iprd_tthdc8_2 = 6;
	regs->reg_s3.iprd_tthdc8_1.iprd_tthdc8_3 = 8;
	regs->reg_s3.iprd_tthdy8_0.iprd_tthdy8_0 = 1;
	regs->reg_s3.iprd_tthdy8_0.iprd_tthdy8_1 = 3;
	regs->reg_s3.iprd_tthdy8_1.iprd_tthdy8_2 = 6;
	regs->reg_s3.iprd_tthdy8_1.iprd_tthdy8_3 = 8;
	regs->reg_s3.iprd_tthd_ul.iprd_tthd_ul = 4;
	regs->reg_s3.iprd_wgty8.iprd_wgty8_0 = 22;
	regs->reg_s3.iprd_wgty8.iprd_wgty8_1 = 23;
	regs->reg_s3.iprd_wgty8.iprd_wgty8_2 = 20;
	regs->reg_s3.iprd_wgty8.iprd_wgty8_3 = 22;
	regs->reg_s3.iprd_wgty4.iprd_wgty4_0 = 22;
	regs->reg_s3.iprd_wgty4.iprd_wgty4_1 = 26;
	regs->reg_s3.iprd_wgty4.iprd_wgty4_2 = 20;
	regs->reg_s3.iprd_wgty4.iprd_wgty4_3 = 22;
	regs->reg_s3.iprd_wgty16.iprd_wgty16_0 = 22;
	regs->reg_s3.iprd_wgty16.iprd_wgty16_1 = 26;
	regs->reg_s3.iprd_wgty16.iprd_wgty16_2 = 20;
	regs->reg_s3.iprd_wgty16.iprd_wgty16_3 = 22;
	regs->reg_s3.iprd_wgtc8.iprd_wgtc8_0 = 18;
	regs->reg_s3.iprd_wgtc8.iprd_wgtc8_1 = 21;
	regs->reg_s3.iprd_wgtc8.iprd_wgtc8_2 = 20;
	regs->reg_s3.iprd_wgtc8.iprd_wgtc8_3 = 19;

	if (slice->slice_type == H264_I_SLICE) {
		regs->reg_s3.ATR_THD0.atr_thd0 = 1;
		regs->reg_s3.ATR_THD0.atr_thd1 = 2;
		regs->reg_s3.ATR_THD1.atr_thd2 = 6;
	} else {
		regs->reg_s3.ATR_THD0.atr_thd0 = 2;
		regs->reg_s3.ATR_THD0.atr_thd1 = 4;
		regs->reg_s3.ATR_THD1.atr_thd2 = 9;
	}
	regs->reg_s3.ATR_THD1.atr_thdqp = 32;

	if (slice->slice_type == H264_I_SLICE) {
		regs->reg_s3.Lvl16_ATR_WGT.lvl16_atr_wgt0 = 16;
		regs->reg_s3.Lvl16_ATR_WGT.lvl16_atr_wgt1 = 16;
		regs->reg_s3.Lvl16_ATR_WGT.lvl16_atr_wgt2 = 16;

		regs->reg_s3.Lvl8_ATR_WGT.lvl8_atr_wgt0 = 22;
		regs->reg_s3.Lvl8_ATR_WGT.lvl8_atr_wgt1 = 21;
		regs->reg_s3.Lvl8_ATR_WGT.lvl8_atr_wgt2 = 20;

		regs->reg_s3.Lvl4_ATR_WGT.lvl4_atr_wgt0 = 20;
		regs->reg_s3.Lvl4_ATR_WGT.lvl4_atr_wgt1 = 18;
		regs->reg_s3.Lvl4_ATR_WGT.lvl4_atr_wgt2 = 16;
	} else {
		regs->reg_s3.Lvl16_ATR_WGT.lvl16_atr_wgt0 = 25;
		regs->reg_s3.Lvl16_ATR_WGT.lvl16_atr_wgt1 = 20;
		regs->reg_s3.Lvl16_ATR_WGT.lvl16_atr_wgt2 = 16;

		regs->reg_s3.Lvl8_ATR_WGT.lvl8_atr_wgt0 = 25;
		regs->reg_s3.Lvl8_ATR_WGT.lvl8_atr_wgt1 = 20;
		regs->reg_s3.Lvl8_ATR_WGT.lvl8_atr_wgt2 = 18;

		regs->reg_s3.Lvl4_ATR_WGT.lvl4_atr_wgt0 = 25;
		regs->reg_s3.Lvl4_ATR_WGT.lvl4_atr_wgt1 = 20;
		regs->reg_s3.Lvl4_ATR_WGT.lvl4_atr_wgt2 = 16;
	}
	/* CIME */
	{
		/* 0x1760 */
		regs->reg_s3.cime_sqi_cfg.cime_pmv_num = 1;
		regs->reg_s3.cime_sqi_cfg.cime_fuse = 1;
		regs->reg_s3.cime_sqi_cfg.itp_mode = 0;
		regs->reg_s3.cime_sqi_cfg.move_lambda = 0;
		regs->reg_s3.cime_sqi_cfg.rime_lvl_mrg = 0;
		regs->reg_s3.cime_sqi_cfg.rime_prelvl_en = 0;
		regs->reg_s3.cime_sqi_cfg.rime_prersu_en = 0;

		/* 0x1764 */
		regs->reg_s3.cime_mvd_th.cime_mvd_th0 = 16;
		regs->reg_s3.cime_mvd_th.cime_mvd_th1 = 48;
		regs->reg_s3.cime_mvd_th.cime_mvd_th2 = 80;

		/* 0x1768 */
		regs->reg_s3.cime_madp_th.cime_madp_th = 16;

		/* 0x176c */
		regs->reg_s3.cime_multi.cime_multi0 = 8;
		regs->reg_s3.cime_multi.cime_multi1 = 12;
		regs->reg_s3.cime_multi.cime_multi2 = 16;
		regs->reg_s3.cime_multi.cime_multi3 = 20;
	}

	/* RIME && FME */
	{
		/* 0x1770 */
		regs->reg_s3.rime_mvd_th.rime_mvd_th0 = 1;
		regs->reg_s3.rime_mvd_th.rime_mvd_th1 = 2;
		regs->reg_s3.rime_mvd_th.fme_madp_th = 0;

		/* 0x1774 */
		regs->reg_s3.rime_madp_th.rime_madp_th0 = 8;
		regs->reg_s3.rime_madp_th.rime_madp_th1 = 16;

		/* 0x1778 */
		regs->reg_s3.rime_multi.rime_multi0 = 4;
		regs->reg_s3.rime_multi.rime_multi1 = 8;
		regs->reg_s3.rime_multi.rime_multi2 = 12;

		/* 0x177C */
		regs->reg_s3.cmv_st_th.cmv_th0 = 64;
		regs->reg_s3.cmv_st_th.cmv_th1 = 96;
		regs->reg_s3.cmv_st_th.cmv_th2 = 128;
	}

	{
		/* 0x1064 */
		regs->reg_rc_roi.madi_st_thd.madi_th0 = 4;
		regs->reg_rc_roi.madi_st_thd.madi_th1 = 9;
		regs->reg_rc_roi.madi_st_thd.madi_th2 = 15;
		/* 0x1068 */
		regs->reg_rc_roi.madp_st_thd0.madp_th0 = 4 << 4;
		regs->reg_rc_roi.madp_st_thd0.madp_th1 = 9 << 4;
		/* 0x106C */
		regs->reg_rc_roi.madp_st_thd1.madp_th2 = 15 << 4;
	}

	{
		RK_U8 *thd = (RK_U8 *) & regs->reg_rc_roi.aq_tthd0;
		RK_U8 *step = (RK_U8 *) & regs->reg_rc_roi.aq_stp0;

		if (slice->slice_type == H264_I_SLICE) {
			for (i = 0; i < MPP_ARRAY_ELEMS(h264_aq_tthd_default);
			     i++) {
				thd[i] = hw->aq_thrd_i[i];
				step[i] = hw->aq_step_i[i] & 0x3f;
			}
		} else {
			for (i = 0; i < MPP_ARRAY_ELEMS(h264_P_aq_step_default);
			     i++) {
				thd[i] = hw->aq_thrd_p[i];
				step[i] = hw->aq_step_p[i] & 0x3f;
			}
		}
	}

	//    mpp_env_get_u32("dump_l2_reg", &dump_l2_reg, 0);

	if (dump_l2_reg) {
		RK_U32 *p = (RK_U32 *) regs;
		mpp_log("L2 reg dump start:\n");

		for (i = 0; i < (sizeof(*regs) / sizeof(RK_U32)); i++)
			mpp_log("%04x %08x\n", 4 + i * 4, p[i]);

		mpp_log("L2 reg done\n");
	}

	hal_h264e_dbg_func("leave\n");
}

static void setup_vepu540c_ext_line_buf(HalVepu540cRegSet *regs,
					HalH264eVepu540cCtx *ctx)
{
	if (ctx->ext_line_buf) {

		regs->reg_base.ebufb_addr =
			mpp_dev_get_iova_address(ctx->dev, ctx->ext_line_buf, 179);

		regs->reg_base.ebuft_addr =
			regs->reg_base.ebufb_addr + ctx->ext_line_buf_size;

	} else {
		regs->reg_base.ebufb_addr = 0;
		regs->reg_base.ebufb_addr = 0;
	}
}
#ifdef HW_DVBM
static void setup_vepu540c_dvbm(HalVepu540cRegSet *regs, HalH264eVepu540cCtx *ctx)
{
	RK_U32 soft_resync = 1;
	RK_U32 frame_match = 0;

	(void)ctx;
	regs->reg_ctl.dvbm_cfg.dvbm_en = 1;
	regs->reg_ctl.dvbm_cfg.src_badr_sel = 0;
	regs->reg_ctl.dvbm_cfg.vinf_frm_match = frame_match;
	regs->reg_ctl.dvbm_cfg.vrsp_half_cycle = 8;

	regs->reg_ctl.vs_ldly.dvbm_ack_sel = soft_resync;
	regs->reg_ctl.vs_ldly.dvbm_ack_soft = 1;
	regs->reg_ctl.vs_ldly.dvbm_inf_sel = 0;

	regs->reg_base.dvbm_id.ch_id = 1;
	regs->reg_base.dvbm_id.frame_id = 0;
	regs->reg_base.dvbm_id.vrsp_rtn_en = 1;
	vepu540c_set_dvbm(&regs->reg_base.online_addr);
}
#else
static MPP_RET setup_vepu540c_dvbm(HalVepu540cRegSet *regs, HalH264eVepu540cCtx *ctx)
{
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct dvbm_addr_cfg dvbm_adr;

	(void)ctx;
	rk_dvbm_ctrl(NULL, DVBM_VEPU_GET_ADR, &dvbm_adr);
	if (dvbm_adr.overflow) {
		mpp_err("cur frame already overflow [%d %d]!\n",
			dvbm_adr.frame_id, dvbm_adr.line_cnt);
		return MPP_NOK;
	}
	regs->reg_ctl.dvbm_cfg.dvbm_en = 1;
	regs->reg_ctl.dvbm_cfg.src_badr_sel = 1;
	regs->reg_ctl.dvbm_cfg.vinf_frm_match = 1;
	regs->reg_ctl.dvbm_cfg.vrsp_half_cycle = 8;

	regs->reg_ctl.vs_ldly.vswm_lcnt_soft = dvbm_adr.line_cnt;
	regs->reg_ctl.vs_ldly.vswm_fcnt_soft = dvbm_adr.frame_id;
	regs->reg_ctl.vs_ldly.dvbm_ack_sel = 1;
	regs->reg_ctl.vs_ldly.dvbm_ack_soft = 1;
	regs->reg_ctl.vs_ldly.dvbm_inf_sel = 1;

	regs->reg_base.dvbm_id.ch_id = 1;
	regs->reg_base.dvbm_id.frame_id = dvbm_adr.frame_id;
	regs->reg_base.dvbm_id.vrsp_rtn_en = 1;


	regs->reg_base.online_addr.reg0156_adr_vsy_t = dvbm_adr.ybuf_top;
	regs->reg_base.online_addr.reg0157_adr_vsc_t = dvbm_adr.cbuf_top;
	regs->reg_base.online_addr.reg0158_adr_vsy_b = dvbm_adr.ybuf_bot;
	regs->reg_base.online_addr.reg0159_adr_vsc_b = dvbm_adr.cbuf_bot;
	regs->reg_base.adr_src0 = dvbm_adr.ybuf_sadr;
	regs->reg_base.adr_src1 = dvbm_adr.cbuf_sadr;
	regs->reg_base.adr_src2 = dvbm_adr.cbuf_sadr;
#else
	regs->reg_base.online_addr.reg0156_adr_vsy_t = 0;
	regs->reg_base.online_addr.reg0157_adr_vsc_t = 0;
	regs->reg_base.online_addr.reg0158_adr_vsy_b = 0;
	regs->reg_base.online_addr.reg0159_adr_vsc_b = 0;
#endif
	return MPP_OK;
}
#endif

static MPP_RET hal_h264e_vepu540c_gen_regs(void *hal, HalEncTask *task)
{
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;
	HalVepu540cRegSet *regs = ctx->regs_set;
	MppEncCfgSet *cfg = ctx->cfg;
	EncFrmStatus *frm = &task->rc_task->frm;
	H264eSps *sps = ctx->sps;
	H264ePps *pps = ctx->pps;
	H264eSlice *slice = ctx->slice;
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	MPP_RET ret = MPP_OK;

	hal_h264e_dbg_func("enter %p\n", hal);
	hal_h264e_dbg_detail("frame %d generate regs now", ctx->frms->seq_idx);

	/* register setup */
	memset(regs, 0, sizeof(*regs));

	setup_vepu540c_normal(regs);
	ret = setup_vepu540c_prep(regs, prep);
	if (ret)
		return ret;

	setup_vepu540c_codec(regs, sps, pps, slice);
	setup_vepu540c_rdo_pred(regs, sps, pps, slice);
	setup_vepu540c_rdo_cfg(ctx);
	setup_vepu540c_scl_cfg(&regs->reg_scl);
	setup_vepu540c_rc_base(regs, sps, slice, &cfg->hw, task->rc_task);
	setup_vepu540c_io_buf(ctx, task);
	if (ctx->online) {
		if (setup_vepu540c_dvbm(regs, ctx))
			return MPP_NOK;
	}
	setup_vepu540c_recn_refr(ctx, regs);

	regs->reg_base.meiw_addr =
		task->mv_info ? mpp_dev_get_iova_address(ctx->dev, task->mv_info, 171) : 0;
	regs->reg_base.enc_pic.mei_stor = task->mv_info ? 1 : 0;

	regs->reg_base.pic_ofst.pic_ofst_y =
		mpp_frame_get_offset_y(task->frame);
	regs->reg_base.pic_ofst.pic_ofst_x =
		mpp_frame_get_offset_x(task->frame);

	setup_vepu540c_split(regs, &cfg->split);
	setup_vepu540c_me(regs, sps, slice);

	if (ctx->osd_cfg.osd_data3)
		vepu540c_set_osd(&ctx->osd_cfg);

	if (ctx->qpmap_en) {
		MPP_RET ret;
		if (ctx->smart_en)
			vepu540c_set_qpmap_smart(&ctx->regs_set->reg_rc_roi.roi_cfg,
						 task->mv_info, task->qpmap,
						 task->mv_flag, task->mv_index, regs->reg_base.enc_pic.pic_qp,
						 prep->width, prep->height, 0, slice->idr_flag);
		else
			vepu540c_set_qpmap_normal(&ctx->regs_set->reg_rc_roi.roi_cfg,
						  task->mv_info, task->qpmap,
						  task->mv_flag, task->mv_index, regs->reg_base.enc_pic.pic_qp,
						  prep->width, prep->height, 0, slice->idr_flag);
		if (ret == MPP_OK)
			regs->reg_base.adr_roir =
				mpp_dev_get_iova_address(ctx->dev, task->qpmap, 186);
	}

	if (ctx->roi_data)
		vepu540c_set_roi(&ctx->regs_set->reg_rc_roi.roi_cfg,
				 (MppEncROICfg *)ctx->roi_data, prep->width, prep->height);

	setup_vepu540c_l2(ctx->regs_set, slice, &cfg->hw);
	setup_vepu540c_ext_line_buf(regs, ctx);
	//  mpp_env_get_u32("dump_l1_reg", &dump_l1_reg, 1);

	if (frm->save_pass1)
		vepu540c_h264e_save_pass1_patch(regs, ctx);

	if (frm->use_pass1)
		vepu540c_h264e_use_pass1_patch(regs, ctx);

	if (dump_l1_reg) {
		RK_U32 *p = (RK_U32 *) & regs->reg_base;
		RK_S32 n = sizeof(Vepu540cBaseCfg) / sizeof(RK_U32);
		RK_S32 i;
		mpp_log("L1 reg dump start:\n");

		for (i = 0; i < n; i++)
			mpp_log("%04x %08x\n", 0x280 + i * 4, p[i]);

		mpp_log("L1 reg done\n");
	}

	ctx->frame_cnt++;

	hal_h264e_dbg_func("leave %p\n", hal);
	return MPP_OK;
}

static MPP_RET hal_h264e_vepu540c_start(void *hal, HalEncTask *task)
{
	MPP_RET ret = MPP_OK;
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;

	(void)task;

	hal_h264e_dbg_func("enter %p\n", hal);

	do {
		MppDevRegWrCfg wr_cfg;
		MppDevRegRdCfg rd_cfg;

		wr_cfg.reg = &ctx->regs_set->reg_ctl;
		wr_cfg.size = sizeof(ctx->regs_set->reg_ctl);
		wr_cfg.offset = VEPU540C_CTL_OFFSET;
#if DUMP_REG
		{
			RK_U32 i;
			RK_U32 *reg = (RK_U32) wr_cfg.reg;
			for (i = 0;
			     i <
			     sizeof(ctx->regs_set->reg_ctl) / sizeof(RK_U32);
			     i++) {
				/* code */
				mpp_log("reg[%d] = 0x%08x\n", i, reg[i]);
			}

		}
#endif
		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}
		wr_cfg.reg = &ctx->regs_set->reg_base;
		wr_cfg.size = sizeof(ctx->regs_set->reg_base);
		wr_cfg.offset = VEPU540C_BASE_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}
		wr_cfg.reg = &ctx->regs_set->reg_rc_roi;
		wr_cfg.size = sizeof(ctx->regs_set->reg_rc_roi);
		wr_cfg.offset = VEPU540C_RCROI_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}
		wr_cfg.reg = &ctx->regs_set->reg_s3;
		wr_cfg.size = sizeof(ctx->regs_set->reg_s3);
		wr_cfg.offset = VEPU540C_WEG_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}
		wr_cfg.reg = &ctx->regs_set->reg_rdo;
		wr_cfg.size = sizeof(ctx->regs_set->reg_rdo);
		wr_cfg.offset = VEPU540C_RDOCFG_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}

		wr_cfg.reg = &ctx->regs_set->reg_scl;
		wr_cfg.size = sizeof(ctx->regs_set->reg_scl);
		wr_cfg.offset = VEPU540C_SCLCFG_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}

		wr_cfg.reg = &ctx->regs_set->jpeg_table;
		wr_cfg.size = sizeof(ctx->regs_set->jpeg_table);
		wr_cfg.offset = VEPU540C_JPEGTAB_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}

		wr_cfg.reg = &ctx->regs_set->reg_osd_cfg;
		wr_cfg.size = sizeof(ctx->regs_set->reg_osd_cfg);
		wr_cfg.offset = VEPU540C_OSD_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);
		if (ret) {
			mpp_err_f("set register write failed %d\n", ret);
			break;
		}

		rd_cfg.reg = &ctx->regs_set->reg_st;
		rd_cfg.size = sizeof(ctx->regs_set->reg_st);
		rd_cfg.offset = VEPU540C_STATUS_OFFSET;

		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_RD, &rd_cfg);
		if (ret) {
			mpp_err_f("set register read failed %d\n", ret);
			break;
		}
		/* send request to hardware */
		ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_SEND, NULL);

		if (ret) {
			mpp_err_f("send cmd failed %d\n", ret);
			break;
		}
	} while (0);

	hal_h264e_dbg_func("leave %p\n", hal);

	return ret;
}

static MPP_RET hal_h264e_vepu540c_status_check(void *hal)
{
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;
	HalVepu540cRegSet *regs_set = ctx->regs_set;

	if (regs_set->reg_ctl.int_sta.lkt_node_done_sta)
		hal_h264e_dbg_detail("lkt_done finish");

	if (regs_set->reg_ctl.int_sta.enc_done_sta)
		hal_h264e_dbg_detail("enc_done finish");

	if (regs_set->reg_ctl.int_sta.vslc_done_sta)
		hal_h264e_dbg_detail("enc_slice finsh");

	if (regs_set->reg_ctl.int_sta.sclr_done_sta)
		hal_h264e_dbg_detail("safe clear finsh");

	if (regs_set->reg_ctl.int_sta.vbsf_oflw_sta)
		mpp_err_f("bit stream overflow");

	if (regs_set->reg_ctl.int_sta.vbuf_lens_sta) {
		mpp_err_f("bus write full");
		return MPP_NOK;
	}

	if (regs_set->reg_ctl.int_sta.enc_err_sta) {
		mpp_err_f("bus error");
		return MPP_NOK;
	}

	if (regs_set->reg_ctl.int_sta.wdg_sta) {
		mpp_err_f("wdg timeout");
		return MPP_NOK;
	}

	return MPP_OK;
}

static MPP_RET hal_h264e_vepu540c_wait(void *hal, HalEncTask *task)
{
	MPP_RET ret = MPP_OK;
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;

	hal_h264e_dbg_func("enter %p\n", hal);
	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_POLL, NULL);
	if (ret) {
		mpp_err_f("poll cmd failed %d\n", ret);
		ret = MPP_ERR_VPUHW;
	}
	hal_h264e_dbg_func("leave %p\n", hal);

	if (ctx->osd_cfg.osd_data3)
		vepu540c_osd_put_dma_buf(&ctx->osd_cfg);

	return ret;
}

static MPP_RET hal_h264e_vepu540c_ret_task(void *hal, HalEncTask *task)
{
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;
	EncRcTaskInfo *rc_info = &task->rc_task->info;
	RK_U32 mb_w = ctx->sps->pic_width_in_mbs;
	RK_U32 mb_h = ctx->sps->pic_height_in_mbs;
	RK_U32 mbs = mb_w * mb_h;
	MPP_RET ret = 0;
	RK_U32 madi_cnt = 0, madp_cnt = 0;

	HalVepu540cRegSet *regs_set = (HalVepu540cRegSet *) ctx->regs_set;
	vepu540c_status *reg_st = (vepu540c_status *)&regs_set->reg_st;
	RK_U32 madi_th_cnt0 =
		reg_st->st_madi_lt_num0.madi_th_lt_cnt0 +
		reg_st->st_madi_rt_num0.madi_th_rt_cnt0 +
		reg_st->st_madi_lb_num0.madi_th_lb_cnt0 +
		reg_st->st_madi_rb_num0.madi_th_rb_cnt0;
	RK_U32 madi_th_cnt1 =
		reg_st->st_madi_lt_num0.madi_th_lt_cnt1 +
		reg_st->st_madi_rt_num0.madi_th_rt_cnt1 +
		reg_st->st_madi_lb_num0.madi_th_lb_cnt1 +
		reg_st->st_madi_rb_num0.madi_th_rb_cnt1;
	RK_U32 madi_th_cnt2 =
		reg_st->st_madi_lt_num1.madi_th_lt_cnt2 +
		reg_st->st_madi_rt_num1.madi_th_rt_cnt2 +
		reg_st->st_madi_lb_num1.madi_th_lb_cnt2 +
		reg_st->st_madi_rb_num1.madi_th_rb_cnt2;
	RK_U32 madi_th_cnt3 =
		reg_st->st_madi_lt_num1.madi_th_lt_cnt3 +
		reg_st->st_madi_rt_num1.madi_th_rt_cnt3 +
		reg_st->st_madi_lb_num1.madi_th_lb_cnt3 +
		reg_st->st_madi_rb_num1.madi_th_rb_cnt3;
	RK_U32 madp_th_cnt0 =
		reg_st->st_madp_lt_num0.madp_th_lt_cnt0 +
		reg_st->st_madp_rt_num0.madp_th_rt_cnt0 +
		reg_st->st_madp_lb_num0.madp_th_lb_cnt0 +
		reg_st->st_madp_rb_num0.madp_th_rb_cnt0;
	RK_U32 madp_th_cnt1 =
		reg_st->st_madp_lt_num0.madp_th_lt_cnt1 +
		reg_st->st_madp_rt_num0.madp_th_rt_cnt1 +
		reg_st->st_madp_lb_num0.madp_th_lb_cnt1 +
		reg_st->st_madp_rb_num0.madp_th_rb_cnt1;
	RK_U32 madp_th_cnt2 =
		reg_st->st_madp_lt_num1.madp_th_lt_cnt2 +
		reg_st->st_madp_rt_num1.madp_th_rt_cnt2 +
		reg_st->st_madp_lb_num1.madp_th_lb_cnt2 +
		reg_st->st_madp_rb_num1.madp_th_rb_cnt2;
	RK_U32 madp_th_cnt3 =
		reg_st->st_madp_lt_num1.madp_th_lt_cnt3 +
		reg_st->st_madp_rt_num1.madp_th_rt_cnt3 +
		reg_st->st_madp_lb_num1.madp_th_lb_cnt3 +
		reg_st->st_madp_rb_num1.madp_th_rb_cnt3;
	RK_U32 md_cnt = (24 * madp_th_cnt3 + 22 * madp_th_cnt2 + 17 * madp_th_cnt1) >> 2;

	hal_h264e_dbg_func("enter %p\n", hal);

	rc_info->motion_level = 0;
	if (md_cnt * 100 > 20 * mbs)
		rc_info->motion_level = 2;
	else if (md_cnt * 100 > 13 * mbs)
		rc_info->motion_level = 1;
	else
		rc_info->motion_level = 0;

	rc_info->complex_level = 0;
	if (madi_th_cnt0 * 100 > 40 * mbs)
		rc_info->complex_level = 0;
	else if (madi_th_cnt0 * 100 > 35 * mbs && madi_th_cnt1 * 100 > 15 * mbs)
		rc_info->complex_level = 0;
	else if (madi_th_cnt0 * 100 > 25 * mbs && madi_th_cnt1 * 100 > 20 * mbs)
		rc_info->complex_level = 1;
	else if (madi_th_cnt0 * 100 > 20 * mbs && madi_th_cnt2 * 100 < 30 * mbs
		 && madi_th_cnt3 * 100 < 25 * mbs)
		rc_info->complex_level = 1;
	else
		rc_info->complex_level = 2;

	ret = hal_h264e_vepu540c_status_check(hal);
	if (ret)
		return ret;
	task->hw_length += regs_set->reg_st.bs_lgth_l32;

	// update total hardware length
	task->length += task->hw_length;

	// setup bit length for rate control
	rc_info->bit_real = task->hw_length * 8;
	rc_info->quality_real = regs_set->reg_st.qp_sum / mbs;


	rc_info->madi = madi_th_cnt0 * regs_set->reg_rc_roi.madi_st_thd.madi_th0 +
			madi_th_cnt1 * (regs_set->reg_rc_roi.madi_st_thd.madi_th0 +
					regs_set->reg_rc_roi.madi_st_thd.madi_th1) / 2 +
			madi_th_cnt2 * (regs_set->reg_rc_roi.madi_st_thd.madi_th1 +
					regs_set->reg_rc_roi.madi_st_thd.madi_th2) / 2 +
			madi_th_cnt3 * regs_set->reg_rc_roi.madi_st_thd.madi_th2;

	madi_cnt = madi_th_cnt0 + madi_th_cnt1 + madi_th_cnt2 + madi_th_cnt3;

	if (madi_cnt)
		rc_info->madi = rc_info->madi / madi_cnt;

	rc_info->madp = madp_th_cnt0 * regs_set->reg_rc_roi.madp_st_thd0.madp_th0 +
			madp_th_cnt1 * (regs_set->reg_rc_roi.madp_st_thd0.madp_th0 +
					regs_set->reg_rc_roi.madp_st_thd0.madp_th1) / 2 +
			madp_th_cnt2 * (regs_set->reg_rc_roi.madp_st_thd0.madp_th1 +
					regs_set->reg_rc_roi.madp_st_thd1.madp_th2) / 2 +
			madp_th_cnt3 * regs_set->reg_rc_roi.madp_st_thd1.madp_th2;

	madp_cnt = madp_th_cnt0 + madp_th_cnt1 + madp_th_cnt2 + madp_th_cnt3;

	if (madp_cnt)
		rc_info->madp = rc_info->madp / madp_cnt;

	rc_info->iblk4_prop = (regs_set->reg_st.st_pnum_i4.pnum_i4 +
			       regs_set->reg_st.st_pnum_i8.pnum_i8 +
			       regs_set->reg_st.st_pnum_i16.pnum_i16) * 256 /
			      mbs;

	ctx->hal_rc_cfg.bit_real = rc_info->bit_real;
	ctx->hal_rc_cfg.quality_real = rc_info->quality_real;
	ctx->hal_rc_cfg.iblk4_prop = rc_info->iblk4_prop;
	ctx->slice->last_slice_type = ctx->slice->slice_type;

	ctx->mb_num = mbs;
	ctx->smear_cnt[0] = regs_set->reg_st.st_smear_cnt.rdo_smear_cnt0 * 4;
	ctx->smear_cnt[1] = regs_set->reg_st.st_smear_cnt.rdo_smear_cnt1 * 4;
	ctx->smear_cnt[2] = regs_set->reg_st.st_smear_cnt.rdo_smear_cnt2 * 4;
	ctx->smear_cnt[3] = regs_set->reg_st.st_smear_cnt.rdo_smear_cnt3 * 4;
	ctx->smear_cnt[4] = ctx->smear_cnt[0] + ctx->smear_cnt[1]
			    + ctx->smear_cnt[2] + ctx->smear_cnt[3];

	task->hal_ret.data = &ctx->hal_rc_cfg;
	task->hal_ret.number = 1;

	hal_h264e_dbg_func("leave %p\n", hal);

	return MPP_OK;
}

static MPP_RET hal_h264e_vepu540c_comb_start(void *hal, HalEncTask *task, HalEncTask *jpeg_task)
{
	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;
	HalVepu540cRegSet *regs = (HalVepu540cRegSet *) ctx->regs_set;
	Vepu540cJpegCfg jpeg_cfg;
	VepuFmtCfg cfg;
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	MppFrameFormat fmt = prep->format;
	hal_h264e_dbg_func("enter %p\n", hal);

	regs->reg_ctl.dtrns_map.jpeg_bus_edin = 7;
	vepu541_set_fmt(&cfg, fmt);
	jpeg_cfg.dev = ctx->dev;
	jpeg_cfg.jpeg_reg_base = &ctx->regs_set->reg_base.jpegReg;
	jpeg_cfg.reg_tab = &ctx->regs_set->jpeg_table;
	jpeg_cfg.enc_task = jpeg_task;
	jpeg_cfg.input_fmt = &cfg;
	jpeg_cfg.online = ctx->online;
	vepu540c_set_jpeg_reg(&jpeg_cfg);
	//osd part todo
	if (jpeg_task->jpeg_tlb_reg)
		memcpy(&regs->jpeg_table, jpeg_task->jpeg_tlb_reg, sizeof(vepu540c_jpeg_tab));
	if (jpeg_task->jpeg_osd_reg)
		memcpy(&regs->reg_osd_cfg.osd_jpeg_cfg, jpeg_task->jpeg_osd_reg, sizeof(vepu540c_osd_reg));
	hal_h264e_dbg_func("leave %p\n", hal);
	return hal_h264e_vepu540c_start(hal, task);
}

static MPP_RET hal_h264e_vepu540c_ret_comb_task(void *hal, HalEncTask *task, HalEncTask *jpeg_task)
{

	HalH264eVepu540cCtx *ctx = (HalH264eVepu540cCtx *) hal;
	EncRcTaskInfo *rc_info = &jpeg_task->rc_task->info;
	HalVepu540cRegSet *regs = (HalVepu540cRegSet *)ctx->regs_set;
	MPP_RET ret = MPP_OK;

	hal_h264e_dbg_func("enter %p\n", hal);
	ret = hal_h264e_vepu540c_ret_task(hal, task);
	if (ret)
		return ret;
	jpeg_task->hw_length += regs->reg_st.jpeg_head_bits_l32;
	// update total hardware length
	jpeg_task->length += jpeg_task->hw_length;
	// setup bit length for rate control
	rc_info->bit_real = jpeg_task->hw_length * 8;

	hal_h264e_dbg_func("leave %p\n", hal);
	return ret;
}

const MppEncHalApi hal_h264e_vepu540c = {
	.name = "hal_h264e_vepu540c",
	.coding = MPP_VIDEO_CodingAVC,
	.ctx_size = sizeof(HalH264eVepu540cCtx),
	.flag = 0,
	.init = hal_h264e_vepu540c_init,
	.deinit = hal_h264e_vepu540c_deinit,
	.prepare = hal_h264e_vepu540c_prepare,
	.get_task = hal_h264e_vepu540c_get_task,
	.gen_regs = hal_h264e_vepu540c_gen_regs,
	.start = hal_h264e_vepu540c_start,
	.wait = hal_h264e_vepu540c_wait,
	.part_start = NULL,
	.part_wait = NULL,
	.ret_task = hal_h264e_vepu540c_ret_task,
	.comb_start = hal_h264e_vepu540c_comb_start,
	.comb_ret_task = hal_h264e_vepu540c_ret_comb_task,
};
