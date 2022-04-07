// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_ENC_REF_SETUP_H__
#define __MPP_ENC_REF_SETUP_H__

#include "rk_venc_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET mpi_enc_gen_ref_cfg(MppEncRefCfg ref, RK_S32 gop_mode);
MPP_RET mpi_enc_gen_smart_gop_ref_cfg(MppEncRefCfg ref, MppEncRefParam *para);
MPP_RET mpi_enc_gen_hir_skip_ref(MppEncRefCfg ref, MppEncRefParam *para);

#ifdef __cplusplus
}
#endif
#endif /*__MPP_ENC_REF_SETUP_H__*/
