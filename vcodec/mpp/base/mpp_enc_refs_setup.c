// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_enc_ref"

#include <linux/string.h>

#include "mpp_log.h"
#include "mpp_enc_refs_setup.h"

MPP_RET mpi_enc_gen_ref_cfg(MppEncRefCfg ref, RK_S32 gop_mode)
{
	MppEncRefLtFrmCfg lt_ref[4];
	MppEncRefStFrmCfg st_ref[16];
	RK_S32 lt_cnt = 0;
	RK_S32 st_cnt = 0;
	MPP_RET ret = MPP_OK;

	memset(&lt_ref, 0, sizeof(lt_ref));
	memset(&st_ref, 0, sizeof(st_ref));

	switch (gop_mode) {
	case 3 : {
		// tsvc4
		//      /-> P1      /-> P3        /-> P5      /-> P7
		//     /           /             /           /
		//    //--------> P2            //--------> P6
		//   //                        //
		//  ///---------------------> P4
		// ///
		// P0 ------------------------------------------------> P8
		lt_cnt = 1;

		/* set 8 frame lt-ref gap */
		lt_ref[0].lt_idx        = 0;
		lt_ref[0].temporal_id   = 0;
		lt_ref[0].ref_mode      = REF_TO_PREV_LT_REF;
		lt_ref[0].lt_gap        = 8;
		lt_ref[0].lt_delay      = 0;

		st_cnt = 9;
		/* set tsvc4 st-ref struct */
		/* st 0 layer 0 - ref */
		st_ref[0].is_non_ref    = 0;
		st_ref[0].temporal_id   = 0;
		st_ref[0].ref_mode      = REF_TO_TEMPORAL_LAYER;
		st_ref[0].ref_arg       = 0;
		st_ref[0].repeat        = 0;
		/* st 1 layer 3 - non-ref */
		st_ref[1].is_non_ref    = 1;
		st_ref[1].temporal_id   = 3;
		st_ref[1].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[1].ref_arg       = 0;
		st_ref[1].repeat        = 0;
		/* st 2 layer 2 - ref */
		st_ref[2].is_non_ref    = 0;
		st_ref[2].temporal_id   = 2;
		st_ref[2].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[2].ref_arg       = 0;
		st_ref[2].repeat        = 0;
		/* st 3 layer 3 - non-ref */
		st_ref[3].is_non_ref    = 1;
		st_ref[3].temporal_id   = 3;
		st_ref[3].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[3].ref_arg       = 0;
		st_ref[3].repeat        = 0;
		/* st 4 layer 1 - ref */
		st_ref[4].is_non_ref    = 0;
		st_ref[4].temporal_id   = 1;
		st_ref[4].ref_mode      = REF_TO_PREV_LT_REF;
		st_ref[4].ref_arg       = 0;
		st_ref[4].repeat        = 0;
		/* st 5 layer 3 - non-ref */
		st_ref[5].is_non_ref    = 1;
		st_ref[5].temporal_id   = 3;
		st_ref[5].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[5].ref_arg       = 0;
		st_ref[5].repeat        = 0;
		/* st 6 layer 2 - ref */
		st_ref[6].is_non_ref    = 0;
		st_ref[6].temporal_id   = 2;
		st_ref[6].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[6].ref_arg       = 0;
		st_ref[6].repeat        = 0;
		/* st 7 layer 3 - non-ref */
		st_ref[7].is_non_ref    = 1;
		st_ref[7].temporal_id   = 3;
		st_ref[7].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[7].ref_arg       = 0;
		st_ref[7].repeat        = 0;
		/* st 8 layer 0 - ref */
		st_ref[8].is_non_ref    = 0;
		st_ref[8].temporal_id   = 0;
		st_ref[8].ref_mode      = REF_TO_TEMPORAL_LAYER;
		st_ref[8].ref_arg       = 0;
		st_ref[8].repeat        = 0;
	} break;
	case 2 : {
		// tsvc3
		//     /-> P1      /-> P3
		//    /           /
		//   //--------> P2
		//  //
		// P0/---------------------> P4
		lt_cnt = 0;

		st_cnt = 5;
		/* set tsvc4 st-ref struct */
		/* st 0 layer 0 - ref */
		st_ref[0].is_non_ref    = 0;
		st_ref[0].temporal_id   = 0;
		st_ref[0].ref_mode      = REF_TO_TEMPORAL_LAYER;
		st_ref[0].ref_arg       = 0;
		st_ref[0].repeat        = 0;
		/* st 1 layer 2 - non-ref */
		st_ref[1].is_non_ref    = 1;
		st_ref[1].temporal_id   = 2;
		st_ref[1].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[1].ref_arg       = 0;
		st_ref[1].repeat        = 0;
		/* st 2 layer 1 - ref */
		st_ref[2].is_non_ref    = 0;
		st_ref[2].temporal_id   = 1;
		st_ref[2].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[2].ref_arg       = 0;
		st_ref[2].repeat        = 0;
		/* st 3 layer 2 - non-ref */
		st_ref[3].is_non_ref    = 1;
		st_ref[3].temporal_id   = 2;
		st_ref[3].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[3].ref_arg       = 0;
		st_ref[3].repeat        = 0;
		/* st 4 layer 0 - ref */
		st_ref[4].is_non_ref    = 0;
		st_ref[4].temporal_id   = 0;
		st_ref[4].ref_mode      = REF_TO_TEMPORAL_LAYER;
		st_ref[4].ref_arg       = 0;
		st_ref[4].repeat        = 0;
	} break;
	case 1 : {
		// tsvc2
		//   /-> P1
		//  /
		// P0--------> P2
		lt_cnt = 0;

		st_cnt = 3;
		/* set tsvc4 st-ref struct */
		/* st 0 layer 0 - ref */
		st_ref[0].is_non_ref    = 0;
		st_ref[0].temporal_id   = 0;
		st_ref[0].ref_mode      = REF_TO_TEMPORAL_LAYER;
		st_ref[0].ref_arg       = 0;
		st_ref[0].repeat        = 0;
		/* st 1 layer 2 - non-ref */
		st_ref[1].is_non_ref    = 1;
		st_ref[1].temporal_id   = 1;
		st_ref[1].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[1].ref_arg       = 0;
		st_ref[1].repeat        = 0;
		/* st 2 layer 1 - ref */
		st_ref[2].is_non_ref    = 0;
		st_ref[2].temporal_id   = 0;
		st_ref[2].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[2].ref_arg       = 0;
		st_ref[2].repeat        = 0;
	} break;
	default : {
		mpp_err_f("unsupport gop mode %d\n", gop_mode);
	} break;
	}

	if (lt_cnt || st_cnt) {
		ret = mpp_enc_ref_cfg_set_cfg_cnt(ref, lt_cnt, st_cnt);

		if (lt_cnt)
			ret = mpp_enc_ref_cfg_add_lt_cfg(ref, lt_cnt, lt_ref);

		if (st_cnt)
			ret = mpp_enc_ref_cfg_add_st_cfg(ref, st_cnt, st_ref);

		/* check and get dpb size */
		ret = mpp_enc_ref_cfg_check(ref);
	}

	return ret;
}

MPP_RET mpi_enc_gen_smart_gop_ref_cfg(MppEncRefCfg ref, MppEncRefParam *para)
{
	MppEncRefLtFrmCfg lt_ref[4];
	MppEncRefStFrmCfg st_ref[16];
	RK_S32 lt_cnt = 1;
	RK_S32 st_cnt = 8;
	RK_S32 pos = 0;
	MPP_RET ret = MPP_OK;
	RK_S32 gop_len = para->gop_len;
	RK_S32 vi_len = para->vi_len;

	memset(&lt_ref, 0, sizeof(lt_ref));
	memset(&st_ref, 0, sizeof(st_ref));

	ret = mpp_enc_ref_cfg_set_cfg_cnt(ref, lt_cnt, st_cnt);

	/* set 8 frame lt-ref gap */
	lt_ref[0].lt_idx        = 0;
	lt_ref[0].temporal_id   = 0;
	lt_ref[0].ref_mode      = REF_TO_PREV_LT_REF;
	lt_ref[0].lt_gap        = gop_len;
	lt_ref[0].lt_delay      = 0;

	ret = mpp_enc_ref_cfg_add_lt_cfg(ref, 1, lt_ref);

	/* st 0 layer 0 - ref */
	st_ref[pos].is_non_ref  = 0;
	st_ref[pos].temporal_id = 0;
	st_ref[pos].ref_mode    = REF_TO_PREV_INTRA;
	st_ref[pos].ref_arg     = 0;
	st_ref[pos].repeat      = 0;
	pos++;

	/* st 1 layer 1 - non-ref */
	if (vi_len > 1) {
		st_ref[pos].is_non_ref  = 0;
		st_ref[pos].temporal_id = 1;
		st_ref[pos].ref_mode    = REF_TO_PREV_REF_FRM;
		st_ref[pos].ref_arg     = 0;
		st_ref[pos].repeat      = vi_len - 2;
		pos++;
	}

	st_ref[pos].is_non_ref  = 0;
	st_ref[pos].temporal_id = 0;
	st_ref[pos].ref_mode    = REF_TO_PREV_INTRA;
	st_ref[pos].ref_arg     = 0;
	st_ref[pos].repeat      = 0;
	pos++;

	ret = mpp_enc_ref_cfg_add_st_cfg(ref, pos, st_ref);

	/* check and get dpb size */
	ret = mpp_enc_ref_cfg_check(ref);

	return ret;
}

MPP_RET mpi_enc_gen_hir_skip_ref(MppEncRefCfg ref, MppEncRefParam *para)
{
	MppEncRefLtFrmCfg lt_ref[4];
	MppEncRefStFrmCfg st_ref[16];
	RK_S32 lt_cnt = 1;
	RK_S32 st_cnt = 8;
	MPP_RET ret = MPP_OK;
	RK_S32 pos = 0;
	RK_S32 gop_len = para->gop_len;
	RK_U32 base_N = para->base_N;
	RK_U32 enh_M = para->enh_M;
	RK_U32 pre_en = para->pre_en;

	memset(&lt_ref, 0, sizeof(lt_ref));
	memset(&st_ref, 0, sizeof(st_ref));

	if (pre_en) {
		lt_ref[0].lt_idx        = 0;
		lt_ref[0].temporal_id   = 0;
		lt_ref[0].ref_mode      = REF_TO_PREV_LT_REF;
		lt_ref[0].lt_gap        = base_N * (enh_M + 1);
		lt_ref[0].lt_delay      = 0;

		st_ref[pos].is_non_ref  = 0;
		st_ref[pos].temporal_id = 0;
		st_ref[pos].ref_mode    = REF_TO_PREV_REF_FRM;
		st_ref[pos].ref_arg     = 0;
		st_ref[pos].repeat      = 0;
		pos++;
		if (enh_M > 1) {
			st_ref[pos].is_non_ref    = 0;
			st_ref[pos].temporal_id   = 1;
			st_ref[pos].ref_mode      = REF_TO_PREV_REF_FRM;
			st_ref[pos].ref_arg       = 0;
			st_ref[pos].repeat        = enh_M - 1;
			pos++;
		}
		st_ref[pos].is_non_ref  = 0;
		st_ref[pos].temporal_id = 0;
		st_ref[pos].ref_mode    = REF_TO_PREV_LT_REF;
		st_ref[pos].ref_arg     = 0;
		st_ref[pos].repeat      = 0;
		pos++;
	} else {
		lt_ref[0].lt_idx        = 0;
		lt_ref[0].temporal_id   = 0;
		lt_ref[0].ref_mode      = REF_TO_PREV_LT_REF;
		lt_ref[0].lt_gap        = gop_len;
		lt_ref[0].lt_delay      = 0;

		st_ref[pos].is_non_ref  = 0;
		st_ref[pos].temporal_id = 0;
		st_ref[pos].ref_mode    = REF_TO_PREV_LT_REF;
		st_ref[pos].ref_arg     = 0;
		st_ref[pos].repeat      = 0;
		pos++;

		st_ref[pos].is_non_ref    = 0;
		st_ref[pos].temporal_id   = 1;
		st_ref[pos].ref_mode      = REF_TO_PREV_REF_FRM;
		st_ref[pos].ref_arg       = 0;
		st_ref[pos].repeat        = enh_M - 1;
		pos++;

		st_ref[pos].is_non_ref  = 0;
		st_ref[pos].temporal_id = 0;
		st_ref[pos].ref_mode    = REF_TO_PREV_INTRA;
		st_ref[pos].ref_arg     = 0;
		st_ref[pos].repeat      = 0;
		pos++;

	}

	if (lt_cnt || st_cnt) {
		ret = mpp_enc_ref_cfg_set_cfg_cnt(ref, lt_cnt, st_cnt);

		if (lt_cnt)
			ret = mpp_enc_ref_cfg_add_lt_cfg(ref, lt_cnt, lt_ref);

		if (st_cnt)
			ret = mpp_enc_ref_cfg_add_st_cfg(ref, pos, st_ref);

		/* check and get dpb size */
		ret = mpp_enc_ref_cfg_check(ref);
	}

	return ret;
}

