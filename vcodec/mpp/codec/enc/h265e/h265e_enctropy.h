// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __H265E_ENCTROPY_H__
#define __H265E_ENCTROPY_H__

#include "mpp_bitwrite.h"
typedef struct h265_contex_model_e {
	RK_U8 m_state;		///< internal state variable
	RK_U8 bBinsCoded;
} h265_contex_model;
typedef struct h265_cabac_ctx {
	MppWriteCtx * m_bitIf;
	RK_U32 m_low;
	RK_U32 m_range;
	RK_U32 m_bufferedByte;
	RK_S32 m_numBufferedBytes;
	RK_S32 m_bitsLeft;
	RK_U64 m_fracBits;
	RK_U8 m_bIsCounter;
} h265_cabac_ctx;

#ifdef __cplusplus
extern "C" {

#endif	/*  */
void h265e_cabac_init(h265_cabac_ctx * cabac_ctx,
		      MppWriteCtx * bitIf);
void h265e_reset_enctropy(void *slice_ctx);
void h265e_cabac_resetBits(h265_cabac_ctx * cabac_ctx);
void h265e_cabac_encodeBin(h265_cabac_ctx * cabac_ctx,
			   h265_contex_model * ctxModel,
			   RK_U32 binValue);
void h265e_cabac_encodeBinTrm(h265_cabac_ctx * cabac_ctx,
			      RK_U32 binValue);
void h265e_cabac_start(h265_cabac_ctx * cabac_ctx);
void h265e_cabac_finish(h265_cabac_ctx * cabac_ctx);
void h265e_cabac_flush(h265_cabac_ctx * cabac_ctx);

#ifdef __cplusplus
}
#endif	/*  */

#endif	/*  */
