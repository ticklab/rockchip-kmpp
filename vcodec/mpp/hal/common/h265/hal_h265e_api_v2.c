// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "hal_h265e_api_v2"

#include <linux/string.h>
#include <linux/module.h>

#include "mpp_mem.h"
#include "mpp_log.h"
//#include "mpp_platform.h"
#include "mpp_enc_hal.h"
#include "hal_h265e_api_v2.h"
#ifdef RKVEC540_HEVC
#include "hal_h265e_vepu541.h"
#endif

#ifdef RKVEC580_HEVC
#include "hal_h265e_vepu580.h"
#endif

#ifdef RKVEC540C_HEVC
#include "hal_h265e_vepu540c.h"
#endif

#include "hal_h265e_debug.h"

RK_U32 hal_h265e_debug;
module_param(hal_h265e_debug, uint, 0644);
MODULE_PARM_DESC(hal_h265e_debug, "bits for hal_h265e debug information");

typedef struct HalH265eV2Ctx_t {
	const MppEncHalApi *api;
	void *hw_ctx;
} HalH265eV2Ctx;

static MPP_RET hal_h265ev2_init(void *hal, MppEncHalCfg * cfg)
{
	HalH265eV2Ctx *ctx = (HalH265eV2Ctx *) hal;
	const MppEncHalApi *api = NULL;
	void *hw_ctx = NULL;
	MPP_RET ret = MPP_OK;

#ifdef RKVEC580_HEVC
	api = &hal_h265e_vepu580;
#endif

#ifdef RKVEC540C_HEVC
	api = &hal_h265e_vepu540c;
#endif

	mpp_assert(api);

	hw_ctx = mpp_calloc_size(void, api->ctx_size);
	if (!hw_ctx)
		return MPP_ERR_MALLOC;

	ctx->api = api;
	ctx->hw_ctx = hw_ctx;

	ret = api->init(hw_ctx, cfg);

	return ret;
}

static MPP_RET hal_h265ev2_deinit(void *hal)
{
	HalH265eV2Ctx *ctx = (HalH265eV2Ctx *) hal;
	const MppEncHalApi *api = ctx->api;
	void *hw_ctx = ctx->hw_ctx;
	MPP_RET ret = MPP_OK;

	if (!hw_ctx || !api || !api->deinit)
		return MPP_OK;

	ret = api->deinit(hw_ctx);
	MPP_FREE(hw_ctx);

	return ret;
}

#define HAL_H265E_FUNC(func) \
    static MPP_RET hal_h265ev2_##func(void *hal)                    \
    {                                                               \
        HalH265eV2Ctx *ctx = (HalH265eV2Ctx *)hal;                  \
        const MppEncHalApi *api = ctx->api;                         \
        void *hw_ctx = ctx->hw_ctx;                                 \
                                                                    \
        if (!hw_ctx || !api || !api->func)                          \
            return MPP_OK;                                          \
                                                                    \
        return api->func(hw_ctx);                                   \
    }

#define HAL_H265E_TASK_FUNC(func) \
    static MPP_RET hal_h265ev2_##func(void *hal, HalEncTask *task)  \
    {                                                               \
        HalH265eV2Ctx *ctx = (HalH265eV2Ctx *)hal;                  \
        const MppEncHalApi *api = ctx->api;                         \
        void *hw_ctx = ctx->hw_ctx;                                 \
                                                                    \
        if (!hw_ctx || !api || !api->func)                          \
            return MPP_OK;                                          \
                                                                    \
        return api->func(hw_ctx, task);                             \
    }

static MPP_RET hal_h265ev2_comb_start(void *hal,
				      HalEncTask * task, HalEncTask * jpeg_task)
{
	HalH265eV2Ctx *ctx = (HalH265eV2Ctx *) hal;
	const MppEncHalApi *api = ctx->api;
	void *hw_ctx = ctx->hw_ctx;

	if (!hw_ctx || !api)
		return MPP_OK;

	if (!api->comb_start)
		return api->start(hw_ctx, task);

	return api->comb_start(hw_ctx, task, jpeg_task);
}

static MPP_RET hal_h265ev2_comb_ret_task(void *hal,
					 HalEncTask * task,
					 HalEncTask * jpeg_task)
{
	HalH265eV2Ctx *ctx = (HalH265eV2Ctx *) hal;
	const MppEncHalApi *api = ctx->api;
	void *hw_ctx = ctx->hw_ctx;

	if (!hw_ctx || !api)
		return MPP_OK;

	if (!api->comb_ret_task)
		return api->ret_task(hw_ctx, task);

	return api->comb_ret_task(hw_ctx, task, jpeg_task);
}

HAL_H265E_FUNC(prepare)
HAL_H265E_TASK_FUNC(get_task)
HAL_H265E_TASK_FUNC(gen_regs)
HAL_H265E_TASK_FUNC(start)
HAL_H265E_TASK_FUNC(wait)
HAL_H265E_TASK_FUNC(part_start)
HAL_H265E_TASK_FUNC(part_wait)
HAL_H265E_TASK_FUNC(ret_task)

const MppEncHalApi hal_api_h265e_v2 = {
	.name = "hal_h265e",
	.coding = MPP_VIDEO_CodingHEVC,
	.ctx_size = sizeof(HalH265eV2Ctx),
	.flag = 0,
	.init = hal_h265ev2_init,
	.deinit = hal_h265ev2_deinit,
	.prepare = hal_h265ev2_prepare,
	.get_task = hal_h265ev2_get_task,
	.gen_regs = hal_h265ev2_gen_regs,
	.start = hal_h265ev2_start,
	.wait = hal_h265ev2_wait,
	.part_start = hal_h265ev2_part_start,
	.part_wait = hal_h265ev2_part_wait,
	.ret_task = hal_h265ev2_ret_task,
	.comb_start = hal_h265ev2_comb_start,
	.comb_ret_task = hal_h265ev2_comb_ret_task,
};
