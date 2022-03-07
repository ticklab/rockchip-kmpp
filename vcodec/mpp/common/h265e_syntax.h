// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __H265E_SYNTAX_H__
#define __H265E_SYNTAX_H__

typedef struct H265eSyntax_t {
	RK_S32 idr_request;
//   RK_S32          eos;
} H265eSyntax;

typedef struct H265eFeedback_t {
	RK_U32 bs_size;
	RK_U32 enc_pic_cnt;
	RK_U32 pic_type;
	RK_U32 avg_ctu_qp;
	RK_U32 gop_idx;
	RK_U32 poc;
	RK_U32 src_idx;
	RK_U32 status;
} H265eFeedback;

#endif
