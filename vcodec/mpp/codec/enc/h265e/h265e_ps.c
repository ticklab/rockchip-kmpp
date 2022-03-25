// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#define MODULE_TAG "h265e_ps"
#include <linux/string.h>
#include "mpp_maths.h"
#include "h265e_ps.h"
#include "mpp_enc_refs.h"

#define MAX_UINT        0xFFFFFFFFU

typedef struct H265levelspec_t {
	RK_U32 maxLumaSamples;
	RK_U32 maxLumaSamplesPerSecond;
	RK_U32 maxBitrateMain;
	RK_U32 maxBitrateHigh;
	RK_U32 maxCpbSizeMain;
	RK_U32 maxCpbSizeHigh;
	RK_U32 minCompressionRatio;
	RK_S32 levelEnum;
	const char *name;
	RK_S32 levelIdc;
} H265levelspec;

H265levelspec levels[] = {
	{36864, 552960, 128, MAX_UINT, 350, MAX_UINT, 2, H265_LEVEL1, "1", 10},
	{122880, 3686400, 1500, MAX_UINT, 1500, MAX_UINT, 2, H265_LEVEL2, "2",
	 20},
	{245760, 7372800, 3000, MAX_UINT, 3000, MAX_UINT, 2, H265_LEVEL2_1,
	 "2.1", 21},
	{552960, 16588800, 6000, MAX_UINT, 6000, MAX_UINT, 2, H265_LEVEL3, "3",
	 30},
	{983040, 33177600, 10000, MAX_UINT, 10000, MAX_UINT, 2, H265_LEVEL3_1,
	 "3.1", 31},
	{2228224, 66846720, 12000, 30000, 12000, 30000, 4, H265_LEVEL4, "4",
	 40},
	{2228224, 133693440, 20000, 50000, 20000, 50000, 4, H265_LEVEL4_1,
	 "4.1", 41},
	{8912896, 267386880, 25000, 100000, 25000, 100000, 6, H265_LEVEL5, "5",
	 50},
	{8912896, 534773760, 40000, 160000, 40000, 160000, 8, H265_LEVEL5_1,
	 "5.1", 51},
	{8912896, 1069547520, 60000, 240000, 60000, 240000, 8, H265_LEVEL5_2,
	 "5.2", 52},
	{35651584, 1069547520, 60000, 240000, 60000, 240000, 8, H265_LEVEL6,
	 "6", 60},
	{35651584, 2139095040, 120000, 480000, 120000, 480000, 8, H265_LEVEL6_1,
	 "6.1", 61},
	{35651584, 4278190080U, 240000, 800000, 240000, 800000, 6,
	 H265_LEVEL6_2, "6.2", 62},
	{MAX_UINT, MAX_UINT, MAX_UINT, MAX_UINT, MAX_UINT, MAX_UINT, 1,
	 H265_LEVEL8_5, "8.5", 85},
};

void init_zscan2raster(RK_S32 maxDepth, RK_S32 depth, RK_U32 startVal,
		       RK_U32 ** curIdx)
{
	RK_S32 stride = 1 << (maxDepth - 1);
	if (depth == maxDepth) {
		(*curIdx)[0] = startVal;
		(*curIdx)++;
	} else {
		RK_S32 step = stride >> depth;
		init_zscan2raster(maxDepth, depth + 1, startVal, curIdx);
		init_zscan2raster(maxDepth, depth + 1, startVal + step, curIdx);
		init_zscan2raster(maxDepth, depth + 1, startVal + step * stride,
				  curIdx);
		init_zscan2raster(maxDepth, depth + 1,
				  startVal + step * stride + step, curIdx);
	}
}

void init_raster2zscan(RK_U32 maxCUSize, RK_U32 maxDepth, RK_U32 * raster2zscan,
		       RK_U32 * zscan2raster)
{
	RK_U32 unitSize = maxCUSize >> (maxDepth - 1);
	RK_U32 numPartInCUSize = (RK_U32) maxCUSize / unitSize;
	RK_U32 i;

	for (i = 0; i < numPartInCUSize * numPartInCUSize; i++) {
		raster2zscan[zscan2raster[i]] = i;
	}
}

void init_raster2pelxy(RK_U32 maxCUSize, RK_U32 maxDepth, RK_U32 * raster2pelx,
		       RK_U32 * raster2pely)
{
	RK_U32 i;

	RK_U32 *tempx = &raster2pelx[0];
	RK_U32 *tempy = &raster2pely[0];

	RK_U32 unitSize = maxCUSize >> (maxDepth - 1);

	RK_U32 numPartInCUSize = maxCUSize / unitSize;

	tempx[0] = 0;
	tempx++;
	for (i = 1; i < numPartInCUSize; i++) {
		tempx[0] = tempx[-1] + unitSize;
		tempy++;
	}

	for (i = 1; i < numPartInCUSize; i++) {
		memcpy(tempx, tempx - numPartInCUSize,
		       sizeof(RK_U32) * numPartInCUSize);
		tempx += numPartInCUSize;
	}

	for (i = 1; i < numPartInCUSize * numPartInCUSize; i++) {
		tempy[i] = (i / numPartInCUSize) * unitSize;
	}
}

MPP_RET h265e_set_vps(H265eCtx * ctx, h265_vps * vps)
{
	RK_S32 i;
	MppEncH265Cfg *codec = &ctx->cfg->codec.h265;
	profile_tier_level *profileTierLevel =
	    &vps->profile_tier_level.general_PTL;
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	RK_U32 maxlumas = prep->width * prep->height;
	RK_S32 level_idc = H265_LEVEL_NONE;

	vps->vps_video_parameter_set_id = 0;
	vps->vps_max_sub_layers_minus1 = 0;
	vps->vps_temporal_id_nesting_flag = 1;
	vps->vps_num_hrd_parameters = 0;
	vps->vps_max_nuh_reserved_zero_layer_id = 0;
	vps->hrd_parameters = NULL;
	for (i = 0; i < MAX_SUB_LAYERS; i++) {
		vps->vps_num_reorder_pics[i] = 0;
		vps->vps_max_dec_pic_buffering_minus1[i] =
		    MPP_MIN(MAX_REFS,
			    MPP_MAX((vps->vps_num_reorder_pics[i] + 3),
				    codec->num_ref) +
			    vps->vps_num_reorder_pics[i]) - 1;
		vps->vps_max_latency_increase_plus1[i] = 0;
	}
	memset(profileTierLevel->profile_compatibility_flag, 0,
	       sizeof(profileTierLevel->profile_compatibility_flag));
	memset(vps->profile_tier_level.sub_layer_profile_present_flag, 0,
	       sizeof(vps->profile_tier_level.sub_layer_profile_present_flag));
	memset(vps->profile_tier_level.sub_layer_level_present_flag, 0,
	       sizeof(vps->profile_tier_level.sub_layer_level_present_flag));
	for (i = 0; i < (RK_S32) MPP_ARRAY_ELEMS(levels); i++) {
		if (levels[i].maxLumaSamples >= maxlumas) {
			level_idc = levels[i].levelEnum;
			break;
		}
	}

	profileTierLevel->profile_space = 0;
	if (codec->level < level_idc) {
		profileTierLevel->general_level_idc = level_idc;
	} else {
		profileTierLevel->general_level_idc = codec->level;
	}
	profileTierLevel->tier_flag = codec->tier ? 1 : 0;
	profileTierLevel->profile_idc = codec->profile;

	profileTierLevel->profile_compatibility_flag[codec->profile] = 1;
	profileTierLevel->profile_compatibility_flag[2] = 1;

	profileTierLevel->general_progressive_source_flag = 1;
	profileTierLevel->general_non_packed_constraint_flag = 0;
	profileTierLevel->general_frame_only_constraint_flag = 0;
	return MPP_OK;
}

MPP_RET h265e_set_sps(H265eCtx * ctx, h265_sps * sps, h265_vps * vps)
{
	RK_U32 i, c;
	MppEncH265Cfg *codec = &ctx->cfg->codec.h265;
	MppEncPrepCfg *prep = &ctx->cfg->prep;
	MppEncRcCfg *rc = &ctx->cfg->rc;
	MppEncRefCfg ref_cfg = ctx->cfg->ref_cfg;
	MppEncH265VuiCfg *vui = &codec->vui;
	RK_S32 i_timebase_num = 1;
	RK_S32 i_timebase_den = rc->fps_out_num / rc->fps_out_denorm;
	RK_U8 convertToBit[MAX_CU_SIZE + 1];
	RK_U32 maxCUDepth, minCUDepth, addCUDepth;
	RK_S32 pad[2] = { 0 };
	RK_S32 minCUSize, log2MinCUSize;
	RK_S32 tuQTMinLog2Size = 2, tuQTMaxLog2Size;
	MppEncCpbInfo *cpb_info = mpp_enc_ref_cfg_get_cpb_info(ref_cfg);
	RK_U32 *tmp = &sps->zscan2raster[0];

	memset(convertToBit, -1, sizeof(convertToBit));
	c = 0;
	for (i = 4; i <= MAX_CU_SIZE; i *= 2) {
		convertToBit[i] = c;
		c++;
	}

	maxCUDepth = (uint32_t) convertToBit[codec->max_cu_size];

	minCUDepth = (codec->max_cu_size >> (maxCUDepth - 1));

	tuQTMaxLog2Size = convertToBit[codec->max_cu_size] + 2 - 1;

	addCUDepth = 0;
	while ((RK_U32) (codec->max_cu_size >> maxCUDepth) >
	       (1u << (tuQTMinLog2Size + addCUDepth))) {
		addCUDepth++;
	}

	maxCUDepth += addCUDepth;
	addCUDepth++;
	init_zscan2raster(maxCUDepth + 1, 1, 0, &tmp);
	init_raster2zscan(codec->max_cu_size, maxCUDepth + 1,
			  &sps->raster2zscan[0], &sps->zscan2raster[0]);
	init_raster2pelxy(codec->max_cu_size, maxCUDepth + 1,
			  &sps->raster2pelx[0], &sps->raster2pely[0]);

	if ((prep->width % minCUDepth) != 0) {
		RK_U32 padsize = 0;
		RK_U32 rem = prep->width % minCUDepth;
		padsize = minCUDepth - rem;
		pad[0] = padsize;	//pad width

		/* set the confirmation window offsets  */
		sps->vui.default_display_window_flag = 1;
		sps->vui.def_disp_win_right_offset = pad[0];
	}

	if ((prep->height % minCUDepth) != 0) {
		RK_U32 padsize = 0;
		RK_U32 rem = prep->height % minCUDepth;
		padsize = minCUDepth - rem;
		pad[1] = padsize;	//pad height

		/* set the confirmation window offsets  */
		sps->vui.default_display_window_flag = 1;
		sps->vui.def_disp_win_bottom_offset = pad[1];
	}
	// pad[0] = p->sourceWidth - p->oldSourceWidth;
	// pad[1] = p->sourceHeight - p->oldSourceHeight;

	sps->sps_seq_parameter_set_id = 0;
	sps->vps_video_parameter_set_id = 0;
	sps->chroma_format_idc = 0x1;	//RKVE_CSP2_I420;
	sps->vps_max_sub_layers_minus1 = 0;
	sps->pic_width_in_luma_samples = prep->width + pad[0];
	sps->pic_height_in_luma_samples = prep->height + pad[1];
	sps->log2_min_coding_block_size_minus3 = -3;
	sps->log2_diff_max_min_coding_block_size = 0;
	sps->max_cu_size = codec->max_cu_size;
	sps->max_cu_depth = maxCUDepth;
	sps->add_cu_depth = addCUDepth;
	sps->rps_list.num_short_term_ref_pic_sets = 0;
	sps->rps_list.m_referencePictureSets = NULL;

	minCUSize = sps->max_cu_size >> (sps->max_cu_depth - addCUDepth);
	log2MinCUSize = 0;
	while (minCUSize > 1) {
		minCUSize >>= 1;
		log2MinCUSize++;
	}
	sps->log2_min_coding_block_size_minus3 = log2MinCUSize - 3;
	sps->log2_diff_max_min_coding_block_size =
	    sps->max_cu_depth - addCUDepth;

	sps->pcm_log2_max_size = 5;
	sps->pcm_enabled_flag = 0;
	sps->pcm_log2_min_size = 3;

	sps->long_term_ref_pics_present_flag = 0;
	sps->log2_diff_max_min_transform_block_size =
	    tuQTMaxLog2Size - tuQTMinLog2Size;
	sps->log2_min_transform_block_size_minus2 = tuQTMinLog2Size - 2;
	sps->max_transform_hierarchy_depth_inter = 1;	//tuQTMaxInterDepth
	sps->max_transform_hierarchy_depth_intra = 1;	//tuQTMaxIntraDepth

	sps->amp_enabled_flag = codec->amp_enable;
	sps->bit_depth_luma_minus8 = 0;
	sps->bit_depth_chroma_minus8 = 0;

	sps->sample_adaptive_offset_enabled_flag =
	    !codec->sao_cfg.slice_sao_chroma_disable
	    || !codec->sao_cfg.slice_sao_luma_disable;

	sps->vps_max_sub_layers_minus1 = 0;
	sps->vps_temporal_id_nesting_flag = 1;

	for (i = 0; i < sps->vps_max_sub_layers_minus1 + 1; i++) {
		sps->vps_max_dec_pic_buffering_minus1[i] =
		    vps->vps_max_dec_pic_buffering_minus1[i];
		sps->vps_num_reorder_pics[i] = vps->vps_num_reorder_pics[i];
	}

	sps->pcm_bit_depth_luma = 8;
	sps->pcm_bit_depth_chroma = 8;

	sps->pcm_loop_filter_disable_flag = 0;
	sps->scaling_list_enabled_flag =
	    codec->trans_cfg.defalut_ScalingList_enable == 0 ? 0 : 1;

	sps->bits_for_poc = 16;
	sps->num_long_term_ref_pic_sps = 0;
	sps->long_term_ref_pics_present_flag = 0;
	sps->sps_temporal_mvp_enable_flag = codec->tmvp_enable;
	sps->sps_strong_intra_smoothing_enable_flag =
	    !codec->pu_cfg.strg_intra_smth_disable;
	if (cpb_info->max_lt_cnt) {
		sps->num_long_term_ref_pic_sps = cpb_info->max_lt_cnt;
		sps->long_term_ref_pics_present_flag = 1;
		sps->sps_temporal_mvp_enable_flag = 0;
		codec->tmvp_enable = 0;
	} else if (cpb_info->max_st_tid) {
		sps->sps_temporal_mvp_enable_flag = 0;
	}
	sps->profile_tier_level = &vps->profile_tier_level;
	sps->vui_parameters_present_flag = 1;
	if (sps->vui_parameters_present_flag) {
		sps->vui.aspect_ratio_info_present_flag = 0;
		sps->vui.aspect_ratio_idc = 0;
		sps->vui.sar_width = 0;
		sps->vui.sar_height = 0;
		sps->vui.overscan_info_present_flag = 0;
		sps->vui.overscan_appropriate_flag = 0;
		sps->vui.video_signal_type_present_flag = 0;
		sps->vui.video_format = MPP_FRAME_VIDEO_FMT_UNSPECIFIED;
		if (prep->range == MPP_FRAME_RANGE_JPEG) {
			sps->vui.video_full_range_flag = 1;
			sps->vui.video_signal_type_present_flag = 1;
		}

		if ((prep->colorprim <= MPP_FRAME_PRI_JEDEC_P22 &&
		     prep->colorprim != MPP_FRAME_PRI_UNSPECIFIED) ||
		    (prep->colortrc <= MPP_FRAME_TRC_ARIB_STD_B67 &&
		     prep->colortrc != MPP_FRAME_TRC_UNSPECIFIED) ||
		    (prep->color <= MPP_FRAME_SPC_ICTCP &&
		     prep->color != MPP_FRAME_SPC_UNSPECIFIED)) {
			sps->vui.video_signal_type_present_flag = 1;
			sps->vui.colour_description_present_flag = 1;
			sps->vui.colour_primaries = prep->colorprim;
			sps->vui.transfer_characteristics = prep->colortrc;
			sps->vui.matrix_coefficients = prep->color;
		}

		sps->vui.chroma_loc_info_present_flag = 0;
		sps->vui.chroma_sample_loc_type_top_field = 0;
		sps->vui.chroma_sample_loc_type_bottom_field = 0;
		sps->vui.neutral_chroma_indication_flag = 0;
		sps->vui.field_seq_flag = 0;
		sps->vui.frame_field_info_present_flag = 0;
		sps->vui.hrd_parameters_present_flag = 0;
		sps->vui.bitstream_restriction_flag = 0;
		sps->vui.tiles_fixed_structure_flag = 0;
		sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
		sps->vui.restricted_ref_pic_lists_flag = 1;
		sps->vui.min_spatial_segmentation_idc = 0;
		sps->vui.max_bytes_per_pic_denom = 2;
		sps->vui.max_bits_per_mincu_denom = 1;
		sps->vui.log2_max_mv_length_horizontal = 15;
		sps->vui.log2_max_mv_length_vertical = 15;
		if (vui->vui_aspect_ratio) {
			sps->vui.aspect_ratio_info_present_flag =
			    ! !vui->vui_aspect_ratio;
			sps->vui.aspect_ratio_idc = vui->vui_aspect_ratio;
		}
		sps->vui.vui_timing_info_present_flag = 1;
		sps->vui.vui_num_units_in_tick = i_timebase_num;
		sps->vui.vui_time_scale = i_timebase_den;
	}

	for (i = 0; i < MAX_SUB_LAYERS; i++) {
		sps->vps_max_latency_increase_plus1[i] = 0;
	}
	//  sps->m_scalingList = NULL;
	memset(sps->lt_ref_pic_poc_lsb_sps, 0,
	       sizeof(sps->lt_ref_pic_poc_lsb_sps));
	memset(sps->used_by_curr_pic_lt_sps_flag, 0,
	       sizeof(sps->used_by_curr_pic_lt_sps_flag));
	return 0;
}

MPP_RET h265e_set_pps(H265eCtx * ctx, h265_pps * pps, h265_sps * sps)
{
	MppEncH265Cfg *codec = &ctx->cfg->codec.h265;
	MppEncRcCfg *rc = &ctx->cfg->rc;
	pps->constrained_intra_pred_flag = 0;
	pps->pps_pic_parameter_set_id = 0;
	pps->pps_seq_parameter_set_id = 0;
	pps->init_qp_minus26 = 0;
	pps->cu_qp_delta_enabled_flag = 0;
	if (rc->rc_mode != MPP_ENC_RC_MODE_FIXQP) {
		pps->cu_qp_delta_enabled_flag = 1;
		pps->diff_cu_qp_delta_depth = 1;
	}

	pps->pps_slice_chroma_qp_offsets_present_flag =
	    (! !codec->trans_cfg.cb_qp_offset)
	    || (! !codec->trans_cfg.cr_qp_offset);

	pps->sps = sps;
	if (pps->pps_slice_chroma_qp_offsets_present_flag) {
		pps->pps_cb_qp_offset = codec->trans_cfg.cb_qp_offset;
		pps->pps_cr_qp_offset = codec->trans_cfg.cr_qp_offset;
	} else {
		pps->pps_cb_qp_offset = 0;
		pps->pps_cr_qp_offset = 0;
	}

	pps->entropy_coding_sync_enabled_flag = 0;
	pps->weighted_pred_flag = 0;
	pps->weighted_bipred_flag = 0;
	pps->output_flag_present_flag = 0;
	pps->sign_data_hiding_flag = 0;
	pps->init_qp_minus26 = codec->intra_qp - 26;
	pps->loop_filter_across_slices_enabled_flag =
	    codec->slice_cfg.loop_filter_across_slices_enabled_flag;
	pps->deblocking_filter_control_present_flag =
	    !codec->dblk_cfg.slice_deblocking_filter_disabled_flag;
	if (pps->deblocking_filter_control_present_flag) {
		pps->deblocking_filter_override_enabled_flag = 0;
		pps->pps_disable_deblocking_filter_flag =
		    (! !codec->dblk_cfg.slice_beta_offset_div2)
		    || (! !codec->dblk_cfg.slice_tc_offset_div2);
		if (!pps->pps_disable_deblocking_filter_flag) {
			pps->pps_beta_offset_div2 =
			    codec->dblk_cfg.slice_beta_offset_div2;
			pps->pps_tc_offset_div2 =
			    codec->dblk_cfg.slice_tc_offset_div2;
		}
	} else {
		pps->deblocking_filter_override_enabled_flag = 0;
		pps->pps_disable_deblocking_filter_flag = 0;
		pps->pps_beta_offset_div2 = 0;
		pps->pps_tc_offset_div2 = 0;
	}

	pps->lists_modification_present_flag = 1;
	pps->log2_parallel_merge_level_minus2 = 0;
	pps->num_ref_idx_l0_default_active_minus1 = 1;
	pps->num_ref_idx_l1_default_active_minus1 = 1;
	pps->transquant_bypass_enable_flag =
	    codec->trans_cfg.transquant_bypass_enabled_flag;
	pps->transform_skip_enabled_flag =
	    codec->trans_cfg.transform_skip_enabled_flag;

	pps->entropy_coding_sync_enabled_flag = 0;
	pps->sign_data_hiding_flag = 0;
	pps->cabac_init_present_flag = codec->entropy_cfg.cabac_init_flag;
	pps->slice_segment_header_extension_present_flag = 0;
	pps->num_extra_slice_header_bits = 0;
	pps->tiles_enabled_flag = 0;
	pps->uniform_spacing_flag = 0;
	pps->num_tile_rows_minus1 = 0;
	pps->num_tile_columns_minus1 = 0;
	pps->loop_filter_across_tiles_enabled_flag = 1;

#if 0
	if (sps->pic_width_in_luma_samples > 1920) {

		const char *soc_name = mpp_get_soc_name();
		/* check tile support on rk3566 and rk3568 */
		if (strstr(soc_name, "rk3566") || strstr(soc_name, "rk3568")) {
			if (sps->pic_width_in_luma_samples <= 3840) {
				pps->num_tile_columns_minus1 = 1;
			} else {
				pps->num_tile_columns_minus1 = 2;
			}

			if (pps->num_tile_columns_minus1) {
				pps->tiles_enabled_flag = 1;
				pps->uniform_spacing_flag = 1;
				pps->loop_filter_across_tiles_enabled_flag = 1;
			}
		}
	}
#endif
	return 0;
}
