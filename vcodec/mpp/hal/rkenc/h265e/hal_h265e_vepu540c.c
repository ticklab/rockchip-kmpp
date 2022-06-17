// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG  "hal_h265e_v540c"

#include <linux/string.h>
#include <linux/dma-buf.h>

//#include "mpp_env.h"
#include "mpp_mem.h"
//#include "mpp_soc.h"
//#include "mpp_common.h"
#include "mpp_packet.h"
#include "mpp_frame_impl.h"
#include "mpp_maths.h"

#include "hal_h265e_debug.h"
#include "h265e_syntax_new.h"
#include "hal_bufs.h"
#include "rkv_enc_def.h"
#include "vepu541_common.h"
#include "vepu540c_common.h"
#include "hal_h265e_vepu540c.h"
#include "hal_h265e_vepu540c_reg.h"
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
#include <soc/rockchip/rockchip_dvbm.h>
#endif

#define  MAX_TITLE_NUM 2
#define hal_h265e_err(fmt, ...) \
    do {\
        mpp_err_f(fmt, ## __VA_ARGS__);\
    } while (0)

const static RefType ref_type_map[2][2] = {
	/* ref_lt = 0	ref_lt = 1 */
	/* cur_lt = 0 */{ST_REF_TO_ST, ST_REF_TO_LT},
	/* cur_lt = 1 */{LT_REF_TO_ST, LT_REF_TO_LT},
};

extern RK_U32 vepu540c_h265_jvt_scl_tab[680];
extern RK_U32 vepu540c_h265_customer_scl_tab[680];

typedef struct vepu540c_h265_fbk_t {
	RK_U32 hw_status;       /* 0:corret, 1:error */
	RK_U32 qp_sum;
	RK_U32 out_strm_size;
	RK_U32 out_hw_strm_size;
	RK_S64 sse_sum;
	RK_U32 st_lvl64_inter_num;
	RK_U32 st_lvl32_inter_num;
	RK_U32 st_lvl16_inter_num;
	RK_U32 st_lvl8_inter_num;
	RK_U32 st_lvl32_intra_num;
	RK_U32 st_lvl16_intra_num;
	RK_U32 st_lvl8_intra_num;
	RK_U32 st_lvl4_intra_num;
	RK_U32 st_cu_num_qp[52];
	RK_U32 st_madp;
	RK_U32 st_madi;
	RK_U32 st_mb_num;
	RK_U32 st_ctu_num;
	RK_U32 st_smear_cnt[5];
} vepu540c_h265_fbk;

typedef struct H265eV540cHalContext_t {
	MppEncHalApi api;
	MppDev dev;
	void *regs;
	void *reg_out[MAX_TITLE_NUM];

	vepu540c_h265_fbk feedback;
	void *dump_files;
	RK_U32 frame_cnt_gen_ready;

	RK_S32 frame_type;
	RK_S32 last_frame_type;

	RK_U32 mb_num;
	RK_U32 smear_cnt[5];

	/* @frame_cnt starts from ZERO */
	RK_U32 frame_cnt;
	Vepu540cOsdCfg osd_cfg;
	void *roi_data;
	MppEncCfgSet *cfg;

	MppBufferGroup tile_grp;
	MppBuffer hw_tile_buf[2];

	/* two-pass deflicker */
	MppBuffer  buf_pass1;



	RK_U32 enc_mode;
	RK_U32 frame_size;
	RK_U32 smera_size;
	RK_S32 max_buf_cnt;
	RK_S32 hdr_status;
	void *input_fmt;
	RK_U8 *src_buf;
	RK_U8 *dst_buf;
	RK_S32 buf_size;
	RK_U32 frame_num;
	HalBufs dpb_bufs;
	RK_S32 fbc_header_len;
	RK_U32 title_num;
	RK_S32 online;
	/* recn and ref buffer offset */
	RK_U32 recn_ref_wrap;
	RK_S32 qpmap_en;
	RK_S32 smart_en;
	RK_S32 motion_static_switch_en;
	MppBuffer recn_ref_buf;
	WrapBufInfo wrap_infos;
	struct hal_shared_buf *shared_buf;
	RK_U32 is_gray;

	RK_S32 ext_line_buf_size;
	MppBuffer ext_line_buf;
	RK_U32 only_smartp;
} H265eV540cHalContext;

#define TILE_BUF_SIZE  MPP_ALIGN(128 * 1024, 256)

static RK_U32 aq_thd_default[16] = {
	0, 0, 0, 0,
	3, 3, 5, 5,
	8, 8, 8, 15,
	15, 20, 25, 25
};

static RK_S32 aq_qp_dealt_default[16] = {
	-8, -7, -6, -5,
	-4, -3, -2, -1,
	0, 1, 2, 3,
	4, 5, 6, 8,
};

static RK_U32 aq_thd_smart[16] = {
	0, 0, 0, 0,
	3, 3, 5, 5,
	8, 8, 8, 15,
	15, 20, 25, 25
};

static RK_S32 aq_qp_dealt_smart[16] = {
	-8, -7, -6, -5,
	-4, -3, -2, -1,
	0, 1, 2, 3,
	4, 6, 7, 9,
};

static RK_U32 lamd_moda_qp[52] = {
	0x00000049, 0x0000005c, 0x00000074, 0x00000092, 0x000000b8, 0x000000e8,
	0x00000124, 0x00000170, 0x000001cf, 0x00000248, 0x000002df, 0x0000039f,
	0x0000048f, 0x000005bf, 0x0000073d, 0x0000091f, 0x00000b7e, 0x00000e7a,
	0x0000123d, 0x000016fb, 0x00001cf4, 0x0000247b, 0x00002df6, 0x000039e9,
	0x000048f6, 0x00005bed, 0x000073d1, 0x000091ec, 0x0000b7d9, 0x0000e7a2,
	0x000123d7, 0x00016fb2, 0x0001cf44, 0x000247ae, 0x0002df64, 0x00039e89,
	0x00048f5c, 0x0005bec8, 0x00073d12, 0x00091eb8, 0x000b7d90, 0x000e7a23,
	0x00123d71, 0x0016fb20, 0x001cf446, 0x00247ae1, 0x002df640, 0x0039e88c,
	0x0048f5c3, 0x005bec81, 0x0073d119, 0x0091eb85
};

static RK_U32 lamd_modb_qp[52] = {
	0x00000070, 0x00000089, 0x000000b0, 0x000000e0, 0x00000112, 0x00000160,
	0x000001c0, 0x00000224, 0x000002c0, 0x00000380, 0x00000448, 0x00000580,
	0x00000700, 0x00000890, 0x00000b00, 0x00000e00, 0x00001120, 0x00001600,
	0x00001c00, 0x00002240, 0x00002c00, 0x00003800, 0x00004480, 0x00005800,
	0x00007000, 0x00008900, 0x0000b000, 0x0000e000, 0x00011200, 0x00016000,
	0x0001c000, 0x00022400, 0x0002c000, 0x00038000, 0x00044800, 0x00058000,
	0x00070000, 0x00089000, 0x000b0000, 0x000e0000, 0x00112000, 0x00160000,
	0x001c0000, 0x00224000, 0x002c0000, 0x00380000, 0x00448000, 0x00580000,
	0x00700000, 0x00890000, 0x00b00000, 0x00e00000
};

static RK_U32 lamd_modb_qp_cvr[52] = {
	0x00000070, 0x00000088, 0x000000B0, 0x000000D8, 0x00000110, 0x00000158,
	0x000001B0, 0x00000228, 0x000002B0, 0x00000368, 0x00000448, 0x00000568,
	0x000006D0, 0x00000890, 0x00000AC8, 0x00000D98,	0x00001120, 0x00001598,
	0x00001B30, 0x00002248, 0x00002B30, 0x00003668, 0x00004488, 0x00005658,
	0x00006CD0, 0x00009E08, 0x0000C720, 0x0000FAE0, 0x00013C18, 0x00018E40,
	0x0001F5C0, 0x00027830, 0x00031C80, 0x000470A0, 0x00059810, 0x00070C50,
	0x0008E148, 0x000B3028, 0x000E1898, 0x0013D708, 0x0018FF30, 0x001F7E78,
	0x0027AE18, 0x00373C28, 0x00459778, 0x0057AE18, 0x0078F3D0, 0x009863F8,
	0x00C00000, 0x00F1E7A0, 0x0130C7F0, 0x01800000
};

static void get_wrap_buf(H265eV540cHalContext *ctx, RK_S32 max_lt_cnt)
{
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	RK_S32 alignment = 64;
	RK_S32 aligned_w = MPP_ALIGN(prep->max_width, alignment);
	RK_S32 aligned_h = MPP_ALIGN(prep->max_height, alignment);
	RK_U32 total_wrap_size;
	WrapInfo *body = &ctx->wrap_infos.body;
	WrapInfo *hdr = &ctx->wrap_infos.hdr;

	body->size = REF_BODY_SIZE(aligned_w, aligned_h);
	body->total_size = body->size + REF_WRAP_BODY_EXT_SIZE(aligned_w, aligned_h);
	hdr->size = REF_HEADER_SIZE(aligned_w, aligned_h);
	hdr->total_size = hdr->size + REF_WRAP_HEADER_EXT_SIZE(aligned_w, aligned_h);
	total_wrap_size = body->total_size + hdr->total_size;

	if (max_lt_cnt > 0) {
		WrapInfo *body_lt = &ctx->wrap_infos.body_lt;
		WrapInfo *hdr_lt = &ctx->wrap_infos.hdr_lt;

		body_lt->size = body->size;
		body_lt->total_size = body->total_size;

		hdr_lt->size = hdr->size;
		hdr_lt->total_size = hdr->total_size;
		if (ctx->only_smartp) {
			body_lt->total_size = body->size;
			hdr_lt->total_size =  hdr->size;
		}

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

static void setup_recn_refr_wrap(H265eV540cHalContext *ctx, hevc_vepu540c_base *regs,
				 HalEncTask *task)
{
	MppDev dev = ctx->dev;
	RK_U32 recn_ref_wrap = ctx->recn_ref_wrap;
	H265eSyntax_new *syn = (H265eSyntax_new *)task->syntax.data;
	RK_U32 cur_is_non_ref = syn->sp.non_reference_flag;
	RK_U32 cur_is_lt = syn->sp.recon_pic.is_lt;
	RK_U32 refr_is_lt = syn->sp.ref_pic.is_lt;
	WrapInfo *bdy_lt = &ctx->wrap_infos.body_lt;
	WrapInfo *hdr_lt = &ctx->wrap_infos.hdr_lt;
	WrapInfo *bdy = &ctx->wrap_infos.body;
	WrapInfo *hdr = &ctx->wrap_infos.hdr;
	RK_U32 ref_iova;
	RK_U32 rfpw_h_off;
	RK_U32 rfpw_b_off;
	RK_U32 rfpr_h_off;
	RK_U32 rfpr_b_off;
	RK_U32 rfp_h_bot;
	RK_U32 rfp_b_bot;
	RK_U32 rfp_h_top;
	RK_U32 rfp_b_top;

	if (recn_ref_wrap)
		ref_iova = mpp_dev_get_iova_address(dev, ctx->recn_ref_buf, 163);

	if (ctx->frame_type == INTRA_FRAME &&
	    syn->sp.recon_pic.slot_idx == syn->sp.ref_pic.slot_idx) {

		hal_h265e_dbg_wrap("cur is idr  lt %d\n", cur_is_lt);
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

		hal_h265e_dbg_wrap("ref type %d\n", type);
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
			if (ctx->only_smartp) {
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
			} else
				mpp_err("WARNING: not support lt ref to lt when buf is wrap");
		} break;
		default: {
		} break;
		}
	}


	regs->reg0163_rfpw_h_addr = ref_iova + rfpw_h_off;
	regs->reg0164_rfpw_b_addr = ref_iova + rfpw_b_off;

	regs->reg0165_rfpr_h_addr = ref_iova + rfpr_h_off;
	regs->reg0166_rfpr_b_addr = ref_iova + rfpr_b_off;

	regs->reg0180_adr_rfpt_h = ref_iova + rfp_h_top;
	regs->reg0181_adr_rfpb_h = ref_iova + rfp_h_bot;
	regs->reg0182_adr_rfpt_b = ref_iova + rfp_b_top;
	regs->reg0183_adr_rfpb_b = ref_iova + rfp_b_bot;
	regs->reg0192_enc_pic.cur_frm_ref = !cur_is_non_ref;

	if (recn_ref_wrap) {
		RK_U32 cur_hdr_off;
		RK_U32 cur_bdy_off;

		hal_h265e_dbg_wrap("cur_is_ref %d\n", !cur_is_non_ref);
		hal_h265e_dbg_wrap("hdr[size %d top %d bot %d cur %d pre %d]\n",
				   hdr->size, hdr->top, hdr->bottom, hdr->cur_off, hdr->pre_off);
		hal_h265e_dbg_wrap("bdy [size %d top %d bot %d cur %d pre %d]\n",
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

static MPP_RET vepu540c_h265_setup_hal_bufs(H265eV540cHalContext *ctx)
{
	MPP_RET ret = MPP_OK;
	VepuFmtCfg *fmt = (VepuFmtCfg *) ctx->input_fmt;
	RK_U32 frame_size;
	Vepu541Fmt input_fmt = VEPU541_FMT_YUV420P;
	RK_S32 mb_wd64, mb_h64;
	MppEncRefCfg ref_cfg = ctx->cfg->ref_cfg;
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	RK_S32 old_max_cnt = ctx->max_buf_cnt;
	RK_S32 new_max_cnt = 2;
	RK_S32 max_lt_cnt;
	RK_S32 smera_size = 0;
	RK_S32 smera_r_size = 0;
	RK_S32 aligned_w = MPP_ALIGN(prep->max_width, 64);
	hal_h265e_enter();

	mb_wd64 = (prep->max_width + 63) / 64;
	mb_h64 = (prep->max_height + 63) / 64;

	frame_size = MPP_ALIGN(prep->max_width, 16) * MPP_ALIGN(prep->max_height, 16);

	vepu541_set_fmt(fmt, ctx->cfg->prep.format);

	input_fmt = (Vepu541Fmt) fmt->format;
	switch (input_fmt) {
	case VEPU541_FMT_YUV420P:
	case VEPU541_FMT_YUV420SP: {
		frame_size = frame_size * 3 / 2;
	}
	break;
	case VEPU541_FMT_YUV422P:
	case VEPU541_FMT_YUV422SP:
	case VEPU541_FMT_YUYV422:
	case VEPU541_FMT_UYVY422:
	case VEPU541_FMT_BGR565: {
		frame_size *= 2;
	}
	break;
	case VEPU540C_FMT_YUV444P:
	case VEPU540C_FMT_YUV444SP:
	case VEPU541_FMT_BGR888: {
		frame_size *= 3;
	}
	break;
	case VEPU541_FMT_BGRA8888: {
		frame_size *= 4;
	}
	break;
	default: {
		hal_h265e_err("invalid src color space: %d\n",
			      input_fmt);
		return MPP_NOK;
	}
	}

	if (ref_cfg) {
		MppEncCpbInfo *info = mpp_enc_ref_cfg_get_cpb_info(ref_cfg);
		new_max_cnt = MPP_MAX(new_max_cnt, info->dpb_size + 1);
		max_lt_cnt = info->max_lt_cnt;
	}

	if (1) {
		smera_size = MPP_ALIGN(prep->max_width, 512) / 512 * MPP_ALIGN(prep->max_height, 32) / 32 * 16;
		smera_r_size = MPP_ALIGN(prep->max_height, 512) / 512 * MPP_ALIGN(prep->max_width, 32) / 32 * 16;

	} else {
		smera_size = MPP_ALIGN(prep->max_width, 256) / 256 * MPP_ALIGN(prep->max_height, 32) / 32;
		smera_r_size = MPP_ALIGN(prep->max_height, 256) / 256 * MPP_ALIGN(prep->max_width, 32) / 32;
	}

	smera_size = MPP_MAX(smera_size, smera_r_size);

	if (aligned_w > 3 * SZ_1K) {
		/* 480 bytes for each ctu above 3072 */

		RK_S32 ext_line_buf_size = (aligned_w / 32 - 91) * 26 * 16;

		if (!ctx->shared_buf->ext_line_buf ) {
			if (ext_line_buf_size != ctx->ext_line_buf_size) {
				mpp_buffer_put(ctx->ext_line_buf);
				ctx->ext_line_buf = NULL;
			}

			if (NULL == ctx->ext_line_buf)
				mpp_buffer_get(NULL, &ctx->ext_line_buf,
					       ext_line_buf_size);
		} else
			ctx->ext_line_buf = ctx->shared_buf->ext_line_buf;

		ctx->ext_line_buf_size = ext_line_buf_size;
	} else {
		if (ctx->ext_line_buf && !ctx->shared_buf->ext_line_buf) {
			mpp_buffer_put(ctx->ext_line_buf);
			ctx->ext_line_buf = NULL;
		}
		ctx->ext_line_buf_size = 0;
	}

	if ((frame_size > ctx->frame_size) ||
	    (new_max_cnt > old_max_cnt) ||
	    (smera_size > ctx->smera_size)) {

		if (!ctx->shared_buf->dpb_bufs) {
			if (ctx->dpb_bufs)
				hal_bufs_deinit(ctx->dpb_bufs);
			hal_bufs_init(&ctx->dpb_bufs);
		}
		new_max_cnt = MPP_MAX(new_max_cnt, old_max_cnt);
		if (ctx->recn_ref_wrap) {
			size_t size[4] = { 0 };

			size[THUMB_TYPE] = (mb_wd64 * mb_h64 << 8);

			if (ctx->cfg->codec.h265.tmvp_enable)
				size[CMV_TYPE] = MPP_ALIGN(mb_wd64 * mb_h64 * 4, 256) * 16;

			size[SMEAR_TYPE] = smera_size;
			hal_h265e_dbg_detail("frame size %d -> %d max count %d -> %d\n",
					     ctx->frame_size, frame_size, old_max_cnt,
					     new_max_cnt);
			get_wrap_buf(ctx, max_lt_cnt);

			if (!ctx->shared_buf->dpb_bufs)
				hal_bufs_setup(ctx->dpb_bufs, new_max_cnt, MPP_ARRAY_ELEMS(size), size);
		} else {
			size_t size[4] = { 0 };

			ctx->fbc_header_len =
				MPP_ALIGN(((mb_wd64 * mb_h64) << 6), SZ_8K);
			size[THUMB_TYPE] = (mb_wd64 * mb_h64 << 8);

			if (ctx->cfg->codec.h265.tmvp_enable)
				size[CMV_TYPE] = MPP_ALIGN(mb_wd64 * mb_h64 * 4, 256) * 16;

			size[SMEAR_TYPE] = smera_size;
			size[RECREF_TYPE] = ctx->fbc_header_len + ((mb_wd64 * mb_h64) << 12) * 3 / 2;//fbc_h + fbc_b

			hal_h265e_dbg_detail("frame size %d -> %d max count %d -> %d\n",
					     ctx->frame_size, frame_size, old_max_cnt,
					     new_max_cnt);

			if (!ctx->shared_buf->dpb_bufs)
				hal_bufs_setup(ctx->dpb_bufs, new_max_cnt, MPP_ARRAY_ELEMS(size), size);
		}

		if (ctx->shared_buf->dpb_bufs)
			ctx->dpb_bufs = ctx->shared_buf->dpb_bufs;

		ctx->frame_size = frame_size;
		ctx->max_buf_cnt = new_max_cnt;
		ctx->smera_size = smera_size;
	}
	hal_h265e_leave();
	return ret;
}

static void setup_vepu540c_hevc_scl_cfg(vepu540c_scl_cfg *regs, HalEncTask *task)
{
	H265eSyntax_new *syn = (H265eSyntax_new *) task->syntax.data;
	hal_h265e_dbg_func("enter\n");
	if (syn->pp.scaling_list_mode < 2)
		memcpy(&regs->q_dc_y16, vepu540c_h265_jvt_scl_tab, sizeof(vepu540c_h265_jvt_scl_tab));
	else
		memcpy(&regs->q_dc_y16, vepu540c_h265_customer_scl_tab, sizeof(vepu540c_h265_customer_scl_tab));

	hal_h265e_dbg_func("leave\n");
}

static void vepu540c_h265_rdo_cfg(H265eV540cHalContext *ctx, vepu540c_rdo_cfg *reg,
				  HalEncTask *task)
{
	rdo_skip_par *p_rdo_skip = NULL;
	rdo_noskip_par *p_rdo_noskip = NULL;
	pre_cst_par *p_pre_cst = NULL;
	RK_S32 delta_qp = 0;
	RK_U32 *smear_cnt = ctx->smear_cnt;
	RK_U32 mb_cnt = ctx->mb_num;
	VepuPpInfo *ppinfo = (VepuPpInfo *)mpp_frame_get_ppinfo(task->frame);

	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		reg->rdo_segment_cfg.rdo_segment_multi = 28;
		reg->rdo_segment_cfg.rdo_segment_en = 1;
		reg->rdo_smear_cfg_comb.rdo_smear_en = 1;
		reg->rdo_smear_cfg_comb.rdo_smear_lvl16_multi = 9;
		reg->rdo_segment_cfg.rdo_smear_lvl8_multi = 8;
		reg->rdo_segment_cfg.rdo_smear_lvl4_multi = 8;
		if (ctx->smart_en) {
			reg->rdo_segment_cfg.rdo_segment_multi = 18;
			reg->rdo_smear_cfg_comb.rdo_smear_lvl16_multi = 16;
			reg->rdo_segment_cfg.rdo_smear_lvl8_multi = 16;
			reg->rdo_segment_cfg.rdo_smear_lvl4_multi = 16;
		}
	} else {
		reg->rdo_segment_cfg.rdo_segment_multi = 16;
		reg->rdo_segment_cfg.rdo_segment_en = 0;
		reg->rdo_smear_cfg_comb.rdo_smear_en = 0;
		reg->rdo_smear_cfg_comb.rdo_smear_lvl16_multi = 16;
		reg->rdo_segment_cfg.rdo_smear_lvl8_multi = 16;
		reg->rdo_segment_cfg.rdo_smear_lvl4_multi = 16;
	}

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
	if (INTRA_FRAME == ctx->frame_type)
		reg->rdo_smear_cfg_comb.stated_mode = 1;
	else if (INTRA_FRAME == ctx->last_frame_type)
		reg->rdo_smear_cfg_comb.stated_mode = 1;
	else
		reg->rdo_smear_cfg_comb.stated_mode = 2;

	reg->weightp_cfg_comb.cime_ds_data_chg_for_wp = 0;
	reg->weightp_cfg_comb.weightp_en = 1;
	reg->weightp_cfg_comb.weightp_y = 0;
	reg->weightp_cfg_comb.weightp_c = 0;
	reg->weightp_cfg_comb.log2weight_denom_y = 0;
	reg->whightp_input_comb.input_weight_y = 1;
	reg->whightp_input_comb.input_weight_u = 1;
	reg->whightp_input_comb.input_weight_v = 1;
	reg->whightp_inoffset_comb.input_offset_y = 0;
	reg->whightp_inoffset_comb.input_offset_u = 0;
	reg->whightp_inoffset_comb.input_offset_v = 0;

	if (!ppinfo) {
		reg->rdo_smear_cfg_comb.smear_stride = 0;
		reg->rdo_smear_cfg_comb.online_en = 1;
	} else {
		if (0 /*ppinfo->smrw_buf*/) {
			RK_S32 pic_wd8_m1 = (ctx->cfg->prep.width + 31) / 32 * 32 / 8 - 1;
			reg->rdo_smear_cfg_comb.smear_stride = ((pic_wd8_m1 + 4) / 4 + 7) / 8 * 16;
			reg->rdo_smear_cfg_comb.online_en = 0;
		} else {
			reg->rdo_smear_cfg_comb.smear_stride = 0;
			reg->rdo_smear_cfg_comb.online_en = 1;
		}

		if (ppinfo->wp_out_par_y & 0x1) {
			reg->weightp_cfg_comb.cime_ds_data_chg_for_wp = 1;
			reg->weightp_cfg_comb.weightp_en = 1;
			reg->weightp_cfg_comb.weightp_y = 1;
			reg->weightp_cfg_comb.weightp_c = 0;
			reg->weightp_cfg_comb.log2weight_denom_y = (ppinfo->wp_out_par_y >> 1) & 0x7;
			reg->whightp_input_comb.input_weight_y = (ppinfo->wp_out_par_y >> 4) & 0x1FF;
			reg->whightp_inoffset_comb.input_offset_y = (ppinfo->wp_out_par_y >> 16) & 0xFF;
		}
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
	reg->rdo_smear_st_thd0_comb.rdo_smear_resi_th0 = 9;
	reg->rdo_smear_st_thd0_comb.rdo_smear_resi_th1 = 6;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th0 = 1;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th1 = 5;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th2 = 1;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th3 = 3;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th4 = 1;
	reg->rdo_smear_st_thd1_comb.rdo_smear_madp_cnt_th5 = 2;

	p_rdo_skip = &reg->rdo_b32_skip;
	p_rdo_skip->atf_thd0.madp_thd0 = 5;
	p_rdo_skip->atf_thd0.madp_thd1 = 10;
	p_rdo_skip->atf_thd1.madp_thd2 = 15;
	p_rdo_skip->atf_thd1.madp_thd3 = 72;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		p_rdo_skip->atf_wgt0.wgt0 = 20;
		p_rdo_skip->atf_wgt0.wgt1 = 16;
		p_rdo_skip->atf_wgt0.wgt2 = 16;
		p_rdo_skip->atf_wgt0.wgt3 = 16;
		p_rdo_skip->atf_wgt1.wgt4 = 24;
		if (ctx->smart_en) {
			p_rdo_skip->atf_thd0.madp_thd0 = 1;
			p_rdo_skip->atf_thd0.madp_thd1 = 7;
			p_rdo_skip->atf_wgt0.wgt1 = 10;
		}
	} else {
		p_rdo_skip->atf_wgt0.wgt0 = 16;
		p_rdo_skip->atf_wgt0.wgt1 = 16;
		p_rdo_skip->atf_wgt0.wgt2 = 16;
		p_rdo_skip->atf_wgt0.wgt3 = 16;
		p_rdo_skip->atf_wgt1.wgt4 = 16;
	}

	p_rdo_noskip = &reg->rdo_b32_inter;
	p_rdo_noskip->ratf_thd0.madp_thd0 = 20;
	p_rdo_noskip->ratf_thd0.madp_thd1 = 40;
	p_rdo_noskip->ratf_thd1.madp_thd2 = 72;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		p_rdo_noskip->atf_wgt.wgt0 = 16;
		p_rdo_noskip->atf_wgt.wgt1 = 16;
		p_rdo_noskip->atf_wgt.wgt2 = 16;
		p_rdo_noskip->atf_wgt.wgt3 = 24;
	} else {
		p_rdo_noskip->atf_wgt.wgt0 = 16;
		p_rdo_noskip->atf_wgt.wgt1 = 16;
		p_rdo_noskip->atf_wgt.wgt2 = 16;
		p_rdo_noskip->atf_wgt.wgt3 = 16;
	}

	p_rdo_noskip = &reg->rdo_b32_intra;
	p_rdo_noskip->ratf_thd0.madp_thd0 = 20;
	p_rdo_noskip->ratf_thd0.madp_thd1 = 40;
	p_rdo_noskip->ratf_thd1.madp_thd2 = 72;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		p_rdo_noskip->atf_wgt.wgt0 = 27;
		p_rdo_noskip->atf_wgt.wgt1 = 25;
		p_rdo_noskip->atf_wgt.wgt2 = 20;
		p_rdo_noskip->atf_wgt.wgt3 = 24;
	} else {
		p_rdo_noskip->atf_wgt.wgt0 = 16;
		p_rdo_noskip->atf_wgt.wgt1 = 16;
		p_rdo_noskip->atf_wgt.wgt2 = 16;
		p_rdo_noskip->atf_wgt.wgt3 = 16;
	}

	p_rdo_skip = &reg->rdo_b16_skip;
	p_rdo_skip->atf_thd0.madp_thd0 = 1;
	p_rdo_skip->atf_thd0.madp_thd1 = 10;
	p_rdo_skip->atf_thd1.madp_thd2 = 15;
	p_rdo_skip->atf_thd1.madp_thd3 = 25;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		p_rdo_skip->atf_wgt0.wgt0 = 20;
		p_rdo_skip->atf_wgt0.wgt1 = 16;
		p_rdo_skip->atf_wgt0.wgt2 = 16;
		p_rdo_skip->atf_wgt0.wgt3 = 16;
		p_rdo_skip->atf_wgt1.wgt4 = 16;
		if (ctx->smart_en) {
			p_rdo_skip->atf_thd0.madp_thd1 = 7;
			p_rdo_skip->atf_wgt0.wgt1 = 10;
		}
	} else {
		p_rdo_skip->atf_wgt0.wgt0 = 16;
		p_rdo_skip->atf_wgt0.wgt1 = 16;
		p_rdo_skip->atf_wgt0.wgt2 = 16;
		p_rdo_skip->atf_wgt0.wgt3 = 16;
		p_rdo_skip->atf_wgt1.wgt4 = 16;
	}

	p_rdo_noskip = &reg->rdo_b16_inter;
	p_rdo_noskip->ratf_thd0.madp_thd0 = 20;
	p_rdo_noskip->ratf_thd0.madp_thd1 = 40;
	p_rdo_noskip->ratf_thd1.madp_thd2 = 72;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		p_rdo_noskip->atf_wgt.wgt0 = 16;
		p_rdo_noskip->atf_wgt.wgt1 = 16;
		p_rdo_noskip->atf_wgt.wgt2 = 16;
		p_rdo_noskip->atf_wgt.wgt3 = 16;
	} else {
		p_rdo_noskip->atf_wgt.wgt0 = 16;
		p_rdo_noskip->atf_wgt.wgt1 = 16;
		p_rdo_noskip->atf_wgt.wgt2 = 16;
		p_rdo_noskip->atf_wgt.wgt3 = 16;
	}

	p_rdo_noskip = &reg->rdo_b16_intra;
	p_rdo_noskip->ratf_thd0.madp_thd0 = 20;
	p_rdo_noskip->ratf_thd0.madp_thd1 = 40;
	p_rdo_noskip->ratf_thd1.madp_thd2 = 72;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		p_rdo_noskip->atf_wgt.wgt0 = 27;
		p_rdo_noskip->atf_wgt.wgt1 = 25;
		p_rdo_noskip->atf_wgt.wgt2 = 20;
		p_rdo_noskip->atf_wgt.wgt3 = 16;
	} else {
		p_rdo_noskip->atf_wgt.wgt0 = 16;
		p_rdo_noskip->atf_wgt.wgt1 = 16;
		p_rdo_noskip->atf_wgt.wgt2 = 16;
		p_rdo_noskip->atf_wgt.wgt3 = 16;
	}

	reg->rdo_b32_intra_atf_cnt_thd.thd0 = 1;
	reg->rdo_b32_intra_atf_cnt_thd.thd1 = 4;
	reg->rdo_b32_intra_atf_cnt_thd.thd2 = 1;
	reg->rdo_b32_intra_atf_cnt_thd.thd3 = 4;

	reg->rdo_b16_intra_atf_cnt_thd_comb.thd0 = 1;
	reg->rdo_b16_intra_atf_cnt_thd_comb.thd1 = 4;
	reg->rdo_b16_intra_atf_cnt_thd_comb.thd2 = 1;
	reg->rdo_b16_intra_atf_cnt_thd_comb.thd3 = 4;
	reg->rdo_atf_resi_thd_comb.big_th0 = 16;
	reg->rdo_atf_resi_thd_comb.big_th1 = 16;
	reg->rdo_atf_resi_thd_comb.small_th0 = 8;
	reg->rdo_atf_resi_thd_comb.small_th1 = 8;

	p_pre_cst = &reg->preintra32_cst;
	p_pre_cst->cst_madi_thd0.madi_thd0 = 5;
	p_pre_cst->cst_madi_thd0.madi_thd1 = 3;
	p_pre_cst->cst_madi_thd0.madi_thd2 = 3;
	p_pre_cst->cst_madi_thd0.madi_thd3 = 6;
	p_pre_cst->cst_madi_thd1.madi_thd4 = 7;
	p_pre_cst->cst_madi_thd1.madi_thd5 = 10;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC && 0 == ctx->smart_en) {
		p_pre_cst->cst_wgt0.wgt0 = 20;
		p_pre_cst->cst_wgt0.wgt1 = 18;
		p_pre_cst->cst_wgt0.wgt2 = 19;
		p_pre_cst->cst_wgt0.wgt3 = 18;
		p_pre_cst->cst_wgt1.wgt4 = 6;
		p_pre_cst->cst_wgt1.wgt5 = 9;
		p_pre_cst->cst_wgt1.wgt6 = 14;
		p_pre_cst->cst_wgt1.wgt7 = 18;
		p_pre_cst->cst_wgt2.wgt8 = 17;
		p_pre_cst->cst_wgt2.wgt9 = 17;
		p_pre_cst->cst_wgt2.mode_th = 5;
	} else {
		p_pre_cst->cst_wgt0.wgt0 = 17;
		p_pre_cst->cst_wgt0.wgt1 = 17;
		p_pre_cst->cst_wgt0.wgt2 = 17;
		p_pre_cst->cst_wgt0.wgt3 = 17;
		p_pre_cst->cst_wgt1.wgt4 = 13;
		p_pre_cst->cst_wgt1.wgt5 = 14;
		p_pre_cst->cst_wgt1.wgt6 = 15;
		p_pre_cst->cst_wgt1.wgt7 = 17;
		p_pre_cst->cst_wgt2.wgt8 = 17;
		p_pre_cst->cst_wgt2.wgt9 = 17;
		p_pre_cst->cst_wgt2.mode_th = 5;
	}

	p_pre_cst = &reg->preintra16_cst;
	p_pre_cst->cst_madi_thd0.madi_thd0 = 5;
	p_pre_cst->cst_madi_thd0.madi_thd1 = 3;
	p_pre_cst->cst_madi_thd0.madi_thd2 = 3;
	p_pre_cst->cst_madi_thd0.madi_thd3 = 6;
	p_pre_cst->cst_madi_thd1.madi_thd4 = 5;
	p_pre_cst->cst_madi_thd1.madi_thd5 = 7;
	if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC && 0 == ctx->smart_en) {
		p_pre_cst->cst_wgt0.wgt0 = 20;
		p_pre_cst->cst_wgt0.wgt1 = 18;
		p_pre_cst->cst_wgt0.wgt2 = 19;
		p_pre_cst->cst_wgt0.wgt3 = 18;
		p_pre_cst->cst_wgt1.wgt4 = 6;
		p_pre_cst->cst_wgt1.wgt5 = 9;
		p_pre_cst->cst_wgt1.wgt6 = 14;
		p_pre_cst->cst_wgt1.wgt7 = 18;
		p_pre_cst->cst_wgt2.wgt8 = 17;
		p_pre_cst->cst_wgt2.wgt9 = 17;
		p_pre_cst->cst_wgt2.mode_th = 5;
	} else {
		p_pre_cst->cst_wgt0.wgt0 = 17;
		p_pre_cst->cst_wgt0.wgt1 = 17;
		p_pre_cst->cst_wgt0.wgt2 = 17;
		p_pre_cst->cst_wgt0.wgt3 = 17;
		p_pre_cst->cst_wgt1.wgt4 = 13;
		p_pre_cst->cst_wgt1.wgt5 = 14;
		p_pre_cst->cst_wgt1.wgt6 = 15;
		p_pre_cst->cst_wgt1.wgt7 = 17;
		p_pre_cst->cst_wgt2.wgt8 = 17;
		p_pre_cst->cst_wgt2.wgt9 = 17;
		p_pre_cst->cst_wgt2.mode_th = 5;
	}

	reg->preintra_sqi_cfg.pre_intra_qp_thd = 28;
	reg->preintra_sqi_cfg.pre_intra4_lambda_mv_bit = 3;
	reg->preintra_sqi_cfg.pre_intra8_lambda_mv_bit = 4;
	reg->preintra_sqi_cfg.pre_intra16_lambda_mv_bit = 4;
	reg->preintra_sqi_cfg.pre_intra32_lambda_mv_bit = 5;
	reg->rdo_atr_i_cu32_madi_cfg0.i_cu32_madi_thd0 = 3;
	reg->rdo_atr_i_cu32_madi_cfg0.i_cu32_madi_thd1 = 35;
	reg->rdo_atr_i_cu32_madi_cfg0.i_cu32_madi_thd2 = 25;
	reg->rdo_atr_i_cu32_madi_cfg1.i_cu32_madi_cnt_thd3 = 0;
	reg->rdo_atr_i_cu32_madi_cfg1.i_cu32_madi_thd4 = 20;
	reg->rdo_atr_i_cu32_madi_cfg1.i_cu32_madi_cost_multi = 24;
	reg->rdo_atr_i_cu16_madi_cfg0.i_cu16_madi_thd0 = 4;
	reg->rdo_atr_i_cu16_madi_cfg0.i_cu16_madi_thd1 = 6;
	reg->rdo_atr_i_cu16_madi_cfg0.i_cu16_madi_cost_multi = 24;
}

static void vepu540c_h265_global_cfg_set(H265eV540cHalContext *ctx,
					 H265eV540cRegSet *regs,
					 HalEncTask *task)
{
	MppEncHwCfg *hw = &ctx->cfg->hw;
	RK_U32 i;
	hevc_vepu540c_rc_roi *rc_regs = &regs->reg_rc_roi;
	hevc_vepu540c_wgt *reg_wgt = &regs->reg_wgt;
	vepu540c_rdo_cfg *reg_rdo = &regs->reg_rdo;
	vepu540c_h265_rdo_cfg(ctx, reg_rdo, task);
	setup_vepu540c_hevc_scl_cfg(&regs->reg_scl, task);

	if (ctx->frame_type == INTRA_FRAME) {
		RK_U8 *thd = (RK_U8 *) & rc_regs->aq_tthd0;
		RK_S8 *step = (RK_S8 *) & rc_regs->aq_stp0;

		for (i = 0; i < MPP_ARRAY_ELEMS(aq_thd_default); i++) {
			thd[i] = hw->aq_thrd_i[i];
			step[i] = hw->aq_step_i[i] & 0x3f;
		}
		reg_wgt->iprd_lamb_satd_ofst.lambda_satd_offset = 11;
		if (ctx->smart_en)
			memcpy(&reg_wgt->rdo_wgta_qp_grpa_0_51[0], lamd_modb_qp,
			       sizeof(lamd_modb_qp));
		else
			memcpy(&reg_wgt->rdo_wgta_qp_grpa_0_51[0], lamd_moda_qp,
			       sizeof(lamd_moda_qp));
	} else {
		RK_U8 *thd = (RK_U8 *) & rc_regs->aq_tthd0;
		RK_S8 *step = (RK_S8 *) & rc_regs->aq_stp0;
		for (i = 0; i < MPP_ARRAY_ELEMS(aq_thd_default); i++) {
			thd[i] = hw->aq_thrd_p[i];
			step[i] = hw->aq_step_p[i] & 0x3f;
		}
		reg_wgt->iprd_lamb_satd_ofst.lambda_satd_offset = 11;
		if (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC)
			memcpy(&reg_wgt->rdo_wgta_qp_grpa_0_51[0], lamd_modb_qp, sizeof(lamd_modb_qp));
		else
			memcpy(&reg_wgt->rdo_wgta_qp_grpa_0_51[0], lamd_modb_qp_cvr, sizeof(lamd_modb_qp_cvr));
	}
	reg_wgt->reg1484_qnt_bias_comb.qnt_bias_i = 171;
	if (ctx->smart_en) {
		if (ctx->motion_static_switch_en)
			reg_wgt->reg1484_qnt_bias_comb.qnt_bias_i = 85;
		else
			reg_wgt->reg1484_qnt_bias_comb.qnt_bias_i = 128;
	}
	reg_wgt->reg1484_qnt_bias_comb.qnt_bias_p = 85;
	{
		/* 0x1760 */
		regs->reg_wgt.me_sqi_cfg.cime_pmv_num = 1;
		regs->reg_wgt.me_sqi_cfg.cime_fuse = 1;
		regs->reg_wgt.me_sqi_cfg.itp_mode = 0;
		regs->reg_wgt.me_sqi_cfg.move_lambda = 2;
		regs->reg_wgt.me_sqi_cfg.rime_lvl_mrg = 0;
		regs->reg_wgt.me_sqi_cfg.rime_prelvl_en = 3;
		regs->reg_wgt.me_sqi_cfg.rime_prersu_en = 3;

		/* 0x1764 */
		regs->reg_wgt.cime_mvd_th.cime_mvd_th0 = 8;
		regs->reg_wgt.cime_mvd_th.cime_mvd_th1 = 20;
		regs->reg_wgt.cime_mvd_th.cime_mvd_th2 = 32;

		/* 0x1768 */
		regs->reg_wgt.cime_madp_th.cime_madp_th = 16;

		/* 0x176c */
		regs->reg_wgt.cime_multi.cime_multi0 = 8;
		regs->reg_wgt.cime_multi.cime_multi1 = 12;
		regs->reg_wgt.cime_multi.cime_multi2 = 16;
		regs->reg_wgt.cime_multi.cime_multi3 = 20;
	}

	/* RIME && FME */
	{
		/* 0x1770 */
		regs->reg_wgt.rime_mvd_th.rime_mvd_th0 = 1;
		regs->reg_wgt.rime_mvd_th.rime_mvd_th1 = 2;
		regs->reg_wgt.rime_mvd_th.fme_madp_th = 0;

		/* 0x1774 */
		regs->reg_wgt.rime_madp_th.rime_madp_th0 = 8;
		regs->reg_wgt.rime_madp_th.rime_madp_th1 = 16;

		/* 0x1778 */
		regs->reg_wgt.rime_multi.rime_multi0 = 4;
		regs->reg_wgt.rime_multi.rime_multi1 = 8;
		regs->reg_wgt.rime_multi.rime_multi2 = 12;

		/* 0x177C */
		regs->reg_wgt.cmv_st_th.cmv_th0 = 64;
		regs->reg_wgt.cmv_st_th.cmv_th1 = 96;
		regs->reg_wgt.cmv_st_th.cmv_th2 = 128;
	}

	{
		/* 0x1064 */
		regs->reg_rc_roi.madi_st_thd.madi_th0 = 5;
		regs->reg_rc_roi.madi_st_thd.madi_th1 = 12;
		regs->reg_rc_roi.madi_st_thd.madi_th2 = 20;
		/* 0x1068 */
		regs->reg_rc_roi.madp_st_thd0.madp_th0 = 4 << 4;
		regs->reg_rc_roi.madp_st_thd0.madp_th1 = 9 << 4;
		/* 0x106C */
		regs->reg_rc_roi.madp_st_thd1.madp_th2 = 15 << 4;
	}
	if (ctx->cfg->tune.scene_mode != MPP_ENC_SCENE_MODE_IPC) {
		regs->reg_wgt.cime_madp_th.cime_madp_th = 0;
		regs->reg_wgt.rime_madp_th.rime_madp_th0 = 0;
		regs->reg_wgt.rime_madp_th.rime_madp_th1 = 0;
		regs->reg_wgt.cime_multi.cime_multi0 = 4;
		regs->reg_wgt.cime_multi.cime_multi1 = 4;
		regs->reg_wgt.cime_multi.cime_multi2 = 4;
		regs->reg_wgt.cime_multi.cime_multi3 = 4;
		regs->reg_wgt.rime_multi.rime_multi0 = 4;
		regs->reg_wgt.rime_multi.rime_multi1 = 4;
		regs->reg_wgt.rime_multi.rime_multi2 = 4;
	} else if (ctx->smart_en) {
		regs->reg_wgt.cime_multi.cime_multi0 = 4;
		regs->reg_wgt.cime_multi.cime_multi1 = 6;
		regs->reg_wgt.cime_multi.cime_multi2 = 8;
		regs->reg_wgt.cime_multi.cime_multi3 = 12;
		regs->reg_wgt.rime_multi.rime_multi0 = 4;
		regs->reg_wgt.rime_multi.rime_multi1 = 6;
		regs->reg_wgt.rime_multi.rime_multi2 = 8;
	}
}

MPP_RET hal_h265e_v540c_init(void *hal, MppEncHalCfg *cfg)
{
	MPP_RET ret = MPP_OK;
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	RK_U32 i = 0;
	H265eV540cRegSet *regs = NULL;
	// mpp_env_get_u32("hal_h265e_debug", &hal_h265e_debug, 0);
	hal_h265e_enter();

	for (i = 0; i < MAX_TITLE_NUM; i++)
		ctx->reg_out[i] = mpp_calloc(H265eV540cStatusElem, 1);

	ctx->regs = mpp_calloc(H265eV540cRegSet, 1);
	ctx->input_fmt = mpp_calloc(VepuFmtCfg, 1);
	ctx->cfg = cfg->cfg;
	ctx->online = cfg->online;
	ctx->recn_ref_wrap = cfg->ref_buf_shared;
	ctx->shared_buf = cfg->shared_buf;
	ctx->qpmap_en = cfg->qpmap_en;
	ctx->smart_en = cfg->smart_en;
	ctx->motion_static_switch_en = cfg->motion_static_switch_en;
	ctx->only_smartp = cfg->only_smartp;

	//hal_bufs_init(&ctx->dpb_bufs);

	ctx->frame_cnt = 0;
	ctx->frame_cnt_gen_ready = 0;
	ctx->enc_mode = 1;
	cfg->type = VPU_CLIENT_RKVENC;
	ret = mpp_dev_init(&cfg->dev, cfg->type);
	if (ret) {
		mpp_err_f("mpp_dev_init failed. ret: %d\n", ret);
		return ret;
	}
	regs = (H265eV540cRegSet *) ctx->regs;
	ctx->dev = cfg->dev;
	ctx->osd_cfg.reg_base = (void *)&regs->reg_osd_cfg.osd_comb_cfg;
	ctx->osd_cfg.dev = ctx->dev;
	ctx->osd_cfg.osd_data3 = NULL;

	ctx->frame_type = INTRA_FRAME;

	{	/* setup default hardware config */
		MppEncHwCfg *hw = &cfg->cfg->hw;

		hw->qp_delta_row_i = 2;
		hw->qp_delta_row = 2;

		if (ctx->smart_en) {
			memcpy(hw->aq_step_i, aq_qp_dealt_smart, sizeof(hw->aq_step_i));
			memcpy(hw->aq_step_p, aq_qp_dealt_smart, sizeof(hw->aq_step_p));
			memcpy(hw->aq_thrd_i, aq_thd_smart, sizeof(hw->aq_thrd_i));
			memcpy(hw->aq_thrd_p, aq_thd_smart, sizeof(hw->aq_thrd_p));
		} else {
			memcpy(hw->aq_step_i, aq_qp_dealt_default, sizeof(hw->aq_step_i));
			memcpy(hw->aq_step_p, aq_qp_dealt_default, sizeof(hw->aq_step_p));
			memcpy(hw->aq_thrd_i, aq_thd_default, sizeof(hw->aq_thrd_i));
			memcpy(hw->aq_thrd_p, aq_thd_default, sizeof(hw->aq_thrd_p));
		}
	}

	hal_h265e_leave();
	return ret;
}

MPP_RET hal_h265e_v540c_deinit(void *hal)
{
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	RK_U32 i = 0;

	hal_h265e_enter();
	MPP_FREE(ctx->regs);

	for (i = 0; i < MAX_TITLE_NUM; i++)
		MPP_FREE(ctx->reg_out[i]);

	MPP_FREE(ctx->input_fmt);

	if (!ctx->shared_buf->ext_line_buf && ctx->ext_line_buf) {
		mpp_buffer_put(ctx->ext_line_buf);
		ctx->ext_line_buf = NULL;
	}

	if (!ctx->shared_buf->dpb_bufs && ctx->dpb_bufs)
		hal_bufs_deinit(ctx->dpb_bufs);

	if (ctx->hw_tile_buf[0]) {
		mpp_buffer_put(ctx->hw_tile_buf[0]);
		ctx->hw_tile_buf[0] = NULL;
	}

	if (ctx->hw_tile_buf[1]) {
		mpp_buffer_put(ctx->hw_tile_buf[1]);
		ctx->hw_tile_buf[1] = NULL;
	}

	if (!ctx->shared_buf->recn_ref_buf && ctx->recn_ref_buf) {
		mpp_buffer_put(ctx->recn_ref_buf);
		ctx->recn_ref_buf = NULL;
	}

	if (ctx->buf_pass1) {
		mpp_buffer_put(ctx->buf_pass1);
		ctx->buf_pass1 = NULL;
	}

	if (ctx->dev) {
		mpp_dev_deinit(ctx->dev);
		ctx->dev = NULL;
	}
	hal_h265e_leave();
	return MPP_OK;
}

static MPP_RET hal_h265e_vepu540c_prepare(void *hal)
{
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	MppEncPrepCfg *prep = &ctx->cfg->prep;

	hal_h265e_dbg_func("enter %p\n", hal);

	if (prep->change & (MPP_ENC_PREP_CFG_CHANGE_INPUT |
			    MPP_ENC_PREP_CFG_CHANGE_FORMAT)) {
		/*RK_S32 i;

		// pre-alloc required buffers to reduce first frame delay
		vepu540c_h265_setup_hal_bufs(ctx);
		for (i = 0; i < ctx->max_buf_cnt; i++)
			hal_bufs_get_buf(ctx->dpb_bufs, i);*/

		prep->change = 0;
	}

	hal_h265e_dbg_func("leave %p\n", hal);

	return MPP_OK;
}

static void vepu540c_h265_set_ext_line_buf(H265eV540cRegSet *regs,
					   H265eV540cHalContext *ctx)
{
	if (ctx->ext_line_buf) {
		regs->reg_base.reg0179_adr_ebufb =
			mpp_dev_get_iova_address(ctx->dev, ctx->ext_line_buf, 179);

		regs->reg_base.reg0178_adr_ebuft =
			regs->reg_base.reg0179_adr_ebufb + ctx->ext_line_buf_size;

	} else {
		regs->reg_base.reg0178_adr_ebuft = 0;
		regs->reg_base.reg0179_adr_ebufb = 0;
	}
}


static MPP_RET
vepu540c_h265_uv_address(hevc_vepu540c_base *reg_base, H265eSyntax_new *syn,
			 Vepu541Fmt input_fmt, HalEncTask *task)
{
	RK_U32 hor_stride = syn->pp.hor_stride;
	RK_U32 ver_stride =
		syn->pp.ver_stride ? syn->pp.ver_stride : syn->pp.pic_height;
	RK_U32 frame_size = hor_stride * ver_stride;
	RK_U32 u_offset = 0, v_offset = 0;
	MPP_RET ret = MPP_OK;

	if (MPP_FRAME_FMT_IS_FBC(mpp_frame_get_fmt(task->frame))) {
		u_offset = mpp_frame_get_fbc_offset(task->frame);
		v_offset = 0;
	} else {
		switch (input_fmt) {
		case VEPU541_FMT_YUV420P: {
			u_offset = frame_size;
			v_offset = frame_size * 5 / 4;
		}
		break;
		case VEPU541_FMT_YUV420SP:
		case VEPU541_FMT_YUV422SP: {
			u_offset = frame_size;
			v_offset = frame_size;
		}
		break;
		case VEPU541_FMT_YUV422P: {
			u_offset = frame_size;
			v_offset = frame_size * 3 / 2;
		}
		break;
		case VEPU541_FMT_YUYV422:
		case VEPU541_FMT_UYVY422: {
			u_offset = 0;
			v_offset = 0;
		}
		break;
		case VEPU540C_FMT_YUV444SP : {
			u_offset = frame_size;
			v_offset = frame_size;
		} break;
		case VEPU540C_FMT_YUV444P : {
			u_offset = frame_size;
			v_offset = frame_size * 2;
		} break;
		case VEPU541_FMT_BGR565:
		case VEPU541_FMT_BGR888:
		case VEPU541_FMT_BGRA8888: {
			u_offset = 0;
			v_offset = 0;
		}
		break;
		default: {
			hal_h265e_err("unknown color space: %d\n",
				      input_fmt);
			u_offset = frame_size;
			v_offset = frame_size * 5 / 4;
		}
		}
	}

	reg_base->reg0161_adr_src1 += u_offset;
	reg_base->reg0162_adr_src2 += v_offset;
	return ret;
}


static MPP_RET vepu540c_h265_set_rc_regs(H265eV540cHalContext *ctx,
					 H265eV540cRegSet *regs,
					 HalEncTask *task)
{
	H265eSyntax_new *syn = (H265eSyntax_new *) task->syntax.data;
	EncRcTaskInfo *rc_cfg = &task->rc_task->info;
	hevc_vepu540c_base *reg_base = &regs->reg_base;
	hevc_vepu540c_rc_roi *reg_rc = &regs->reg_rc_roi;
	MppEncCfgSet *cfg = ctx->cfg;
	MppEncRcCfg *rc = &cfg->rc;
	MppEncHwCfg *hw = &cfg->hw;
	MppEncCodecCfg *codec = &cfg->codec;
	MppEncH265Cfg *h265 = &codec->h265;
	RK_S32 mb_wd32 = (syn->pp.pic_width + 31) / 32;
	RK_S32 mb_h32 = (syn->pp.pic_height + 31) / 32;

	RK_U32 ctu_target_bits_mul_16 =
		(rc_cfg->bit_target << 4) / (mb_wd32 * mb_h32);
	RK_U32 ctu_target_bits;
	RK_S32 negative_bits_thd, positive_bits_thd;

	if (rc->rc_mode == MPP_ENC_RC_MODE_FIXQP) {
		reg_base->reg0192_enc_pic.pic_qp = rc_cfg->quality_target;
		reg_base->reg0240_synt_sli1.sli_qp = rc_cfg->quality_target;

		reg_base->reg213_rc_qp.rc_max_qp = rc_cfg->quality_target;
		reg_base->reg213_rc_qp.rc_min_qp = rc_cfg->quality_target;
	} else {
		if (ctu_target_bits_mul_16 >= 0x100000)
			ctu_target_bits_mul_16 = 0x50000;
		ctu_target_bits = (ctu_target_bits_mul_16 * mb_wd32) >> 4;
		negative_bits_thd = 0 - 5 * ctu_target_bits / 16;
		positive_bits_thd = 5 * ctu_target_bits / 16;

		reg_base->reg0192_enc_pic.pic_qp = rc_cfg->quality_target;
		reg_base->reg0240_synt_sli1.sli_qp = rc_cfg->quality_target;
		reg_base->reg212_rc_cfg.rc_en = 1;
		reg_base->reg212_rc_cfg.aq_en = 1;
		reg_base->reg212_rc_cfg.aq_mode = 1;
		reg_base->reg212_rc_cfg.rc_ctu_num = mb_wd32;
		reg_base->reg213_rc_qp.rc_qp_range =
			(ctx->frame_type ==
			 INTRA_FRAME) ? hw->qp_delta_row_i : hw->qp_delta_row;
		if (ctx->smart_en)
			reg_base->reg213_rc_qp.rc_qp_range = 0;
		reg_base->reg213_rc_qp.rc_max_qp = rc_cfg->quality_max;
		reg_base->reg213_rc_qp.rc_min_qp = rc_cfg->quality_min;
		reg_base->reg214_rc_tgt.ctu_ebit = ctu_target_bits_mul_16;

		reg_rc->rc_dthd_0_8[0] = 4 * negative_bits_thd;
		reg_rc->rc_dthd_0_8[1] = negative_bits_thd;
		reg_rc->rc_dthd_0_8[2] = positive_bits_thd;
		reg_rc->rc_dthd_0_8[3] = 4 * positive_bits_thd;
		reg_rc->rc_dthd_0_8[4] = 0x7FFFFFFF;
		reg_rc->rc_dthd_0_8[5] = 0x7FFFFFFF;
		reg_rc->rc_dthd_0_8[6] = 0x7FFFFFFF;
		reg_rc->rc_dthd_0_8[7] = 0x7FFFFFFF;
		reg_rc->rc_dthd_0_8[8] = 0x7FFFFFFF;

		reg_rc->rc_adj0.qp_adj0 = -2;
		reg_rc->rc_adj0.qp_adj1 = -1;
		reg_rc->rc_adj0.qp_adj2 = 0;
		reg_rc->rc_adj0.qp_adj3 = 1;
		reg_rc->rc_adj0.qp_adj4 = 2;
		reg_rc->rc_adj1.qp_adj5 = 0;
		reg_rc->rc_adj1.qp_adj6 = 0;
		reg_rc->rc_adj1.qp_adj7 = 0;
		reg_rc->rc_adj1.qp_adj8 = 0;

		reg_rc->roi_qthd0.qpmin_area0 =
			h265->qpmin_map[0] >
			0 ? h265->qpmin_map[0] : rc_cfg->quality_min;
		reg_rc->roi_qthd0.qpmax_area0 =
			h265->qpmax_map[0] >
			0 ? h265->qpmax_map[0] : rc_cfg->quality_max;
		reg_rc->roi_qthd0.qpmin_area1 =
			h265->qpmin_map[1] >
			0 ? h265->qpmin_map[1] : rc_cfg->quality_min;
		reg_rc->roi_qthd0.qpmax_area1 =
			h265->qpmax_map[1] >
			0 ? h265->qpmax_map[1] : rc_cfg->quality_max;
		reg_rc->roi_qthd0.qpmin_area2 =
			h265->qpmin_map[2] >
			0 ? h265->qpmin_map[2] : rc_cfg->quality_min;;
		reg_rc->roi_qthd1.qpmax_area2 =
			h265->qpmax_map[2] >
			0 ? h265->qpmax_map[2] : rc_cfg->quality_max;
		reg_rc->roi_qthd1.qpmin_area3 =
			h265->qpmin_map[3] >
			0 ? h265->qpmin_map[3] : rc_cfg->quality_min;;
		reg_rc->roi_qthd1.qpmax_area3 =
			h265->qpmax_map[3] >
			0 ? h265->qpmax_map[3] : rc_cfg->quality_max;
		reg_rc->roi_qthd1.qpmin_area4 =
			h265->qpmin_map[4] >
			0 ? h265->qpmin_map[4] : rc_cfg->quality_min;;
		reg_rc->roi_qthd1.qpmax_area4 =
			h265->qpmax_map[4] >
			0 ? h265->qpmax_map[4] : rc_cfg->quality_max;
		reg_rc->roi_qthd2.qpmin_area5 =
			h265->qpmin_map[5] >
			0 ? h265->qpmin_map[5] : rc_cfg->quality_min;;
		reg_rc->roi_qthd2.qpmax_area5 =
			h265->qpmax_map[5] >
			0 ? h265->qpmax_map[5] : rc_cfg->quality_max;
		reg_rc->roi_qthd2.qpmin_area6 =
			h265->qpmin_map[6] >
			0 ? h265->qpmin_map[6] : rc_cfg->quality_min;;
		reg_rc->roi_qthd2.qpmax_area6 =
			h265->qpmax_map[6] >
			0 ? h265->qpmax_map[6] : rc_cfg->quality_max;
		reg_rc->roi_qthd2.qpmin_area7 =
			h265->qpmin_map[7] >
			0 ? h265->qpmin_map[7] : rc_cfg->quality_min;;
		reg_rc->roi_qthd3.qpmax_area7 =
			h265->qpmax_map[7] >
			0 ? h265->qpmax_map[7] : rc_cfg->quality_max;
		reg_rc->roi_qthd3.qpmap_mode = h265->qpmap_mode;
	}
	return MPP_OK;
}

static MPP_RET vepu540c_h265_set_pp_regs(H265eV540cRegSet *regs,
					 VepuFmtCfg *fmt,
					 MppEncPrepCfg *prep_cfg)
{
	hevc_vepu540c_control_cfg *reg_ctl = &regs->reg_ctl;
	hevc_vepu540c_base *reg_base = &regs->reg_base;
	RK_S32 stridey = 0;
	RK_S32 stridec = 0;

	reg_ctl->reg0012_dtrns_map.src_bus_edin = fmt->src_endian;
	reg_base->reg0198_src_fmt.src_cfmt = fmt->format;
	reg_base->reg0198_src_fmt.alpha_swap = fmt->alpha_swap;
	reg_base->reg0198_src_fmt.rbuv_swap = fmt->rbuv_swap;
	reg_base->reg0198_src_fmt.out_fmt = 1;

	reg_base->reg0203_src_proc.src_mirr = prep_cfg->mirroring > 0;
	reg_base->reg0203_src_proc.src_rot = prep_cfg->rotation;

	if (prep_cfg->hor_stride)
		stridey = prep_cfg->hor_stride;
	else {
		if (fmt->format == VEPU541_FMT_BGRA8888 )
			stridey = prep_cfg->width * 4;
		else if (fmt->format == VEPU541_FMT_BGR888 )
			stridey = prep_cfg->width * 3;
		else if (fmt->format == VEPU541_FMT_BGR565 ||
			 fmt->format == VEPU541_FMT_YUYV422 ||
			 fmt->format == VEPU541_FMT_UYVY422)
			stridey = prep_cfg->width * 2;
		else
			stridey = prep_cfg->width;
	}

	switch (fmt->format) {
	case VEPU540C_FMT_YUV444SP : {
		stridec = stridey * 2;
	} break;
	case VEPU541_FMT_YUV422SP :
	case VEPU541_FMT_YUV420SP :
	case VEPU540C_FMT_YUV444P : {
		stridec = stridey;
	} break;
	default : {
		stridec = stridey / 2;
	} break;
	}


	if (reg_base->reg0198_src_fmt.src_cfmt < VEPU541_FMT_ARGB1555) {
		reg_base->reg0199_src_udfy.csc_wgt_r2y = 77;
		reg_base->reg0199_src_udfy.csc_wgt_g2y = 150;
		reg_base->reg0199_src_udfy.csc_wgt_b2y = 29;

		reg_base->reg0200_src_udfu.csc_wgt_r2u = -43;
		reg_base->reg0200_src_udfu.csc_wgt_g2u = -85;
		reg_base->reg0200_src_udfu.csc_wgt_b2u = 128;

		reg_base->reg0201_src_udfv.csc_wgt_r2v = 128;
		reg_base->reg0201_src_udfv.csc_wgt_g2v = -107;
		reg_base->reg0201_src_udfv.csc_wgt_b2v = -21;

		reg_base->reg0202_src_udfo.csc_ofst_y = 0;
		reg_base->reg0202_src_udfo.csc_ofst_u = 128;
		reg_base->reg0202_src_udfo.csc_ofst_v = 128;
	}

	reg_base->reg0205_src_strd0.src_strd0 = stridey;
	reg_base->reg0206_src_strd1.src_strd1 = stridec;

	return MPP_OK;
}

static void vepu540c_h265_set_slice_regs(H265eSyntax_new *syn,
					 hevc_vepu540c_base *regs)
{
	regs->reg0237_synt_sps.smpl_adpt_ofst_e =
		syn->pp.sample_adaptive_offset_enabled_flag;
	regs->reg0237_synt_sps.num_st_ref_pic =
		syn->pp.num_short_term_ref_pic_sets;
	regs->reg0237_synt_sps.num_lt_ref_pic =
		syn->pp.num_long_term_ref_pics_sps;
	regs->reg0237_synt_sps.lt_ref_pic_prsnt =
		syn->pp.long_term_ref_pics_present_flag;
	regs->reg0237_synt_sps.tmpl_mvp_e =
		syn->pp.sps_temporal_mvp_enabled_flag;
	regs->reg0237_synt_sps.log2_max_poc_lsb =
		syn->pp.log2_max_pic_order_cnt_lsb_minus4;
	regs->reg0237_synt_sps.strg_intra_smth =
		syn->pp.strong_intra_smoothing_enabled_flag;

	regs->reg0238_synt_pps.dpdnt_sli_seg_en =
		syn->pp.dependent_slice_segments_enabled_flag;
	regs->reg0238_synt_pps.out_flg_prsnt_flg =
		syn->pp.output_flag_present_flag;
	regs->reg0238_synt_pps.num_extr_sli_hdr =
		syn->pp.num_extra_slice_header_bits;
	regs->reg0238_synt_pps.sgn_dat_hid_en =
		syn->pp.sign_data_hiding_enabled_flag;
	regs->reg0238_synt_pps.cbc_init_prsnt_flg =
		syn->pp.cabac_init_present_flag;
	regs->reg0238_synt_pps.pic_init_qp = syn->pp.init_qp_minus26 + 26;
	regs->reg0238_synt_pps.cu_qp_dlt_en = syn->pp.cu_qp_delta_enabled_flag;
	regs->reg0238_synt_pps.chrm_qp_ofst_prsn =
		syn->pp.pps_slice_chroma_qp_offsets_present_flag;
	regs->reg0238_synt_pps.lp_fltr_acrs_sli =
		syn->pp.pps_loop_filter_across_slices_enabled_flag;
	regs->reg0238_synt_pps.dblk_fltr_ovrd_en =
		syn->pp.deblocking_filter_override_enabled_flag;
	regs->reg0238_synt_pps.lst_mdfy_prsnt_flg =
		syn->pp.lists_modification_present_flag;
	regs->reg0238_synt_pps.sli_seg_hdr_extn =
		syn->pp.slice_segment_header_extension_present_flag;
	regs->reg0238_synt_pps.cu_qp_dlt_depth = syn->pp.diff_cu_qp_delta_depth;
	regs->reg0238_synt_pps.lpf_fltr_acrs_til =
		syn->pp.loop_filter_across_tiles_enabled_flag;

	regs->reg0239_synt_sli0.cbc_init_flg = syn->sp.cbc_init_flg;
	regs->reg0239_synt_sli0.mvd_l1_zero_flg = syn->sp.mvd_l1_zero_flg;
	regs->reg0239_synt_sli0.mrg_up_flg = syn->sp.merge_up_flag;
	regs->reg0239_synt_sli0.mrg_lft_flg = syn->sp.merge_left_flag;
	regs->reg0239_synt_sli0.ref_pic_lst_mdf_l0 = syn->sp.ref_pic_lst_mdf_l0;

	regs->reg0239_synt_sli0.num_refidx_l1_act = syn->sp.num_refidx_l1_act;
	regs->reg0239_synt_sli0.num_refidx_l0_act = syn->sp.num_refidx_l0_act;

	regs->reg0239_synt_sli0.num_refidx_act_ovrd =
		syn->sp.num_refidx_act_ovrd;

	regs->reg0239_synt_sli0.sli_sao_chrm_flg = syn->sp.sli_sao_chrm_flg;
	regs->reg0239_synt_sli0.sli_sao_luma_flg = syn->sp.sli_sao_luma_flg;
	regs->reg0239_synt_sli0.sli_tmprl_mvp_e = syn->sp.sli_tmprl_mvp_en;
	regs->reg0192_enc_pic.num_pic_tot_cur = syn->sp.tot_poc_num;

	regs->reg0239_synt_sli0.pic_out_flg = syn->sp.pic_out_flg;
	regs->reg0239_synt_sli0.sli_type = syn->sp.slice_type;
	regs->reg0239_synt_sli0.sli_rsrv_flg = syn->sp.slice_rsrv_flg;
	regs->reg0239_synt_sli0.dpdnt_sli_seg_flg = syn->sp.dpdnt_sli_seg_flg;
	regs->reg0239_synt_sli0.sli_pps_id = syn->sp.sli_pps_id;
	regs->reg0239_synt_sli0.no_out_pri_pic = syn->sp.no_out_pri_pic;

	regs->reg0240_synt_sli1.sp_tc_ofst_div2 = syn->sp.sli_tc_ofst_div2;;
	regs->reg0240_synt_sli1.sp_beta_ofst_div2 = syn->sp.sli_beta_ofst_div2;
	regs->reg0240_synt_sli1.sli_lp_fltr_acrs_sli =
		syn->sp.sli_lp_fltr_acrs_sli;
	regs->reg0240_synt_sli1.sp_dblk_fltr_dis = syn->sp.sli_dblk_fltr_dis;
	regs->reg0240_synt_sli1.dblk_fltr_ovrd_flg = syn->sp.dblk_fltr_ovrd_flg;
	regs->reg0240_synt_sli1.sli_cb_qp_ofst = syn->pp.pps_slice_chroma_qp_offsets_present_flag ?
						 syn->sp.sli_cb_qp_ofst : syn->pp.pps_cb_qp_offset;
	regs->reg0240_synt_sli1.max_mrg_cnd = syn->sp.max_mrg_cnd;

	regs->reg0240_synt_sli1.col_ref_idx = syn->sp.col_ref_idx;
	regs->reg0240_synt_sli1.col_frm_l0_flg = syn->sp.col_frm_l0_flg;
	regs->reg0241_synt_sli2.sli_poc_lsb = syn->sp.sli_poc_lsb;
	regs->reg0241_synt_sli2.sli_hdr_ext_len = syn->sp.sli_hdr_ext_len;

}

static void vepu540c_h265_set_ref_regs(H265eSyntax_new *syn,
				       hevc_vepu540c_base *regs)
{
	regs->reg0242_synt_refm0.st_ref_pic_flg = syn->sp.st_ref_pic_flg;
	regs->reg0242_synt_refm0.poc_lsb_lt0 = syn->sp.poc_lsb_lt0;
	regs->reg0242_synt_refm0.num_lt_pic = syn->sp.num_lt_pic;

	regs->reg0243_synt_refm1.dlt_poc_msb_prsnt0 =
		syn->sp.dlt_poc_msb_prsnt0;
	regs->reg0243_synt_refm1.dlt_poc_msb_cycl0 = syn->sp.dlt_poc_msb_cycl0;
	regs->reg0243_synt_refm1.used_by_lt_flg0 = syn->sp.used_by_lt_flg0;
	regs->reg0243_synt_refm1.used_by_lt_flg1 = syn->sp.used_by_lt_flg1;
	regs->reg0243_synt_refm1.used_by_lt_flg2 = syn->sp.used_by_lt_flg2;
	regs->reg0243_synt_refm1.dlt_poc_msb_prsnt0 =
		syn->sp.dlt_poc_msb_prsnt0;
	regs->reg0243_synt_refm1.dlt_poc_msb_cycl0 = syn->sp.dlt_poc_msb_cycl0;
	regs->reg0243_synt_refm1.dlt_poc_msb_prsnt1 =
		syn->sp.dlt_poc_msb_prsnt1;
	regs->reg0243_synt_refm1.num_negative_pics = syn->sp.num_neg_pic;
	regs->reg0243_synt_refm1.num_pos_pic = syn->sp.num_pos_pic;

	regs->reg0243_synt_refm1.used_by_s0_flg = syn->sp.used_by_s0_flg;
	regs->reg0244_synt_refm2.dlt_poc_s0_m10 = syn->sp.dlt_poc_s0_m10;
	regs->reg0244_synt_refm2.dlt_poc_s0_m11 = syn->sp.dlt_poc_s0_m11;
	regs->reg0245_synt_refm3.dlt_poc_s0_m12 = syn->sp.dlt_poc_s0_m12;
	regs->reg0245_synt_refm3.dlt_poc_s0_m13 = syn->sp.dlt_poc_s0_m13;

	regs->reg0246_synt_long_refm0.poc_lsb_lt1 = syn->sp.poc_lsb_lt1;
	regs->reg0247_synt_long_refm1.dlt_poc_msb_cycl1 =
		syn->sp.dlt_poc_msb_cycl1;
	regs->reg0246_synt_long_refm0.poc_lsb_lt2 = syn->sp.poc_lsb_lt2;
	regs->reg0243_synt_refm1.dlt_poc_msb_prsnt2 =
		syn->sp.dlt_poc_msb_prsnt2;
	regs->reg0247_synt_long_refm1.dlt_poc_msb_cycl2 =
		syn->sp.dlt_poc_msb_cycl2;
	regs->reg0240_synt_sli1.lst_entry_l0 = syn->sp.lst_entry_l0;
	regs->reg0239_synt_sli0.ref_pic_lst_mdf_l0 = syn->sp.ref_pic_lst_mdf_l0;

	return;
}

static void vepu540c_h265_set_me_regs(H265eV540cHalContext *ctx,
				      H265eSyntax_new *syn,
				      hevc_vepu540c_base *regs)
{

	RK_S32 x_gmv = 0;
	RK_S32 y_gmv = 0;
	RK_S32 srch_lftw, srch_rgtw, srch_uph, srch_dwnh;
	RK_S32 frm_sta = 0, frm_end = 0, pic_w = 0;
	RK_S32 pic_wdt_align =
		((regs->reg0196_enc_rsl.pic_wd8_m1 + 1) * 8 + 31) / 32;

	regs->reg0220_me_rnge.cime_srch_dwnh = 15;
	regs->reg0220_me_rnge.cime_srch_uph = 14;
	regs->reg0220_me_rnge.cime_srch_rgtw = 12;
	regs->reg0220_me_rnge.cime_srch_lftw = 12;
	regs->reg0221_me_cfg.rme_srch_h = 3;
	regs->reg0221_me_cfg.rme_srch_v = 3;

	regs->reg0221_me_cfg.srgn_max_num = 72;
	regs->reg0221_me_cfg.cime_dist_thre = 1024;
	regs->reg0221_me_cfg.rme_dis = 0;
	regs->reg0221_me_cfg.fme_dis = 0;
	regs->reg0220_me_rnge.dlt_frm_num = 0x1;
	srch_lftw = regs->reg0220_me_rnge.cime_srch_lftw * 4;
	srch_rgtw = regs->reg0220_me_rnge.cime_srch_rgtw * 4;
	srch_uph = regs->reg0220_me_rnge.cime_srch_uph * 2;
	srch_dwnh = regs->reg0220_me_rnge.cime_srch_dwnh * 2;

	if (syn->pp.sps_temporal_mvp_enabled_flag &&
	    (ctx->frame_type != INTRA_FRAME)) {
		if (ctx->last_frame_type == INTRA_FRAME)
			regs->reg0222_me_cach.colmv_load = 0;

		else
			regs->reg0222_me_cach.colmv_load = 1;
		regs->reg0222_me_cach.colmv_stor = 1;
	}
	// calc cme_linebuf_w
	{
		if (x_gmv - srch_lftw < 0)
			frm_sta = (x_gmv - srch_lftw - 15) / 16;

		else
			frm_sta = (x_gmv - srch_lftw) / 16;
		if (x_gmv + srch_rgtw < 0)
			frm_end = pic_wdt_align - 1 + (x_gmv + srch_rgtw) / 16;

		else {
			frm_end =
				pic_wdt_align - 1 + (x_gmv + srch_rgtw + 15) / 16;
		}

		if (frm_sta < 0)
			frm_sta = 0;

		else if (frm_sta > pic_wdt_align - 1)
			frm_sta = pic_wdt_align - 1;
		frm_end = mpp_clip(frm_end, 0, pic_wdt_align - 1);
		pic_w = (frm_end - frm_sta + 1) * 32;
		regs->reg0222_me_cach.cme_linebuf_w = pic_w / 32;
	}

	// calc cime_hgt_rama and cime_size_rama
	{
		RK_U32 rama_size = 1796;
		RK_U32 ramb_h;
		RK_U32 ctu_2_h = 4;
		RK_U32 ctu_8_w = 1;
		RK_U32 cur_srch_8_w, cur_srch_2_h, cur_srch_h;

		if ((y_gmv % 8 - srch_uph % 8) < 0) {
			cur_srch_2_h =
				(8 + (y_gmv % 8 - srch_uph % 8) % 8 + srch_uph +
				 srch_dwnh) / 2 + ctu_2_h;
		} else {
			cur_srch_2_h =
				((y_gmv % 8 - srch_uph % 8) % 8 + srch_uph +
				 srch_dwnh) / 2 + ctu_2_h;
		}
		regs->reg0222_me_cach.cime_size_rama =
			(cur_srch_2_h + 3) / 4 * 4;

		if ((x_gmv % 8 - srch_lftw % 8) < 0) {
			cur_srch_8_w =
				(8 + (x_gmv % 8 - srch_lftw % 8) % 8 + srch_lftw +
				 srch_rgtw + 7) / 8 + ctu_8_w;
		} else {
			cur_srch_8_w =
				((x_gmv % 8 - srch_lftw % 8) % 8 + srch_lftw +
				 srch_rgtw + 7) / 8 + ctu_8_w;
		}

		cur_srch_h = ctu_2_h;
		ramb_h = cur_srch_2_h;
		while ((rama_size >
			((cur_srch_h -
			  ctu_2_h) * regs->reg0222_me_cach.cme_linebuf_w +
			 (ramb_h * cur_srch_8_w)))
		       && (cur_srch_h < regs->reg0222_me_cach.cime_size_rama)) {
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
		    ((cur_srch_h -
		      ctu_2_h) * regs->reg0222_me_cach.cme_linebuf_w +
		     (ramb_h * cur_srch_8_w)))
			cur_srch_h = cur_srch_h - ctu_2_h;

		regs->reg0222_me_cach.cime_size_rama =
			((cur_srch_h -
			  ctu_2_h) * regs->reg0222_me_cach.cme_linebuf_w +
			 ctu_2_h * cur_srch_8_w) / 4;
		regs->reg0222_me_cach.cime_hgt_rama = cur_srch_h / 2;
		regs->reg0222_me_cach.fme_prefsu_en = 1;
	}

}

void vepu540c_h265_set_hw_address(H265eV540cHalContext *ctx,
				  hevc_vepu540c_base *regs, HalEncTask *task)
{
	HalEncTask *enc_task = task;
	HalBuf *recon_buf, *ref_buf;
	MppBuffer mv_info_buf = enc_task->mv_info;
	H265eSyntax_new *syn = (H265eSyntax_new *) enc_task->syntax.data;
	VepuFmtCfg *fmt = (VepuFmtCfg *) ctx->input_fmt;
	RK_U32 len = mpp_packet_get_length(task->packet);
	RK_U32 is_phys = mpp_frame_get_is_full(task->frame);
	VepuPpInfo *ppinfo = (VepuPpInfo *)mpp_frame_get_ppinfo(task->frame);

	hal_h265e_enter();

	if (!ctx->online && !is_phys) {
		regs->reg0160_adr_src0 =
			mpp_dev_get_iova_address(ctx->dev, enc_task->input, 160);
		regs->reg0161_adr_src1 = regs->reg0160_adr_src0;
		regs->reg0162_adr_src2 = regs->reg0160_adr_src0;

		vepu540c_h265_uv_address(regs, syn, (Vepu541Fmt) fmt->format, task);
	}

	recon_buf = hal_bufs_get_buf(ctx->dpb_bufs, syn->sp.recon_pic.slot_idx);
	ref_buf = hal_bufs_get_buf(ctx->dpb_bufs, syn->sp.ref_pic.slot_idx);

	if (ctx->recn_ref_wrap)
		setup_recn_refr_wrap(ctx, regs, task);

	else {
		if (!syn->sp.non_reference_flag) {
			regs->reg0163_rfpw_h_addr =
				mpp_dev_get_iova_address(ctx->dev, recon_buf->buf[RECREF_TYPE], 163);
			regs->reg0164_rfpw_b_addr =
				regs->reg0163_rfpw_h_addr + ctx->fbc_header_len;
		}
		regs->reg0165_rfpr_h_addr =
			mpp_dev_get_iova_address(ctx->dev, ref_buf->buf[RECREF_TYPE], 165);
		regs->reg0166_rfpr_b_addr =
			regs->reg0165_rfpr_h_addr + ctx->fbc_header_len;
		regs->reg0180_adr_rfpt_h = 0xffffffff;
		regs->reg0181_adr_rfpb_h = 0;
		regs->reg0182_adr_rfpt_b = 0xffffffff;
		regs->reg0183_adr_rfpb_b = 0;
	}
	regs->reg0185_adr_smr_wr =
		mpp_dev_get_iova_address(ctx->dev, recon_buf->buf[SMEAR_TYPE], 185);
	if (0/*ppinfo && ppinfo->smrw_buf*/)
		regs->reg0184_adr_smr_rd =
			mpp_dev_get_iova_address2(ctx->dev, (struct dma_buf *)ppinfo->smrw_buf, 184);
	else
		regs->reg0184_adr_smr_rd =
			mpp_dev_get_iova_address(ctx->dev, ref_buf->buf[SMEAR_TYPE], 184);


	if (ctx->cfg->codec.h265.tmvp_enable) {
		regs->reg0167_cmvw_addr =
			mpp_dev_get_iova_address(ctx->dev, recon_buf->buf[CMV_TYPE], 167);
		regs->reg0168_cmvr_addr =
			mpp_dev_get_iova_address(ctx->dev, ref_buf->buf[CMV_TYPE], 168);
	}

	regs->reg0169_dspw_addr =
		mpp_dev_get_iova_address(ctx->dev, recon_buf->buf[THUMB_TYPE], 169);
	regs->reg0170_dspr_addr =
		mpp_dev_get_iova_address(ctx->dev, ref_buf->buf[THUMB_TYPE], 170);

	if (syn->pp.tiles_enabled_flag) {
		if (NULL == ctx->hw_tile_buf[0]) {
			mpp_buffer_get(NULL, &ctx->hw_tile_buf[0],
				       TILE_BUF_SIZE);
		}

		if (NULL == ctx->hw_tile_buf[1]) {
			mpp_buffer_get(NULL, &ctx->hw_tile_buf[1],
				       TILE_BUF_SIZE);
		}

		regs->reg0176_lpfw_addr =
			mpp_dev_get_iova_address(ctx->dev, ctx->hw_tile_buf[0], 176);
		regs->reg0177_lpfr_addr =
			mpp_dev_get_iova_address(ctx->dev, ctx->hw_tile_buf[1], 177);
	}

	if (mv_info_buf) {
		regs->reg0192_enc_pic.mei_stor = 1;
		regs->reg0171_meiw_addr =
			mpp_dev_get_iova_address(ctx->dev, mv_info_buf, 171);
	} else {
		regs->reg0192_enc_pic.mei_stor = 0;
		regs->reg0171_meiw_addr = 0;
	}

	if (!enc_task->output->cir_flag) {
		if (enc_task->output->buf) {
			regs->reg0174_bsbs_addr =
				mpp_dev_get_iova_address(ctx->dev, enc_task->output->buf, 174) + enc_task->output->start_offset;
		} else
			regs->reg0174_bsbs_addr = enc_task->output->mpi_buf_id + enc_task->output->start_offset;

		/* TODO: stream size relative with syntax */
		regs->reg0172_bsbt_addr = regs->reg0174_bsbs_addr;
		regs->reg0173_bsbb_addr = regs->reg0174_bsbs_addr;
		regs->reg0175_bsbr_addr = regs->reg0174_bsbs_addr;

		regs->reg0172_bsbt_addr += enc_task->output->size - 1;
		regs->reg0174_bsbs_addr = regs->reg0174_bsbs_addr + len;
	} else {
		RK_U32 size = mpp_buffer_get_size(enc_task->output->buf);
		regs->reg0173_bsbb_addr = mpp_dev_get_iova_address(ctx->dev, enc_task->output->buf, 173);
		regs->reg0174_bsbs_addr = regs->reg0173_bsbb_addr + ((enc_task->output->start_offset + len) % size);
		regs->reg0175_bsbr_addr = regs->reg0173_bsbb_addr + enc_task->output->r_pos;
		regs->reg0172_bsbt_addr = regs->reg0173_bsbb_addr + size;
	}

	if (len && task->output->buf) {
		dma_buf_end_cpu_access_partial(mpp_buffer_get_dma(task->output->buf),
					       DMA_TO_DEVICE, task->output->start_offset, len);
	} else if (len && enc_task->output->mpi_buf_id) {
		struct device *dev = mpp_get_dev(ctx->dev);
		dma_sync_single_for_device(dev, enc_task->output->mpi_buf_id, len, DMA_TO_DEVICE);
	}
	regs->reg0204_pic_ofst.pic_ofst_y = mpp_frame_get_offset_y(task->frame);
	regs->reg0204_pic_ofst.pic_ofst_x = mpp_frame_get_offset_x(task->frame);
}

static MPP_RET vepu540c_h265e_save_pass1_patch(H265eV540cRegSet *regs, H265eV540cHalContext *ctx)
{
	hevc_vepu540c_base *reg_base = &regs->reg_base;
	RK_S32 width_align = MPP_ALIGN(ctx->cfg->prep.width, 32);
	RK_S32 height_align = MPP_ALIGN(ctx->cfg->prep.height, 32);

	if (NULL == ctx->buf_pass1) {
		mpp_buffer_get(NULL, &ctx->buf_pass1, width_align * height_align * 3 / 2);
		if (!ctx->buf_pass1) {
			mpp_err("buf_pass1 malloc fail, debreath invaild");
			return MPP_NOK;
		}
	}

	reg_base->reg0192_enc_pic.cur_frm_ref = 1;
	reg_base->reg0163_rfpw_h_addr = 0;
	reg_base->reg0164_rfpw_b_addr = mpp_dev_get_iova_address(ctx->dev, ctx->buf_pass1, 163);
	reg_base->reg0192_enc_pic.rec_fbc_dis = 1;

	return MPP_OK;
}

static MPP_RET vepu540c_h265e_use_pass1_patch(H265eV540cRegSet *regs, H265eV540cHalContext *ctx,
					      H265eSyntax_new *syn)
{
	hevc_vepu540c_control_cfg *reg_ctl = &regs->reg_ctl;
	hevc_vepu540c_base *reg_base = &regs->reg_base;
	RK_S32 stridey = MPP_ALIGN(syn->pp.pic_width, 32);
	VepuFmtCfg *fmt = (VepuFmtCfg *)ctx->input_fmt;

	reg_ctl->reg0012_dtrns_map.src_bus_edin = fmt->src_endian;
	reg_base->reg0198_src_fmt.src_cfmt = VEPU541_FMT_YUV420SP;
	reg_base->reg0198_src_fmt.src_rcne = 1;
	reg_base->reg0198_src_fmt.out_fmt = 1;
	reg_base->reg0205_src_strd0.src_strd0 = stridey;
	reg_base->reg0206_src_strd1.src_strd1 = 3 * stridey;
	reg_base->reg0160_adr_src0 = mpp_dev_get_iova_address(ctx->dev, ctx->buf_pass1, 160);
	reg_base->reg0161_adr_src1 = reg_base->reg0160_adr_src0 + 2 * stridey;
	reg_base->reg0162_adr_src2 = 0;
	return MPP_OK;
}

#ifdef HW_DVBM
static MPP_RER vepu540c_h265_set_dvbm(H265eV540cRegSet *regs, HalEncTask *task)
{
	RK_U32 soft_resync = 1;
	RK_U32 frame_match = 0;

	regs->reg_ctl.reg0024_dvbm_cfg.dvbm_en = 1;
	regs->reg_ctl.reg0024_dvbm_cfg.src_badr_sel = 0;
	regs->reg_ctl.reg0024_dvbm_cfg.vinf_frm_match = frame_match;
	regs->reg_ctl.reg0024_dvbm_cfg.vrsp_half_cycle = 8;

	regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_sel = soft_resync;
	regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_soft = 1;
	regs->reg_ctl.reg0006_vs_ldly.dvbm_inf_sel = 0;

	regs->reg_base.reg0194_dvbm_id.ch_id = 1;
	regs->reg_base.reg0194_dvbm_id.frame_id = 0;
	regs->reg_base.reg0194_dvbm_id.vrsp_rtn_en = 1;
	vepu540c_set_dvbm(&regs->reg_base.online_addr);
	return MPP_OK;
}
#else
static MPP_RET vepu540c_h265_set_dvbm(H265eV540cRegSet *regs, HalEncTask *task)
{
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct dvbm_addr_cfg dvbm_adr;
	MppFrame frm = task->frame;
	RK_U32 is_full = mpp_frame_get_is_full(frm);

	if (!is_full) {
		rk_dvbm_ctrl(NULL, DVBM_VEPU_GET_ADR, &dvbm_adr);
		regs->reg_ctl.reg0024_dvbm_cfg.dvbm_en = 1;
		regs->reg_ctl.reg0024_dvbm_cfg.src_badr_sel = 1;
		regs->reg_ctl.reg0024_dvbm_cfg.vinf_frm_match = 1;
		regs->reg_ctl.reg0024_dvbm_cfg.vrsp_half_cycle = 8;

		regs->reg_ctl.reg0006_vs_ldly.vswm_lcnt_soft = dvbm_adr.line_cnt;
		regs->reg_ctl.reg0006_vs_ldly.vswm_fcnt_soft = dvbm_adr.frame_id;
		regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_sel = 1;
		regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_soft = 1;
		regs->reg_ctl.reg0006_vs_ldly.dvbm_inf_sel = 1;

		regs->reg_base.reg0194_dvbm_id.ch_id = 1;
		regs->reg_base.reg0194_dvbm_id.frame_id = dvbm_adr.frame_id;
		regs->reg_base.reg0194_dvbm_id.vrsp_rtn_en = 1;

		regs->reg_base.online_addr.reg0156_adr_vsy_t = dvbm_adr.ybuf_top;
		regs->reg_base.online_addr.reg0157_adr_vsc_t = dvbm_adr.cbuf_top;
		regs->reg_base.online_addr.reg0158_adr_vsy_b = dvbm_adr.ybuf_bot;
		regs->reg_base.online_addr.reg0159_adr_vsc_b = dvbm_adr.cbuf_bot;
		regs->reg_base.reg0160_adr_src0 = dvbm_adr.ybuf_sadr;
		regs->reg_base.reg0161_adr_src1 = dvbm_adr.cbuf_sadr;
		regs->reg_base.reg0162_adr_src2 = dvbm_adr.cbuf_sadr;
		if (dvbm_adr.overflow) {
			mpp_err("cur frame already overflow [%d %d]!\n",
				dvbm_adr.frame_id, dvbm_adr.line_cnt);
			return MPP_NOK;
		}
	} else {
		RK_U32 phy_addr = mpp_frame_get_phy_addr(frm);
		//  MppFrameFormat fmt = mpp_frame_get_fmt(frm);
		RK_S32 hor_stride = mpp_frame_get_hor_stride(frm);
		RK_S32 ver_stride = mpp_frame_get_ver_stride(frm);
		RK_U32 off_in[2] = { 0 };
		off_in[0] = hor_stride * ver_stride;
		off_in[1] = hor_stride * ver_stride;
		if (phy_addr) {
			regs->reg_base.reg0160_adr_src0 = phy_addr;
			regs->reg_base.reg0161_adr_src1 = phy_addr + off_in[0];
			regs->reg_base.reg0162_adr_src2 = phy_addr +  off_in[1];

		} else
			mpp_err("online case set full frame err");
	}
#else
	regs->reg_base.online_addr.reg0156_adr_vsy_t = 0;
	regs->reg_base.online_addr.reg0157_adr_vsc_t = 0;
	regs->reg_base.online_addr.reg0158_adr_vsy_b = 0;
	regs->reg_base.online_addr.reg0159_adr_vsc_b = 0;
#endif

	return MPP_OK;
}
#endif

static MPP_RET hal_h265e_v540c_gen_regs(void *hal, HalEncTask *task)
{
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	HalEncTask *enc_task = task;
	H265eSyntax_new *syn = (H265eSyntax_new *) enc_task->syntax.data;
	H265eV540cRegSet *regs = ctx->regs;
	RK_U32 pic_width_align8, pic_height_align8;
	RK_S32 pic_wd32, pic_h32;
	VepuFmtCfg *fmt = (VepuFmtCfg *) ctx->input_fmt;
	hevc_vepu540c_control_cfg *reg_ctl = &regs->reg_ctl;
	hevc_vepu540c_base *reg_base = &regs->reg_base;
	hevc_vepu540c_rc_roi *reg_rc_roi = &regs->reg_rc_roi;
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	EncFrmStatus *frm = &task->rc_task->frm;
	RK_U32 is_gray = 0;
	rdo_noskip_par *p_rdo_intra = NULL;
	RK_U32 is_phys = mpp_frame_get_is_full(task->frame);

	hal_h265e_enter();
	pic_width_align8 = (syn->pp.pic_width + 7) & (~7);
	pic_height_align8 = (syn->pp.pic_height + 7) & (~7);
	pic_wd32 = (syn->pp.pic_width + 31) / 32;
	pic_h32 = (syn->pp.pic_height + 31) / 32;

	hal_h265e_dbg_simple("frame %d | type %d | start gen regs",
			     ctx->frame_cnt, ctx->frame_type);

	memset(regs, 0, sizeof(H265eV540cRegSet));

	reg_ctl->reg0004_enc_strt.lkt_num = 0;
	reg_ctl->reg0004_enc_strt.vepu_cmd = ctx->enc_mode;
	reg_ctl->reg0005_enc_clr.safe_clr = 0x0;
	reg_ctl->reg0005_enc_clr.force_clr = 0x0;

	reg_ctl->reg0008_int_en.enc_done_en = 1;
	reg_ctl->reg0008_int_en.lkt_node_done_en = 1;
	reg_ctl->reg0008_int_en.sclr_done_en = 1;
	reg_ctl->reg0008_int_en.vslc_done_en = 1;
	reg_ctl->reg0008_int_en.vbsf_oflw_en = 1;
	reg_ctl->reg0008_int_en.vbuf_lens_en = 1;
	reg_ctl->reg0008_int_en.enc_err_en = 1;
	reg_ctl->reg0008_int_en.dvbm_fcfg_en = 1;
	reg_ctl->reg0008_int_en.wdg_en = 1;
	reg_ctl->reg0008_int_en.lkt_err_int_en = 0;
	reg_ctl->reg0008_int_en.lkt_err_stop_en = 1;
	reg_ctl->reg0008_int_en.lkt_force_stop_en = 1;
	reg_ctl->reg0008_int_en.jslc_done_en = 1;
	reg_ctl->reg0008_int_en.jbsf_oflw_en = 1;
	reg_ctl->reg0008_int_en.jbuf_lens_en = 1;
	reg_ctl->reg0008_int_en.dvbm_dcnt_en = 0;

	reg_ctl->reg0012_dtrns_map.jpeg_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.src_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.meiw_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.bsw_bus_edin = 0x7;
	reg_ctl->reg0012_dtrns_map.lktr_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.roir_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.lktw_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.rec_nfbc_bus_edin = 0x0;
	/* enable rdo clk gating */
	{
		RK_U32 *rdo_ckg = (RK_U32*)&reg_ctl->reg0022_rdo_ckg;

		*rdo_ckg = 0xffffffff;
	}
	//   reg_ctl->reg0013_dtrns_cfg.dspr_otsd        = (ctx->frame_type == INTER_P_FRAME);
	reg_ctl->reg0013_dtrns_cfg.axi_brsp_cke = 0x0;
	reg_ctl->reg0014_enc_wdg.vs_load_thd = 0x1fffff;
	reg_ctl->reg0014_enc_wdg.rfp_load_thd = 0xff;

	reg_ctl->reg0021_func_en.cke = 1;
	reg_ctl->reg0021_func_en.resetn_hw_en = 1;
	reg_ctl->reg0021_func_en.enc_done_tmvp_en = 1;

	reg_base->reg0196_enc_rsl.pic_wd8_m1 = pic_width_align8 / 8 - 1;
	reg_base->reg0197_src_fill.pic_wfill = (syn->pp.pic_width & 0x7)
					       ? (8 - (syn->pp.pic_width & 0x7)) : 0;
	reg_base->reg0196_enc_rsl.pic_hd8_m1 = pic_height_align8 / 8 - 1;
	reg_base->reg0197_src_fill.pic_hfill = (syn->pp.pic_height & 0x7)
					       ? (8 - (syn->pp.pic_height & 0x7)) : 0;

	reg_base->reg0192_enc_pic.enc_stnd = 1; //H265
	reg_base->reg0192_enc_pic.cur_frm_ref =
		!syn->sp.non_reference_flag;    //current frame will be refered
	reg_base->reg0192_enc_pic.bs_scp = 1;
	reg_base->reg0192_enc_pic.log2_ctu_num =
		mpp_ceil_log2(pic_wd32 * pic_h32);

	reg_base->reg0203_src_proc.src_mirr = 0;
	reg_base->reg0203_src_proc.src_rot = 0;

	reg_rc_roi->klut_ofst.chrm_klut_ofst =
		(ctx->frame_type == INTRA_FRAME) ? 6 : (ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC ? 9 :
							6);
	reg_rc_roi->klut_ofst.inter_chrm_dist_multi = 4;

	reg_base->reg0216_sli_splt.sli_splt_mode = syn->sp.sli_splt_mode;
	reg_base->reg0216_sli_splt.sli_splt_cpst = syn->sp.sli_splt_cpst;
	reg_base->reg0216_sli_splt.sli_splt = syn->sp.sli_splt;
	reg_base->reg0216_sli_splt.sli_flsh = syn->sp.sli_flsh;
	reg_base->reg0216_sli_splt.sli_max_num_m1 = syn->sp.sli_max_num_m1;

	reg_base->reg0218_sli_cnum.sli_splt_cnum_m1 = syn->sp.sli_splt_cnum_m1;
	reg_base->reg0217_sli_byte.sli_splt_byte = syn->sp.sli_splt_byte;
	reg_base->reg0248_sao_cfg.sao_lambda_multi = ctx->cfg->codec.h265.sao_cfg.sao_bit_ratio;

	vepu540c_h265_set_me_regs(ctx, syn, reg_base);

	reg_base->reg0232_rdo_cfg.chrm_spcl = 1;
	reg_base->reg0232_rdo_cfg.cu_inter_e = 0x00db;
	reg_base->reg0232_rdo_cfg.cu_intra_e = 0xf;
	reg_base->reg0232_rdo_cfg.lambda_qp_use_avg_cu16_flag = 1;
	reg_base->reg0232_rdo_cfg.yuvskip_calc_en = 1;
	reg_base->reg0232_rdo_cfg.atf_e = ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC ? 1 : 0;
	reg_base->reg0232_rdo_cfg.atr_e = 1;

	if (syn->pp.num_long_term_ref_pics_sps) {
		reg_base->reg0232_rdo_cfg.ltm_col = 0;
		reg_base->reg0232_rdo_cfg.ltm_idx0l0 = 1;
	} else {
		reg_base->reg0232_rdo_cfg.ltm_col = 0;
		reg_base->reg0232_rdo_cfg.ltm_idx0l0 = 0;
	}

	reg_base->reg0232_rdo_cfg.ccwa_e = 1;
	reg_base->reg0232_rdo_cfg.scl_lst_sel =
		syn->pp.scaling_list_mode;

	reg_base->reg0233_iprd_csts.rdo_mark_mode = 0;

	{
		RK_U32 i_nal_type = 0;

		/* TODO: extend syn->frame_coding_type definition */
		if (ctx->frame_type == INTRA_FRAME) {
			/* reset ref pictures */
			i_nal_type = NAL_IDR_W_RADL;
		} else if (ctx->frame_type == INTER_P_FRAME)
			i_nal_type = NAL_TRAIL_R;

		else
			i_nal_type = NAL_TRAIL_R;
		reg_base->reg0236_synt_nal.nal_unit_type = i_nal_type;
	}

	if (ctx->online || is_phys) {
		if (vepu540c_h265_set_dvbm(regs, task))
			return MPP_NOK;
	}
	vepu540c_h265_set_hw_address(ctx, reg_base, task);
	vepu540c_h265_set_pp_regs(regs, fmt, prep);
	vepu540c_h265_set_rc_regs(ctx, regs, task);
	vepu540c_h265_set_slice_regs(syn, reg_base);
	vepu540c_h265_set_ref_regs(syn, reg_base);
	vepu540c_h265_set_ext_line_buf(regs, ctx);
	if (ctx->osd_cfg.osd_data3)
		vepu540c_set_osd(&ctx->osd_cfg);

	if (ctx->qpmap_en && ctx->cfg->tune.scene_mode == MPP_ENC_SCENE_MODE_IPC) {
		MPP_RET ret;
		if (ctx->smart_en)
			ret = vepu540c_set_qpmap_smart(&reg_rc_roi->roi_cfg,
						       task->mv_info, task->qpmap,
						       task->mv_flag, task->mv_index, reg_base->reg0192_enc_pic.pic_qp,
						       prep->width, prep->height, 1, ctx->frame_type == INTRA_FRAME);
		else
			ret = vepu540c_set_qpmap_normal(&reg_rc_roi->roi_cfg,
							task->mv_info, task->qpmap,
							task->mv_flag, task->mv_index, reg_base->reg0192_enc_pic.pic_qp,
							prep->width, prep->height, 1, ctx->frame_type == INTRA_FRAME);

		if (ret == MPP_OK)
			reg_base->reg0186_adr_roir =
				mpp_dev_get_iova_address(ctx->dev, task->qpmap, 186);
	}

	if (ctx->roi_data)
		vepu540c_set_roi(&reg_rc_roi->roi_cfg,
				 (MppEncROICfg *)ctx->roi_data, prep->width, prep->height);

	/*paramet cfg */
	vepu540c_h265_global_cfg_set(ctx, regs, task);

	is_gray = mpp_frame_get_is_gray(task->frame);
	if (ctx->is_gray != is_gray) {
		if (ctx->is_gray) {
			//mpp_log("gray to color.\n");
			// TODO
		} else {
			//mpp_log("color to gray.\n");
			p_rdo_intra = &regs->reg_rdo.rdo_b16_intra;
			p_rdo_intra->atf_wgt.wgt0 = 0;
			p_rdo_intra->atf_wgt.wgt1 = 0;
			p_rdo_intra->atf_wgt.wgt2 = 0;
			p_rdo_intra->atf_wgt.wgt3 = 0;
			p_rdo_intra = &regs->reg_rdo.rdo_b32_intra;
			p_rdo_intra->atf_wgt.wgt0 = 0;
			p_rdo_intra->atf_wgt.wgt1 = 0;
			p_rdo_intra->atf_wgt.wgt2 = 0;
			p_rdo_intra->atf_wgt.wgt3 = 0;
			regs->reg_rdo.rdo_smear_cfg_comb.rdo_smear_en = 0;
			if (reg_base->reg0192_enc_pic.pic_qp < 30) {
				reg_base->reg0192_enc_pic.pic_qp = 30;
				reg_base->reg0240_synt_sli1.sli_qp = 30;
			} else if (reg_base->reg0192_enc_pic.pic_qp < 32) {
				reg_base->reg0192_enc_pic.pic_qp = 32;
				reg_base->reg0240_synt_sli1.sli_qp = 32;
			} else if (reg_base->reg0192_enc_pic.pic_qp < 34) {
				reg_base->reg0192_enc_pic.pic_qp = 34;
				reg_base->reg0240_synt_sli1.sli_qp = 34;
			}
		}
		ctx->is_gray = is_gray;
	}

	/* two pass register patch */
	if (frm->save_pass1)
		vepu540c_h265e_save_pass1_patch(regs, ctx);
	if (frm->use_pass1)
		vepu540c_h265e_use_pass1_patch(regs, ctx, syn);
	ctx->frame_num++;

	hal_h265e_leave();
	return MPP_OK;
}

static MPP_RET hal_h265e_v540c_start(void *hal, HalEncTask *enc_task)
{
	MPP_RET ret = MPP_OK;
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	RK_U32 *regs = (RK_U32 *) ctx->regs;
	H265eV540cRegSet *hw_regs = ctx->regs;
	H265eV540cStatusElem *reg_out =
		(H265eV540cStatusElem *) ctx->reg_out[0];
	MppDevRegWrCfg cfg;
	MppDevRegRdCfg cfg1;
	RK_U32 i = 0;
	hal_h265e_enter();

	if (enc_task->flags.err) {
		hal_h265e_err("enc_task->flags.err %08x, return e arly",
			      enc_task->flags.err);
		return MPP_NOK;
	}

	cfg.reg = (RK_U32 *) & hw_regs->reg_ctl;
	cfg.size = sizeof(hevc_vepu540c_control_cfg);
	cfg.offset = VEPU540C_CTL_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	if (hal_h265e_debug & HAL_H265E_DBG_CTL_REGS) {
		regs = (RK_U32 *) & hw_regs->reg_ctl;
		for (i = 0; i < sizeof(hevc_vepu540c_control_cfg) / 4; i++) {
			hal_h265e_dbg_ctl("ctl reg[%04x]: 0%08x\n", i * 4,
					  regs[i]);
		}
	}

	cfg.reg = &hw_regs->reg_base;
	cfg.size = sizeof(hevc_vepu540c_base);
	cfg.offset = VEPU540C_BASE_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	if (hal_h265e_debug & HAL_H265E_DBG_REGS) {
		regs = (RK_U32 *) (&hw_regs->reg_base);
		for (i = 0; i < 32; i++) {
			hal_h265e_dbg_regs("hw add cfg reg[%04x]: 0%08x\n",
					   i * 4, regs[i]);
		}
		regs += 32;
		for (i = 0; i < (sizeof(hevc_vepu540c_base) - 128) / 4; i++) {
			hal_h265e_dbg_regs("set reg[%04x]: 0%08x\n", i * 4,
					   regs[i]);
		}
	}
	cfg.reg = &hw_regs->reg_rc_roi;
	cfg.size = sizeof(hevc_vepu540c_rc_roi);
	cfg.offset = VEPU540C_RCROI_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	if (hal_h265e_debug & HAL_H265E_DBG_RCKUT_REGS) {
		regs = (RK_U32 *) & hw_regs->reg_rc_roi;
		for (i = 0; i < sizeof(hevc_vepu540c_rc_roi) / 4; i++) {
			hal_h265e_dbg_rckut("set reg[%04x]: 0%08x\n", i * 4,
					    regs[i]);
		}
	}

	cfg.reg = &hw_regs->reg_wgt;
	cfg.size = sizeof(hevc_vepu540c_wgt);
	cfg.offset = VEPU540C_WEG_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	if (hal_h265e_debug & HAL_H265E_DBG_WGT_REGS) {
		regs = (RK_U32 *) & hw_regs->reg_wgt;
		for (i = 0; i < sizeof(hevc_vepu540c_wgt) / 4; i++) {
			hal_h265e_dbg_wgt("set reg[%04x]: 0%08x\n", i * 4,
					  regs[i]);
		}
	}

	cfg.reg = &hw_regs->reg_rdo;
	cfg.size = sizeof(vepu540c_rdo_cfg);
	cfg.offset = VEPU540C_RDOCFG_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	cfg.reg = &hw_regs->reg_osd_cfg;
	cfg.size = sizeof(vepu540c_osd_regs);
	cfg.offset = VEPU540C_OSD_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	cfg.reg = &hw_regs->reg_scl;
	cfg.size = sizeof(vepu540c_jpeg_tab) + sizeof(vepu540c_scl_cfg);
	cfg.offset = VEPU540C_SCLCFG_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	cfg1.reg = &reg_out->hw_status;
	cfg1.size = sizeof(RK_U32);
	cfg1.offset = VEPU540C_REG_BASE_HW_STATUS;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_RD, &cfg1);
	if (ret) {
		mpp_err_f("set register read failed %d\n", ret);
		return ret;
	}

	cfg1.reg = &reg_out->st;
	cfg1.size = sizeof(H265eV540cStatusElem) - 4;
	cfg1.offset = VEPU540C_STATUS_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_RD, &cfg1);
	if (ret) {
		mpp_err_f("set register read failed %d\n", ret);
		return ret;
	}

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_SEND, NULL);
	if (ret)
		mpp_err_f("send cmd failed %d\n", ret);
	hal_h265e_leave();
	return ret;
}

static MPP_RET vepu540c_h265_set_feedback(H265eV540cHalContext *ctx,
					  HalEncTask *enc_task)
{
	EncRcTaskInfo *hal_rc_ret = (EncRcTaskInfo *) & enc_task->rc_task->info;
	vepu540c_h265_fbk *fb = &ctx->feedback;
	MppEncCfgSet *cfg = ctx->cfg;
	H265eV540cRegSet *regs_set = ctx->regs;
	RK_S32 mb64_num =
		((cfg->prep.width + 63) / 64) * ((cfg->prep.height + 63) / 64);
	RK_S32 mbs =
		((cfg->prep.width + 15) / 16) * ((cfg->prep.height + 15) / 16);
	RK_S32 mb8_num = (mb64_num << 6);
	RK_S32 mb4_num = (mb8_num << 2);
	H265eV540cStatusElem *elem = (H265eV540cStatusElem *) ctx->reg_out[0];
	RK_U32 hw_status = elem->hw_status;
	RK_U32 madi_cnt = 0, madp_cnt = 0, md_cnt = 0, madi_lvl = 0;

	RK_U32 madi_th_cnt0 =
		elem->st.st_madi_lt_num0.madi_th_lt_cnt0 +
		elem->st.st_madi_rt_num0.madi_th_rt_cnt0 +
		elem->st.st_madi_lb_num0.madi_th_lb_cnt0 +
		elem->st.st_madi_rb_num0.madi_th_rb_cnt0;
	RK_U32 madi_th_cnt1 =
		elem->st.st_madi_lt_num0.madi_th_lt_cnt1 +
		elem->st.st_madi_rt_num0.madi_th_rt_cnt1 +
		elem->st.st_madi_lb_num0.madi_th_lb_cnt1 +
		elem->st.st_madi_rb_num0.madi_th_rb_cnt1;
	RK_U32 madi_th_cnt2 =
		elem->st.st_madi_lt_num1.madi_th_lt_cnt2 +
		elem->st.st_madi_rt_num1.madi_th_rt_cnt2 +
		elem->st.st_madi_lb_num1.madi_th_lb_cnt2 +
		elem->st.st_madi_rb_num1.madi_th_rb_cnt2;
	RK_U32 madi_th_cnt3 =
		elem->st.st_madi_lt_num1.madi_th_lt_cnt3 +
		elem->st.st_madi_rt_num1.madi_th_rt_cnt3 +
		elem->st.st_madi_lb_num1.madi_th_lb_cnt3 +
		elem->st.st_madi_rb_num1.madi_th_rb_cnt3;
	RK_U32 madp_th_cnt0 =
		elem->st.st_madp_lt_num0.madp_th_lt_cnt0 +
		elem->st.st_madp_rt_num0.madp_th_rt_cnt0 +
		elem->st.st_madp_lb_num0.madp_th_lb_cnt0 +
		elem->st.st_madp_rb_num0.madp_th_rb_cnt0;
	RK_U32 madp_th_cnt1 =
		elem->st.st_madp_lt_num0.madp_th_lt_cnt1 +
		elem->st.st_madp_rt_num0.madp_th_rt_cnt1 +
		elem->st.st_madp_lb_num0.madp_th_lb_cnt1 +
		elem->st.st_madp_rb_num0.madp_th_rb_cnt1;
	RK_U32 madp_th_cnt2 =
		elem->st.st_madp_lt_num1.madp_th_lt_cnt2 +
		elem->st.st_madp_rt_num1.madp_th_rt_cnt2 +
		elem->st.st_madp_lb_num1.madp_th_lb_cnt2 +
		elem->st.st_madp_rb_num1.madp_th_rb_cnt2;
	RK_U32 madp_th_cnt3 =
		elem->st.st_madp_lt_num1.madp_th_lt_cnt3 +
		elem->st.st_madp_rt_num1.madp_th_rt_cnt3 +
		elem->st.st_madp_lb_num1.madp_th_lb_cnt3 +
		elem->st.st_madp_rb_num1.madp_th_rb_cnt3;
	md_cnt = (24 * madp_th_cnt3 + 22 * madp_th_cnt2 + 17 * madp_th_cnt1) >> 2;
	if (ctx->smart_en)
		md_cnt = (12 * madp_th_cnt3 + 11 * madp_th_cnt2 + 8 * madp_th_cnt1) >> 2;
	madi_cnt = (6 * madi_th_cnt3 + 5 * madi_th_cnt2 + 4 * madi_th_cnt1) >> 2;

	hal_rc_ret->motion_level = 0;
	if (md_cnt * 100 > 15 * mbs)
		hal_rc_ret->motion_level = 200;
	else if (md_cnt * 100 > 5 * mbs)
		hal_rc_ret->motion_level = 100;
	else if (md_cnt * 100 > (mbs >> 2))
		hal_rc_ret->motion_level = 1;
	else
		hal_rc_ret->motion_level = 0;

	madi_lvl = 0;
	if (madi_cnt * 100 > 30 * mbs)
		madi_lvl = 2;
	else if (madi_cnt * 100 > 13 * mbs)
		madi_lvl = 1;
	else
		madi_lvl = 0;
	hal_rc_ret->complex_level = madi_lvl;

	hal_h265e_enter();

	fb->qp_sum += elem->st.qp_sum;

	fb->out_strm_size += elem->st.bs_lgth_l32;

	fb->sse_sum += (RK_S64) (elem->st.sse_h32 << 16) +
		       ((elem->st.st_sse_bsl.sse_l16 >> 16) & 0xffff);

	fb->hw_status = hw_status;
	if (hw_status & RKV_ENC_INT_LINKTABLE_FINISH)
		hal_h265e_err("RKV_ENC_INT_LINKTABLE_FINISH");

	if (hw_status & RKV_ENC_INT_ONE_FRAME_FINISH)
		hal_h265e_dbg_detail("RKV_ENC_INT_ONE_FRAME_FINISH");

	if (hw_status & RKV_ENC_INT_ONE_SLICE_FINISH)
		hal_h265e_err("RKV_ENC_INT_ONE_SLICE_FINISH");

	if (hw_status & RKV_ENC_INT_SAFE_CLEAR_FINISH)
		hal_h265e_err("RKV_ENC_INT_SAFE_CLEAR_FINISH");

	if (hw_status & RKV_ENC_INT_BIT_STREAM_OVERFLOW) {
		hal_h265e_err("RKV_ENC_INT_BIT_STREAM_OVERFLOW");
		return MPP_NOK;
	}

	if (hw_status & RKV_ENC_INT_BUS_WRITE_FULL) {
		hal_h265e_err("RKV_ENC_INT_BUS_WRITE_FULL");
		return MPP_NOK;
	}

	if (hw_status & RKV_ENC_INT_BUS_WRITE_ERROR) {
		hal_h265e_err("RKV_ENC_INT_BUS_WRITE_ERROR");
		return MPP_NOK;
	}

	if (hw_status & RKV_ENC_INT_BUS_READ_ERROR) {
		hal_h265e_err("RKV_ENC_INT_BUS_READ_ERROR");
		return MPP_NOK;
	}

	if (hw_status & RKV_ENC_INT_TIMEOUT_ERROR) {
		hal_h265e_err("RKV_ENC_INT_TIMEOUT_ERROR");
		return MPP_NOK;
	}

	fb->st_madi = madi_th_cnt0 * regs_set->reg_rc_roi.madi_st_thd.madi_th0 +
		      madi_th_cnt1 * (regs_set->reg_rc_roi.madi_st_thd.madi_th0 +
				      regs_set->reg_rc_roi.madi_st_thd.madi_th1) / 2 +
		      madi_th_cnt2 * (regs_set->reg_rc_roi.madi_st_thd.madi_th1 +
				      regs_set->reg_rc_roi.madi_st_thd.madi_th2) / 2 +
		      madi_th_cnt3 * regs_set->reg_rc_roi.madi_st_thd.madi_th2;

	madi_cnt = madi_th_cnt0 + madi_th_cnt1 + madi_th_cnt2 + madi_th_cnt3;

	if (madi_cnt)
		fb->st_madi = fb->st_madi / madi_cnt;

	fb->st_madp = madp_th_cnt0 * regs_set->reg_rc_roi.madp_st_thd0.madp_th0 +
		      madp_th_cnt1 * (regs_set->reg_rc_roi.madp_st_thd0.madp_th0 +
				      regs_set->reg_rc_roi.madp_st_thd0.madp_th1) / 2 +
		      madp_th_cnt2 * (regs_set->reg_rc_roi.madp_st_thd0.madp_th1 +
				      regs_set->reg_rc_roi.madp_st_thd1.madp_th2) / 2 +
		      madp_th_cnt3 * regs_set->reg_rc_roi.madp_st_thd1.madp_th2;

	madp_cnt = madp_th_cnt0 + madp_th_cnt1 + madp_th_cnt2 + madp_th_cnt3;

	if (madp_cnt)
		fb->st_madp =  fb->st_madp  / madp_cnt;



	fb->st_mb_num += elem->st.st_bnum_b16.num_b16;
	//  fb->st_ctu_num += elem->st.st_bnum_cme.num_ctu;

	fb->st_lvl64_inter_num += elem->st.st_pnum_p64.pnum_p64;
	fb->st_lvl32_inter_num += elem->st.st_pnum_p32.pnum_p32;
	fb->st_lvl32_intra_num += elem->st.st_pnum_i32.pnum_i32;
	fb->st_lvl16_inter_num += elem->st.st_pnum_p16.pnum_p16;
	fb->st_lvl16_intra_num += elem->st.st_pnum_i16.pnum_i16;
	fb->st_lvl8_inter_num += elem->st.st_pnum_p8.pnum_p8;
	fb->st_lvl8_intra_num += elem->st.st_pnum_i8.pnum_i8;
	fb->st_lvl4_intra_num += elem->st.st_pnum_i4.pnum_i4;
	memcpy(&fb->st_cu_num_qp[0], &elem->st.st_b8_qp, 52 * sizeof(RK_U32));

	fb->st_smear_cnt[0] = elem->st.st_smear_cnt.rdo_smear_cnt0 * 4;
	fb->st_smear_cnt[1] = elem->st.st_smear_cnt.rdo_smear_cnt1 * 4;
	fb->st_smear_cnt[2] = elem->st.st_smear_cnt.rdo_smear_cnt2 * 4;
	fb->st_smear_cnt[3] = elem->st.st_smear_cnt.rdo_smear_cnt3 * 4;
	fb->st_smear_cnt[4] = fb->st_smear_cnt[0] + fb->st_smear_cnt[1]
			      + fb->st_smear_cnt[2] + fb->st_smear_cnt[3];

	hal_rc_ret->bit_real += fb->out_strm_size * 8;

	if (fb->st_mb_num)
		fb->st_madi = fb->st_madi / fb->st_mb_num;

	else
		fb->st_madi = 0;
	if (fb->st_ctu_num)
		fb->st_madp = fb->st_madp / fb->st_ctu_num;

	else
		fb->st_madp = 0;

	if (mb4_num > 0)
		hal_rc_ret->iblk4_prop =
			((((fb->st_lvl4_intra_num + fb->st_lvl8_intra_num) << 2) +
			  (fb->st_lvl16_intra_num << 4) +
			  (fb->st_lvl32_intra_num << 6)) << 8) / mb4_num;

	if (mb64_num > 0) {
		/*
		   hal_cfg[k].inter_lv8_prop = ((fb->st_lvl8_inter_num + (fb->st_lvl16_inter_num << 2) +
		   (fb->st_lvl32_inter_num << 4) +
		   (fb->st_lvl64_inter_num << 6)) << 8) / mb8_num; */

		hal_rc_ret->quality_real = fb->qp_sum / mb8_num;
		// hal_cfg[k].sse          = fb->sse_sum / mb64_num;
	}

	hal_rc_ret->madi = fb->st_madi;
	hal_rc_ret->madp = fb->st_madp;
	hal_h265e_leave();
	return MPP_OK;
}

//#define DUMP_DATA
static MPP_RET hal_h265e_v540c_wait(void *hal, HalEncTask *task)
{
	MPP_RET ret = MPP_OK;
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	HalEncTask *enc_task = task;
	H265eV540cStatusElem *elem = (H265eV540cStatusElem *) ctx->reg_out;
	hal_h265e_enter();

	if (enc_task->flags.err) {
		hal_h265e_err("enc_task->flags.err %08x, return early",
			      enc_task->flags.err);
		return MPP_NOK;
	}

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_POLL, NULL);

#ifdef DUMP_DATA
	static FILE *fp_fbd = NULL;
	static FILE *fp_fbh = NULL;
	static FILE *fp_dws = NULL;
	HalBuf *recon_buf;
	static RK_U32 frm_num = 0;
	H265eSyntax_new *syn = (H265eSyntax_new *) enc_task->syntax.data;
	recon_buf = hal_bufs_get_buf(ctx->dpb_bufs, syn->sp.recon_pic.slot_idx);
	char file_name[20] = "";
	size_t rec_size = mpp_buffer_get_size(recon_buf->buf[0]);
	size_t dws_size = mpp_buffer_get_size(recon_buf->buf[1]);

	void *ptr = mpp_buffer_get_ptr(recon_buf->buf[0]);
	void *dws_ptr = mpp_buffer_get_ptr(recon_buf->buf[1]);

	sprintf(&file_name[0], "fbd%d.bin", frm_num);
	if (fp_fbd != NULL) {
		fclose(fp_fbd);
		fp_fbd = NULL;
	} else
		fp_fbd = fopen(file_name, "wb+");
	if (fp_fbd) {
		fwrite(ptr + ctx->fbc_header_len, 1,
		       rec_size - ctx->fbc_header_len, fp_fbd);
		fflush(fp_fbd);
	}

	sprintf(&file_name[0], "fbh%d.bin", frm_num);

	if (fp_fbh != NULL) {
		fclose(fp_fbh);
		fp_fbh = NULL;
	} else
		fp_fbh = fopen(file_name, "wb+");

	if (fp_fbh) {
		fwrite(ptr, 1, ctx->fbc_header_len, fp_fbh);
		fflush(fp_fbh);
	}

	sprintf(&file_name[0], "dws%d.bin", frm_num);

	if (fp_dws != NULL) {
		fclose(fp_dws);
		fp_dws = NULL;
	} else
		fp_dws = fopen(file_name, "wb+");

	if (fp_dws) {
		fwrite(dws_ptr, 1, dws_size, fp_dws);
		fflush(fp_dws);
	}
	frm_num++;
#endif
	if (ret)
		mpp_err_f("poll cmd failed %d status %d \n", ret,
			  elem->hw_status);

	hal_h265e_leave();
	return ret;
}

static MPP_RET hal_h265e_v540c_get_task(void *hal, HalEncTask *task)
{
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	//  MppFrame frame = task->frame;
	EncFrmStatus *frm_status = &task->rc_task->frm;

	hal_h265e_enter();

	if (vepu540c_h265_setup_hal_bufs(ctx)) {
		hal_h265e_err
		("vepu541_h265_allocate_buffers failed, free buffers and return\n");
		task->flags.err |= HAL_ENC_TASK_ERR_ALLOC;
		return MPP_ERR_MALLOC;
	}

	ctx->last_frame_type = ctx->frame_type;
	if (frm_status->is_intra)
		ctx->frame_type = INTRA_FRAME;

	else
		ctx->frame_type = INTER_P_FRAME;

	ctx->roi_data = mpp_frame_get_roi(task->frame);

	ctx->osd_cfg.osd_data3 = mpp_frame_get_osd(task->frame);

	ctx->mb_num = ctx->feedback.st_mb_num;

	memcpy(ctx->smear_cnt, ctx->feedback.st_smear_cnt, sizeof(ctx->smear_cnt));

	memset(&ctx->feedback, 0, sizeof(vepu540c_h265_fbk));

	hal_h265e_leave();
	return MPP_OK;
}

static MPP_RET hal_h265e_v540c_ret_task(void *hal, HalEncTask *task)
{
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	HalEncTask *enc_task = task;
	vepu540c_h265_fbk *fb = &ctx->feedback;
	MPP_RET ret = MPP_OK;

	hal_h265e_enter();


	ret = vepu540c_h265_set_feedback(ctx, enc_task);
	if (ret)
		return ret;
	enc_task->hw_length = fb->out_strm_size;
	enc_task->length += fb->out_strm_size;

	hal_h265e_dbg_detail("output stream size %d\n", fb->out_strm_size);

	hal_h265e_leave();
	return MPP_OK;
}

static MPP_RET hal_h265e_v540c_comb_start(void *hal, HalEncTask *enc_task,
					  HalEncTask *jpeg_enc_task)
{
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	H265eV540cRegSet *hw_regs = ctx->regs;
	Vepu540cJpegCfg jpeg_cfg;

	hal_h265e_enter();
	hw_regs->reg_ctl.reg0012_dtrns_map.jpeg_bus_edin = 7;
	jpeg_cfg.dev = ctx->dev;
	jpeg_cfg.jpeg_reg_base = &hw_regs->reg_base.jpegReg;
	jpeg_cfg.reg_tab = &hw_regs->jpeg_table;
	jpeg_cfg.enc_task = jpeg_enc_task;
	jpeg_cfg.input_fmt = ctx->input_fmt;
	jpeg_cfg.online = ctx->online;
	vepu540c_set_jpeg_reg(&jpeg_cfg);

	if (jpeg_enc_task->jpeg_tlb_reg)
		memcpy(&hw_regs->jpeg_table, jpeg_enc_task->jpeg_tlb_reg, sizeof(vepu540c_jpeg_tab));
	if (jpeg_enc_task->jpeg_osd_reg)
		memcpy(&hw_regs->reg_osd_cfg.osd_jpeg_cfg, jpeg_enc_task->jpeg_osd_reg, sizeof(vepu540c_osd_reg));

	hal_h265e_leave();
	return hal_h265e_v540c_start(hal, enc_task);
}


static MPP_RET hal_h265e_v540c_ret_comb_task(void *hal, HalEncTask *task, HalEncTask *jpeg_enc_task)
{
	H265eV540cHalContext *ctx = (H265eV540cHalContext *) hal;
	HalEncTask *enc_task = task;
	H265eV540cStatusElem *elem = (H265eV540cStatusElem *) ctx->reg_out[0];
	vepu540c_h265_fbk *fb = &ctx->feedback;
	EncRcTaskInfo *hal_rc_ret = (EncRcTaskInfo *) &jpeg_enc_task->rc_task->info;

	MPP_RET ret = MPP_OK;

	hal_h265e_enter();

	ret = vepu540c_h265_set_feedback(ctx, enc_task);
	if (ret)
		return ret;
	enc_task->hw_length = fb->out_strm_size;
	enc_task->length += fb->out_strm_size;

	if (elem->hw_status & RKV_ENC_INT_JPEG_OVERFLOW)
		jpeg_enc_task->jpeg_overflow = 1;

	jpeg_enc_task->hw_length = elem->st.jpeg_head_bits_l32;
	jpeg_enc_task->length += jpeg_enc_task->hw_length;
	hal_rc_ret->bit_real += jpeg_enc_task->hw_length * 8;

	hal_h265e_dbg_detail("output stream size %d\n", fb->out_strm_size);

	hal_h265e_leave();
	return ret;
}

const MppEncHalApi hal_h265e_vepu540c = {
	"hal_h265e_v540c",
	MPP_VIDEO_CodingHEVC,
	sizeof(H265eV540cHalContext),
	0,
	hal_h265e_v540c_init,
	hal_h265e_v540c_deinit,
	hal_h265e_vepu540c_prepare,
	hal_h265e_v540c_get_task,
	hal_h265e_v540c_gen_regs,
	hal_h265e_v540c_start,
	hal_h265e_v540c_wait,
	NULL,
	NULL,
	hal_h265e_v540c_ret_task,
	hal_h265e_v540c_comb_start,
	hal_h265e_v540c_ret_comb_task
};
