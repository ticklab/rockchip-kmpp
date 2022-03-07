// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#define MODULE_TAG "H265E_SYNTAX"

#include <linux/string.h>

#include "h265e_codec.h"
#include "h265e_syntax_new.h"

static void fill_picture_parameters(const H265eCtx * h, H265ePicParams * pp)
{
	const h265_pps *pps = (h265_pps *) & h->pps;
	const h265_sps *sps = (h265_sps *) & h->sps;
	MppEncCfgSet *cfg = h->cfg;
	memset(pp, 0, sizeof(H265ePicParams));

	pp->pic_width = h->cfg->prep.width;
	pp->pic_height = h->cfg->prep.height;
	pp->hor_stride = h->cfg->prep.hor_stride;
	pp->ver_stride = h->cfg->prep.ver_stride;
	pp->pps_id = h->slice->pps_pic_parameter_set_id;
	pp->sps_id = pps->pps_seq_parameter_set_id;
	pp->vps_id = sps->vps_video_parameter_set_id;
	pp->mpp_format = cfg->prep.format;

	pp->wFormatAndSequenceInfoFlags = (sps->chroma_format_idc << 0) |
	    (sps->separate_colour_plane_flag << 2) |
	    ((sps->bit_depth_luma_minus8) << 3) |
	    ((sps->bit_depth_chroma_minus8) << 6) |
	    ((sps->bits_for_poc - 4) << 9) | (0 << 13) | (0 << 14) | (0 << 15);

	pp->sps_max_dec_pic_buffering_minus1 =
	    sps->vps_max_dec_pic_buffering_minus1[sps->
						  vps_max_sub_layers_minus1];
	pp->log2_min_luma_coding_block_size_minus3 =
	    sps->log2_min_coding_block_size_minus3;
	pp->log2_diff_max_min_luma_coding_block_size =
	    sps->log2_diff_max_min_coding_block_size;

	pp->log2_min_transform_block_size_minus2 =
	    sps->log2_min_transform_block_size_minus2;
	pp->log2_diff_max_min_transform_block_size =
	    sps->log2_diff_max_min_transform_block_size;

	pp->max_transform_hierarchy_depth_inter =
	    sps->max_transform_hierarchy_depth_inter;
	pp->max_transform_hierarchy_depth_intra =
	    sps->max_transform_hierarchy_depth_intra;

	pp->num_short_term_ref_pic_sets =
	    sps->rps_list.num_short_term_ref_pic_sets;
	pp->num_long_term_ref_pics_sps = sps->num_long_term_ref_pic_sps;

	pp->sample_adaptive_offset_enabled_flag =
	    sps->sample_adaptive_offset_enabled_flag;

	pp->num_ref_idx_l0_default_active_minus1 =
	    pps->num_ref_idx_l0_default_active_minus1 - 1;
	pp->num_ref_idx_l1_default_active_minus1 =
	    pps->num_ref_idx_l1_default_active_minus1 - 1;
	pp->init_qp_minus26 = pps->init_qp_minus26;

	pp->CodingParamToolFlags = (sps->scaling_list_enabled_flag << 0) |
	    (sps->amp_enabled_flag << 1) |
	    (sps->sample_adaptive_offset_enabled_flag << 2) |
	    (sps->pcm_enabled_flag << 3) |
	    ((sps->pcm_enabled_flag ? (sps->pcm_bit_depth_luma - 1) : 0) << 4) |
	    ((sps->
	      pcm_enabled_flag ? (sps->pcm_bit_depth_chroma -
				  1) : 0) << 8) | ((sps->
						    pcm_enabled_flag ? (sps->
									pcm_log2_min_size
									-
									3) : 0)
						   << 12) | ((sps->
							      pcm_enabled_flag
							      ? (sps->
								 pcm_log2_max_size
								 -
								 sps->
								 pcm_log2_min_size)
							      : 0) << 14) |
	    (sps->pcm_loop_filter_disable_flag << 16) | (sps->
							 long_term_ref_pics_present_flag
							 << 17) | (sps->
								   sps_temporal_mvp_enable_flag
								   << 18) |
	    (sps->
	     sps_strong_intra_smoothing_enable_flag << 19) | (0 << 20) | (pps->
									  output_flag_present_flag
									  << 21)
	    | (pps->num_extra_slice_header_bits << 22) | (pps->
							  sign_data_hiding_flag
							  << 25) | (pps->
								    cabac_init_present_flag
								    << 26) | (0
									      <<
									      27);

	pp->CodingSettingPicturePropertyFlags =
	    (pps->constrained_intra_pred_flag << 0) | (pps->
						       transform_skip_enabled_flag
						       << 1) | (pps->
								cu_qp_delta_enabled_flag
								<< 2) | (pps->
									 pps_slice_chroma_qp_offsets_present_flag
									 << 3) |
	    (pps->weighted_pred_flag << 4) | (pps->
					      weighted_bipred_flag << 5) |
	    (pps->transquant_bypass_enable_flag << 6) | (pps->
							 tiles_enabled_flag <<
							 7) | (pps->
							       entropy_coding_sync_enabled_flag
							       << 8) | (pps->
									uniform_spacing_flag
									<< 9) |
	    (pps->loop_filter_across_tiles_enabled_flag << 10) | (pps->
								  loop_filter_across_slices_enabled_flag
								  << 11) |
	    (pps->deblocking_filter_override_enabled_flag << 12) | (pps->
								    pps_disable_deblocking_filter_flag
								    << 13) |
	    (pps->lists_modification_present_flag << 14) | (pps->
							    slice_segment_header_extension_present_flag
							    << 15);

	pp->pps_cb_qp_offset = pps->pps_cb_qp_offset;
	pp->pps_cr_qp_offset = pps->pps_cr_qp_offset;
	pp->diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth;
	pp->pps_beta_offset_div2 = pps->pps_beta_offset_div2;
	pp->pps_tc_offset_div2 = pps->pps_tc_offset_div2;
	pp->log2_parallel_merge_level_minus2 =
	    pps->log2_parallel_merge_level_minus2;
	if (pps->tiles_enabled_flag) {
		RK_U8 i = 0;

		mpp_assert(pps->num_tile_columns_minus1 <= 19);
		mpp_assert(pps->num_tile_rows_minus1 <= 21);

		pp->num_tile_columns_minus1 = pps->num_tile_columns_minus1;
		pp->num_tile_rows_minus1 = pps->num_tile_rows_minus1;

		for (i = 0; i < pp->num_tile_columns_minus1; i++)
			pp->column_width_minus1[i] =
			    pps->tile_column_width_array[i];

		for (i = 0; i < pp->num_tile_rows_minus1; i++)
			pp->row_height_minus1[i] =
			    pps->tile_row_height_array[i];
	}
}

static void fill_slice_parameters(const H265eCtx * h, H265eSlicParams * sp)
{
	MppEncH265Cfg *codec = &h->cfg->codec.h265;
	h265_slice *slice = h->slice;
	memset(sp, 0, sizeof(H265eSlicParams));
	if (codec->slice_cfg.split_enable) {
		sp->sli_splt_cpst = 1;
		sp->sli_splt = 1;
		sp->sli_splt_mode = codec->slice_cfg.split_mode;
		if (codec->slice_cfg.split_mode) {
			sp->sli_splt_cnum_m1 = codec->slice_cfg.slice_size - 1;
		} else {
			sp->sli_splt_byte = codec->slice_cfg.slice_size;
		}
		sp->sli_max_num_m1 = 50;
		sp->sli_flsh = 1;
	}

	sp->cbc_init_flg = slice->cabac_init_flag;
	sp->mvd_l1_zero_flg = slice->lmvd_l1_zero;
	sp->merge_up_flag = codec->merge_cfg.merge_up_flag;
	sp->merge_left_flag = codec->merge_cfg.merge_left_flag;
	sp->ref_pic_lst_mdf_l0 = slice->ref_pic_list_modification_flag_l0;

	sp->num_refidx_l1_act = 0;
	sp->num_refidx_l0_act = 1;

	sp->num_refidx_act_ovrd =
	    (((RK_U32) slice->num_ref_idx[0] !=
	      slice->pps->num_ref_idx_l0_default_active_minus1)
	     || (slice->slice_type == B_SLICE
		 && (RK_U32) slice->num_ref_idx[1] !=
		 slice->pps->num_ref_idx_l1_default_active_minus1));

	sp->sli_sao_chrm_flg = slice->sps->sample_adaptive_offset_enabled_flag
	    && slice->sao_enable_flag_chroma;
	sp->sli_sao_luma_flg = slice->sps->sample_adaptive_offset_enabled_flag
	    && slice->sao_enable_flag;

	sp->sli_tmprl_mvp_en = slice->tmprl_mvp_en;
	sp->pic_out_flg = slice->pic_output_flag;
	sp->slice_type = slice->slice_type;
	sp->slice_rsrv_flg = 0;
	sp->dpdnt_sli_seg_flg = 0;
	sp->sli_pps_id = slice->pps->pps_pic_parameter_set_id;
	sp->no_out_pri_pic = slice->no_output_of_prior_pics_flag;

	sp->sli_tc_ofst_div2 = slice->pps_tc_offset_div2;
	sp->sli_beta_ofst_div2 = slice->pps_beta_offset_div2;
	sp->sli_lp_fltr_acrs_sli =
	    slice->loop_filter_across_slices_enabled_flag;
	sp->sli_dblk_fltr_dis = slice->deblocking_filter_disable;
	sp->dblk_fltr_ovrd_flg = slice->deblocking_filter_override_flag;
	sp->sli_cb_qp_ofst = slice->slice_qp_delta_cb;
	sp->sli_qp = slice->slice_qp;
	sp->max_mrg_cnd = slice->max_num_merge_cand;
	sp->non_reference_flag = slice->temporal_layer_non_ref_flag;
	sp->col_ref_idx = 0;
	sp->col_frm_l0_flg = slice->col_from_l0_flag;
	sp->sli_poc_lsb =
	    (slice->poc - slice->last_idr +
	     (1 << slice->sps->bits_for_poc)) % (1 << slice->sps->bits_for_poc);

	sp->sli_hdr_ext_len = slice->slice_header_extension_length;
}

RK_S32 fill_ref_parameters(const H265eCtx * h, H265eSlicParams * sp)
{
	h265_slice *slice = h->slice;
	h265_reference_picture_set *rps = slice->rps;
	RK_U32 numRpsCurrTempList = 0;
	RK_S32 ref_num = 0;
	h265_dpb_frm *ref_frame;
	RK_S32 i, j, k;
	RK_S32 prev = 0;

	sp->dlt_poc_msb_prsnt0 = 0;
	sp->dlt_poc_msb_cycl0 = 0;
	sp->tot_poc_num = 0;
	sp->st_ref_pic_flg = 0;
	sp->num_neg_pic = rps->num_negative_pic;
	sp->num_pos_pic = rps->num_positive_pic;
	for (j = 0; j < rps->num_negative_pic; j++) {
		if (0 == j) {
			sp->dlt_poc_s0_m10 = prev - rps->delta_poc[j] - 1;
			sp->used_by_s0_flg = rps->refed[j];
		} else if (1 == j) {
			sp->dlt_poc_s0_m11 = prev - rps->delta_poc[j] - 1;
			sp->used_by_s0_flg |= rps->refed[j] << 1;
		} else if (2 == j) {
			sp->dlt_poc_s0_m12 = prev - rps->delta_poc[j] - 1;
			sp->used_by_s0_flg |= rps->refed[j] << 2;
		} else if (3 == j) {
			sp->dlt_poc_s0_m13 = prev - rps->delta_poc[j] - 1;
			sp->used_by_s0_flg |= rps->refed[j] << 3;
		}
		prev = rps->delta_poc[j];
	}

	for (i = 0; i < rps->num_of_pictures; i++) {
		if (rps->refed[i]) {
			sp->tot_poc_num++;
		}
	}

	if (slice->sps->long_term_ref_pics_present_flag) {
		RK_S32 numLtrpInSH = rps->num_long_term_pic;
		RK_S32 numLtrpInSPS = 0;
		RK_S32 counter = 0;
		RK_U32 poc_lsb_lt[3] = { 0, 0, 0 };
		RK_U32 used_by_lt_flg[3] = { 0, 0, 0 };
		RK_U32 dlt_poc_msb_prsnt[3] = { 0, 0, 0 };
		RK_U32 dlt_poc_msb_cycl[3] = { 0, 0, 0 };
		RK_S32 prevDeltaMSB = 0;
		RK_S32 offset = rps->num_negative_pic + rps->num_positive_pic;
		RK_S32 numLongTerm = rps->num_of_pictures - offset;
		RK_S32 bitsForLtrpInSPS = 0;
		for (k = rps->num_of_pictures - 1;
		     k > rps->num_of_pictures - rps->num_long_term_pic - 1;
		     k--) {
			RK_U32 lsb =
			    rps->poc[k] % (1 << slice->sps->bits_for_poc);
			RK_U32 find_flag = 0;
			for (i = 0;
			     i < (RK_S32) slice->sps->num_long_term_ref_pic_sps;
			     i++) {
				if ((lsb ==
				     slice->sps->lt_ref_pic_poc_lsb_sps[i])
				    && (rps->used[k] ==
					slice->sps->
					used_by_curr_pic_lt_sps_flag[i])) {
					find_flag = 1;
					break;
				}
			}

			if (find_flag) {
				numLtrpInSPS++;
			} else {
				counter++;
			}
		}

		numLtrpInSH -= numLtrpInSPS;

		while (slice->sps->num_long_term_ref_pic_sps >
		       (RK_U32) (1 << bitsForLtrpInSPS)) {
			bitsForLtrpInSPS++;
		}

		if (slice->sps->num_long_term_ref_pic_sps > 0) {
			sp->num_lt_sps = numLtrpInSPS;
		}
		sp->num_lt_pic = numLtrpInSH;
		// Note that the LSBs of the LT ref. pic. POCs must be sorted before.
		// Not sorted here because LT ref indices will be used in setRefPicList()
		sp->poc_lsb_lt0 = 0;
		sp->used_by_lt_flg0 = 0;
		sp->dlt_poc_msb_prsnt0 = 0;
		sp->dlt_poc_msb_cycl0 = 0;
		sp->poc_lsb_lt1 = 0;
		sp->used_by_lt_flg1 = 0;
		sp->dlt_poc_msb_prsnt1 = 0;
		sp->dlt_poc_msb_cycl1 = 0;
		sp->poc_lsb_lt2 = 0;
		sp->used_by_lt_flg2 = 0;
		sp->dlt_poc_msb_prsnt2 = 0;
		sp->dlt_poc_msb_cycl2 = 0;

		for (i = rps->num_of_pictures - 1; i > offset - 1; i--) {
			RK_U32 deltaFlag = 0;
			if ((i == rps->num_of_pictures - 1)
			    || (i == rps->num_of_pictures - 1 - numLtrpInSPS)) {
				deltaFlag = 1;
			}
			poc_lsb_lt[numLongTerm - 1 - (i - offset)] =
			    rps->poc_lsblt[i];
			used_by_lt_flg[numLongTerm - 1 - (i - offset)] =
			    rps->refed[i];
			dlt_poc_msb_prsnt[numLongTerm - 1 - (i - offset)] =
			    rps->delta_poc_msb_present_flag[i];

			if (rps->delta_poc_msb_present_flag[i]) {
				if (deltaFlag) {
					dlt_poc_msb_cycl[numLongTerm - 1 -
							 (i - offset)] =
					    rps->delta_poc_msb_cycle_lt[i];
				} else {
					RK_S32 differenceInDeltaMSB =
					    rps->delta_poc_msb_cycle_lt[i] -
					    prevDeltaMSB;
					mpp_assert(differenceInDeltaMSB >= 0);
					dlt_poc_msb_cycl[numLongTerm - 1 -
							 (i - offset)] =
					    differenceInDeltaMSB;
				}
			}
			prevDeltaMSB = rps->delta_poc_msb_cycle_lt[i];
		}

		sp->poc_lsb_lt0 = poc_lsb_lt[0];
		sp->used_by_lt_flg0 = used_by_lt_flg[0];
		sp->dlt_poc_msb_prsnt0 = dlt_poc_msb_prsnt[0];
		sp->dlt_poc_msb_cycl0 = dlt_poc_msb_cycl[0];
		sp->poc_lsb_lt1 = poc_lsb_lt[1];
		sp->used_by_lt_flg1 = used_by_lt_flg[1];
		sp->dlt_poc_msb_prsnt1 = dlt_poc_msb_prsnt[1];
		sp->dlt_poc_msb_cycl1 = dlt_poc_msb_cycl[1];
		sp->poc_lsb_lt2 = poc_lsb_lt[2];
		sp->used_by_lt_flg2 = used_by_lt_flg[2];
		sp->dlt_poc_msb_prsnt2 = dlt_poc_msb_prsnt[2];
		sp->dlt_poc_msb_cycl2 = dlt_poc_msb_cycl[2];
	}

	sp->lst_entry_l0 = 0;
	sp->ref_pic_lst_mdf_l0 = 0;

	if (slice->slice_type == I_SLICE) {
		numRpsCurrTempList = 0;
	} else {
		ref_num =
		    rps->num_negative_pic + rps->num_positive_pic +
		    rps->num_long_term_pic;
		for (i = 0; i < ref_num; i++) {
			if (rps->used[i]) {
				numRpsCurrTempList++;
			}
		}
	}

	if (slice->pps->lists_modification_present_flag
	    && numRpsCurrTempList > 1) {
		h265_rpl_modification *rpl_modification =
		    &slice->rpl_modification;
		if (slice->slice_type != I_SLICE) {
			sp->ref_pic_lst_mdf_l0 =
			    rpl_modification->rpl_modification_flag_l0 ? 1 : 0;
			if (sp->ref_pic_lst_mdf_l0) {
				sp->lst_entry_l0 =
				    rpl_modification->ref_pic_set_idx_l0[0];
			}
		}
	}

	sp->recon_pic.slot_idx = h->dpb->curr->slot_idx;
	ref_frame = slice->ref_pic_list[0][0];
	if (ref_frame != NULL) {
		sp->ref_pic.slot_idx = ref_frame->slot_idx;
	} else {
		sp->ref_pic.slot_idx = h->dpb->curr->slot_idx;
	}
	return 0;
}

RK_S32 h265e_syntax_fill(void *ctx)
{
	H265eCtx *h = (H265eCtx *) ctx;
	H265eSyntax_new *syn = (H265eSyntax_new *) & h->syntax;
	fill_picture_parameters(h, &syn->pp);
	fill_slice_parameters(h, &syn->sp);
	fill_ref_parameters(h, &syn->sp);
	return 0;
}
