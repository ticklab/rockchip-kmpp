// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_2STR_H__
#define __MPP_2STR_H__

#include "rk_type.h"
#include "rk_venc_rc.h"
#include "mpp_frame.h"

#ifdef  __cplusplus
extern "C" {
#endif

const char *strof_ctx_type(MppCtxType type);
const char *strof_coding_type(MppCodingType coding);
const char *strof_rc_mode(MppEncRcMode rc_mode);
const char *strof_profle(MppCodingType coding, RK_U32 profile);
const char *strof_gop_mode(MppEncRcGopMode gop_mode);
const char *strof_pixel_fmt(MppFrameFormat fmt);
const char *strof_bool(RK_U32 enable);
const char *strof_drop(RK_U32 mode);
const char *strof_suprmode(RK_U32 mode);


#ifdef  __cplusplus
}
#endif

#endif
