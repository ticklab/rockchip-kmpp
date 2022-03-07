// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#define MODULE_TAG "hal_jpege_api_v2"

#include <linux/string.h>
#include "mpp_mem.h"
//#include "mpp_platform.h"

#include "mpp_enc_hal.h"
#include "vepu5xx.h"
#include "hal_jpege_debug.h"
#include "hal_jpege_api_v2.h"
#include "hal_jpege_vepu540c.h"

typedef struct HaljpegeCtx_t {
	const MppEncHalApi *api;
	void *hw_ctx;
} HaljpegeCtx;

RK_U32 hal_jpege_debug = 0;

static MPP_RET hal_jpege_init(void *hal, MppEncHalCfg * cfg)
{
	HaljpegeCtx *ctx = (HaljpegeCtx *) hal;
	const MppEncHalApi *api = NULL;
	void *hw_ctx = NULL;
	MPP_RET ret = MPP_OK;
	//   RK_U32 vcodec_type = mpp_get_vcodec_type();

	api = &hal_jpege_vepu540c;
	// mpp_env_get_u32("hal_jpege_debug", &hal_jpege_debug, 0);

	/*  if (vcodec_type & HAVE_RKVENC) {
	   RK_U32 hw_id = mpp_get_client_hw_id(VPU_CLIENT_RKVENC);
	   switch (hw_id) {
	   case HWID_VEPU540C : {
	   api = &hal_jpege_vepu540c;
	   } break;
	   default : {
	   mpp_err("vcodec type %08x can not find JPEG encoder device\n",
	   vcodec_type);
	   ret = MPP_NOK;
	   } break;
	   }
	   }else {
	   mpp_err("vcodec type %08x can not find JPEG encoder device\n",
	   vcodec_type);
	   ret = MPP_NOK;
	   } */

	mpp_assert(api);

	if (!ret)
		hw_ctx = mpp_calloc_size(void, api->ctx_size);

	ctx->api = api;
	ctx->hw_ctx = hw_ctx;

	if (ret)
		return ret;

	ret = api->init(hw_ctx, cfg);
	return ret;
}

static MPP_RET hal_jpege_deinit(void *hal)
{
	HaljpegeCtx *ctx = (HaljpegeCtx *) hal;
	const MppEncHalApi *api = ctx->api;
	void *hw_ctx = ctx->hw_ctx;
	MPP_RET ret = MPP_OK;

	if (!hw_ctx || !api || !api->deinit)
		return MPP_OK;

	ret = api->deinit(hw_ctx);
	MPP_FREE(hw_ctx);
	return ret;
}

#define HAL_JPEGE_FUNC(func) \
    static MPP_RET hal_jpege_##func(void *hal)                      \
    {                                                               \
        HaljpegeCtx *ctx = (HaljpegeCtx *)hal;                      \
        const MppEncHalApi *api = ctx->api;                         \
        void *hw_ctx = ctx->hw_ctx;                                 \
                                                                    \
        if (!hw_ctx || !api || !api->func)                          \
            return MPP_OK;                                          \
                                                                    \
        return api->func(hw_ctx);                                   \
    }

#define HAL_JPEGE_TASK_FUNC(func) \
    static MPP_RET hal_jpege_##func(void *hal, HalEncTask *task)    \
    {                                                               \
        HaljpegeCtx *ctx = (HaljpegeCtx *)hal;                      \
        const MppEncHalApi *api = ctx->api;                         \
        void *hw_ctx = ctx->hw_ctx;                                 \
                                                                    \
        if (!hw_ctx || !api || !api->func)                          \
            return MPP_OK;                                          \
                                                                    \
        return api->func(hw_ctx, task);                             \
    }

HAL_JPEGE_FUNC(prepare)
    HAL_JPEGE_TASK_FUNC(get_task)
    HAL_JPEGE_TASK_FUNC(gen_regs)
    HAL_JPEGE_TASK_FUNC(start)
    HAL_JPEGE_TASK_FUNC(wait)
    HAL_JPEGE_TASK_FUNC(part_start)
    HAL_JPEGE_TASK_FUNC(part_wait)
    HAL_JPEGE_TASK_FUNC(ret_task)

const MppEncHalApi hal_api_jpege_v2 = {
	.name = "hal_jpege",
	.coding = MPP_VIDEO_CodingMJPEG,
	.ctx_size = sizeof(HaljpegeCtx),
	.flag = 0,
	.init = hal_jpege_init,
	.deinit = hal_jpege_deinit,
	.prepare = hal_jpege_prepare,
	.get_task = hal_jpege_get_task,
	.gen_regs = hal_jpege_gen_regs,
	.start = hal_jpege_start,
	.wait = hal_jpege_wait,
	.part_start = hal_jpege_part_start,
	.part_wait = hal_jpege_part_wait,
	.ret_task = hal_jpege_ret_task,
};
