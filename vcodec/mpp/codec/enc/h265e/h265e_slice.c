// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#define MODULE_TAG "h265e_slice"

#include <linux/string.h>

#include "mpp_log.h"
#include "h265e_codec.h"
#include "h265e_slice.h"
h265_dpb_frm * get_ref_pic(h265_dpb_frm * frame_list, RK_S32 poc)
{
	RK_S32 index = 0;
	h265_dpb_frm * frame = NULL;
	h265e_dbg_func("enter\n");
	for (index = 0; index < MAX_REFS; index++) {
		frame = &frame_list[index];
		if (frame->inited && frame->poc == poc)
			break;
	}
	h265e_dbg_func("leave\n");
	return frame;
}

h265_dpb_frm * get_lt_ref_pic(h265_dpb_frm * frame_list, h265_slice * slice,
			      RK_S32 poc, RK_U32 pocHasMsb)
{
	RK_S32 index = 0;
	h265_dpb_frm * frame = NULL;
	h265_dpb_frm * stPic = &frame_list[MAX_REFS - 1];
	RK_S32 pocCycle = 1 << slice->sps->bits_for_poc;
	h265e_dbg_func("enter\n");
	if (!pocHasMsb)
		poc = poc % pocCycle;
	for (index = MAX_REFS - 1; index >= 0; index--) {
		frame = &frame_list[index];
		if (frame->on_used && frame->poc != slice->poc
		    && frame->slice->is_referenced) {
			RK_S32 picPoc = frame->poc;
			if (!pocHasMsb)
				picPoc = picPoc % pocCycle;
			if (poc == picPoc) {
				if (frame->is_long_term)
					return frame;

				else
					stPic = frame;
			}
		}
	}
	h265e_dbg_func("leave\n");
	return stPic;
}

void h265e_slice_set_ref_list(h265_dpb_frm * frame_list,
			      h265_slice * slice)
{
	h265_reference_picture_set * rps = slice->rps;
	h265_dpb_frm * refPic = NULL;
	h265_dpb_frm * refPicSetStCurr0[MAX_REFS];
	h265_dpb_frm * refPicSetStCurr1[MAX_REFS];
	h265_dpb_frm * refPicSetLtCurr[MAX_REFS];
	h265_dpb_frm * rpsCurrList0[MAX_REFS + 1];
	h265_dpb_frm * rpsCurrList1[MAX_REFS + 1];
	RK_S32 numPocTotalCurr = 0;
	RK_S32 numPocStCurr0 = 0;
	RK_S32 numPocStCurr1 = 0;
	RK_S32 numPocLtCurr = 0;
	RK_S32 i, cIdx = 0, rIdx = 0;
	h265e_dbg_func("enter\n");
	if (slice->slice_type == I_SLICE) {
		memset(slice->ref_pic_list, 0, sizeof(slice->ref_pic_list));
		memset(slice->num_ref_idx, 0, sizeof(slice->num_ref_idx));
		return;
	}
	for (i = 0; i < rps->num_negative_pic; i++) {
		if (rps->used[i]) {
			refPic =
				get_ref_pic(frame_list,
					    slice->poc + rps->delta_poc[i]);
			refPic->is_long_term = 0;
			refPicSetStCurr0[numPocStCurr0] = refPic;
			numPocStCurr0++;
			refPic->check_lt_msb = 0;
		}
	}
	for (; i < rps->num_negative_pic + rps->num_positive_pic; i++) {
		if (rps->used[i]) {
			refPic =
				get_ref_pic(frame_list,
					    slice->poc + rps->delta_poc[i]);
			refPic->is_long_term = 0;
			refPicSetStCurr1[numPocStCurr1] = refPic;
			numPocStCurr1++;
			refPic->check_lt_msb = 0;
		}
	}
	for (i =
		     rps->num_negative_pic + rps->num_positive_pic +
		     rps->num_long_term_pic - 1;
	     i > rps->num_negative_pic + rps->num_positive_pic - 1; i--) {
		if (rps->used[i]) {
			refPic =
				get_lt_ref_pic(frame_list, slice, rps->real_poc[i],
					       rps->check_lt_msb[i]);
			refPic->is_long_term = 1;
			refPicSetLtCurr[numPocLtCurr] = refPic;
			numPocLtCurr++;
		}
		if (refPic == NULL) {
			refPic =
				get_lt_ref_pic(frame_list, slice, rps->real_poc[i],
					       rps->check_lt_msb[i]);
		}
		refPic->check_lt_msb = rps->check_lt_msb[i];
	}

	// ref_pic_list_init
	numPocTotalCurr = numPocStCurr0 + numPocStCurr1 + numPocLtCurr;
	for (i = 0; i < numPocStCurr0; i++, cIdx++)
		rpsCurrList0[cIdx] = refPicSetStCurr0[i];
	for (i = 0; i < numPocStCurr1; i++, cIdx++)
		rpsCurrList0[cIdx] = refPicSetStCurr1[i];
	for (i = 0; i < numPocLtCurr; i++, cIdx++)
		rpsCurrList0[cIdx] = refPicSetLtCurr[i];
	mpp_assert(cIdx == numPocTotalCurr);
	if (slice->slice_type == B_SLICE) {
		cIdx = 0;
		for (i = 0; i < numPocStCurr1; i++, cIdx++)
			rpsCurrList1[cIdx] = refPicSetStCurr1[i];
		for (i = 0; i < numPocStCurr0; i++, cIdx++)
			rpsCurrList1[cIdx] = refPicSetStCurr0[i];
		for (i = 0; i < numPocLtCurr; i++, cIdx++)
			rpsCurrList1[cIdx] = refPicSetLtCurr[i];
		mpp_assert(cIdx == numPocTotalCurr);
	}
	memset(slice->is_used_long_term, 0, sizeof(slice->is_used_long_term));
	for (rIdx = 0; rIdx < slice->num_ref_idx[0]; rIdx++) {
		cIdx =
			slice->rpl_modification.rpl_modification_flag_l0 ? slice->
			rpl_modification.ref_pic_set_idx_l0[rIdx] : (RK_U32) rIdx %
			numPocTotalCurr;
		mpp_assert(cIdx >= 0 && cIdx < numPocTotalCurr);
		slice->ref_pic_list[0][rIdx] = rpsCurrList0[cIdx];
		slice->is_used_long_term[0][rIdx] =
			(cIdx >= numPocStCurr0 + numPocStCurr1);
	}
	if (slice->slice_type != B_SLICE) {
		slice->num_ref_idx[1] = 0;
		memset(slice->ref_pic_list[1], 0,
		       sizeof(slice->ref_pic_list[1]));
	} else {
		for (rIdx = 0; rIdx < slice->num_ref_idx[1]; rIdx++) {
			cIdx =
				slice->rpl_modification.
				rpl_modification_flag_l1 ? slice->rpl_modification.
				ref_pic_set_idx_l1[rIdx] : (RK_U32) rIdx %
				numPocTotalCurr;
			mpp_assert(cIdx >= 0 && cIdx < numPocTotalCurr);
			slice->ref_pic_list[1][rIdx] = rpsCurrList1[cIdx];
			slice->is_used_long_term[1][rIdx] =
				(cIdx >= numPocStCurr0 + numPocStCurr1);
		}
	}
	h265e_dbg_func("leave\n");
}

void h265e_slice_set_ref_poc_list(h265_slice * slice)
{
	RK_S32 dir, numRefIdx;
	h265e_dbg_func("enter\n");
	for (dir = 0; dir < 2; dir++) {
		for (numRefIdx = 0; numRefIdx < slice->num_ref_idx[dir];
		     numRefIdx++) {
			slice->ref_poc_list[dir][numRefIdx] =
				slice->ref_pic_list[dir][numRefIdx]->poc;
		}
	}
	h265e_dbg_func("leave\n");
}

void h265e_slice_init(void *ctx, EncFrmStatus curr)
{
	H265eCtx * p = (H265eCtx *) ctx;
	h265_vps * vps = &p->vps;
	h265_sps * sps = &p->sps;
	h265_pps * pps = &p->pps;
	MppEncCfgSet * cfg = p->cfg;
	MppEncH265Cfg * codec = &cfg->codec.h265;
	h265_slice * slice = p->dpb->curr->slice;
	p->slice = p->dpb->curr->slice;
	h265e_dbg_func("enter\n");
	memset(slice, 0, sizeof(h265_slice));
	slice->sps = sps;
	slice->vps = vps;
	slice->pps = pps;
	slice->num_ref_idx[0] = 0;
	slice->num_ref_idx[1] = 0;
	slice->col_from_l0_flag = 1;
	slice->col_ref_idx = 0;
	slice->slice_qp_delta_cb = 0;
	slice->slice_qp_delta_cr = 0;
	slice->max_num_merge_cand = 5;
	slice->cabac_init_flag = 0;
	slice->tmprl_mvp_en = sps->sps_temporal_mvp_enable_flag;
	slice->pic_output_flag = 1;
	p->dpb->curr->is_key_frame = 0;
	if (curr.is_idr) {
		slice->slice_type = I_SLICE;
		p->dpb->curr->is_key_frame = 1;
		p->dpb->curr->status.is_intra = 1;
		p->dpb->gop_idx = 0;
	} else {
		slice->slice_type = P_SLICE;
		p->dpb->curr->status.is_intra = 0;
	}
	p->dpb->curr->status.val = curr.val;
	if (slice->slice_type != B_SLICE && !curr.non_recn)
		slice->is_referenced = 1;
	if (slice->pps->deblocking_filter_override_enabled_flag)
		h265e_dbg_slice("to do in this case");

	else {
		slice->deblocking_filter_disable =
			pps->pps_disable_deblocking_filter_flag;
		slice->pps_beta_offset_div2 = pps->pps_beta_offset_div2;
		slice->pps_tc_offset_div2 = pps->pps_tc_offset_div2;
	}
	slice->sao_enable_flag = !codec->sao_cfg.slice_sao_luma_disable;
	slice->sao_enable_flag_chroma =
		!codec->sao_cfg.slice_sao_chroma_disable;
	slice->max_num_merge_cand = codec->merge_cfg.max_mrg_cnd;
	slice->cabac_init_flag = codec->entropy_cfg.cabac_init_flag;
	slice->pic_output_flag = 1;
	slice->pps_pic_parameter_set_id = pps->pps_pic_parameter_set_id;
	if (slice->pps->pps_slice_chroma_qp_offsets_present_flag) {
		slice->slice_qp_delta_cb = codec->trans_cfg.cb_qp_offset;
		slice->slice_qp_delta_cr = codec->trans_cfg.cr_qp_offset;
	}
	slice->poc = p->dpb->curr->seq_idx;
	slice->gop_idx = p->dpb->gop_idx;
	p->dpb->curr->gop_idx = p->dpb->gop_idx++;
	p->dpb->curr->poc = slice->poc;
	if (curr.is_lt_ref)
		p->dpb->curr->is_long_term = 1;
	h265e_dbg_slice
	("slice->slice_type = %d slice->is_referenced = %d \n",
	 slice->slice_type, slice->is_referenced);
	h265e_dbg_func("leave\n");
}

void code_st_refpic_set(MppWriteCtx * bitIf,
			h265_reference_picture_set * rps, RK_S32 idx)
{
	if (idx > 0) {
		mpp_writer_put_bits(bitIf, rps->inter_rps_prediction, 1);	// inter_RPS_prediction_flag
	}
	if (rps->inter_rps_prediction) {
		RK_S32 deltaRPS = rps->delta_rps;
		RK_S32 j;
		mpp_writer_put_ue(bitIf,
				  rps->delta_ridx_minus1);	// delta index of the Reference Picture Set used for prediction minus 1
		mpp_writer_put_bits(bitIf, (deltaRPS >= 0 ? 0 : 1), 1);	//delta_rps_sign
		mpp_writer_put_ue(bitIf, abs(deltaRPS) - 1);	// absolute delta RPS minus 1
		for (j = 0; j < rps->num_ref_idc; j++) {
			RK_S32 refIdc = rps->ref_idc[j];
			mpp_writer_put_bits(bitIf, (refIdc == 1 ? 1 : 0), 1);	//first bit is "1" if Idc is 1
			if (refIdc != 1) {
				mpp_writer_put_bits(bitIf, refIdc >> 1, 1);	//second bit is "1" if Idc is 2, "0" otherwise.
			}
		}
	} else {
		RK_S32 prev = 0;
		RK_S32 j;
		mpp_writer_put_ue(bitIf, rps->num_negative_pic);
		mpp_writer_put_ue(bitIf, rps->num_positive_pic);
		for (j = 0; j < rps->num_negative_pic; j++) {
			mpp_writer_put_ue(bitIf, prev - rps->delta_poc[j] - 1);
			prev = rps->delta_poc[j];
			mpp_writer_put_bits(bitIf, rps->used[j], 1);
		}
		prev = 0;
		for (j = rps->num_negative_pic;
		     j < rps->num_negative_pic + rps->num_positive_pic; j++) {
			mpp_writer_put_ue(bitIf, rps->delta_poc[j] - prev - 1);
			prev = rps->delta_poc[j];
			mpp_writer_put_bits(bitIf, rps->used[j], 1);
		}
	}
}

RK_U8 find_matching_ltrp(h265_slice * slice, RK_U32 * ltrpsIndex,
			 RK_S32 ltrpPOC, RK_U32 usedFlag)
{
	RK_U32 lsb = ltrpPOC % (1 << slice->sps->bits_for_poc);
	RK_U32 k;
	for (k = 0; k < slice->sps->num_long_term_ref_pic_sps; k++) {
		if ((lsb == slice->sps->lt_ref_pic_poc_lsb_sps[k])
		    && (usedFlag ==
			slice->sps->used_by_curr_pic_lt_sps_flag[k])) {
			*ltrpsIndex = k;
			return 1;
		}
	}
	return 0;
}

RK_S32 get_num_rps_cur_templist(h265_reference_picture_set * rps)
{
	RK_S32 numRpsCurrTempList = 0;
	RK_S32 i;
	for (i = 0;
	     i <
	     rps->num_negative_pic + rps->num_positive_pic +
	     rps->num_long_term_pic; i++) {
		if (rps->used[i])
			numRpsCurrTempList++;
	}
	return numRpsCurrTempList;
}

#ifdef SW_ENC_PSKIP
void h265e_code_slice_header(h265_slice * slice, MppWriteCtx * bitIf)
{
	RK_U32 i = 0;
	h265_reference_picture_set * rps = slice->rps;
	mpp_writer_put_bits(bitIf, 1, 1);	//first_slice_segment_in_pic_flag
	mpp_writer_put_ue(bitIf, slice->pps_pic_parameter_set_id);
	slice->tmprl_mvp_en = 0;
	if (!slice->dependent_slice_segment_flag) {
		RK_S32 code = 0;
		for (i = 0;
		     i < (RK_U32) slice->pps->num_extra_slice_header_bits;
		     i++) {
			mpp_writer_put_bits(bitIf,
					    (slice->
					     slice_reserved_flag >> i) & 0x1,
					    1);
		}
		mpp_writer_put_ue(bitIf, slice->slice_type);
		if (slice->pps->output_flag_present_flag) {
			mpp_writer_put_bits(bitIf,
					    slice->pic_output_flag ? 1 : 0, 1);
		}
		if (slice->slice_type != I_SLICE) {	// skip frame can't iDR
			RK_S32 picOrderCntLSB =
				(slice->poc - slice->last_idr +
				 (1 << slice->sps->bits_for_poc)) %
				(1 << slice->sps->bits_for_poc);
			mpp_writer_put_bits(bitIf, picOrderCntLSB,
					    slice->sps->bits_for_poc);
			mpp_writer_put_bits(bitIf, 0, 1);
			code_st_refpic_set(bitIf, rps,
					   slice->sps->rps_list.
					   num_short_term_ref_pic_sets);
			if (slice->sps->long_term_ref_pics_present_flag) {
				RK_S32 numLtrpInSH = rps->num_of_pictures;
				RK_S32 ltrpInSPS[MAX_REFS];
				RK_S32 numLtrpInSPS = 0;
				RK_U32 ltrpIndex;
				RK_S32 counter = 0;
				RK_S32 k;
				RK_S32 bitsForLtrpInSPS = 0;
				RK_S32 prevDeltaMSB = 0;
				RK_S32 offset = 0;
				for (k = rps->num_of_pictures - 1;
				     k >
				     rps->num_of_pictures -
				     rps->num_long_term_pic - 1; k--) {
					if (find_matching_ltrp
					    (slice, &ltrpIndex, rps->poc[k],
					     rps->used[k])) {
						ltrpInSPS[numLtrpInSPS] =
							ltrpIndex;
						numLtrpInSPS++;
					} else
						counter++;
				}
				numLtrpInSH -= numLtrpInSPS;
				while (slice->sps->
				       num_long_term_ref_pic_sps >
				       (RK_U32) (1 << bitsForLtrpInSPS))
					bitsForLtrpInSPS++;
				if (slice->sps->num_long_term_ref_pic_sps >
				    0)
					mpp_writer_put_ue(bitIf, numLtrpInSPS);
				mpp_writer_put_ue(bitIf, numLtrpInSH);

				// Note that the LSBs of the LT ref. pic. POCs must be sorted before.
				// Not sorted here because LT ref indices will be used in setRefPicList()
				prevDeltaMSB = 0;
				offset =
					rps->num_negative_pic +
					rps->num_positive_pic;
				for (k = rps->num_of_pictures - 1;
				     k > offset - 1; k--) {
					if (counter < numLtrpInSPS) {
						if (bitsForLtrpInSPS > 0) {
							mpp_writer_put_bits
							(bitIf,
							 ltrpInSPS[counter],
							 bitsForLtrpInSPS);
						}
					} else {
						mpp_writer_put_bits(bitIf,
								    rps->
								    poc_lsblt
								    [k],
								    slice->
								    sps->
								    bits_for_poc);
						mpp_writer_put_bits(bitIf,
								    rps->
								    used[k],
								    1);
					}
					mpp_writer_put_bits(bitIf,
							    rps->
							    delta_poc_msb_present_flag
							    [k], 1);
					if (rps->
					    delta_poc_msb_present_flag[k]) {
						RK_U32 deltaFlag = 0;
						if ((k ==
						     rps->num_of_pictures - 1)
						    || (k ==
							rps->num_of_pictures -
							1 - numLtrpInSPS))
							deltaFlag = 1;
						if (deltaFlag) {
							mpp_writer_put_ue
							(bitIf,
							 rps->
							 delta_poc_msb_cycle_lt
							 [k]);
						} else {
							RK_S32
							differenceInDeltaMSB
								=
									rps->
									delta_poc_msb_cycle_lt
									[k] - prevDeltaMSB;
							mpp_writer_put_ue
							(bitIf,
							 differenceInDeltaMSB);
						}
						prevDeltaMSB =
							rps->
							delta_poc_msb_cycle_lt[k];
					}
				}
			}
			if (slice->sps->sps_temporal_mvp_enable_flag) {
				mpp_writer_put_bits(bitIf,
						    slice->
						    tmprl_mvp_en ? 1 : 0, 1);
			}
		}
		if (slice->sps->sample_adaptive_offset_enabled_flag) {	//skip frame close sao
			mpp_writer_put_bits(bitIf, 0, 1);
			mpp_writer_put_bits(bitIf, 0, 1);
		}

		//check if numrefidxes match the defaults. If not, override
		if (slice->slice_type != I_SLICE) {
			RK_U32 overrideFlag =
				(slice->num_ref_idx[0] !=
				 (RK_S32) slice->pps->
				 num_ref_idx_l0_default_active_minus1);
			mpp_writer_put_bits(bitIf, overrideFlag ? 1 : 0, 1);
			if (overrideFlag) {
				mpp_writer_put_ue(bitIf,
						  slice->num_ref_idx[0] - 1);
				slice->num_ref_idx[1] = 0;
			}
		}
		if (slice->pps->lists_modification_present_flag
		    && get_num_rps_cur_templist(rps) > 1) {
			h265_rpl_modification * rpl_modification =
				&slice->rpl_modification;
			mpp_writer_put_bits(bitIf,
					    rpl_modification->
					    rpl_modification_flag_l0 ? 1 : 0,
					    1);
			if (rpl_modification->rpl_modification_flag_l0) {
				RK_S32 numRpsCurrTempList0 =
					get_num_rps_cur_templist(rps);
				if (numRpsCurrTempList0 > 1) {
					RK_S32 length = 1;
					numRpsCurrTempList0--;
					while (numRpsCurrTempList0 >>= 1)
						length++;
					for (i = 0;
					     i <
					     (RK_U32) slice->num_ref_idx[0];
					     i++) {
						mpp_writer_put_bits(bitIf,
								    rpl_modification->
								    ref_pic_set_idx_l0
								    [i],
								    length);
					}
				}
			}
		}
		if (slice->pps->cabac_init_present_flag)
			mpp_writer_put_bits(bitIf, slice->cabac_init_flag, 1);
		if (slice->tmprl_mvp_en) {
			if (slice->slice_type != I_SLICE &&
			    ((slice->col_from_l0_flag == 1
			      && slice->num_ref_idx[0] > 1)
			     || (slice->col_from_l0_flag == 0
				 && slice->num_ref_idx[1] > 1)))
				mpp_writer_put_ue(bitIf, slice->col_ref_idx);
		}
		if (slice->slice_type != I_SLICE) {
			RK_S32 flag =
				MRG_MAX_NUM_CANDS - slice->max_num_merge_cand;
			flag = flag == 5 ? 4 : flag;
			mpp_writer_put_ue(bitIf, flag);
		}
		code = slice->slice_qp - (slice->pps->init_qp_minus26 + 26);
		mpp_writer_put_se(bitIf, code);
		if (slice->pps->pps_slice_chroma_qp_offsets_present_flag) {
			code = slice->slice_qp_delta_cb;
			mpp_writer_put_se(bitIf, code);
			code = slice->slice_qp_delta_cr;
			mpp_writer_put_se(bitIf, code);
		}
		if (slice->pps->deblocking_filter_control_present_flag) {
			if (slice->pps->
			    deblocking_filter_override_enabled_flag) {
				mpp_writer_put_bits(bitIf,
						    slice->
						    deblocking_filter_override_flag,
						    1);
			}
			if (slice->deblocking_filter_override_flag) {
				mpp_writer_put_bits(bitIf,
						    slice->
						    deblocking_filter_disable,
						    1);
				if (!slice->deblocking_filter_disable) {
					mpp_writer_put_se(bitIf,
							  slice->
							  pps_beta_offset_div2);
					mpp_writer_put_se(bitIf,
							  slice->
							  pps_tc_offset_div2);
				}
			}
		}
	}
	if (slice->pps->slice_segment_header_extension_present_flag) {
		mpp_writer_put_ue(bitIf, slice->slice_header_extension_length);
		for (i = 0; i < slice->slice_header_extension_length; i++)
			mpp_writer_put_bits(bitIf, 0, 8);
	}
	h265e_dbg_func("leave\n");
}

void code_skip_flag(h265_slice * slice, RK_U32 abs_part_idx, DataCu * cu)
{

	// get context function is here
	h265_cabac_ctx * cabac_ctx = &slice->cabac_ctx;
	h265_sps * sps = slice->sps;
	RK_U32 ctxSkip;
	RK_U32 tpelx =
		cu->pixel_x + sps->raster2pelx[sps->zscan2raster[abs_part_idx]];
	RK_U32 tpely =
		cu->pixel_y + sps->raster2pely[sps->zscan2raster[abs_part_idx]];

	//RK_U32 ctxSkip = cu->getCtxSkipFlag(abs_part_idx);
	h265e_dbg_skip("tpelx = %d", tpelx);
	if (cu->cur_addr == 0)
		ctxSkip = 0;

	else if ((tpely == 0) || (tpelx == 0))
		ctxSkip = 1;

	else
		ctxSkip = 2;
	h265e_dbg_skip("ctxSkip = %d", ctxSkip);
	h265e_cabac_encodeBin(cabac_ctx,
			      &slice->contex_models[OFF_SKIP_FLAG_CTX + ctxSkip], 1);
}

static void code_merge_index(h265_slice * slice)
{
	h265_cabac_ctx * cabac_ctx = &slice->cabac_ctx;
	h265e_cabac_encodeBin(cabac_ctx,
			      &slice->contex_models[OFF_MERGE_IDX_EXT_CTX],
			      0);
}  static void code_split_flag(h265_slice * slice, RK_U32 abs_part_idx,
			       RK_U32 depth, DataCu * cu)
{
	h265_sps * sps = slice->sps;
	h265_cabac_ctx * cabac_ctx = NULL;
	RK_U32 currSplitFlag = 0;
	if (depth == slice->sps->max_cu_depth - slice->sps->add_cu_depth)
		return;
	h265e_dbg_skip("depth %d cu->cu_depth %d", depth,
		       cu->cu_depth[sps->zscan2raster[abs_part_idx]]);
	cabac_ctx = &slice->cabac_ctx;
	currSplitFlag =
		(cu->cu_depth[sps->zscan2raster[abs_part_idx]] > depth) ? 1 : 0;
	h265e_cabac_encodeBin(cabac_ctx,
			      &slice->contex_models[OFF_SPLIT_FLAG_CTX],
			      currSplitFlag);
}

static void encode_cu(h265_slice * slice, RK_U32 abs_part_idx, RK_U32 depth,
		      DataCu * cu)
{
	h265_sps * sps = slice->sps;
	RK_U32 bBoundary = 0;
	RK_U32 lpelx =
		cu->pixel_x + sps->raster2pelx[sps->zscan2raster[abs_part_idx]];
	RK_U32 rpelx = lpelx + (sps->max_cu_size >> depth) - 1;
	RK_U32 tpely =
		cu->pixel_y + sps->raster2pely[sps->zscan2raster[abs_part_idx]];
	RK_U32 bpely = tpely + (sps->max_cu_size >> depth) - 1;
	h265e_dbg_skip("EncodeCU depth %d, abs_part_idx %d", depth,
		       abs_part_idx);
	if ((rpelx < sps->pic_width_in_luma_samples)
	    && (bpely < sps->pic_height_in_luma_samples)) {
		h265e_dbg_skip("code_split_flag in depth %d", depth);
		code_split_flag(slice, abs_part_idx, depth, cu);
	} else {
		h265e_dbg_skip("boundary flag found");
		bBoundary = 1;
	}
	h265e_dbg_skip("cu_depth[%d] = %d maxCUDepth %d, add_cu_depth %d",
		       abs_part_idx,
		       cu->cu_depth[sps->zscan2raster[abs_part_idx]],
		       sps->max_cu_depth, sps->add_cu_depth);
	if ((depth < cu->cu_depth[sps->zscan2raster[abs_part_idx]]
	     && (depth < (sps->max_cu_depth - sps->add_cu_depth)))
	    || bBoundary) {
		RK_U32 qNumParts = (256 >> (depth << 1)) >> 2;
		RK_U32 partUnitIdx = 0;
		for (partUnitIdx = 0; partUnitIdx < 4;
		     partUnitIdx++, abs_part_idx += qNumParts) {
			h265e_dbg_skip
			("depth %d partUnitIdx = %d, qNumParts %d, abs_part_idx %d",
			 depth, partUnitIdx, qNumParts, abs_part_idx);
			lpelx =
				cu->pixel_x +
				sps->raster2pelx[sps->zscan2raster[abs_part_idx]];
			tpely =
				cu->pixel_y +
				sps->raster2pely[sps->zscan2raster[abs_part_idx]];
			if ((lpelx < sps->pic_width_in_luma_samples)
			    && (tpely < sps->pic_height_in_luma_samples))
				encode_cu(slice, abs_part_idx, depth + 1, cu);
		}
		return;
	}
	h265e_dbg_skip("code_skip_flag in depth %d", depth);
	code_skip_flag(slice, abs_part_idx, cu);
	h265e_dbg_skip("code_merge_index in depth %d", depth);
	code_merge_index(slice);
	return;
}

static void proc_cu8(DataCu * cu, RK_U32 pos_x, RK_U32 pos_y)
{
	RK_S32 nSize = 8;
	RK_S32 nSubPart = nSize * nSize / 4 / 4;
	RK_S32 puIdx = pos_x / 8 + pos_y / 8 * 8;
	h265e_dbg_skip("8 ctu puIdx %d no need split", puIdx);
	memset(cu->cu_depth + puIdx * nSubPart, 3, nSubPart);
}  static void proc_cu16(h265_slice * slice, DataCu * cu, RK_U32 pos_x,
			 RK_U32 pos_y)
{
	RK_U32 m;
	h265_sps * sps = slice->sps;
	RK_S32 nSize = 16;
	RK_S32 nSubPart = nSize * nSize / 4 / 4;
	RK_S32 puIdx = pos_x / 16 + pos_y / 16 * 4;
	RK_U32 cu_x_1, cu_y_1;
	h265e_dbg_skip("cu 16 pos_x %d pos_y %d", pos_x, pos_y);
	if ((cu->pixel_x + pos_x + 15 < sps->pic_width_in_luma_samples) &&
	    (cu->pixel_y + pos_y + 15 < sps->pic_height_in_luma_samples)) {
		h265e_dbg_skip("16 ctu puIdx %d no need split", puIdx);
		memset(cu->cu_depth + puIdx * nSubPart, 2, nSubPart);
		return;
	} else if ((cu->pixel_x + pos_x >= sps->pic_width_in_luma_samples) ||
		   (cu->pixel_y + pos_y >= sps->pic_height_in_luma_samples)) {
		h265e_dbg_skip("16 ctu puIdx %d out of pic", puIdx);
		memset(cu->cu_depth + puIdx * nSubPart, 2, nSubPart);
		return;
	}
	for (m = 0; m < 4; m++) {
		cu_x_1 = pos_x + (m & 1) * (nSize >> 1);
		cu_y_1 = pos_y + (m >> 1) * (nSize >> 1);
		proc_cu8(cu, cu_x_1, cu_y_1);
	}
}

static void proc_cu32(h265_slice * slice, DataCu * cu, RK_U32 pos_x,
		      RK_U32 pos_y)
{
	RK_U32 m;
	h265_sps * sps = slice->sps;
	RK_S32 nSize = 32;
	RK_S32 nSubPart = nSize * nSize / 4 / 4;
	RK_S32 puIdx = pos_x / 32 + pos_y / 32 * 2;
	RK_U32 cu_x_1, cu_y_1;
	h265e_dbg_skip("cu 32 pos_x %d pos_y %d", pos_x, pos_y);
	if ((cu->pixel_x + pos_x + 31 < sps->pic_width_in_luma_samples) &&
	    (cu->pixel_y + pos_y + 31 < sps->pic_height_in_luma_samples)) {
		h265e_dbg_skip("32 ctu puIdx %d no need split", puIdx);
		memset(cu->cu_depth + puIdx * nSubPart, 1, nSubPart);
		return;
	} else if ((cu->pixel_x + pos_x >= sps->pic_width_in_luma_samples) ||
		   (cu->pixel_y + pos_y >= sps->pic_height_in_luma_samples)) {
		h265e_dbg_skip("32 ctu puIdx %d out of pic", puIdx);
		memset(cu->cu_depth + puIdx * nSubPart, 1, nSubPart);
		return;
	}
	for (m = 0; m < 4; m++) {
		cu_x_1 = pos_x + (m & 1) * (nSize >> 1);
		cu_y_1 = pos_y + (m >> 1) * (nSize >> 1);
		proc_cu16(slice, cu, cu_x_1, cu_y_1);
	}
}

static void proc_ctu(h265_slice * slice, DataCu * cu)
{
	h265_sps * sps = slice->sps;
	RK_U32 k, m;
	RK_U32 cu_x_1, cu_y_1, m_nCtuSize = 64;
	RK_U32 lpelx = cu->pixel_x;
	RK_U32 rpelx = lpelx + 63;
	RK_U32 tpely = cu->pixel_y;
	RK_U32 bpely = tpely + 63;
	for (k = 0; k < 256; k++) {
		cu->cu_depth[k] = 0;
		cu->cu_size[k] = 64;
	}
	if ((rpelx < sps->pic_width_in_luma_samples)
	    && (bpely < sps->pic_height_in_luma_samples))
		return;
	for (m = 0; m < 4; m++) {
		cu_x_1 = (m & 1) * (m_nCtuSize >> 1);
		cu_y_1 = (m >> 1) * (m_nCtuSize >> 1);
		proc_cu32(slice, cu, cu_x_1, cu_y_1);
	}
	for (k = 0; k < 256; k++) {
		switch (cu->cu_depth[k]) {
		case 0:
			cu->cu_size[k] = 64;
			break;
		case 1:
			cu->cu_size[k] = 32;
			break;
		case 2:
			cu->cu_size[k] = 16;
			break;
		case 3:
			cu->cu_size[k] = 8;
			break;
		}
	}
}

static void h265e_write_nal(MppWriteCtx * bitIf)
{
	h265e_dbg_func("enter\n");
	mpp_writer_put_raw_bits(bitIf, 0x0, 24);
	mpp_writer_put_raw_bits(bitIf, 0x01, 8);
	mpp_writer_put_bits(bitIf, 0, 1);	// forbidden_zero_bit
	mpp_writer_put_bits(bitIf, 1, 6);	// nal_unit_type
	mpp_writer_put_bits(bitIf, 0, 6);	// nuh_reserved_zero_6bits
	mpp_writer_put_bits(bitIf, 1, 3);	// nuh_temporal_id_plus1
	h265e_dbg_func("leave\n");
}
static void h265e_write_algin(MppWriteCtx * bitIf)
{
	h265e_dbg_func("enter\n");
	mpp_writer_put_bits(bitIf, 1, 1);
	mpp_writer_align_zero(bitIf);
	h265e_dbg_func("leave\n");
}

RK_S32 h265e_code_slice_skip_frame(void *ctx, h265_slice * slice,
				      RK_U8 * buf, RK_S32 len)
{
	MppWriteCtx bitIf;
	H265eCtx * p = (H265eCtx *) ctx;
	h265_sps * sps = &p->sps;
	h265_cabac_ctx * cabac_ctx = &slice->cabac_ctx;
	DataCu cu;
	RK_U32 mb_wd = ((sps->pic_width_in_luma_samples + 63) >> 6);
	RK_U32 mb_h = ((sps->pic_height_in_luma_samples + 63) >> 6);
	RK_U32 i = 0, j = 0, cu_cnt = 0;
	h265e_dbg_func("enter\n");
	if (!buf || !len) {
		mpp_err("buf or size no set");
		return MPP_NOK;
	}
	mpp_writer_init(&bitIf, buf, len);
	h265e_write_nal(&bitIf);
	h265e_code_slice_header(slice, &bitIf);
	h265e_write_algin(&bitIf);
	h265e_reset_enctropy((void *)slice);
	h265e_cabac_init(cabac_ctx, &bitIf);
	cu.mb_w = mb_wd;
	cu.mb_h = mb_h;
	slice->is_referenced = 0;
	for (i = 0; i < mb_h; i++) {
		for (j = 0; j < mb_wd; j++) {
			cu.pixel_x = j * 64;
			cu.pixel_y = i * 64;
			cu.cur_addr = cu_cnt;
			proc_ctu(slice, &cu);
			encode_cu(slice, 0, 0, &cu);
			h265e_cabac_encodeBinTrm(cabac_ctx, 0);
			cu_cnt++;
		}
	}
	h265e_cabac_finish(cabac_ctx);
	h265e_write_algin(&bitIf);
	h265e_dbg_func("leave\n");
	return mpp_writer_bytes(&bitIf);
}
#endif

