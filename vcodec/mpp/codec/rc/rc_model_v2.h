// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __RC_MODEL_V2_H__
#define __RC_MODEL_V2_H__

#include "mpp_rc_api.h"

#ifdef  __cplusplus
extern "C" {
#endif

MPP_RET rc_model_v2_init(void *ctx, RcCfg * cfg);
MPP_RET rc_model_v2_deinit(void *ctx);

MPP_RET rc_model_v2_check_reenc(void *ctx, EncRcTask * task);

MPP_RET rc_model_v2_start(void *ctx, EncRcTask * task);
MPP_RET rc_model_v2_end(void *ctx, EncRcTask * task);

MPP_RET rc_model_v2_hal_start(void *ctx, EncRcTask * task);
MPP_RET rc_model_v2_hal_end(void *ctx, EncRcTask * task);

void rc_model_v2_proc_show(void *seq_file, void *ctx,
			   RK_S32 chl_id);

#ifdef  __cplusplus
}
#endif
#endif				/* __RC_MODEL_V2_H__ */
