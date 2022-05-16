// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#define MODULE_TAG  "vepu541_common"

#include <linux/string.h>

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_maths.h"
#include "jpege_syntax.h"
#include "vepu541_common.h"
#include "vepu540c_common.h"
#include "hal_enc_task.h"
#include "mpp_frame_impl.h"
#include "mpp_packet.h"
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
#include <soc/rockchip/rockchip_dvbm.h>
#endif

MPP_RET vepu540c_set_roi(void *roi_reg_base, MppEncROICfg * roi,
			 RK_S32 w, RK_S32 h)
{
	MppEncROIRegion *region = roi->regions;
	Vepu540cRoiCfg *roi_cfg = (Vepu540cRoiCfg *) roi_reg_base;
	Vepu540cRoiRegion *reg_regions = &roi_cfg->regions[0];
	MPP_RET ret = MPP_NOK;
	RK_S32 i = 0;
	memset(reg_regions, 0, sizeof(Vepu540cRoiRegion) * 8);
	if (NULL == roi_cfg || NULL == roi) {
		mpp_err_f("invalid buf %p roi %p\n", roi_cfg, roi);
		goto DONE;
	}

	if (roi->number > VEPU540C_MAX_ROI_NUM) {
		mpp_err_f("invalid region number %d\n", roi->number);
		goto DONE;
	}

	/* check region config */
	ret = MPP_OK;
	for (i = 0; i < (RK_S32) roi->number; i++, region++) {
		if (region->x + region->w > w || region->y + region->h > h)
			ret = MPP_NOK;

		if (region->intra > 1
		    || region->qp_area_idx >= VEPU541_MAX_ROI_NUM
		    || region->area_map_en > 1 || region->abs_qp_en > 1)
			ret = MPP_NOK;

		if ((region->abs_qp_en && region->quality > 51) ||
		    (!region->abs_qp_en
		     && (region->quality > 51 || region->quality < -51)))
			ret = MPP_NOK;

		if (ret) {
			mpp_err_f("region %d invalid param:\n", i);
			mpp_err_f("position [%d:%d:%d:%d] vs [%d:%d]\n",
				  region->x, region->y, region->w, region->h, w,
				  h);
			mpp_err_f("force intra %d qp area index %d\n",
				  region->intra, region->qp_area_idx);
			mpp_err_f("abs qp mode %d value %d\n",
				  region->abs_qp_en, region->quality);
			goto DONE;
		}
		reg_regions->roi_pos_lt.roi_lt_x =
			MPP_ALIGN(region->x, 16) >> 4;
		reg_regions->roi_pos_lt.roi_lt_y =
			MPP_ALIGN(region->y, 16) >> 4;
		reg_regions->roi_pos_rb.roi_rb_x =
			MPP_ALIGN(region->x + region->w, 16) >> 4;
		reg_regions->roi_pos_rb.roi_rb_y =
			MPP_ALIGN(region->y + region->h, 16) >> 4;
		reg_regions->roi_base.roi_qp_value = region->quality;
		reg_regions->roi_base.roi_qp_adj_mode = region->abs_qp_en;
		reg_regions->roi_base.roi_en = 1;
		reg_regions->roi_base.roi_pri = 0x1f;
		if (region->intra) {
			reg_regions->roi_mdc.roi_mdc_intra16 = 1;
			reg_regions->roi_mdc.roi0_mdc_intra32_hevc = 1;
		}
		reg_regions++;
	}

DONE:
	return ret;
}

#define THRES_BLK_MOVE_0 72
#define THRES_BLK_MOVE_1 200
MPP_RET vepu540c_set_qpmap_smart(void *roi_reg_base, MppBuffer mv_info, MppBuffer qpmap,
				 RK_U8 *mv_flag[3], RK_U8 *mv_index, RK_U32 qp_out,
				 RK_S32 w, RK_S32 h, RK_S32 is_hevc, RK_S32 is_idr)
{
	Vepu540cRoiCfg *roi_cfg = (Vepu540cRoiCfg *) roi_reg_base;
	RK_S32 i, j;
	RK_S32 mb_w, mb_h;
	RK_U32 *mdr = NULL;
	RK_U32 val;
	RK_U32 qpmap_index = 0;
	Vepu540cQpmapCfg *qpmap_cfg = NULL;
	RK_U8 index, refidx0, refidx1;
	RK_S32 cnt = 0;
	RK_U8 mv_final_flag;
	RK_U8 max_mv_final_flag;
	RK_S32 qp_delta_base;
	RK_S32 delta_qp_m;
	RK_S32 mv_blk_cnt = 0;
	RK_S32 coef_move;

	if (!mv_info || !qpmap || !mv_flag[0] || !mv_flag[1] || !mv_flag[2] || !mv_index)
		return MPP_NOK;

	roi_cfg->bmap_cfg.bmap_en = is_idr ? 0 : 1;
	roi_cfg->bmap_cfg.bmap_pri = 17;
	roi_cfg->bmap_cfg.bmap_qpmin = 10;
	roi_cfg->bmap_cfg.bmap_qpmax = 51;
	roi_cfg->bmap_cfg.bmap_mdc_dpth = 0;

	index = *mv_index;
	refidx0 = (index + 1) % 3;
	refidx1 = (index + 2) % 3;
	if (!is_hevc) {
		mdr = (RK_U32 *)mpp_buffer_get_ptr(mv_info);
		qpmap_cfg = (Vepu540cQpmapCfg *)mpp_buffer_get_ptr(qpmap);
		mb_w = MPP_ALIGN(w, 64) / 16;
		mb_h = MPP_ALIGN(h, 16) / 16;
		if (is_idr) {
			memset(mv_flag[0], 0, mb_w * mb_h);
			memset(mv_flag[1], 0, mb_w * mb_h);
			memset(mv_flag[2], 0, mb_w * mb_h);
			return MPP_NOK;
		}
		memset(qpmap_cfg, 0, sizeof(Vepu540cQpmapCfg) * mb_w * mb_h);
		memset(mv_flag[index], 0, mb_w * mb_h);
		for (i = 0; i < mb_h; i++) {
			for (j = 0; j < mb_w; j++) {
				val = mdr[i * mb_w + j] & 0xfff;
				if (val <= THRES_BLK_MOVE_0)
					mv_flag[index][cnt] = 0;
				else if (val <= THRES_BLK_MOVE_1)
					mv_flag[index][cnt] = 1;
				else
					mv_flag[index][cnt] = 2;
				if (val <= THRES_BLK_MOVE_0 && (2 == mv_flag[refidx0][cnt] || 2 == mv_flag[refidx1][cnt]))
					mv_blk_cnt ++;
				cnt++;
			}
		}

		if (qp_out < 36)
			qp_delta_base = 0;
		else if (qp_out < 42)
			qp_delta_base = 1;
		else if (qp_out < 46)
			qp_delta_base = 2;
		else
			qp_delta_base = 3;
		coef_move = mv_blk_cnt * 100;
		if (coef_move < 15 * cnt) {
			if (qp_out > 40)
				roi_cfg->bmap_cfg.bmap_qpmin = 28;
			else if (qp_out > 35)
				roi_cfg->bmap_cfg.bmap_qpmin = 25;
			else
				roi_cfg->bmap_cfg.bmap_qpmin = 23;

			dma_buf_begin_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
			for (i = 0; i < cnt; i++) {
				if (0 == mv_flag[index][i] && (2 == mv_flag[refidx0][i] || 2 == mv_flag[refidx1][i])) {
					delta_qp_m = qp_delta_base;
					if (coef_move < 1 * cnt)
						delta_qp_m += 5;
					else if (coef_move < 3 * cnt)
						delta_qp_m += 4;
					else if (coef_move < 7 * cnt)
						delta_qp_m += 3;
					else
						delta_qp_m += 2;
					qpmap_cfg[i].roi_qp_adju = 0x80 - delta_qp_m;
				}
			}
			dma_buf_end_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
		}
	} else {
		mdr = (RK_U32 *)mpp_buffer_get_ptr(mv_info);
		qpmap_cfg = (Vepu540cQpmapCfg *)mpp_buffer_get_ptr(qpmap);
		mb_w = MPP_ALIGN(w, 32) / 16;
		mb_h = MPP_ALIGN(h, 32) / 16;
		if (is_idr) {
			memset(mv_flag[0], 0, mb_w * mb_h);
			memset(mv_flag[1], 0, mb_w * mb_h);
			memset(mv_flag[2], 0, mb_w * mb_h);
			return MPP_NOK;
		}
		memset(qpmap_cfg, 0, sizeof(Vepu540cQpmapCfg) * mb_w * mb_h / 4);
		memset(mv_flag[index], 0, mb_w * mb_h);
		for (i = 0; i < mb_h; i++) {
			for (j = 0; j < mb_w; j++) {
				val = mdr[i * mb_w + j] & 0xfff;
				if (val <= THRES_BLK_MOVE_0)
					mv_flag[index][cnt] = 0;
				else if (val <= THRES_BLK_MOVE_1)
					mv_flag[index][cnt] = 1;
				else
					mv_flag[index][cnt] = 2;
				if (val <= THRES_BLK_MOVE_0 && (2 == mv_flag[refidx0][cnt] || 2 == mv_flag[refidx1][cnt]))
					mv_blk_cnt ++;
				cnt++;
			}
		}

		if (qp_out < 36)
			qp_delta_base = 0;
		else if (qp_out < 42)
			qp_delta_base = 1;
		else if (qp_out < 46)
			qp_delta_base = 2;
		else
			qp_delta_base = 3;
		coef_move = mv_blk_cnt * 100;
		max_mv_final_flag = 0;
		if (coef_move < 15 * cnt) {
			if (qp_out > 40)
				roi_cfg->bmap_cfg.bmap_qpmin = 28;
			else if (qp_out > 35)
				roi_cfg->bmap_cfg.bmap_qpmin = 25;
			else
				roi_cfg->bmap_cfg.bmap_qpmin = 23;

			dma_buf_begin_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
			for (i = 0; i < cnt; i++) {
				mv_final_flag = 0;
				if (mv_flag[index][i] == 0 && (2 == mv_flag[refidx0][i] || 2 == mv_flag[refidx1][i]))
					mv_final_flag = 1;
				max_mv_final_flag = max(max_mv_final_flag, mv_final_flag);

				if ((i + 1) % 4)
					continue;

				else {
					if (max_mv_final_flag > 0) {
						delta_qp_m = qp_delta_base;
						if (coef_move < 1 * cnt)
							delta_qp_m += 5;
						else if (coef_move < 3 * cnt)
							delta_qp_m += 4;
						else if (coef_move < 7 * cnt)
							delta_qp_m += 3;
						else
							delta_qp_m += 2;
						qpmap_cfg[qpmap_index++].roi_qp_adju = 0x80 - delta_qp_m;
					} else
						qpmap_index++;

					max_mv_final_flag = 0;
				}
			}
			dma_buf_end_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
		}
	}

	*mv_index = (*mv_index + 1) % 3;

	return MPP_OK;
}

MPP_RET vepu540c_set_qpmap_normal(void *roi_reg_base, MppBuffer mv_info, MppBuffer qpmap,
				  RK_U8 *mv_flag[3], RK_U8 *mv_index, RK_U32 qp_out,
				  RK_S32 w, RK_S32 h, RK_S32 is_hevc, RK_S32 is_idr)
{
	Vepu540cRoiCfg *roi_cfg = (Vepu540cRoiCfg *) roi_reg_base;
	RK_S32 i, j;
	RK_S32 mb_w, mb_h;
	RK_U32 *mdr = NULL;
	RK_U32 val;
	RK_U32 qpmap_index = 0;
	Vepu540cQpmapCfg *qpmap_cfg = NULL;
	RK_U8 index, refidx0, refidx1;
	RK_S32 cnt = 0;
	RK_U8 mv_final_flag;
	RK_U8 max_mv_final_flag;
	RK_S32 qp_delta_base;
	RK_S32 delta_qp_m;
	RK_S32 mv_blk_cnt = 0;
	RK_S32 coef_move;

	if (!mv_info || !qpmap || !mv_flag[0] || !mv_flag[1] || !mv_flag[2] || !mv_index)
		return MPP_NOK;

	roi_cfg->bmap_cfg.bmap_en = is_idr ? 0 : 1;
	roi_cfg->bmap_cfg.bmap_pri = 17;
	roi_cfg->bmap_cfg.bmap_qpmin = 10;
	roi_cfg->bmap_cfg.bmap_qpmax = 51;
	roi_cfg->bmap_cfg.bmap_mdc_dpth = 0;

	index = *mv_index;
	refidx0 = (index + 1) % 3;
	refidx1 = (index + 2) % 3;
	if (!is_hevc) {
		mdr = (RK_U32 *)mpp_buffer_get_ptr(mv_info);
		qpmap_cfg = (Vepu540cQpmapCfg *)mpp_buffer_get_ptr(qpmap);
		mb_w = MPP_ALIGN(w, 64) / 16;
		mb_h = MPP_ALIGN(h, 16) / 16;
		if (is_idr) {
			memset(mv_flag[0], 0, mb_w * mb_h);
			memset(mv_flag[1], 0, mb_w * mb_h);
			memset(mv_flag[2], 0, mb_w * mb_h);
			return MPP_NOK;
		}
		memset(qpmap_cfg, 0, sizeof(Vepu540cQpmapCfg) * mb_w * mb_h);
		memset(mv_flag[index], 0, mb_w * mb_h);
		for (i = 0; i < mb_h; i++) {
			for (j = 0; j < mb_w; j++) {
				val = mdr[i * mb_w + j] & 0xfff;
				if (val <= THRES_BLK_MOVE_0)
					mv_flag[index][cnt] = 0;
				else if (val <= THRES_BLK_MOVE_1)
					mv_flag[index][cnt] = 1;
				else
					mv_flag[index][cnt] = 2;
				if (val <= THRES_BLK_MOVE_0 && (2 == mv_flag[refidx0][cnt] || 2 == mv_flag[refidx1][cnt]))
					mv_blk_cnt ++;
				cnt++;
			}
		}

		if (qp_out < 36)
			qp_delta_base = 0;
		else if (qp_out < 42)
			qp_delta_base = 1;
		else if (qp_out < 46)
			qp_delta_base = 2;
		else
			qp_delta_base = 3;
		coef_move = mv_blk_cnt * 100;
		if (coef_move < 10 * cnt && coef_move > (cnt >> 5)) {
			if (qp_out > 40)
				roi_cfg->bmap_cfg.bmap_qpmin = 28;
			else if (qp_out > 35)
				roi_cfg->bmap_cfg.bmap_qpmin = 25;
			else
				roi_cfg->bmap_cfg.bmap_qpmin = 23;
		}

		if (coef_move < 10 * cnt) {
			dma_buf_begin_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
			for (i = 0; i < cnt; i++) {
				if (0 == mv_flag[index][i] && (2 == mv_flag[refidx0][i] || 2 == mv_flag[refidx1][i])) {
					delta_qp_m = qp_delta_base;
					if (coef_move < 1 * cnt)
						delta_qp_m += 5;
					else if (coef_move < 3 * cnt)
						delta_qp_m += 4;
					else if (coef_move < 7 * cnt)
						delta_qp_m += 3;
					else
						delta_qp_m += 2;
					qpmap_cfg[i].roi_qp_adju = 0x80 - delta_qp_m;
				}
			}
			dma_buf_end_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
		}
	} else {
		mdr = (RK_U32 *)mpp_buffer_get_ptr(mv_info);
		qpmap_cfg = (Vepu540cQpmapCfg *)mpp_buffer_get_ptr(qpmap);
		mb_w = MPP_ALIGN(w, 32) / 16;
		mb_h = MPP_ALIGN(h, 32) / 16;
		if (is_idr) {
			memset(mv_flag[0], 0, mb_w * mb_h);
			memset(mv_flag[1], 0, mb_w * mb_h);
			memset(mv_flag[2], 0, mb_w * mb_h);
			return MPP_NOK;
		}
		memset(qpmap_cfg, 0, sizeof(Vepu540cQpmapCfg) * mb_w * mb_h / 4);
		memset(mv_flag[index], 0, mb_w * mb_h);
		for (i = 0; i < mb_h; i++) {
			for (j = 0; j < mb_w; j++) {
				val = mdr[i * mb_w + j] & 0xfff;
				if (val <= THRES_BLK_MOVE_0)
					mv_flag[index][cnt] = 0;
				else if (val <= THRES_BLK_MOVE_1)
					mv_flag[index][cnt] = 1;
				else
					mv_flag[index][cnt] = 2;
				if (val <= THRES_BLK_MOVE_0 && (2 == mv_flag[refidx0][cnt] || 2 == mv_flag[refidx1][cnt]))
					mv_blk_cnt ++;
				cnt++;
			}
		}

		if (qp_out < 36)
			qp_delta_base = 0;
		else if (qp_out < 42)
			qp_delta_base = 1;
		else if (qp_out < 46)
			qp_delta_base = 2;
		else
			qp_delta_base = 3;
		coef_move = mv_blk_cnt * 100;
		max_mv_final_flag = 0;
		if (coef_move < 10 * cnt && coef_move > (cnt >> 5)) {
			if (qp_out > 40)
				roi_cfg->bmap_cfg.bmap_qpmin = 28;
			else if (qp_out > 35)
				roi_cfg->bmap_cfg.bmap_qpmin = 25;
			else
				roi_cfg->bmap_cfg.bmap_qpmin = 23;
		}

		if (coef_move < 10 * cnt) {
			dma_buf_begin_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
			for (i = 0; i < cnt; i++) {
				mv_final_flag = 0;
				if (mv_flag[index][i] == 0 && (2 == mv_flag[refidx0][i] || 2 == mv_flag[refidx1][i]))
					mv_final_flag = 1;
				max_mv_final_flag = max(max_mv_final_flag, mv_final_flag);

				if ((i + 1) % 4)
					continue;

				else {
					if (max_mv_final_flag > 0) {
						delta_qp_m = qp_delta_base;
						if (coef_move < 1 * cnt)
							delta_qp_m += 5;
						else if (coef_move < 3 * cnt)
							delta_qp_m += 4;
						else if (coef_move < 7 * cnt)
							delta_qp_m += 3;
						else
							delta_qp_m += 2;
						qpmap_cfg[qpmap_index++].roi_qp_adju = 0x80 - delta_qp_m;
					} else
						qpmap_index++;

					max_mv_final_flag = 0;
				}
			}
			dma_buf_end_cpu_access(mpp_buffer_get_dma(qpmap), DMA_FROM_DEVICE);
		}
	}

	*mv_index = (*mv_index + 1) % 3;

	return MPP_OK;
}

MPP_RET vepu540c_set_osd(Vepu540cOsdCfg * cfg)
{
	vepu540c_osd_reg *regs = (vepu540c_osd_reg *) (cfg->reg_base);
	//  MppDev dev = cfg->dev;
	MppEncOSDData3 *osd = cfg->osd_data3;
	MppEncOSDRegion3 *region = osd->region;
	MppEncOSDRegion3 *tmp = region;
	RK_U32 num;
	RK_U32 i = 0;

	if (!osd || osd->num_region == 0)
		return MPP_OK;

	if (osd->num_region > 8) {
		mpp_err_f("do NOT support more than 8 regions invalid num %d\n",
			  osd->num_region);
		mpp_assert(osd->num_region <= 8);
		return MPP_NOK;
	}
	num = osd->num_region;
	for (i = 0; i < num; i++, tmp++) {
		vepu540c_osd_com *reg = (vepu540c_osd_com *) & regs->osd_cfg[i];
		VepuFmtCfg fmt_cfg;
		MppFrameFormat fmt = tmp->fmt;
		vepu541_set_fmt(&fmt_cfg, fmt);
		reg->cfg0.osd_en = tmp->enable;
		reg->cfg0.osd_range_trns_en = tmp->range_trns_en;
		reg->cfg0.osd_range_trns_sel = tmp->range_trns_sel;
		reg->cfg0.osd_fmt = fmt_cfg.format;
		reg->cfg0.osd_rbuv_swap = tmp->rbuv_swap;
		reg->cfg1.osd_lt_xcrd = tmp->lt_x;
		reg->cfg1.osd_lt_ycrd = tmp->lt_y;
		reg->cfg2.osd_rb_xcrd = tmp->rb_x;
		reg->cfg2.osd_rb_ycrd = tmp->rb_y;
		reg->cfg1.osd_endn = tmp->osd_endn;
		reg->cfg5.osd_stride = tmp->stride;
		reg->cfg5.osd_ch_ds_mode = tmp->ch_ds_mode;

		reg->cfg0.osd_yg_inv_en = tmp->inv_cfg.yg_inv_en;
		reg->cfg0.osd_uvrb_inv_en = tmp->inv_cfg.uvrb_inv_en;
		reg->cfg0.osd_alpha_inv_en = tmp->inv_cfg.alpha_inv_en;
		reg->cfg0.osd_inv_sel = tmp->inv_cfg.inv_sel;
		reg->cfg2.osd_uv_sw_inv_en = tmp->inv_cfg.uv_sw_inv_en;
		reg->cfg2.osd_inv_size = tmp->inv_cfg.inv_size;
		reg->cfg5.osd_inv_stride = tmp->inv_cfg.inv_stride;

		reg->cfg0.osd_alpha_swap = tmp->alpha_cfg.alpha_swap;
		reg->cfg0.osd_bg_alpha = tmp->alpha_cfg.bg_alpha;
		reg->cfg0.osd_fg_alpha = tmp->alpha_cfg.fg_alpha;
		reg->cfg0.osd_fg_alpha_sel = tmp->alpha_cfg.fg_alpha_sel;

		reg->cfg0.osd_qp_adj_en = tmp->qp_cfg.qp_adj_en;
		reg->cfg8.osd_qp_adj_sel = tmp->qp_cfg.qp_adj_sel;
		reg->cfg8.osd_qp = tmp->qp_cfg.qp;
		reg->cfg8.osd_qp_max = tmp->qp_cfg.qp_max;
		reg->cfg8.osd_qp_min = tmp->qp_cfg.qp_min;
		reg->cfg8.osd_qp_prj = tmp->qp_cfg.qp_prj;
		if (tmp->inv_cfg.inv_buf.buf)
			reg->osd_inv_st_addr = mpp_dev_get_mpi_ioaddress(cfg->dev, tmp->inv_cfg.inv_buf.buf, 0);
		if (tmp->osd_buf.buf)
			reg->osd_st_addr = mpp_dev_get_mpi_ioaddress(cfg->dev, tmp->osd_buf.buf, 0);
		memcpy(reg->lut, tmp->lut, sizeof(tmp->lut));
	}

	regs->whi_cfg0.osd_csc_yr = 77;
	regs->whi_cfg0.osd_csc_yg = 150;
	regs->whi_cfg0.osd_csc_yb = 29;

	regs->whi_cfg1.osd_csc_ur = -43;
	regs->whi_cfg1.osd_csc_ug = -85;
	regs->whi_cfg1.osd_csc_ub = 128;

	regs->whi_cfg2.osd_csc_vr = 128;
	regs->whi_cfg2.osd_csc_vg = -107;
	regs->whi_cfg2.osd_csc_vb = -21;

	regs->whi_cfg3.osd_csc_ofst_y = 0;
	regs->whi_cfg3.osd_csc_ofst_u = 128;
	regs->whi_cfg3.osd_csc_ofst_v = 128;

	regs->whi_cfg4.osd_inv_yg_max = 255;
	regs->whi_cfg4.osd_inv_yg_min = 0;
	regs->whi_cfg4.osd_inv_uvrb_max = 255;
	regs->whi_cfg4.osd_inv_uvrb_min = 0;
	regs->whi_cfg5.osd_inv_alpha_max = 255;
	regs->whi_cfg5.osd_inv_alpha_min = 0;

	return MPP_OK;
}

MPP_RET vepu540c_osd_put_dma_buf(Vepu540cOsdCfg * cfg)
{

	MppEncOSDData3 *osd = cfg->osd_data3;
	MppEncOSDRegion3 *region = osd->region;
	MppEncOSDRegion3 *tmp = region;
	RK_U32 num;
	RK_U32 i = 0;

	if (!osd || osd->num_region == 0)
		return MPP_OK;

	if (osd->num_region > 8) {
		mpp_err_f("do NOT support more than 8 regions invalid num %d\n",
			  osd->num_region);
		mpp_assert(osd->num_region <= 8);
		return MPP_NOK;
	}
	num = osd->num_region;
	for (i = 0; i < num; i++, tmp++) {
		if (tmp->inv_cfg.inv_buf.buf)
			mpp_dev_release_mpi_ioaddress(cfg->dev, tmp->inv_cfg.inv_buf.buf);
		if (tmp->osd_buf.buf)
			mpp_dev_release_mpi_ioaddress(cfg->dev, tmp->osd_buf.buf);
	}
	return MPP_OK;
}

static MPP_RET
vepu540c_jpeg_set_uv_offset(Vepu540cJpegReg * regs, JpegeSyntax * syn,
			    Vepu541Fmt input_fmt, HalEncTask * task)
{
	RK_U32 hor_stride = syn->hor_stride;
	RK_U32 ver_stride = syn->ver_stride ? syn->ver_stride : syn->height;
	RK_U32 frame_size = hor_stride * ver_stride;
	RK_U32 u_offset = 0, v_offset = 0;
	MPP_RET ret = MPP_OK;

	if (MPP_FRAME_FMT_IS_FBC(mpp_frame_get_fmt(task->frame))) {
		u_offset = mpp_frame_get_fbc_offset(task->frame);
		v_offset = 0;
		mpp_log("fbc case u_offset = %d", u_offset);
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
		case VEPU541_FMT_BGR565:
		case VEPU541_FMT_BGR888:
		case VEPU541_FMT_BGRA8888: {
			u_offset = 0;
			v_offset = 0;
		}
		break;
		default: {
			mpp_err("unknown color space: %d\n", input_fmt);
			u_offset = frame_size;
			v_offset = frame_size * 5 / 4;
		}
		}
	}

	/* input cb addr */
	if (u_offset)
		regs->reg0265_adr_src1 += u_offset;
	/* input cr addr */
	if (v_offset)
		regs->reg0266_adr_src2 += v_offset;

	return ret;
}

MPP_RET vepu540c_set_jpeg_reg(Vepu540cJpegCfg * cfg)
{
	HalEncTask *task = (HalEncTask *) cfg->enc_task;
	JpegeSyntax *syn = (JpegeSyntax *) task->syntax.data;
	Vepu540cJpegReg *regs = (Vepu540cJpegReg *) cfg->jpeg_reg_base;
	VepuFmtCfg *fmt = (VepuFmtCfg *) cfg->input_fmt;
	RK_S32 stridey = 0;
	RK_S32 stridec = 0;

	if (!cfg->online) {
		regs->reg0264_adr_src0 = mpp_dev_get_iova_address(cfg->dev, task->input, 264);
		regs->reg0265_adr_src1 = regs->reg0264_adr_src0;
		regs->reg0266_adr_src2 = regs->reg0264_adr_src0;
		vepu540c_jpeg_set_uv_offset(regs, syn, (Vepu541Fmt) fmt->format, task);
	}

	regs->reg0257_adr_bsbb = mpp_dev_get_iova_address(cfg->dev, task->output->buf,
							  257) + task->output->start_offset;
	regs->reg0256_adr_bsbt = regs->reg0257_adr_bsbb + task->output->size - 1;
	regs->reg0258_adr_bsbr = regs->reg0257_adr_bsbb;
	regs->reg0259_adr_bsbs = regs->reg0257_adr_bsbb + mpp_packet_get_length(task->packet);

	regs->reg0272_enc_rsl.pic_wd8_m1 = MPP_ALIGN(syn->width, 16) / 8 - 1;
	regs->reg0273_src_fill.pic_wfill = MPP_ALIGN(syn->width, 16) - syn->width;
	regs->reg0272_enc_rsl.pic_hd8_m1 = MPP_ALIGN(syn->height, 16) / 8 - 1;
	regs->reg0273_src_fill.pic_hfill = MPP_ALIGN(syn->height, 16) - syn->height;

	regs->reg0274_src_fmt.src_cfmt = fmt->format;
	regs->reg0274_src_fmt.alpha_swap = fmt->alpha_swap;
	regs->reg0274_src_fmt.rbuv_swap = fmt->rbuv_swap;
	regs->reg0274_src_fmt.src_range_trns_en = 0;
	regs->reg0274_src_fmt.src_range_trns_sel = 0;
	regs->reg0274_src_fmt.chroma_ds_mode = 0;
	regs->reg0274_src_fmt.out_fmt = 1;

	regs->reg0279_src_proc.src_mirr = syn->mirroring > 0;
	regs->reg0279_src_proc.src_rot = syn->rotation;

	if (syn->hor_stride)
		stridey = syn->hor_stride;

	else {
		if (regs->reg0274_src_fmt.src_cfmt == VEPU541_FMT_BGRA8888)
			stridey = syn->width * 4;
		else if (regs->reg0274_src_fmt.src_cfmt == VEPU541_FMT_BGR888)
			stridey = syn->width * 3;
		else if (regs->reg0274_src_fmt.src_cfmt == VEPU541_FMT_BGR565 ||
			 regs->reg0274_src_fmt.src_cfmt == VEPU541_FMT_YUYV422
			 || regs->reg0274_src_fmt.src_cfmt ==
			 VEPU541_FMT_UYVY422)
			stridey = syn->width * 2;
	}

	stridec = (regs->reg0274_src_fmt.src_cfmt == VEPU541_FMT_YUV422SP ||
		   regs->reg0274_src_fmt.src_cfmt == VEPU541_FMT_YUV420SP) ?
		  stridey : stridey / 2;

	if (regs->reg0274_src_fmt.src_cfmt < VEPU541_FMT_ARGB1555) {
		regs->reg0275_src_udfy.csc_wgt_r2y = 77;
		regs->reg0275_src_udfy.csc_wgt_g2y = 150;
		regs->reg0275_src_udfy.csc_wgt_b2y = 29;

		regs->reg0276_src_udfu.csc_wgt_r2u = -43;
		regs->reg0276_src_udfu.csc_wgt_g2u = -85;
		regs->reg0276_src_udfu.csc_wgt_b2u = 128;

		regs->reg0277_src_udfv.csc_wgt_r2v = 128;
		regs->reg0277_src_udfv.csc_wgt_g2v = -107;
		regs->reg0277_src_udfv.csc_wgt_b2v = -21;

		regs->reg0278_src_udfo.csc_ofst_y = 0;
		regs->reg0278_src_udfo.csc_ofst_u = 128;
		regs->reg0278_src_udfo.csc_ofst_v = 128;
	}
	regs->reg0281_src_strd0.src_strd0 = stridey;
	regs->reg0282_src_strd1.src_strd1 = stridec;
	regs->reg0280_pic_ofst.pic_ofst_y = mpp_frame_get_offset_y(task->frame);
	regs->reg0280_pic_ofst.pic_ofst_x = mpp_frame_get_offset_x(task->frame);
	//to be done

	regs->reg0283_src_flt.pp_corner_filter_strength = 0;
	regs->reg0283_src_flt.pp_edge_filter_strength = 0;
	regs->reg0283_src_flt.pp_internal_filter_strength = 0;

	regs->reg0284_y_cfg.bias_y = 0;
	regs->reg0285_u_cfg.bias_u = 0;
	regs->reg0286_v_cfg.bias_v = 0;

	regs->reg0287_base_cfg.jpeg_ri = 0;
	regs->reg0287_base_cfg.jpeg_out_mode = 0;
	regs->reg0287_base_cfg.jpeg_start_rst_m = 0;
	regs->reg0287_base_cfg.jpeg_pic_last_ecs = 1;
	regs->reg0287_base_cfg.jpeg_slen_fifo = 0;
	regs->reg0287_base_cfg.jpeg_stnd = 1;	//enable

	regs->reg0288_uvc_cfg.uvc_partition0_len = 0;
	regs->reg0288_uvc_cfg.uvc_partition_len = 0;
	regs->reg0288_uvc_cfg.uvc_skip_len = 0;

	if (cfg->online) {
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
		struct dvbm_addr_cfg dvbm_adr;
		RK_U32 is_full = mpp_frame_get_is_full(task->frame);

		if (!is_full) {
			rk_dvbm_ctrl(NULL, DVBM_VEPU_GET_ADR, &dvbm_adr);
			if (dvbm_adr.overflow) {
				mpp_err("cur frame already overflow [%d %d]!\n",
					dvbm_adr.frame_id, dvbm_adr.line_cnt);
				return MPP_NOK;
			}
			regs->reg0260_adr_vsy_b = dvbm_adr.ybuf_bot;
			regs->reg0261_adr_vsc_b = dvbm_adr.cbuf_bot;
			regs->reg0262_adr_vsy_t = dvbm_adr.ybuf_top;
			regs->reg0263_adr_vsc_t = dvbm_adr.cbuf_top;
			regs->reg0264_adr_src0 = dvbm_adr.ybuf_sadr;
			regs->reg0265_adr_src1 = dvbm_adr.cbuf_sadr;
			regs->reg0266_adr_src2 = dvbm_adr.cbuf_sadr;
		} else {

			RK_U32 phy_addr = mpp_frame_get_phy_addr(task->frame);
			if (phy_addr) {
				regs->reg0264_adr_src0 = phy_addr;
				regs->reg0265_adr_src1 = regs->reg0264_adr_src0;
				regs->reg0266_adr_src2 = regs->reg0264_adr_src0;
				vepu540c_jpeg_set_uv_offset(regs, syn, (Vepu541Fmt) fmt->format, task);
			} else
				mpp_err("online case set full frame err");
		}
#else
		regs->reg0260_adr_vsy_b = 0;
		regs->reg0261_adr_vsc_b = 0;
		regs->reg0262_adr_vsy_t = 0;
		regs->reg0263_adr_vsc_t = 0;
#endif
	}
	return MPP_OK;
}

void vepu540c_set_dvbm(vepu540c_online *online_addr)
{
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct dvbm_addr_cfg dvbm_adr;

	rk_dvbm_ctrl(NULL, DVBM_VEPU_GET_ADR, &dvbm_adr);
	online_addr->reg0156_adr_vsy_t = dvbm_adr.ybuf_top;
	online_addr->reg0157_adr_vsc_t = dvbm_adr.cbuf_top;
	online_addr->reg0158_adr_vsy_b = dvbm_adr.ybuf_bot;
	online_addr->reg0159_adr_vsc_b = dvbm_adr.cbuf_bot;
#else
	online_addr->reg0156_adr_vsy_t = 0;
	online_addr->reg0157_adr_vsc_t = 0;
	online_addr->reg0158_adr_vsy_b = 0;
	online_addr->reg0159_adr_vsc_b = 0;
#endif
}
