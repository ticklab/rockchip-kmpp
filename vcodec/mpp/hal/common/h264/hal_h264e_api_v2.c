// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#define MODULE_TAG "hal_h264e_api_v2"

#include <linux/string.h>
#include <linux/module.h>

#include "mpp_mem.h"
//#include "mpp_platform.h"

#include "mpp_enc_hal.h"
#include "hal_h264e_debug.h"
#include "h264e_syntax.h"
#include "hal_h264e_api_v2.h"

#ifdef RKVEC580_H264
#include "hal_h264e_vepu580.h"
#endif

#ifdef RKVEC540C_H264
#include "hal_h264e_vepu540c.h"
#endif



typedef struct HalH264eCtx_t {
	const MppEncHalApi *api;
	void *hw_ctx;
} HalH264eCtx;

RK_U32 hal_h264e_debug = 0;
module_param(hal_h264e_debug, uint, 0644);
MODULE_PARM_DESC(hal_h264e_debug, "bits for hal_h264e debug information");

static MPP_RET hal_h264e_init(void *hal, MppEncHalCfg * cfg)
{
	HalH264eCtx *ctx = (HalH264eCtx *) hal;
	const MppEncHalApi *api = NULL;
	void *hw_ctx = NULL;

//      mpp_env_get_u32("hal_h264e_debug", &hal_h264e_debug, 0);

#ifdef RKVEC580_H264
	api = &hal_h264e_vepu580;
#endif

#ifdef RKVEC540C_H264
	api = &hal_h264e_vepu540c;
#endif

	mpp_assert(api);

	hw_ctx = mpp_calloc_size(void, api->ctx_size);
	if (!hw_ctx)
		return MPP_ERR_MALLOC;

	ctx->api = api;
	ctx->hw_ctx = hw_ctx;

	return api->init(hw_ctx, cfg);
}

static MPP_RET hal_h264e_deinit(void *hal)
{
	HalH264eCtx *ctx = (HalH264eCtx *) hal;
	const MppEncHalApi *api = ctx->api;
	void *hw_ctx = ctx->hw_ctx;
	MPP_RET ret = MPP_OK;

	if (!hw_ctx || !api || !api->deinit)
		return MPP_OK;

	ret = api->deinit(hw_ctx);
	MPP_FREE(hw_ctx);

	return ret;
}

#define HAL_H264E_FUNC(func) \
    static MPP_RET hal_h264e_##func(void *hal)                      \
    {                                                               \
        HalH264eCtx *ctx = (HalH264eCtx *)hal;                      \
        const MppEncHalApi *api = ctx->api;                         \
        void *hw_ctx = ctx->hw_ctx;                                 \
                                                                    \
        if (!hw_ctx || !api || !api->func)                          \
            return MPP_OK;                                          \
                                                                    \
        return api->func(hw_ctx);                                   \
    }

#define HAL_H264E_TASK_FUNC(func) \
    static MPP_RET hal_h264e_##func(void *hal, HalEncTask *task)    \
    {                                                               \
        HalH264eCtx *ctx = (HalH264eCtx *)hal;                      \
        const MppEncHalApi *api = ctx->api;                         \
        void *hw_ctx = ctx->hw_ctx;                                 \
                                                                    \
        if (!hw_ctx || !api || !api->func)                          \
            return MPP_OK;                                          \
                                                                    \
        return api->func(hw_ctx, task);                             \
    }

static MPP_RET hal_h264e_comb_start(void *hal,
				    HalEncTask * task, HalEncTask * jpeg_task)
{
	HalH264eCtx *ctx = (HalH264eCtx *) hal;
	const MppEncHalApi *api = ctx->api;
	void *hw_ctx = ctx->hw_ctx;

	if (!hw_ctx || !api)
		return MPP_OK;

	if (!api->comb_start)
		return api->start(hw_ctx, task);

	return api->comb_start(hw_ctx, task, jpeg_task);
}

static MPP_RET hal_h264e_comb_ret_task(void *hal,
				       HalEncTask * task,
				       HalEncTask * jpeg_task)
{
	HalH264eCtx *ctx = (HalH264eCtx *) hal;
	const MppEncHalApi *api = ctx->api;
	void *hw_ctx = ctx->hw_ctx;

	if (!hw_ctx || !api)
		return MPP_OK;

	if (!api->comb_ret_task)
		return api->ret_task(hw_ctx, task);

	return api->comb_ret_task(hw_ctx, task, jpeg_task);
}

HAL_H264E_FUNC(prepare)
HAL_H264E_TASK_FUNC(get_task)
HAL_H264E_TASK_FUNC(gen_regs)
HAL_H264E_TASK_FUNC(start)
HAL_H264E_TASK_FUNC(wait)
HAL_H264E_TASK_FUNC(part_start)
HAL_H264E_TASK_FUNC(part_wait)
HAL_H264E_TASK_FUNC(ret_task)

const MppEncHalApi hal_api_h264e_v2 = {
	.name = "hal_h264e",
	.coding = MPP_VIDEO_CodingAVC,
	.ctx_size = sizeof(HalH264eCtx),
	.flag = 0,
	.init = hal_h264e_init,
	.deinit = hal_h264e_deinit,
	.prepare = hal_h264e_prepare,
	.get_task = hal_h264e_get_task,
	.gen_regs = hal_h264e_gen_regs,
	.start = hal_h264e_start,
	.wait = hal_h264e_wait,
	.part_start = hal_h264e_part_start,
	.part_wait = hal_h264e_part_wait,
	.ret_task = hal_h264e_ret_task,
	.comb_start = hal_h264e_comb_start,
	.comb_ret_task = hal_h264e_comb_ret_task,
};
