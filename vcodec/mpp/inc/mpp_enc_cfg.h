// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_ENC_CFG_H__
#define __MPP_ENC_CFG_H__

#include "rk_venc_cmd.h"
#include "rk_venc_ref.h"
/*
 * MppEncCfgSet shows the relationship between different configuration
 * Due to the huge amount of configurable parameters we need to setup
 * only minimum amount of necessary parameters.
 *
 * For normal user rc and prep config are enough.
 */

typedef enum ENC_FRAME_TYPE_E {
	INTER_P_FRAME = 0,
	INTER_B_FRAME = 1,
	INTRA_FRAME = 2,
	INTER_VI_FRAME = 3,
} ENC_FRAME_TYPE;

typedef struct MppEncCfgSet_t {
	MppEncBaseCfg base;

	// esential config
	MppEncPrepCfg prep;
	MppEncRcCfg rc;

	// hardware related config
	MppEncHwCfg hw;

	// codec detail config
	MppEncCodecCfg codec;

	MppEncSliceSplit split;
	MppEncROICfg roi;
	MppEncOSDData3 osd;
	MppEncRefCfg ref_cfg;
} MppEncCfgSet;

#endif /*__MPP_ENC_CFG_H__*/
