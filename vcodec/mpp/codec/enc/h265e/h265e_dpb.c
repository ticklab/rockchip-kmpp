// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG  "h265e_dpb"

#include <linux/string.h>

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_maths.h"

#include "h265e_codec.h"
#include "h265e_dpb.h"

void h265e_dpb_set_ref_list(h265_rps_list * rps_list,
			    h265_reference_picture_set * rps, RK_S32 delta_poc)
{
	RK_S32 i;
	RK_S32 ref_idx = -1;
	h265_rpl_modification *rpl_modification = rps_list->rpl_modification;

	h265e_dbg_func("enter\n");
	rpl_modification->rpl_modification_flag_l0 = 0;
	rpl_modification->rpl_modification_flag_l1 = 0;

	for (i = 0; i < REF_PIC_LIST_NUM_IDX; i++) {
		rpl_modification->ref_pic_set_idx_l0[i] = 0;
		rpl_modification->ref_pic_set_idx_l0[i] = 0;
	}

	rpl_modification->rpl_modification_flag_l0 = 0;

	if (rps->num_of_pictures > 1) {
		for (i = 0; i < rps->num_of_pictures; i++) {
			h265e_dbg_dpb("rps->delta_poc[%d] = %d", i,
				      rps->delta_poc[i]);
			if (rps->delta_poc[i] == delta_poc) {
				ref_idx = i;
				h265e_dbg_dpb("get ref ref_idx %d", ref_idx);
				break;
			}
		}
		if (-1 == ref_idx) {
			mpp_err("Did not find the right reference picture");
			return;
		} else if (ref_idx != 0) {
			rpl_modification->rpl_modification_flag_l0 = 1;
			rpl_modification->ref_pic_set_idx_l0[0] = ref_idx;
			for (i = 1; i < rps->num_of_pictures - 1; i++) {
				if (i != ref_idx)
					rpl_modification->
					    ref_pic_set_idx_l0[i] = i;
			}
			rpl_modification->ref_pic_set_idx_l0[ref_idx] = 0;
		}
	}
	rpl_modification->rpl_modification_flag_l1 = 0;
	h265e_dbg_func("leave\n");
}

/* get buffer at init */
MPP_RET h265e_dpb_init_curr(h265_dpb * dpb, h265_dpb_frm * frm)
{
	h265e_dbg_func("enter\n");
	mpp_assert(!frm->on_used);

	frm->dpb = dpb;

	if (!frm->slice) {
		frm->slice = mpp_calloc(h265_slice, 1);
	}

	frm->inited = 1;
	frm->on_used = 1;
	frm->seq_idx = dpb->seq_idx;
	dpb->seq_idx++;

	h265e_dbg_func("leave\n");
	return MPP_OK;
}

MPP_RET h265e_dpb_get_curr(h265_dpb * dpb)
{
	RK_U32 i;

	h265e_dbg_func("enter\n");
	for (i = 0; i < MPP_ARRAY_ELEMS(dpb->frame_list); i++) {
		if (!dpb->frame_list[i].on_used) {
			dpb->curr = &dpb->frame_list[i];
			h265e_dbg_dpb("get free dpb slot_index %d",
				      dpb->curr->slot_idx);
			break;
		}
	}
	h265e_dpb_init_curr(dpb, dpb->curr);
	h265e_dbg_func("leave\n");
	return MPP_OK;
}

/* put buffer at deinit */
MPP_RET h265e_dpb_frm_deinit(h265_dpb_frm * frm)
{

	MPP_FREE(frm->slice);
	frm->inited = 0;
	h265e_dbg_func("leave\n");
	return MPP_OK;
}

MPP_RET h265e_dpb_init(h265_dpb ** dpb)
{
	MPP_RET ret = MPP_OK;
	h265_dpb *p = NULL;
	h265_rps_list *rps_list = NULL;
	RK_U32 i;

	h265e_dbg_func("enter\n");
	if (NULL == dpb) {
		mpp_err_f("invalid parameter %p \n", dpb);
		return MPP_ERR_VALUE;
	}

	p = mpp_calloc_size(h265_dpb, sizeof(h265_dpb));

	if (NULL == p)
		return MPP_ERR_MALLOC;

	p->last_idr = 0;
	p->poc_cra = 0;
	p->max_ref_l0 = 1;
	p->max_ref_l1 = 0;

	rps_list = &p->rps_list;
	rps_list->lt_num = 0;
	rps_list->st_num = 0;

	memset(rps_list->poc, 0, sizeof(rps_list->poc));

	rps_list->rpl_modification = mpp_calloc(h265_rpl_modification, 1);

	for (i = 0; i < MPP_ARRAY_ELEMS(p->frame_list); i++)
		p->frame_list[i].slot_idx = i;

	mpp_assert(dpb);
	*dpb = p;
	h265e_dbg_func("leave\n");
	return ret;
}

MPP_RET h265e_dpb_deinit(h265_dpb * dpb)
{
	RK_U32 i;

	if (NULL == dpb)
		return MPP_OK;

	h265e_dbg_func("enter\n");
	for (i = 0; i < MPP_ARRAY_ELEMS(dpb->frame_list); i++) {
		if (dpb->frame_list[i].inited)
			h265e_dpb_frm_deinit(&dpb->frame_list[i]);
	}

	MPP_FREE(dpb->rps_list.rpl_modification);

	MPP_FREE(dpb);

	h265e_dbg_func("leave\n");
	return MPP_OK;
}

enum NALUnitType get_nal_unit_type(h265_dpb * dpb, int curPOC)
{
	h265e_dbg_func("enter\n");
	if (curPOC == 0) {
		return NAL_IDR_W_RADL;
	}
	if (dpb->curr->is_key_frame) {
		return NAL_IDR_W_RADL;
	}
	if (dpb->poc_cra > 0) {
		if (curPOC < dpb->poc_cra) {
			// All leading pictures are being marked as TFD pictures here since current encoder uses all
			// reference pictures while encoding leading pictures. An encoder can ensure that a leading
			// picture can be still decodable when random accessing to a CRA/CRANT/BLA/BLANT picture by
			// controlling the reference pictures used for encoding that leading picture. Such a leading
			// picture need not be marked as a TFD picture.
			return NAL_RASL_R;
		}
	}
	if (dpb->last_idr > 0) {
		if (curPOC < dpb->last_idr) {
			return NAL_RADL_R;
		}
	}

	h265e_dbg_func("leave\n");
	return NAL_TRAIL_R;
}

static inline int getLSB(int poc, int maxLSB)
{
	if (poc >= 0) {
		return poc % maxLSB;
	} else {
		return (maxLSB - ((-poc) % maxLSB)) % maxLSB;
	}
}

void sort_delta_poc(h265_reference_picture_set * rps)
{
	// sort in increasing order (smallest first)
	RK_S32 j, k;
	RK_S32 numNegPics = 0;
	for (j = 1; j < rps->num_of_pictures; j++) {
		RK_S32 deltaPOC = rps->delta_poc[j];
		RK_U32 used = rps->used[j];
		RK_U32 refed = rps->refed[j];
		for (k = j - 1; k >= 0; k--) {
			int temp = rps->delta_poc[k];
			if (deltaPOC < temp) {
				rps->delta_poc[k + 1] = temp;
				rps->used[k + 1] = rps->used[k];
				rps->refed[k + 1] = rps->refed[k];
				rps->delta_poc[k] = deltaPOC;
				rps->used[k] = used;
				rps->refed[k] = refed;
			}
		}
	}

	// flip the negative values to largest first
	numNegPics = rps->num_negative_pic;
	for (j = 0, k = numNegPics - 1; j < numNegPics >> 1; j++, k--) {
		RK_S32 deltaPOC = rps->delta_poc[j];
		RK_U32 used = rps->used[j];
		RK_U32 refed = rps->refed[j];
		rps->delta_poc[j] = rps->delta_poc[k];
		rps->used[j] = rps->used[k];
		rps->refed[j] = rps->refed[k];
		rps->delta_poc[k] = deltaPOC;
		rps->used[k] = used;
		rps->refed[k] = refed;
	}
}

void h265e_dpb_apply_rps(h265_dpb * dpb, h265_reference_picture_set * rps,
			 int curPoc)
{
	h265_dpb_frm *outPic = NULL;
	RK_S32 i, isReference;
	// loop through all pictures in the reference picture buffer
	RK_U32 index = 0;
	h265_dpb_frm *frame_list = &dpb->frame_list[0];
	h265e_dbg_func("enter\n");
	for (index = 0; index < MPP_ARRAY_ELEMS(dpb->frame_list); index++) {
		outPic = &frame_list[index];
		if (!outPic->inited || !outPic->slice->is_referenced) {
			continue;
		}

		isReference = 0;
		// loop through all pictures in the Reference Picture Set
		// to see if the picture should be kept as reference picture
		for (i = 0; i < rps->num_positive_pic + rps->num_negative_pic;
		     i++) {
			h265e_dbg_dpb
			    ("outPic->slice->poc %d,curPoc %d dealt %d",
			     outPic->slice->poc, curPoc, rps->delta_poc[i]);
			if (!outPic->is_long_term
			    && outPic->slice->poc ==
			    curPoc + rps->delta_poc[i]) {
				isReference = 1;
				outPic->used_by_cur = (rps->used[i] == 1);
				outPic->is_long_term = 0;
			}
		}

		for (; i < rps->num_of_pictures; i++) {
			if (rps->check_lt_msb[i] == 0) {
				if (outPic->is_long_term
				    && (outPic->slice->poc ==
					rps->real_poc[i])) {
					isReference = 1;
					outPic->used_by_cur =
					    (rps->used[i] == 1);
				}
			} else {
				if (outPic->is_long_term
				    && (outPic->slice->poc ==
					rps->real_poc[i])) {
					isReference = 1;
					outPic->used_by_cur =
					    (rps->used[i] == 1);
				}
			}
		}

		// mark the picture as "unused for reference" if it is not in
		// the Reference Picture Set
		if (outPic->slice->poc != curPoc && isReference == 0) {
			h265e_dbg_dpb("free unreference buf poc %d",
				      outPic->slice->poc);
			outPic->slice->is_referenced = 0;
			outPic->used_by_cur = 0;
			outPic->on_used = 0;
			outPic->is_long_term = 0;
		}
	}
	h265e_dbg_func("leave\n");
}

void h265e_dpb_dec_refresh_marking(h265_dpb * dpb, RK_S32 poc_cur,
				   enum NALUnitType nalUnitType)
{
	RK_U32 index = 0;

	h265e_dbg_func("enter\n");

	if (nalUnitType == NAL_BLA_W_LP || nalUnitType == NAL_BLA_W_RADL || nalUnitType == NAL_BLA_N_LP || nalUnitType == NAL_IDR_W_RADL || nalUnitType == NAL_IDR_N_LP) {	// IDR or BLA picture
		// mark all pictures as not used for reference
		h265_dpb_frm *frame_List = &dpb->frame_list[0];
		for (index = 0; index < MPP_ARRAY_ELEMS(dpb->frame_list);
		     index++) {
			h265_dpb_frm *frame = &frame_List[index];
			if (frame->inited && (frame->poc != poc_cur)) {
				frame->slice->is_referenced = 0;
				frame->is_long_term = 0;
				if (frame->poc < poc_cur) {
					frame->used_by_cur = 0;
					frame->on_used = 0;
					frame->status.val = 0;
				}
			}
		}

		if (nalUnitType == NAL_BLA_W_LP
		    || nalUnitType == NAL_BLA_W_RADL
		    || nalUnitType == NAL_BLA_N_LP) {
			dpb->poc_cra = poc_cur;
		}
	} else {		// CRA or No DR
		if (dpb->refresh_pending == 1 && poc_cur > dpb->poc_cra) {	// CRA reference marking pending
			h265_dpb_frm *frame_list = &dpb->frame_list[0];
			for (index = 0;
			     index < MPP_ARRAY_ELEMS(dpb->frame_list);
			     index++) {

				h265_dpb_frm *frame = &frame_list[index];
				if (frame->inited && frame->poc != poc_cur
				    && frame->poc != dpb->poc_cra) {
					frame->slice->is_referenced = 0;
					frame->on_used = 0;
				}
			}

			dpb->refresh_pending = 0;
		}
		if (nalUnitType == NAL_CRA_NUT) {	// CRA picture found
			dpb->refresh_pending = 1;
			dpb->poc_cra = poc_cur;
		}
	}
	h265e_dbg_func("leave\n");
}

// Function will arrange the long-term pictures in the decreasing order of poc_lsb_lt,
// and among the pictures with the same lsb, it arranges them in increasing delta_poc_msb_cycle_lt value
void h265e_dpb_arrange_lt_rps(h265_dpb * dpb, h265_slice * slice)
{
	h265_reference_picture_set *rps = slice->rps;
	RK_U32 tempArray[MAX_REFS];
	RK_S32 offset = rps->num_negative_pic + rps->num_positive_pic;
	RK_S32 i, j, ctr = 0;
	RK_S32 maxPicOrderCntLSB = 1 << slice->sps->bits_for_poc;
	RK_S32 numLongPics;
	RK_S32 currMSB = 0, currLSB = 0;
	// Arrange long-term reference pictures in the correct order of LSB and MSB,
	// and assign values for pocLSBLT and MSB present flag
	RK_S32 longtermPicsPoc[MAX_REFS], longtermPicsLSB[MAX_REFS],
	    indices[MAX_REFS];
	RK_S32 longtermPicsRealPoc[MAX_REFS];
	RK_S32 longtermPicsMSB[MAX_REFS];
	RK_U32 mSBPresentFlag[MAX_REFS];
	(void)dpb;

	h265e_dbg_func("enter\n");
	if (!rps->num_long_term_pic) {
		return;
	}
	memset(longtermPicsPoc, 0, sizeof(longtermPicsPoc));	// Store POC values of LTRP
	memset(longtermPicsLSB, 0, sizeof(longtermPicsLSB));	// Store POC LSB values of LTRP
	memset(longtermPicsMSB, 0, sizeof(longtermPicsMSB));	// Store POC LSB values of LTRP
	memset(longtermPicsRealPoc, 0, sizeof(longtermPicsRealPoc));
	memset(indices, 0, sizeof(indices));	// Indices to aid in tracking sorted LTRPs
	memset(mSBPresentFlag, 0, sizeof(mSBPresentFlag));	// Indicate if MSB needs to be present

	// Get the long-term reference pictures

	for (i = rps->num_of_pictures - 1; i >= offset; i--, ctr++) {
		longtermPicsPoc[ctr] = rps->poc[i];	// LTRP POC
		longtermPicsRealPoc[ctr] = rps->real_poc[i];
		longtermPicsLSB[ctr] = getLSB(longtermPicsPoc[ctr], maxPicOrderCntLSB);	// LTRP POC LSB
		indices[ctr] = i;
		longtermPicsMSB[ctr] =
		    longtermPicsPoc[ctr] - longtermPicsLSB[ctr];
	}

	numLongPics = rps->num_long_term_pic;
	mpp_assert(ctr == numLongPics);

	// Arrange pictures in decreasing order of MSB;
	for (i = 0; i < numLongPics; i++) {
		for (j = 0; j < numLongPics - 1; j++) {
			if (longtermPicsMSB[j] < longtermPicsMSB[j + 1]) {
				MPP_SWAP(RK_S32, longtermPicsPoc[j],
					 longtermPicsPoc[j + 1]);
				MPP_SWAP(RK_S32, longtermPicsRealPoc[j],
					 longtermPicsRealPoc[j + 1]);
				MPP_SWAP(RK_S32, longtermPicsLSB[j],
					 longtermPicsLSB[j + 1]);
				MPP_SWAP(RK_S32, longtermPicsMSB[j],
					 longtermPicsMSB[j + 1]);
				MPP_SWAP(RK_S32, indices[j], indices[j + 1]);
			}
		}
	}

	for (i = 0; i < numLongPics; i++) {
		if (slice->gop_idx / maxPicOrderCntLSB > 0) {
			mSBPresentFlag[i] = 1;
		}
	}

	// tempArray for usedByCurr flag
	memset(tempArray, 0, sizeof(tempArray));
	for (i = 0; i < numLongPics; i++) {
		tempArray[i] = rps->used[indices[i]] ? 1 : 0;
	}

	// Now write the final values;
	ctr = 0;
	// currPicPoc = currMSB + currLSB
	currLSB = getLSB(slice->gop_idx, maxPicOrderCntLSB);
	currMSB = slice->gop_idx - currLSB;

	for (i = rps->num_of_pictures - 1; i >= offset; i--, ctr++) {
		rps->poc[i] = longtermPicsPoc[ctr];
		rps->delta_poc[i] = -slice->poc + longtermPicsRealPoc[ctr];

		rps->used[i] = tempArray[ctr];
		rps->poc_lsblt[i] = longtermPicsLSB[ctr];
		rps->delta_poc_msb_cycle_lt[i] =
		    (currMSB -
		     (longtermPicsPoc[ctr] -
		      longtermPicsLSB[ctr])) / maxPicOrderCntLSB;
		rps->delta_poc_msb_present_flag[i] = mSBPresentFlag[ctr];

		mpp_assert(rps->delta_poc_msb_cycle_lt[i] >= 0);	// Non-negative value
	}

	for (i = rps->num_of_pictures - 1, ctr = 1; i >= offset; i--, ctr++) {
		for (j = rps->num_of_pictures - 1 - ctr; j >= offset; j--) {
			// Here at the encoder we know that we have set the full POC value for the LTRPs, hence we
			// don't have to check the MSB present flag values for this constraint.
			mpp_assert(rps->real_poc[i] != rps->real_poc[j]);	// If assert fails, LTRP entry repeated in RPS!!!
		}
	}

	h265e_dbg_func("leave\n");
}

static h265_dpb_frm *h265e_find_cpb_in_dpb(h265_dpb_frm * frms, RK_S32 cnt,
					   EncFrmStatus * frm)
{
	RK_S32 seq_idx = frm->seq_idx;
	RK_S32 i;

	if (!frm->valid)
		return NULL;

	h265e_dbg_dpb("frm %d start finding slot \n", frm->seq_idx);
	for (i = 0; i < cnt; i++) {
		EncFrmStatus *p = NULL;

		if (!frms[i].inited) {
			continue;
		}
		p = &frms[i].status;
		if (p->valid && p->seq_idx == seq_idx) {
			h265e_dbg_dpb("frm %d match slot %d valid %d\n",
				      p->seq_idx, i, p->valid);
			return &frms[i];
		}
	}
	mpp_err_f("can not find match frm %d\n", seq_idx);
	return NULL;
}

static h265_dpb_frm *h265e_find_cpb_frame(h265_dpb_frm * frms, RK_S32 cnt,
					  EncFrmStatus * frm)
{
	RK_S32 seq_idx = frm->seq_idx;
	RK_S32 i;

	if (!frm->valid)
		return NULL;

	h265e_dbg_dpb("frm %d start finding slot \n", frm->seq_idx);
	for (i = 0; i < cnt; i++) {
		EncFrmStatus *p = NULL;
		if (!frms[i].on_used) {
			continue;
		}

		p = &frms[i].status;

		if (p->valid && p->seq_idx == seq_idx) {
			h265e_dbg_dpb("frm %d match slot %d valid %d\n",
				      p->seq_idx, i, p->valid);
			mpp_assert(p->is_non_ref == frm->is_non_ref);
			mpp_assert(p->is_lt_ref == frm->is_lt_ref);
			mpp_assert(p->lt_idx == frm->lt_idx);
			mpp_assert(p->temporal_id == frm->temporal_id);
			return &frms[i];
		}
	}

	mpp_err_f("can not find match frm %d\n", seq_idx);

	return NULL;
}

static MPP_RET h265e_check_frame_cpb(h265_dpb_frm * frm, RK_S32 cnt,
				     EncFrmStatus * frms)
{
	EncFrmStatus *p = &frm->status;
	RK_S32 seq_idx, i;
	MPP_RET ret = MPP_NOK;
	h265e_dbg_func("enter\n");

	seq_idx = p->seq_idx;
	for (i = 0; i < cnt; i++) {

		if (!frms[i].valid) {
			continue;
		}

		if (frms[i].seq_idx == seq_idx) {
			ret = MPP_OK;
		}
	}

	h265e_dbg_func("leave\n");
	return ret;
}

void h265e_dpb_cpb2rps(h265_dpb * dpb, RK_S32 curPoc, h265_slice * slice,
		       EncCpbStatus * cpb)
{
	RK_S32 i;
	RK_S32 st_size = 0;
	RK_S32 lt_size = 0;
	RK_S32 nLongTermRefPicPoc[MAX_NUM_LONG_TERM_REF_PIC_POC];
	RK_S32 nLongTermRefPicRealPoc[MAX_NUM_LONG_TERM_REF_PIC_POC];
	RK_S32 nLongTermDealtPoc[MAX_NUM_LONG_TERM_REF_PIC_POC];
	RK_U32 isMsbValid[MAX_NUM_LONG_TERM_REF_PIC_POC];
	RK_U32 isShortTermValid[MAX_REFS];
	h265_rps_list *rps_list = &dpb->rps_list;
	h265_reference_picture_set *rps =
	    (h265_reference_picture_set *) & slice->local_rps;
	RK_S32 idx_rps;
	h265_dpb_frm *p = NULL;
	RK_S32 ref_dealt_poc = 0;

	h265e_dbg_func("enter\n");

	memset(isShortTermValid, 1, sizeof(RK_U32) * MAX_REFS);
	memset(rps, 0, sizeof(h265_reference_picture_set));
	for (idx_rps = 0; idx_rps < 16; idx_rps++) {
		rps->delta_poc[idx_rps] = 0;
		rps->used[idx_rps] = 0;
		rps->refed[idx_rps] = 0;
	}

	memset(rps->delta_poc, 0, MAX_REFS * sizeof(int));

	if (cpb->curr.is_lt_ref)
		mpp_assert(slice->sps->long_term_ref_pics_present_flag);

	idx_rps = 0;
	for (i = 0; i < MAX_CPB_REFS; i++) {
		EncFrmStatus *frm = &cpb->init[i];

		if (!frm->valid)
			continue;

		mpp_assert(!frm->is_non_ref);

		h265e_dbg_dpb
		    ("idx %d frm %d valid %d is_non_ref %d lt_ref %d\n", i,
		     frm->seq_idx, frm->valid, frm->is_non_ref, frm->is_lt_ref);

		p = h265e_find_cpb_frame(dpb->frame_list, MAX_REFS, frm);
		if (p) {
			if (!frm->is_lt_ref) {
				p->status.val = frm->val;
				rps->delta_poc[idx_rps] = p->poc - curPoc;
				rps->used[idx_rps] = 1;
				idx_rps++;
				st_size++;
				h265e_dbg_dpb
				    ("found st %d st_size %d %p deat_poc %d\n",
				     i, st_size, frm,
				     rps->delta_poc[idx_rps - 1]);
			} else {
				nLongTermRefPicPoc[lt_size] = p->gop_idx;
				nLongTermRefPicRealPoc[lt_size] = p->poc;
				nLongTermDealtPoc[lt_size] = p->poc - curPoc;
				isMsbValid[lt_size] =
				    p->gop_idx >=
				    (RK_S32) (1 << p->slice->sps->bits_for_poc);
				p->status.val = frm->val;
				h265e_dbg_dpb
				    ("found lt %d lt_size %d %p dealt poc %d\n",
				     i, lt_size, frm,
				     nLongTermDealtPoc[lt_size]);
				lt_size++;
			}
		}
	}
	sort_delta_poc(rps);

	if (slice->slice_type == I_SLICE) {
		rps->inter_rps_prediction = 0;
		rps->num_long_term_pic = 0;
		rps->num_negative_pic = 0;
		rps->num_positive_pic = 0;
		rps->num_of_pictures = 0;

	} else {
		p = h265e_find_cpb_frame(dpb->frame_list, MAX_REFS, &cpb->refr);
		if (p == NULL) {
			mpp_err("ref frame no found in refer index %d",
				cpb->refr.seq_idx);
		} else {
			ref_dealt_poc = p->poc - curPoc;
		}

		for (i = 0; i < st_size; i++) {
			rps->refed[i] = (rps->delta_poc[i] == ref_dealt_poc);
		}
	}

	if (lt_size > 0) {
		for (i = 0; i < lt_size; i++) {
			h265e_dbg_dpb("numLongTermRefPic %d nShortTerm %d",
				      lt_size, st_size);
			rps->poc[i + st_size] = nLongTermRefPicPoc[i];
			rps->real_poc[i + st_size] = nLongTermRefPicRealPoc[i];
			rps->used[i + st_size] = 1;
			rps->refed[i + st_size] = p->is_long_term;
			rps->delta_poc[i + st_size] = nLongTermDealtPoc[i];
			rps->check_lt_msb[i + st_size] = isMsbValid[i];
		}
	}

	rps->num_negative_pic = st_size;
	rps->num_positive_pic = 0;
	rps->num_long_term_pic = lt_size;
	rps->num_of_pictures = st_size + lt_size;
	slice->rps = rps;
	h265e_dpb_apply_rps(dpb, slice->rps, curPoc);
	h265e_dpb_arrange_lt_rps(dpb, slice);
	h265e_dpb_set_ref_list(rps_list, rps, ref_dealt_poc);
	memcpy(&slice->rpl_modification, rps_list->rpl_modification,
	       sizeof(h265_rpl_modification));
	h265e_dbg_func("leave\n");
}

void h265e_dpb_free_unsed(h265_dpb * dpb, EncCpbStatus * cpb)
{
	RK_S32 i = 0;

	h265e_dbg_func("enter\n");

	if (cpb->curr.is_non_ref) {
		h265_dpb_frm *frm =
		    h265e_find_cpb_frame(dpb->frame_list, MAX_REFS, &cpb->curr);
		if (frm) {
			h265e_dbg_dpb("free curr unreference buf poc %d",
				      frm->slice->poc);
			frm->is_long_term = 0;
			frm->used_by_cur = 0;
			frm->on_used = 0;
			frm->slice->is_referenced = 0;
		}
	}

	for (i = 0; i < (RK_S32) MPP_ARRAY_ELEMS(dpb->frame_list); i++) {
		h265_dpb_frm *frm = &dpb->frame_list[i];
		if (!frm->on_used)
			continue;
		if (h265e_check_frame_cpb(frm, MAX_REFS, &cpb->final[0])) {
			h265e_dbg_dpb("cpb final unreference buf poc %d",
				      frm->slice->poc);
			frm->is_long_term = 0;
			frm->used_by_cur = 0;
			frm->on_used = 0;
			frm->slice->is_referenced = 0;
		}
	}

	h265e_dbg_func("leave\n");
}

void h265e_dpb_proc_cpb(h265_dpb * dpb, EncCpbStatus * cpb)
{
	EncFrmStatus *curr = &cpb->curr;
	RK_U32 index = 0, i = 0;
	h265_dpb_frm *p = NULL;
	RK_U32 need_rebuild = 0;
	RK_S32 max_gop_id = 0, max_poc = 0;
	h265_dpb_frm *frame = NULL;

	if (!dpb || !cpb)
		return;

	if (curr->is_idr) {
		for (index = 0; index < MPP_ARRAY_ELEMS(dpb->frame_list);
		     index++) {
			h265_dpb_frm *frame = &dpb->frame_list[index];
			if (frame->inited) {
				frame->slice->is_referenced = 0;
				frame->is_long_term = 0;
				frame->used_by_cur = 0;
				frame->on_used = 0;
				frame->status.val = 0;
			}
		}
		return;
	}

	for (i = 0; i < MAX_CPB_REFS; i++) {
		EncFrmStatus *frm = &cpb->init[i];

		if (!frm->valid)
			continue;

		mpp_assert(!frm->is_non_ref);

		h265e_dbg_dpb
		    ("idx %d frm %d valid %d is_non_ref %d lt_ref %d\n", i,
		     frm->seq_idx, frm->valid, frm->is_non_ref, frm->is_lt_ref);

		p = h265e_find_cpb_in_dpb(dpb->frame_list, MAX_REFS, frm);
		if (!p->on_used) {
			p->on_used = 1;
			p->status.val = frm->val;
			p->slice->is_referenced = 1;
			need_rebuild = 1;
		}
	}

	if (need_rebuild) {
		h265e_dbg_dpb("cpb roll back found");
		for (index = 0; index < MPP_ARRAY_ELEMS(dpb->frame_list);
		     index++) {
			frame = &dpb->frame_list[index];
			if (frame->on_used) {
				if (max_poc < frame->slice->poc) {
					max_poc = frame->slice->poc;
				}
				if (max_gop_id < frame->slice->gop_idx) {
					max_gop_id = frame->slice->gop_idx;
				}
			}
		}
		frame = dpb->curr;
		if (frame->inited) {
			frame->slice->is_referenced = 0;
			frame->is_long_term = 0;
			frame->used_by_cur = 0;
			frame->on_used = 0;
			frame->status.val = 0;
		}
		dpb->seq_idx = max_poc;
		dpb->gop_idx = max_gop_id;
	}

	for (index = 0; index < MPP_ARRAY_ELEMS(dpb->frame_list); index++) {
		frame = &dpb->frame_list[index];
		if (frame->inited && !frame->on_used) {
			h265e_dbg_dpb
			    ("reset index %d frame->inited %d rame->on_used %d",
			     index, frame->inited, frame->on_used);
			frame->status.val = 0;
		}
	}
}

void h265e_dpb_build_list(h265_dpb * dpb, EncCpbStatus * cpb)
{
	RK_S32 poc_cur = dpb->curr->slice->poc;
	h265_slice *slice = dpb->curr->slice;
	RK_U32 bGPBcheck = 0;
	RK_S32 i;

	h265e_dbg_func("enter\n");
	if (get_nal_unit_type(dpb, poc_cur) == NAL_IDR_W_RADL ||
	    get_nal_unit_type(dpb, poc_cur) == NAL_IDR_N_LP) {
		dpb->last_idr = poc_cur;
	}

	slice->last_idr = dpb->last_idr;
	slice->temporal_layer_non_ref_flag = !slice->is_referenced;
	// Set the nal unit type
	slice->nal_unit_type = get_nal_unit_type(dpb, poc_cur);

	// If the slice is un-referenced, change from _R "referenced" to _N "non-referenced" NAL unit type
	if (slice->temporal_layer_non_ref_flag) {
		switch (slice->nal_unit_type) {
		case NAL_TRAIL_R:
			slice->nal_unit_type = NAL_TRAIL_N;
			break;
		case NAL_RADL_R:
			slice->nal_unit_type = NAL_RADL_N;
			break;
		case NAL_RASL_R:
			slice->nal_unit_type = NAL_RASL_N;
			break;
		default:
			break;
		}
	}
	// Do decoding refresh marking if any
	h265e_dpb_dec_refresh_marking(dpb, poc_cur, slice->nal_unit_type);
	h265e_dpb_cpb2rps(dpb, poc_cur, slice, cpb);

	slice->num_ref_idx[L0] = MPP_MIN(dpb->max_ref_l0, slice->rps->num_of_pictures);	// Ensuring L0 contains just the -ve POC
	slice->num_ref_idx[L1] =
	    MPP_MIN(dpb->max_ref_l1, slice->rps->num_of_pictures);

	h265e_slice_set_ref_list(dpb->frame_list, slice);

	// Slice type refinement
	if ((slice->slice_type == B_SLICE) && (slice->num_ref_idx[L1] == 0)) {
		slice->slice_type = P_SLICE;
	}

	if (slice->slice_type == B_SLICE) {
		// TODO: Can we estimate this from lookahead?
		RK_U32 bLowDelay = 1;
		RK_S32 curPOC = slice->poc;
		RK_S32 refIdx = 0;
		slice->col_from_l0_flag = 0;

		for (refIdx = 0; refIdx < slice->num_ref_idx[L0] && bLowDelay;
		     refIdx++) {
			if (slice->ref_pic_list[L0][refIdx]->poc > curPOC) {
				bLowDelay = 0;
			}
		}

		for (refIdx = 0; refIdx < slice->num_ref_idx[L1] && bLowDelay;
		     refIdx++) {
			if (slice->ref_pic_list[L1][refIdx]->poc > curPOC) {
				bLowDelay = 0;
			}
		}

	}
	h265e_slice_set_ref_poc_list(slice);
	if (slice->slice_type == B_SLICE) {
		if (slice->num_ref_idx[L0] == slice->num_ref_idx[L1]) {
			bGPBcheck = 0;
			for (i = 0; i < slice->num_ref_idx[L1]; i++) {
				if (slice->ref_poc_list[L1][i] !=
				    slice->ref_poc_list[L0][i]) {
					bGPBcheck = 0;
					break;
				}
			}
		}
	}

	slice->lmvd_l1_zero = bGPBcheck;
	if (slice->slice_type == I_SLICE) {
		slice->tot_poc_num = 0;
	} else {
		slice->tot_poc_num = slice->local_rps.num_of_pictures;
	}
	h265e_dpb_free_unsed(dpb, cpb);
	h265e_dbg_func("leave\n");
}
