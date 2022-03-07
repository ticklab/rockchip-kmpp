// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __H265E_DPB_H__
#define __H265E_DPB_H__

#include "h265e_slice.h"
#include "h265_syntax.h"
#include "mpp_enc_cfg.h"
#include "mpp_rc_defs.h"
/*
 * H.265 encoder dpb structure info
 *
 *     +----------+ DPB +-> build list +-> REF_LIST
 *     |             +                        +
 *     v             v                        v
 * FRM_BUF_GRP    DPB_FRM +------+------> RPS
 *     +             +
 *     |             v
 *     +--------> FRM_BUF
 *
 * H265eDpb is overall structure contain the whole dpb info.
 * It is composed H265eDpbFrm and H265eRpsList.
 *
 */

#define H265E_MAX_BUF_CNT   8
#define H265_MAX_GOP        64	///< max. value of hierarchical GOP size
#define H265_MAX_GOP_CNT    (H265_MAX_GOP + 1)

#define RPSLIST_MAX         (H265_MAX_GOP * 3)

typedef struct h265_dpb_e h265_dpb;
typedef struct h265_reference_picture_set_e h265_reference_picture_set;
typedef struct h265_rpl_modification_e h265_rpl_modification;
typedef struct h265_slice_e h265_slice;

/*
 * Split reference frame configure to two parts
 * The first part is slice depended info like poc / frame_num, and frame
 * type and flags.
 * The other part is gop structure depended info like gop index, ref_status
 * and ref_frm_index. This part is inited from dpb gop hierarchy info.
 */

typedef struct h265_dpb_frm_e {
	h265_dpb *dpb;

	RK_S32 slot_idx;
	RK_S32 seq_idx;
	RK_S32 gop_idx;
	RK_S32 gop_cnt;
	EncFrmStatus status;

	RK_U32 on_used;
	RK_U32 inited;

	RK_U32 is_long_term;
	RK_U32 used_by_cur;
	RK_U32 check_lt_msb;
	RK_S32 poc;

	h265_slice *slice;

	RK_S64 pts;
	RK_S64 dts;

	RK_U32 is_key_frame;
} h265_dpb_frm;

typedef struct h265_rps_list_e {
	RK_S32 lt_num;
	RK_S32 st_num;
	RK_S32 poc_cur_list;

	RK_S32 poc[RPSLIST_MAX];

	RK_U32 used_by_cur[MAX_REFS];

	h265_rpl_modification *rpl_modification;
} h265_rps_list;

/*
 * dpb frame arrangement
 *
 * If dpb size is 3 then dpb frame will be total 4 frames.
 * Then the frame 3 is always current frame and frame 0~2 is reference frame
 * in the gop structure.
 *
 * When one frame is encoded all it info will be moved to its gop position for
 * next frame encoding.
 */
typedef struct h265_dpb_e {
	RK_S32 seq_idx;
	RK_S32 gop_idx;
	// status and count for one gop structure

	RK_S32 last_idr;
	RK_S32 poc_cra;
	RK_U32 refresh_pending;
	RK_S32 max_ref_l0;
	RK_S32 max_ref_l1;

	h265_rps_list rps_list;
	h265_dpb_frm *curr;
	h265_dpb_frm frame_list[MAX_REFS + 1];
} h265_dpb;

#ifdef __cplusplus
extern "C" {
#endif

	void h265e_set_ref_list(h265_rps_list * rps_list,
				h265_reference_picture_set * rps);
	MPP_RET h265e_dpb_init(h265_dpb ** dpb);
	MPP_RET h265e_dpb_deinit(h265_dpb * dpb);
	MPP_RET h265e_dpb_setup_buf_size(h265_dpb * dpb, RK_U32 size[],
					 RK_U32 count);
	MPP_RET h265e_dpb_get_curr(h265_dpb * dpb);
	void h265e_dpb_build_list(h265_dpb * dpb, EncCpbStatus * cpb);
	void h265e_dpb_proc_cpb(h265_dpb * dpb, EncCpbStatus * cpb);

#define h265e_dpb_dump_frms(dpb) h265e_dpb_dump_frm(dpb, __FUNCTION__)

	void h265e_dpb_dump_frm(h265_dpb * dpb, const char *caller);

#ifdef __cplusplus
}
#endif
#endif				/* __H265E_DPB_H__ */
