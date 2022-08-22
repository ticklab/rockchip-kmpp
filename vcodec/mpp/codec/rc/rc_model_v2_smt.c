/*
 * Copyright 2016 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef USE_SMART_RC
#define MODULE_TAG "rc_model_v2_smt"
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/math64.h>

#include "mpp_mem.h"
#include "mpp_maths.h"
#include "mpp_2str.h"
#include "mpp_rc.h"
#include "rc_debug.h"
#include "rc_ctx.h"
#include "rc_model_v2.h"

#define MAD_THDI 20
#define LOW_QP 30
#define LOW_QP_level0 30
#define LOW_QP_level1 31
#define LOW_LOW_QP 35
#define LOW_PRE_DIFF_BIT_USE   -20000

extern RK_S32 tab_lnx[64];

typedef struct RcModelV2SmtCtx_t {
	RcCfg usr_cfg;
	EncRcTaskInfo hal_cfg;
	RK_U32 frame_type;
	RK_U32 last_frame_type;
	//RK_S64 gop_total_bits;
	RK_U32 bit_per_frame;
	MppDataV2 *i_bit;
	RK_U32 i_sumbits;
	RK_U32 i_scale;
	MppDataV2 *idr_bit;
	RK_U32 idr_sumbits;
	RK_U32 idr_scale;
	MppDataV2 *p_bit;
	RK_U32 p_sumbits;
	RK_U32 p_scale;
	MppDataV2 *pre_p_bit;
	RK_S32 target_bps;
	RK_S32 pre_target_bits;
	RK_S32 pre_real_bits;
	RK_S32 frm_bits_thr;
	RK_S32 ins_bps;
	RK_S32 last_inst_bps;
	RK_U32 water_level_thr;
	MppDataV2 *stat_bits;
	MppDataV2 *stat_rate;
	RK_S32 stat_watl_thrd;
	RK_S32 stat_watl;
	RK_S32 stat_last_watl;
	RK_S32 next_i_ratio;	// scale 64
	RK_S32 next_ratio;	// scale 64
	RK_S32 pre_i_qp;
	RK_S32 pre_p_qp;
	RK_S32 scale_qp;	// scale 64
	MppDataV2 *means_qp;
	RK_S64 frm_num;
	RK_S32 reenc_cnt;
	RK_S32 codec_type;	// 264:   0  ; 265:   1
	RK_S32 qp_min;
	RK_S32 qp_max;
	RK_S32 qp_step;
	MppEncGopMode gop_mode;
	RK_S32 window_len;
	RK_S32 intra_to_inter_rate;
	RK_S32 acc_intra_bits_in_fps;
	RK_S32 acc_inter_bits_in_fps;
	RK_S32 acc_total_bits;
	RK_S64 acc_total_count;
	RK_S64 acc_intra_count;
	RK_S64 acc_inter_count;
	RK_S32 last_fps_bits;
	RK_S32 pre_gop_left_bit;
	MppData *qp_p;
	MppData *sse_p;
	MppData *intra;
	MppData *inter;
	MppData *gop_bits;
	MppData *intra_percent;
	MppDataV2 *motion_level;
	MppDataV2 *complex_level;
	MppPIDCtx pid_fps;
	RK_S32 bps_target_low_rate;
	RK_S32 bps_target_high_rate;
	RK_S32 bits_target_low_rate;
	RK_S32 bits_target_high_rate;
	RK_S32 bits_per_pic_low_rate;
	RK_S32 bits_per_intra_low_rate;
	RK_S32 bits_per_inter_low_rate;
	RK_S32 bits_per_pic_high_rate;
	RK_S32 bits_per_intra_high_rate;
	RK_S32 bits_per_inter_high_rate;
	RK_S32 pre_diff_bit_low_rate;
	RK_S32 pre_diff_bit_high_rate;
	RK_S32 gop_min;
	MppPIDCtx pid_intra_low_rate;
	MppPIDCtx pid_intra_high_rate;
	MppPIDCtx pid_inter_low_rate;
	MppPIDCtx pid_inter_high_rate;
	RK_S32 bits_one_gop[1000];
	RK_S32 bits_one_gop_use_flag;
	RK_S32 bits_one_gop_sum;
	RK_S32 delta_bits_per_frame;
	RK_S32 frame_cnt_in_gop;
	RK_S32 bits_target_use;
	RK_S32 qp_out;
	RK_S32 qp_prev_out;
	RK_S32 qp_preavg;
	RK_S32 intra_prerealbit;
	RK_S32 intra_preqp;
	RK_S32 intra_presse;
	RK_S32 intra_premadi;
	RK_U32 st_madi;
} RcModelV2SmtCtx;

typedef struct InfoList_t {
	RK_U16 flag;		// 1 - valid   0 - unvaild
	RK_U16 up_left[2];	// 0 - y idx   1 - x idx
	RK_U16 down_right[2];	// 0 - y idx   1 - x idx
} InfoList;

typedef struct RoiInfo_t {
	RK_U16 flag;		// 1 - valid        0 - unvaild
	RK_U16 is_move;		// 1 - is motion    0 - is motionless
	RK_U16 up_left[2];	// 0 - y idx        1 - x idx
	RK_U16 down_right[2];	// 0 - y idx        1 - x idx
} RoiInfo;

MPP_RET bits_model_smt_deinit(RcModelV2SmtCtx * ctx)
{
	rc_dbg_func("enter %p\n", ctx);

	if (ctx->qp_p) {
		mpp_data_deinit(ctx->qp_p);
		ctx->qp_p = NULL;
	}

	if (ctx->sse_p) {
		mpp_data_deinit(ctx->sse_p);
		ctx->sse_p = NULL;
	}

	if (ctx->intra) {
		mpp_data_deinit(ctx->intra);
		ctx->intra = NULL;
	}

	if (ctx->inter) {
		mpp_data_deinit(ctx->inter);
		ctx->inter = NULL;
	}

	if (ctx->gop_bits) {
		mpp_data_deinit(ctx->gop_bits);
		ctx->gop_bits = NULL;
	}

	if (ctx->intra_percent) {
		mpp_data_deinit(ctx->intra_percent);
		ctx->intra_percent = NULL;
	}

	if (ctx->i_bit != NULL) {
		mpp_data_deinit_v2(ctx->i_bit);
		ctx->i_bit = NULL;
	}

	if (ctx->p_bit != NULL) {
		mpp_data_deinit_v2(ctx->p_bit);
		ctx->p_bit = NULL;
	}

	if (ctx->pre_p_bit != NULL) {
		mpp_data_deinit_v2(ctx->pre_p_bit);
		ctx->pre_p_bit = NULL;
	}

	if (ctx->stat_rate != NULL) {
		mpp_data_deinit_v2(ctx->stat_rate);
		ctx->stat_rate = NULL;
	}

	if (ctx->stat_bits != NULL) {
		mpp_data_deinit_v2(ctx->stat_bits);
		ctx->stat_bits = NULL;
	}

	if (ctx->motion_level != NULL) {
		mpp_data_deinit_v2(ctx->motion_level);
		ctx->motion_level = NULL;
	}

	if (ctx->complex_level != NULL) {
		mpp_data_deinit_v2(ctx->complex_level);
		ctx->complex_level = NULL;
	}

	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET bits_model_smt_init(RcModelV2SmtCtx * ctx)
{
	RK_S32 gop_len = ctx->usr_cfg.igop;
	RcFpsCfg *fps = &ctx->usr_cfg.fps;
	RK_S32 mad_len = 10;
	RK_S32 avg_low_rate = 0;
	RK_S32 avg_high_rate = 0;
	RK_S32 bit_ratio[5] = {7, 8, 9, 10, 11};
	RK_S32 target_bps = ctx->target_bps;
	RK_U32 stat_len =
		fps->fps_out_num * ctx->usr_cfg.stats_time / fps->fps_out_denorm;
	stat_len = stat_len ? stat_len : 1;

	rc_dbg_func("enter %p\n", ctx);
	ctx->frm_num = 0;

	// smt
	ctx->frame_cnt_in_gop = 0;
	ctx->bits_one_gop_use_flag = 0;
	ctx->gop_min = gop_len;

	ctx->qp_min = 18;
	ctx->qp_max = 51;
	ctx->qp_step = 4;

	ctx->bit_per_frame =
		target_bps * fps->fps_out_denorm / fps->fps_out_num;

	if (gop_len < fps->fps_out_num)
		ctx->window_len = fps->fps_out_num;
	else
		ctx->window_len = gop_len;

	if (ctx->window_len < 10)
		ctx->window_len = 10;

	if (ctx->window_len > fps->fps_out_num)
		ctx->window_len = fps->fps_out_num;

	if (ctx->intra)
		mpp_data_deinit(ctx->intra);
	mpp_data_init(&ctx->intra, gop_len);

	if (ctx->inter)
		mpp_data_deinit(ctx->inter);
	mpp_data_init(&ctx->inter, fps->fps_out_num);	/* need test */

	if (ctx->gop_bits)
		mpp_data_deinit(ctx->gop_bits);
	mpp_data_init(&ctx->gop_bits, gop_len);

	if (ctx->intra_percent)
		mpp_data_deinit(ctx->intra_percent);
	mpp_data_init(&ctx->intra_percent, gop_len);

	if (ctx->motion_level)
		mpp_data_deinit_v2(ctx->motion_level);
	mpp_data_init_v2(&ctx->motion_level, mad_len, 0);

	if (ctx->complex_level)
		mpp_data_deinit_v2(ctx->complex_level);
	mpp_data_init_v2(&ctx->complex_level, mad_len, 0);

	if (ctx->stat_bits)
		mpp_data_deinit_v2(ctx->stat_bits);
	mpp_data_init_v2(&ctx->stat_bits, stat_len, ctx->bit_per_frame);

	mpp_pid_reset(&ctx->pid_fps);
	mpp_pid_reset(&ctx->pid_intra_low_rate);
	mpp_pid_reset(&ctx->pid_intra_high_rate);
	mpp_pid_reset(&ctx->pid_inter_low_rate);
	mpp_pid_reset(&ctx->pid_inter_high_rate);

	mpp_pid_set_param(&ctx->pid_fps, 4, 6, 0, 100, ctx->window_len);
	mpp_pid_set_param(&ctx->pid_intra_low_rate, 4, 6, 0, 100,
			  ctx->window_len);
	mpp_pid_set_param(&ctx->pid_intra_high_rate, 4, 6, 0, 100,
			  ctx->window_len);
	mpp_pid_set_param(&ctx->pid_inter_low_rate, 4, 6, 0, 100,
			  ctx->window_len);
	mpp_pid_set_param(&ctx->pid_inter_high_rate, 4, 6, 0, 100,
			  ctx->window_len);

	ctx->bps_target_low_rate = ctx->usr_cfg.bps_min;
	ctx->bps_target_high_rate = ctx->usr_cfg.bps_max;
	ctx->bits_per_pic_low_rate =
		axb_div_c(ctx->bps_target_low_rate, fps->fps_out_denorm,
			  fps->fps_out_num);
	ctx->bits_per_pic_high_rate =
		axb_div_c(ctx->bps_target_high_rate, fps->fps_out_denorm,
			  fps->fps_out_num);

	ctx->acc_intra_bits_in_fps = 0;
	ctx->acc_inter_bits_in_fps = 0;
	ctx->acc_total_bits = 0;
	ctx->acc_intra_count = 0;
	ctx->acc_inter_count = 0;
	ctx->last_fps_bits = 0;

	avg_low_rate = ctx->bits_per_pic_low_rate;
	avg_high_rate = ctx->bits_per_pic_high_rate;

	if (gop_len == 0) {
		ctx->gop_mode = MPP_GOP_ALL_INTER;
		ctx->bits_per_inter_low_rate = avg_low_rate;
		ctx->bits_per_intra_low_rate = avg_low_rate * 10;
		ctx->bits_per_inter_high_rate = avg_high_rate;
		ctx->bits_per_intra_high_rate = avg_high_rate * 10;
	} else if (gop_len == 1) {
		ctx->gop_mode = MPP_GOP_ALL_INTRA;
		ctx->bits_per_inter_low_rate = 0;
		ctx->bits_per_intra_low_rate = avg_low_rate;
		ctx->bits_per_inter_high_rate = 0;
		ctx->bits_per_intra_high_rate = avg_high_rate;
		ctx->intra_to_inter_rate = 0;
	} else if (gop_len < ctx->window_len) {
		ctx->gop_mode = MPP_GOP_SMALL;
		ctx->intra_to_inter_rate = gop_len + 1;

		ctx->bits_per_inter_low_rate = avg_low_rate >> 1;
		ctx->bits_per_intra_low_rate =
			ctx->bits_per_inter_low_rate * ctx->intra_to_inter_rate;
		ctx->bits_per_inter_high_rate = avg_high_rate >> 1;
		ctx->bits_per_intra_high_rate =
			ctx->bits_per_inter_high_rate * ctx->intra_to_inter_rate;
	} else {
		ctx->gop_mode = MPP_GOP_LARGE;
		ctx->intra_to_inter_rate = gop_len + 1;
		if (gop_len <= 50) {
			ctx->bits_per_intra_low_rate = ctx->bits_per_pic_low_rate * bit_ratio[0] / 2;
			ctx->bits_per_intra_high_rate = ctx->bits_per_pic_high_rate * bit_ratio[0] / 2;
		} else if (gop_len <= 100) {
			ctx->bits_per_intra_low_rate = ctx->bits_per_pic_low_rate * bit_ratio[1] / 2;
			ctx->bits_per_intra_high_rate = ctx->bits_per_pic_high_rate * bit_ratio[1] / 2;
		} else if (gop_len <= 200) {
			ctx->bits_per_intra_low_rate = ctx->bits_per_pic_low_rate * bit_ratio[2] / 2;
			ctx->bits_per_intra_high_rate = ctx->bits_per_pic_high_rate * bit_ratio[2] / 2;
		} else if (gop_len <= 300) {
			ctx->bits_per_intra_low_rate = ctx->bits_per_pic_low_rate * bit_ratio[3] / 2;
			ctx->bits_per_intra_high_rate = ctx->bits_per_pic_high_rate * bit_ratio[3] / 2;
		} else {
			ctx->bits_per_intra_low_rate = ctx->bits_per_pic_low_rate * bit_ratio[4] / 2;
			ctx->bits_per_intra_high_rate = ctx->bits_per_pic_high_rate * bit_ratio[4] / 2;
		}
		ctx->bits_per_inter_low_rate = ctx->bits_per_pic_low_rate;
		ctx->bits_per_inter_low_rate -=
			ctx->bits_per_intra_low_rate / (fps->fps_out_num - 1);
		ctx->bits_per_inter_high_rate = ctx->bits_per_pic_high_rate;
		ctx->bits_per_inter_high_rate -=
			ctx->bits_per_intra_high_rate / (fps->fps_out_num - 1);
	}

	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET bits_model_update_smt(RcModelV2SmtCtx * ctx, RK_S32 real_bit)
{
	// smt
	RK_S32 gop_len = ctx->usr_cfg.igop;
	RcFpsCfg *fps = &ctx->usr_cfg.fps;
	RK_S32 bps_target_temp = 0;
	RK_S32 mod = 0;

	rc_dbg_func("enter %p\n", ctx);

	mpp_data_update_v2(ctx->stat_bits, real_bit);
	ctx->pre_diff_bit_low_rate = ctx->bits_target_low_rate - real_bit;
	ctx->pre_diff_bit_high_rate = ctx->bits_target_high_rate - real_bit;
	ctx->bits_one_gop[ctx->frame_cnt_in_gop % 1000] = real_bit;
	ctx->frame_cnt_in_gop++;

	if (ctx->frame_cnt_in_gop == gop_len) {
		RK_S32 i = 0;
		RK_S32 gop_len_save = gop_len;
		ctx->frame_cnt_in_gop = 0;
		ctx->bits_one_gop_use_flag = 1;
		ctx->bits_one_gop_sum = 0;
		if (gop_len > 1000)
			gop_len_save = 1000;
		for (i = 0; i < gop_len_save; i++)
			ctx->bits_one_gop_sum += ctx->bits_one_gop[i];

		ctx->delta_bits_per_frame =
			ctx->bps_target_high_rate / (fps->fps_out_num) -
			ctx->bits_one_gop_sum / gop_len_save;
	}

	if (ctx->frame_type == INTRA_FRAME) {
		ctx->acc_intra_count++;
		ctx->acc_intra_bits_in_fps += real_bit;
		mpp_data_update(ctx->intra, real_bit);
		mpp_data_update(ctx->gop_bits, real_bit);
		mpp_pid_update(&ctx->pid_intra_low_rate,
			       real_bit - ctx->bits_target_low_rate);
		mpp_pid_update(&ctx->pid_intra_high_rate,
			       real_bit - ctx->bits_target_high_rate);
	} else {
		ctx->acc_inter_count++;
		ctx->acc_inter_bits_in_fps += real_bit;
		mpp_data_update(ctx->inter, real_bit);
		mpp_data_update(ctx->gop_bits, real_bit);
		mpp_pid_update(&ctx->pid_inter_low_rate,
			       real_bit - ctx->bits_target_low_rate);
		mpp_pid_update(&ctx->pid_inter_high_rate,
			       real_bit - ctx->bits_target_high_rate);
	}

	ctx->acc_total_count++;
	ctx->last_fps_bits += real_bit;
	/* new fps start */
	mod = ctx->acc_intra_count + ctx->acc_inter_count;
	mod = mod % fps->fps_out_num;
	if (0 == mod) {
		bps_target_temp =
			(ctx->bps_target_low_rate + ctx->bps_target_high_rate) >> 1;
		if (bps_target_temp * 3 > (ctx->last_fps_bits * 2))
			mpp_pid_update(&ctx->pid_fps, bps_target_temp - ctx->last_fps_bits);
		else {
			bps_target_temp = ctx->bps_target_low_rate * 4 / 10 + ctx->bps_target_high_rate * 6 / 10;
			mpp_pid_update(&ctx->pid_fps, bps_target_temp - ctx->last_fps_bits);
		}
		ctx->acc_intra_bits_in_fps = 0;
		ctx->acc_inter_bits_in_fps = 0;
		ctx->last_fps_bits = 0;
	}

	/* new frame start */
	ctx->qp_prev_out = ctx->qp_out;

	rc_dbg_func("leave %p\n", ctx);

	return MPP_OK;
}

MPP_RET reenc_calc_cbr_ratio_smt(RcModelV2SmtCtx * ctx, EncRcTaskInfo * cfg)
{
	RK_S32 stat_time = ctx->usr_cfg.stats_time;
	RK_S32 last_ins_bps = mpp_data_sum_v2(ctx->stat_bits) / stat_time;
	RK_S32 ins_bps =
		(last_ins_bps * stat_time - mpp_data_get_pre_val_v2(ctx->stat_bits,
								    -1) +
		 cfg->bit_real) / stat_time;
	RK_S32 real_bit = cfg->bit_real;
	RK_S32 target_bit = cfg->bit_target;
	RK_S32 target_bps = ctx->target_bps;
	RK_S32 water_level = 0;
	RK_S32 idx1, idx2;
	RK_S32 i_flag = 0;
	RK_S32 bit_diff_ratio, ins_ratio, bps_ratio, wl_ratio;

	rc_dbg_func("enter %p\n", ctx);

	i_flag = (ctx->frame_type == INTRA_FRAME);

	if (real_bit + ctx->stat_watl > ctx->stat_watl_thrd)
		water_level = ctx->stat_watl_thrd - ctx->bit_per_frame;
	else
		water_level =
			real_bit + ctx->stat_watl_thrd - ctx->bit_per_frame;

	if (water_level < ctx->stat_last_watl)
		water_level = ctx->stat_last_watl;

	if (target_bit > real_bit)
		bit_diff_ratio = 32 * (real_bit - target_bit) / target_bit;
	else
		bit_diff_ratio = 32 * (real_bit - target_bit) / real_bit;

	idx1 = ins_bps / (target_bps >> 5);
	idx2 = last_ins_bps / (target_bps >> 5);

	idx1 = mpp_clip(idx1, 0, 63);
	idx2 = mpp_clip(idx2, 0, 63);
	ins_ratio = tab_lnx[idx1] - tab_lnx[idx2];

	bps_ratio = 96 * (ins_bps - target_bps) / target_bps;
	wl_ratio =
		32 * (water_level -
		      (ctx->water_level_thr >> 3)) / (ctx->water_level_thr >> 3);
	if (last_ins_bps < ins_bps && target_bps != last_ins_bps) {
		ins_ratio = 6 * ins_ratio;
		ins_ratio = mpp_clip(ins_ratio, -192, 256);
	} else {
		if (i_flag) {
			ins_ratio = 3 * ins_ratio;
			ins_ratio = mpp_clip(ins_ratio, -192, 256);
		} else
			ins_ratio = 0;
	}
	if (bit_diff_ratio >= 256)
		bit_diff_ratio = 256;

	if (bps_ratio >= 32)
		bps_ratio = 32;

	if (wl_ratio >= 32)
		wl_ratio = 32;

	if (bit_diff_ratio < -128)
		ins_ratio = ins_ratio - 128;
	else
		ins_ratio = ins_ratio + bit_diff_ratio;

	if (bps_ratio < -32)
		ins_ratio = ins_ratio - 32;
	else
		ins_ratio = ins_ratio + bps_ratio;

	if (wl_ratio < -32)
		wl_ratio = ins_ratio - 32;
	else
		wl_ratio = ins_ratio + wl_ratio;

	ctx->next_ratio = wl_ratio;

	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET reenc_calc_vbr_ratio_smt(RcModelV2SmtCtx * ctx, EncRcTaskInfo * cfg)
{
	RK_S32 stat_time = ctx->usr_cfg.stats_time;
	RK_S32 last_ins_bps = mpp_data_sum_v2(ctx->stat_bits) / stat_time;
	RK_S32 ins_bps =
		(last_ins_bps * stat_time -
		 mpp_data_get_pre_val_v2(ctx->stat_bits, -1)
		 + cfg->bit_real) / stat_time;
	RK_S32 bps_change = ctx->target_bps;
	RK_S32 max_bps_target = ctx->usr_cfg.bps_max;
	RK_S32 real_bit = cfg->bit_real;
	RK_S32 target_bit = cfg->bit_target;
	RK_S32 idx1, idx2;
	RK_S32 bit_diff_ratio, ins_ratio, bps_ratio;

	rc_dbg_func("enter %p\n", ctx);

	if (target_bit <= real_bit)
		bit_diff_ratio = 32 * (real_bit - target_bit) / target_bit;
	else
		bit_diff_ratio = 32 * (real_bit - target_bit) / real_bit;

	idx1 = ins_bps / (max_bps_target >> 5);
	idx2 = last_ins_bps / (max_bps_target >> 5);
	idx1 = mpp_clip(idx1, 0, 63);
	idx2 = mpp_clip(idx2, 0, 63);
	if (last_ins_bps < ins_bps && bps_change < ins_bps) {
		ins_ratio = 6 * (tab_lnx[idx1] - tab_lnx[idx2]);
		ins_ratio = mpp_clip(ins_ratio, -192, 256);
	} else
		ins_ratio = 0;

	bps_ratio = 96 * (ins_bps - bps_change) / bps_change;

	if (bit_diff_ratio >= 256)
		bit_diff_ratio = 256;
	if (bps_ratio >= 32)
		bps_ratio = 32;

	if (bit_diff_ratio < -128)
		ins_ratio = ins_ratio - 128;
	else
		ins_ratio = bit_diff_ratio + ins_ratio;

	if (bps_ratio < -32)
		ins_ratio = ins_ratio - 32;
	else
		ins_ratio = ins_ratio + bps_ratio;

	ctx->next_ratio = ins_ratio;
	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET check_re_enc_smt(RcModelV2SmtCtx * ctx, EncRcTaskInfo * cfg)
{
	RK_S32 frame_type = ctx->frame_type;
	RK_S32 i_flag = 0;
	RK_S32 big_flag = 0;
	RK_S32 stat_time = ctx->usr_cfg.stats_time;
	RK_S32 last_ins_bps = mpp_data_sum_v2(ctx->stat_bits) / stat_time;
	RK_S32 ins_bps =
		(last_ins_bps * stat_time -
		 mpp_data_get_pre_val_v2(ctx->stat_bits, -1)
		 + cfg->bit_real) / stat_time;
	RK_S32 target_bps;
	RK_S32 flag1 = 0;
	RK_S32 flag2 = 0;

	rc_dbg_func("enter %p\n", ctx);

	if (ctx->usr_cfg.mode == RC_CBR)
		target_bps = ctx->usr_cfg.bps_target;
	else
		target_bps = ctx->usr_cfg.bps_max;

	if (ctx->reenc_cnt >= ctx->usr_cfg.max_reencode_times)
		return MPP_OK;

	i_flag = (frame_type == INTRA_FRAME);
	if (!i_flag && cfg->bit_real > 3 * cfg->bit_target)
		big_flag = 1;

	if (i_flag && cfg->bit_real > 3 * cfg->bit_target / 2)
		big_flag = 1;

	if (ctx->usr_cfg.mode == RC_CBR) {
		flag1 = target_bps / 20 < ins_bps - last_ins_bps;
		if (target_bps + target_bps / 10 < ins_bps ||
		    target_bps - target_bps / 10 > ins_bps)
			flag2 = 1;
	} else {
		flag1 = target_bps - (target_bps >> 3) < ins_bps;
		flag2 = target_bps / 20 < ins_bps - last_ins_bps;
	}

	if (!(big_flag && flag1 && flag2))
		return MPP_OK;

	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET rc_model_v2_smt_h265_init(void *ctx, RcCfg * cfg)
{
	RcModelV2SmtCtx *p = (RcModelV2SmtCtx *) ctx;

	rc_dbg_func("enter %p\n", ctx);

	p->codec_type = 1;
	memcpy(&p->usr_cfg, cfg, sizeof(RcCfg));
	bits_model_smt_init(p);

	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET rc_model_v2_smt_h264_init(void *ctx, RcCfg * cfg)
{
	RcModelV2SmtCtx *p = (RcModelV2SmtCtx *) ctx;

	rc_dbg_func("enter %p\n", ctx);

	p->codec_type = 0;
	memcpy(&p->usr_cfg, cfg, sizeof(RcCfg));
	bits_model_smt_init(p);

	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET rc_model_v2_smt_deinit(void *ctx)
{
	RcModelV2SmtCtx *p = (RcModelV2SmtCtx *) ctx;
	rc_dbg_func("enter %p\n", ctx);
	bits_model_smt_deinit(p);
	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

static void set_coef(void *ctx, RK_S32 *coef, RK_S32 val)
{
	RcModelV2SmtCtx *p = (RcModelV2SmtCtx *) ctx;
	if (mpp_data_sum_v2(p->complex_level) == 0)
		*coef = val + 0;

	else if (mpp_data_sum_v2(p->complex_level) == 1) {
		if (mpp_data_get_pre_val_v2(p->complex_level, 0) == 0)
			*coef = val + 10;
		else
			*coef = val + 25;
	} else if (mpp_data_sum_v2(p->complex_level) == 2) {
		if (mpp_data_get_pre_val_v2(p->complex_level, 0) == 0)
			*coef = val + 25;
		else
			*coef = val + 35;
	} else if (mpp_data_sum_v2(p->complex_level) == 3) {
		if (mpp_data_get_pre_val_v2(p->complex_level, 0) == 0)
			*coef = val + 35;
		else
			*coef = val + 51;
	} else if (mpp_data_sum_v2(p->complex_level) >= 4
		   && mpp_data_sum_v2(p->complex_level) <= 6) {
		if (mpp_data_get_pre_val_v2(p->complex_level, 0) == 0) {
			if (mpp_data_get_pre_val_v2(p->complex_level, 1) == 0)
				*coef = val + 35;
			else
				*coef = val + 51;
		} else
			*coef = val + 64;
	} else if (mpp_data_sum_v2(p->complex_level) >= 7
		   && mpp_data_sum_v2(p->complex_level) <= 9) {
		if (mpp_data_get_pre_val_v2(p->complex_level, 0) == 0) {
			if (mpp_data_get_pre_val_v2(p->complex_level, 1) == 0)
				*coef = val + 64;
			else
				*coef = val + 72;
		} else
			*coef = val + 72;
	} else
		*coef = val + 80;
}

static RK_U32 mb_num[9] = {
	0,      200,    700,    1200,
	2000,   4000,   8000,   16000,
	20000
};

static RK_U32 tab_bit[9] = {
	3780,  3570,  3150,  2940,
	2730,  3780,  2100,  1680,
	2100
};

static RK_U8 qscale2qp[96] = {
	15,  15,  15,  15,  15,  16, 18, 20, 21, 22, 23,
	24,  25,  25,  26,  27,  28, 28, 29, 29, 30, 30,
	30,  31,  31,  32,  32,  33, 33, 33, 34, 34, 34,
	34,  35,  35,  35,  36,  36, 36, 36, 36, 37, 37,
	37,  37,  38,  38,  38,  38, 38, 39, 39, 39, 39,
	39,  39,  40,  40,  40,  40, 41, 41, 41, 41, 41,
	41,  41,  42,  42,  42,  42, 42, 42, 42, 42, 43,
	43,  43,  43,  43,  43,  43, 43, 44, 44, 44, 44,
	44,  44,  44,  44,  45,  45, 45, 45,
};

static RK_S32 cal_first_i_start_qp(RK_S32 target_bit, RK_U32 total_mb)
{
	RK_S32 cnt = 0;
	RK_S32 index;
	RK_S32 i;

	for (i = 0; i < 8; i++) {
		if (mb_num[i] > total_mb)
			break;
		cnt++;
	}

	index = (total_mb * tab_bit[cnt] - 350) / target_bit; // qscale
	index = mpp_clip(index, 4, 95);

	return qscale2qp[index];
}

MPP_RET rc_model_v2_smt_start(void *ctx, EncRcTask * task)
{
	RcModelV2SmtCtx *p = (RcModelV2SmtCtx *) ctx;
	EncFrmStatus *frm = &task->frm;
	EncRcTaskInfo *info = &task->info;
	RcFpsCfg *fps = &p->usr_cfg.fps;
	RK_S32 madi = 0;
	RK_S32 qp_add = 0;
	RK_S32 qp_add_p = 0;
	RK_S32 qp_minus = 0;

	if (frm->reencode)
		return MPP_OK;

	if (frm->is_intra) {
		p->frame_type = INTRA_FRAME;
		p->acc_total_count = 0;
		p->acc_intra_bits_in_fps = 0;
	} else
		p->frame_type = INTER_P_FRAME;

	switch (p->gop_mode) {
	case MPP_GOP_ALL_INTER: {
		if (p->frame_type == INTRA_FRAME) {
			p->bits_target_low_rate = p->bits_per_intra_low_rate;
			p->bits_target_high_rate = p->bits_per_intra_high_rate;
		} else {
			p->bits_target_low_rate = p->bits_per_inter_low_rate - mpp_pid_calc(&p->pid_inter_low_rate);
			p->bits_target_high_rate = p->bits_per_inter_high_rate - mpp_pid_calc(&p->pid_inter_high_rate);
		}
	}
	break;
	case MPP_GOP_ALL_INTRA: {
		p->bits_target_low_rate = p->bits_per_intra_low_rate - mpp_pid_calc(&p->pid_intra_low_rate);
		p->bits_target_high_rate = p->bits_per_intra_high_rate - mpp_pid_calc(&p->pid_intra_high_rate);
	}
	break;
	default: {
		if (p->frame_type == INTRA_FRAME) {
			//float intra_percent = 0.0;
			RK_S32 diff_bit = mpp_pid_calc(&p->pid_fps);
			/* only affected by last gop */
			p->pre_gop_left_bit = p->pid_fps.i - diff_bit;

			mpp_pid_reset(&p->pid_fps);

			if (p->acc_intra_count) {
				p->bits_target_low_rate = (p->bits_per_intra_low_rate + diff_bit);
				p->bits_target_high_rate = (p->bits_per_intra_high_rate + diff_bit);
			} else {
				p->bits_target_low_rate = p->bits_per_intra_low_rate - mpp_pid_calc(&p->pid_intra_low_rate);
				p->bits_target_high_rate = p->bits_per_intra_high_rate - mpp_pid_calc(&p->pid_intra_high_rate);
			}
		} else {
			if (p->last_frame_type == INTRA_FRAME) {
				RK_S32 diff_bit = mpp_pid_calc(&p->pid_fps);
				/*
				 * case - inter frame after intra frame
				 * update inter target bits with compensation of previous intra frame
				 */
				RK_S32 bits_prev_intra = mpp_data_avg(p->intra, 1, 1, 1);

				p->bits_per_inter_low_rate = (p->bps_target_low_rate * p->gop_min  / fps->fps_out_num -
							      bits_prev_intra + diff_bit + p->pre_gop_left_bit) / (p->gop_min - 1);
				p->bits_target_low_rate = p->bits_per_inter_low_rate;
				p->bits_per_inter_high_rate = (p->bps_target_high_rate * p->gop_min  / fps->fps_out_num -
							       bits_prev_intra + diff_bit +  p->pre_gop_left_bit) / (p->gop_min - 1);
				p->bits_target_high_rate = p->bits_per_inter_high_rate;
			} else {
				RK_S32 diff_bit_low_rate = mpp_pid_calc (&p->pid_inter_low_rate);
				RK_S32 diff_bit_high_rate = mpp_pid_calc(&p->pid_inter_high_rate);

				p->bits_target_low_rate = p->bits_per_inter_low_rate - diff_bit_low_rate;
				if (p->bits_target_low_rate > p->bits_per_pic_low_rate * 2)
					p->bits_target_low_rate = 2 * p->bits_per_pic_low_rate;

				p->bits_target_high_rate = p->bits_per_inter_high_rate - diff_bit_high_rate;
				if (p->bits_target_high_rate > p->bits_per_pic_high_rate * 2)
					p->bits_target_high_rate = 2 * p->bits_per_pic_high_rate;
			}

		}
	}
	break;

	}

	if (NULL == p->qp_p)
		mpp_data_init(&p->qp_p, MPP_MIN(p->gop_min, 15));
	if (NULL == p->sse_p)
		mpp_data_init(&p->sse_p, MPP_MIN(p->gop_min, 15));

	madi = p->hal_cfg.madi;

	if (p->frm_num == 0) {
		RK_S32 mb_w = MPP_ALIGN(p->usr_cfg.width, 16) / 16;
		RK_S32 mb_h = MPP_ALIGN(p->usr_cfg.height, 16) / 16;
		p->bits_target_use =  (p->bits_target_high_rate - p->bits_target_low_rate) / 2 +
				      p->bits_target_low_rate;
		p->qp_out = cal_first_i_start_qp(p->bits_target_high_rate * 5, mb_w * mb_h);

		if (p->qp_out < p->usr_cfg.fm_lv_min_i_quality + 3)
			p->qp_out = p->usr_cfg.fm_lv_min_i_quality + 3;
		p->qp_out = mpp_clip(p->qp_out, p->qp_min, p->qp_max);
		p->qp_preavg = 0;
	}

	if (p->frame_type == INTRA_FRAME) {
		if (p->frm_num > 0) {
			RK_S32 sse = mpp_data_avg(p->sse_p, 1, 1, 1) + 1;
			RK_S32 bit_target_use =
				(p->bits_target_low_rate +
				 p->bits_target_high_rate) / 2;
			p->qp_out = mpp_data_avg(p->qp_p, -1, 1, 1) - 3;

			if (bit_target_use < 100)
				bit_target_use = 100;

			if (sse < p->intra_presse) {
				if (bit_target_use <= p->intra_prerealbit)
					p->qp_out = p->intra_preqp;

				else {
					if (madi >= MAD_THDI)
						p->qp_out = p->intra_preqp - 1;
					else {
						if (p->intra_preqp -  p->qp_out >= 4)
							p->qp_out = p->intra_preqp - (madi <= 12 ? 4 : 3);
					}
				}
			} else if (sse > p->intra_presse && p->qp_out < p->intra_preqp) {
				if (p->intra_preqp - p->qp_out >= 3)
					p->qp_out = p->intra_preqp - 2;
			} else {
				if (sse <= 2 * p->intra_presse)
					p->qp_out = p->intra_preqp;
				else if (sse <= 4 * p->intra_presse)
					p->qp_out = mpp_clip(p->qp_out, p->intra_preqp - 1, p->intra_preqp + 1);
				else
					p->qp_out = mpp_clip(p->qp_out, p->intra_preqp - 2, p->intra_preqp + 2);
			}
			if (p->qp_prev_out < 25)
				qp_add = 4;
			else if (p->qp_prev_out < 28)
				qp_add = 2;
			else if (p->qp_prev_out < 32)
				qp_add = 1;

			if (p->qp_prev_out > 45)
				qp_minus = 5;
			else if (p->qp_prev_out > 40)
				qp_minus = 4;
			else if (p->qp_prev_out  > 35)
				qp_minus = 3;
			p->qp_out = mpp_clip(p->qp_out, p->qp_min, p->qp_max);
			p->qp_out = mpp_clip(p->qp_out, p->qp_prev_out - 4 - qp_minus, p->qp_prev_out + qp_add);
			if (p->bits_target_low_rate + p->bits_target_high_rate < 0) {
				if (p->qp_out > 34) {
					sse = mpp_data_avg(p->sse_p, 1, 1, 1) + 1;
					bit_target_use = (p->bits_target_low_rate + p->bits_target_high_rate) / 2;
					qp_add = 0;
					qp_minus = 0;
					p->qp_out = mpp_data_avg(p->qp_p, -1, 1, 1) - 3;

					if (bit_target_use < 100)
						bit_target_use = 100;

					if (sse < p->intra_presse) {
						if (bit_target_use <= p->intra_prerealbit)
							p->qp_out = p->intra_preqp;
						else {
							if (madi >= MAD_THDI)
								p->qp_out = p->intra_preqp - 1;
							else {
								if (p->intra_preqp -  p->qp_out >= 4)
									p->qp_out = p->intra_preqp - (madi <= 12 ? 4 : 3);
							}
						}
					} else if (sse > p->intra_presse && p->qp_out < p->intra_preqp) {
						if (p->intra_preqp - p->qp_out >= 3)
							p->qp_out = p->intra_preqp - 2;
					} else {
						if (sse <= 2 * p->intra_presse)
							p->qp_out = p->intra_preqp;
						else if (sse <= 4 * p->intra_presse)
							p->qp_out = mpp_clip(p->qp_out, p->intra_preqp - 1, p->intra_preqp + 1);
						else
							p->qp_out = mpp_clip(p->qp_out, p->intra_preqp - 2, p->intra_preqp + 2);
					}
					if (p->qp_prev_out < 25)
						qp_add = 4;
					else if (p->qp_prev_out < 28)
						qp_add = 2;
					else if (p->qp_prev_out < 32)
						qp_add = 1;

					if (p->qp_prev_out > 45)
						qp_minus = 5;
					else if (p->qp_prev_out > 40)
						qp_minus = 4;
					else if (p->qp_prev_out  > 35)
						qp_minus = 3;
					p->qp_out = mpp_clip(p->qp_out, p->qp_min, p->qp_max);
					//mpp_log("lbr=%d, hbr=%d, qp_prev=%d, qpout=%d\n",p->bits_target_low_rate, p->bits_target_high_rate, p->qp_prev_out, p->qp_out);
					p->qp_out = mpp_clip(p->qp_out, p->qp_prev_out - 4 - qp_minus, p->qp_prev_out + qp_add);
					p->qp_out = mpp_clip(p->qp_out, 30, p->qp_prev_out + qp_add);
					//mpp_log("qpout_clip=%d\n", p->qp_out);
				}
			}

			p->bits_target_use =  (p->bits_target_low_rate + p->bits_target_high_rate) / 2;
			if (p->bits_target_use < 100)
				p->bits_target_use = 100;
		}
	} else {
		p->bits_target_use = (p->bits_target_low_rate + p->bits_target_high_rate) / 2;
		p->qp_out = p->qp_preavg;

		if (p->last_frame_type == INTRA_FRAME) {
			RK_S32 qp_add = 0;
			if (p->qp_prev_out < 25)
				qp_add = 2;

			else if (p->qp_prev_out < 29)
				qp_add = 1;
			p->qp_out = mpp_clip(p->qp_out, p->qp_prev_out + qp_add, p->qp_prev_out + 4 + qp_add);
			p->bits_target_use = (p->bits_target_low_rate + p->bits_target_high_rate) / 2;
			if (p->bits_target_use < 100)
				p->bits_target_use = 100;
		} else {
			RK_S32 bits_target_use = 0;
			RK_S32 pre_diff_bit_use = 0;
			RK_S32 frame_low_qp;
			RK_S32 low_pre_diff_bit_use = 0;
			RK_S32 coef = 1024;
			RK_S32 coef2 = 512;

			rc_dbg_rc("pre motion_level %u, sum %u, complex_level %u, sum %u\n",
				  mpp_data_get_pre_val_v2(p->motion_level, 0),
				  mpp_data_sum_v2(p->motion_level),
				  mpp_data_get_pre_val_v2(p->complex_level, 0),
				  mpp_data_sum_v2(p->complex_level));
			if (mpp_data_sum_v2(p->motion_level) == 0)
				set_coef(ctx, &coef, 0);

			else if (mpp_data_sum_v2(p->motion_level) == 1) {
				if (mpp_data_get_pre_val_v2(p->motion_level, 0) == 0)
					set_coef(ctx, &coef, 102);

				else
					set_coef(ctx, &coef, 154);
			} else if (mpp_data_sum_v2(p->motion_level) == 2) {
				if (mpp_data_get_pre_val_v2(p->motion_level, 0)
				    == 0)
					set_coef(ctx, &coef, 154);

				else if (mpp_data_get_pre_val_v2
					 (p->motion_level, 0) == 1) {
					if (mpp_data_get_pre_val_v2
					    (p->motion_level, 1) == 0)
						set_coef(ctx, &coef, 205);

					else if (mpp_data_get_pre_val_v2
						 (p->motion_level, 1) == 1)
						set_coef(ctx, &coef, 256);

					else
						set_coef(ctx, &coef, 307);
				} else
					set_coef(ctx, &coef, 307);
			} else if (mpp_data_sum_v2(p->motion_level) <= 5) {
				if (mpp_data_get_pre_val_v2(p->motion_level, 0) == 0) {
					if (mpp_data_get_pre_val_v2 (p->motion_level, 1) == 0)
						set_coef(ctx, &coef, 307);

					else if (mpp_data_get_pre_val_v2 (p->motion_level, 1) == 1)
						set_coef(ctx, &coef, 358);

					else
						set_coef(ctx, &coef, 410);
				} else if (mpp_data_get_pre_val_v2 (p->motion_level, 0) == 1) {
					if (mpp_data_get_pre_val_v2 (p->motion_level, 1) == 0)
						set_coef(ctx, &coef, 358);

					else if (mpp_data_get_pre_val_v2 (p->motion_level, 1) == 1)
						set_coef(ctx, &coef, 410);

					else
						set_coef(ctx, &coef, 461);
				} else
					set_coef(ctx, &coef, 461);
			} else if (mpp_data_sum_v2(p->motion_level) >= 6  && mpp_data_sum_v2(p->motion_level) <= 8) {
				if (mpp_data_get_pre_val_v2(p->motion_level, 0) == 0) {
					if (mpp_data_get_pre_val_v2(p->motion_level, 1) == 0)
						set_coef(ctx, &coef, 410);

					else if (mpp_data_get_pre_val_v2 (p->motion_level, 1) == 1)
						set_coef(ctx, &coef, 461);

					else
						set_coef(ctx, &coef, 512);
				} else if (mpp_data_get_pre_val_v2 (p->motion_level, 0) == 1) {
					if (mpp_data_get_pre_val_v2(p->motion_level, 1) == 0)
						set_coef(ctx, &coef, 512);

					else if (mpp_data_get_pre_val_v2 (p->motion_level, 1) == 1)
						set_coef(ctx, &coef, 563);

					else
						set_coef(ctx, &coef, 614);
				} else
					set_coef(ctx, &coef, 614);
			} else if (mpp_data_sum_v2(p->motion_level) >= 9  && mpp_data_sum_v2(p->motion_level) <= 14)
				set_coef(ctx, &coef, 666);

			else if (mpp_data_sum_v2(p->motion_level) >= 15 && mpp_data_sum_v2(p->motion_level) <= 18)
				set_coef(ctx, &coef, 768);

			else
				set_coef(ctx, &coef, 900);

			if (coef > 1024)
				coef = 1024;

			if (coef >= 900)
				coef2 = 1024;
			else if (coef >= 307)	// 0.7~0.3 --> 1.0~0.5
				coef2 = 512 + (coef - 307) * (1024 - 512) / (717 - 307);
			else	// 0.3~0.0 --> 0.5~0.0
				coef2 = 0 + coef * (512 - 0) / (307 - 0);
			bits_target_use = p->bits_target_low_rate;	// bits_target_high_rate
			pre_diff_bit_use = p->pre_diff_bit_low_rate;	// pre_diff_bit_high_rate

			bits_target_use = ((p->bits_target_high_rate - p->bits_target_low_rate) * coef2 +
					   p->bits_target_low_rate * 1024) >> 10;
			pre_diff_bit_use = ((p->pre_diff_bit_high_rate - p->pre_diff_bit_low_rate) * coef2 +
					    p->pre_diff_bit_low_rate * 1024) >> 10;
			//mpp_log("coef=%d, coef2=%d, md_lvl=%d, cplx_lvl=%d\n", coef, coef2, mpp_data_sum_v2(p->motion_level), mpp_data_sum_v2(p->complex_level));
			if (bits_target_use < 100)
				bits_target_use = 100;

			p->bits_target_use = bits_target_use;
			if (abs(pre_diff_bit_use) * 100 <= bits_target_use * 3)
				p->qp_out = p->qp_prev_out - 1;
			else if (pre_diff_bit_use * 100 > bits_target_use * 3) {
				if (pre_diff_bit_use >= bits_target_use)
					p->qp_out = (p->qp_out >= 30 && madi < MAD_THDI) ? p->qp_prev_out - 4 : p->qp_prev_out - 3;
				else if (pre_diff_bit_use * 4 >= bits_target_use * 1)
					p->qp_out = (p->qp_out >= 30 && madi < MAD_THDI) ? p->qp_prev_out - 3 : p->qp_prev_out - 2;
				else if (pre_diff_bit_use * 10 > bits_target_use * 1)
					p->qp_out = (madi < MAD_THDI) ? p->qp_prev_out - 2 : p->qp_prev_out - 1;
				else
					p->qp_out = p->qp_prev_out - 1;
			} else {
				RK_S32 weight = 1;
				pre_diff_bit_use = abs(pre_diff_bit_use);
				weight = (madi < MAD_THDI ? 1 : 3);
				if (pre_diff_bit_use >= 2 * weight * bits_target_use)
					p->qp_out = p->qp_prev_out + 3;
				else if (pre_diff_bit_use * 3 >= bits_target_use * 2 * weight)
					p->qp_out = p->qp_prev_out + 2;
				else if (pre_diff_bit_use * 5 >  bits_target_use * weight)
					p->qp_out = p->qp_prev_out + 1;
				else
					p->qp_out = p->qp_prev_out;
			}

			p->qp_out = mpp_clip(p->qp_out, p->qp_min, p->qp_max);

			pre_diff_bit_use = ((p->pre_diff_bit_high_rate - p->pre_diff_bit_low_rate) * coef2 +
					    p->pre_diff_bit_low_rate * 1024) >> 10;
			low_pre_diff_bit_use = LOW_PRE_DIFF_BIT_USE;
			low_pre_diff_bit_use = (p->bps_target_low_rate + p->bps_target_high_rate) / 2 / fps->fps_out_num;
			low_pre_diff_bit_use = -low_pre_diff_bit_use / 5;

			if (p->codec_type == 1) {
				if (madi >= MAD_THDI)
					frame_low_qp = LOW_QP_level1;
				else
					frame_low_qp = LOW_QP_level0;
			} else
				frame_low_qp = LOW_QP;

			if (p->qp_out > frame_low_qp) {
				if (pre_diff_bit_use <= 2 * low_pre_diff_bit_use)
					coef2 += 512;
				else if (pre_diff_bit_use <= low_pre_diff_bit_use)
					coef2 += 205;
				else
					coef2 += 102;

				if (coef2 >= 1024)
					coef2 = 1024;

				if (p->qp_out > LOW_LOW_QP)
					coef2 = 1024;

				if (coef2 == 1024 && p->qp_prev_out > frame_low_qp) {
					RK_S32 delta_coef = 0;
					if (p->bits_one_gop_use_flag == 1 && p->delta_bits_per_frame > 0) {
						delta_coef = p->delta_bits_per_frame * 1024 / (p->bits_per_inter_high_rate -
											       p->bits_per_inter_low_rate);
						if (delta_coef >= 512)
							delta_coef = 512;
						else if (delta_coef >= 307)
							delta_coef = 307;
						else
							delta_coef = delta_coef;

						if (p->qp_prev_out > (frame_low_qp + 1))
							delta_coef += 102;

						coef2 += delta_coef;
					}
				}
				bits_target_use = ((p->bits_target_high_rate - p->bits_target_low_rate) * coef2 +
						   p->bits_target_low_rate * 1024) >> 10;
				pre_diff_bit_use = ((p->pre_diff_bit_high_rate - p->pre_diff_bit_low_rate) * coef2 +
						    p->pre_diff_bit_low_rate * 1024) >> 10;
				if (bits_target_use < 100)
					bits_target_use = 100;

				p->bits_target_use = bits_target_use;
				if (abs(pre_diff_bit_use) * 100 <= bits_target_use *	 3)
					p->qp_out = p->qp_prev_out;
				else if (pre_diff_bit_use * 100 > bits_target_use * 3) {
					if (pre_diff_bit_use >= bits_target_use)
						p->qp_out = (p->qp_out >= 30 && madi < MAD_THDI) ?  p->qp_prev_out - 3 : p->qp_prev_out - 2;
					else if (pre_diff_bit_use * 4 >= bits_target_use * 1)
						p->qp_out = (p->qp_out >= 30 && madi < MAD_THDI) ? p->qp_prev_out - 2 : p->qp_prev_out - 1;
					else if (pre_diff_bit_use * 10 > bits_target_use * 1)
						p->qp_out = (madi <  MAD_THDI) ? p->qp_prev_out - 1 : p->qp_prev_out;
					else
						p->qp_out = p->qp_prev_out;
				} else {
					RK_S32 weight = 1;
					pre_diff_bit_use = abs(pre_diff_bit_use);
					weight = (madi < MAD_THDI ? 1 : 2);
					if (pre_diff_bit_use >= 2 * weight * bits_target_use)
						p->qp_out = p->qp_prev_out + 1;
					else if (pre_diff_bit_use * 3 >= bits_target_use * 2 * weight)
						p->qp_out = p->qp_prev_out + 1;
					else if (pre_diff_bit_use * 5 > bits_target_use * weight)
						p->qp_out = p->qp_prev_out;
					else
						p->qp_out = p->qp_prev_out;
				}
			}
			p->qp_out = mpp_clip(p->qp_out, p->qp_min, p->qp_max);
			if (p->qp_out > 45) {
				qp_add = 0;
				qp_minus = 5;
			} else if (p->qp_out > 40) {
				qp_add = 1;
				qp_minus = 4;
			} else if (p->qp_out > 36) {
				qp_add = 1;
				qp_minus = 3;
			} else if (p->qp_out > 33) {
				qp_add = 2;
				qp_minus = 2;
			} else if (p->qp_out > 30) {
				qp_add = 3;
				qp_minus = 1;
			} else {
				qp_add = 4;
				qp_minus = 0;
			}
			//mpp_log("lbr=%d, hbr=%d, qp_prev=%d, qpout=%d\n",p->bits_target_low_rate, p->bits_target_high_rate, p->qp_prev_out, p->qp_out);
			p->qp_out = mpp_clip(p->qp_out, p->qp_prev_out - qp_minus, p->qp_prev_out + qp_add);
			//mpp_log("qpout_clip=%d\n", p->qp_out);
		}
	}

	qp_add = 4;
	qp_add_p = 4;
	if (mpp_data_sum_v2(p->motion_level) >= 7 || mpp_data_get_pre_val_v2(p->motion_level, 0) == 2) {
		qp_add = 6;
		qp_add_p = 5;
		if (mpp_data_sum_v2(p->complex_level) >= 15) {
			qp_add = 7;
			qp_add_p = 6;
		}
	} else if (mpp_data_sum_v2(p->motion_level) >= 4
		   || mpp_data_get_pre_val_v2(p->motion_level, 0) == 1) {
		qp_add = 5;
		qp_add_p = 4;
		if (mpp_data_sum_v2(p->complex_level) >= 15) {
			qp_add = 6;
			qp_add_p = 5;
		}
	} else if (mpp_data_sum_v2(p->motion_level) >= 1) {
		qp_add = 4;
		qp_add_p = 4;
		if (mpp_data_sum_v2(p->complex_level) >= 15) {
			qp_add = 5;
			qp_add_p = 5;
		}
	} else if (mpp_data_sum_v2(p->complex_level) >= 12) {
		qp_add = 5;
		qp_add_p = 5;
	}

	if (p->frame_type == INTRA_FRAME) {
		if (p->qp_out < p->usr_cfg.fm_lv_min_i_quality + qp_add)
			p->qp_out = p->usr_cfg.fm_lv_min_i_quality + qp_add;
	} else {
		if (p->qp_out < p->usr_cfg.fm_lv_min_quality + qp_add_p)
			p->qp_out = p->usr_cfg.fm_lv_min_quality + qp_add_p;
	}

	info->bit_target = p->bits_target_use;
	info->quality_target = p->qp_out;
	info->quality_max = p->usr_cfg.max_quality;
	info->quality_min = p->usr_cfg.min_quality;
	p->frm_num++;
	p->reenc_cnt = 0;
	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET rc_model_v2_smt_check_reenc(void *ctx, EncRcTask * task)
{
	RcModelV2SmtCtx *p = (RcModelV2SmtCtx *) ctx;
	EncRcTaskInfo *cfg = (EncRcTaskInfo *) & task->info;
	EncFrmStatus *frm = &task->frm;
	RcCfg *usr_cfg = &p->usr_cfg;

	rc_dbg_func("enter ctx %p cfg %p\n", ctx, cfg);

	frm->reencode = 0;

	if ((usr_cfg->mode == RC_FIXQP) ||
	    (task->force.force_flag & ENC_RC_FORCE_QP))
		return MPP_OK;

	if (check_re_enc_smt(p, cfg)) {
		if (p->usr_cfg.mode == RC_CBR)
			reenc_calc_cbr_ratio_smt(p, cfg);
		else
			reenc_calc_vbr_ratio_smt(p, cfg);
		if (p->next_ratio != 0
		    && cfg->quality_target < cfg->quality_max) {
			p->reenc_cnt++;
			frm->reencode = 1;
		}
	}
	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET rc_model_v2_smt_end(void *ctx, EncRcTask * task)
{
	RcModelV2SmtCtx *p = (RcModelV2SmtCtx *) ctx;
	EncRcTaskInfo *cfg = (EncRcTaskInfo *) & task->info;
	RK_S32 bit_real = cfg->bit_real;
	RK_S32 madi = cfg->madi;
	RK_U32 qp_sum;
	RK_U32 avg_qp = 0;

	rc_dbg_func("enter ctx %p cfg %p\n", ctx, cfg);
	rc_dbg_rc("motion_level %u, complex_level %u\n", cfg->motion_level, cfg->complex_level);
	mpp_data_update_v2(p->motion_level, cfg->motion_level);
	mpp_data_update_v2(p->complex_level, cfg->complex_level);
	if (p->codec_type == 1)
		qp_sum = cfg->quality_real >> 5;	// 265
	else
		qp_sum = cfg->quality_real >> 4;	// 264

	avg_qp = qp_sum;
	p->qp_preavg = avg_qp;
	if (p->frame_type == INTER_P_FRAME) {
		if (madi >= MAD_THDI)
			avg_qp = p->qp_out;
	}

	if (p->frame_type == INTER_P_FRAME || p->gop_mode == MPP_GOP_ALL_INTRA) {
		mpp_data_update(p->qp_p, avg_qp);
		mpp_data_update(p->sse_p, cfg->madp);
	} else {
		p->intra_preqp = p->qp_out;
		p->intra_presse = cfg->madp;
		p->intra_premadi = madi;
		p->intra_prerealbit = bit_real;
	}

	p->st_madi = cfg->madi;
	rc_dbg_rc("bits_mode_update real_bit %d", bit_real);
	bits_model_update_smt(p, bit_real);
	p->pre_target_bits = cfg->bit_target;
	p->pre_real_bits = bit_real;
	p->qp_prev_out = p->qp_out;
	p->last_inst_bps = p->ins_bps;
	p->last_frame_type = p->frame_type;
	task->qp_out = p->qp_out;

	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

MPP_RET rc_model_v2_smt_hal_start(void *ctx, EncRcTask * task)
{
	rc_dbg_func("smt_hal_start enter ctx %p task %p\n", ctx, task);
	return MPP_OK;
}

MPP_RET rc_model_v2_smt_hal_end(void *ctx, EncRcTask * task)
{
	rc_dbg_func("smt_hal_end enter ctx %p task %p\n", ctx, task);
	rc_dbg_func("leave %p\n", ctx);
	return MPP_OK;
}

const RcImplApi smt_h264e = {
	"smart",
	MPP_VIDEO_CodingAVC,
	sizeof(RcModelV2SmtCtx),
	rc_model_v2_smt_h264_init,
	rc_model_v2_smt_deinit,
	NULL,
	rc_model_v2_smt_check_reenc,
	rc_model_v2_smt_start,
	rc_model_v2_smt_end,
	rc_model_v2_smt_hal_start,
	rc_model_v2_smt_hal_end,
	NULL,
};

const RcImplApi smt_h265e = {
	"smart",
	MPP_VIDEO_CodingHEVC,
	sizeof(RcModelV2SmtCtx),
	rc_model_v2_smt_h265_init,
	rc_model_v2_smt_deinit,
	NULL,
	rc_model_v2_smt_check_reenc,
	rc_model_v2_smt_start,
	rc_model_v2_smt_end,
	rc_model_v2_smt_hal_start,
	rc_model_v2_smt_hal_end,
	NULL,
};
#endif
