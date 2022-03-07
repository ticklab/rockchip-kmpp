// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "jpege_rc"

#include "mpp_mem.h"
#include "mpp_maths.h"

#include "rc_debug.h"
#include "rc_ctx.h"
#include "rc_model_v2.h"

const RcImplApi default_jpege = {
	"default",
	MPP_VIDEO_CodingMJPEG,
	sizeof(RcModelV2Ctx),
	rc_model_v2_init,
	rc_model_v2_deinit,
	NULL,
	rc_model_v2_check_reenc,
	rc_model_v2_start,
	rc_model_v2_end,
	rc_model_v2_hal_start,
	rc_model_v2_hal_end,
	rc_model_v2_proc_show,
};
