// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __RK_VENC_RC_H__
#define __RK_VENC_RC_H__

#include "rk_type.h"

/* Rate control parameter */
typedef enum MppEncRcMode_e {
	MPP_ENC_RC_MODE_VBR,
	MPP_ENC_RC_MODE_CBR,
	MPP_ENC_RC_MODE_FIXQP,
	MPP_ENC_RC_MODE_AVBR,
	MPP_ENC_RC_MODE_BUTT
} MppEncRcMode;

typedef enum MppEncRcPriority_e {
	MPP_ENC_RC_BY_BITRATE_FIRST,
	MPP_ENC_RC_BY_FRM_SIZE_FIRST,
	MPP_ENC_RC_PRIORITY_BUTT
} MppEncRcPriority;

typedef enum MppEncRcDropFrmMode_e {
	MPP_ENC_RC_DROP_FRM_DISABLED,
	MPP_ENC_RC_DROP_FRM_NORMAL,
	MPP_ENC_RC_DROP_FRM_PSKIP,
	MPP_ENC_RC_DROP_FRM_BUTT
} MppEncRcDropFrmMode;

typedef enum MppEncRcSuperFrameMode_t {
	MPP_ENC_RC_SUPER_FRM_NONE,
	MPP_ENC_RC_SUPER_FRM_DROP,
	MPP_ENC_RC_SUPER_FRM_REENC,
	MPP_ENC_RC_SUPER_FRM_BUTT
} MppEncRcSuperFrameMode;

typedef enum MppEncRcGopMode_e {
	MPP_ENC_RC_NORMAL_P,
	MPP_ENC_RC_SMART_P,
	MPP_ENC_RC_GOP_MODE_BUTT,
} MppEncRcGopMode;

#endif /*__RK_VENC_RC_H__*/
