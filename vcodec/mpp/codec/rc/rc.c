// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#define MODULE_TAG "rc"

#include <linux/module.h>

#include "mpp_mem.h"
#include "mpp_maths.h"

#include "rc_debug.h"
#include "rc.h"
#include "rc_base.h"
#include "mpp_log.h"
#include "h264e_rc.h"
#include "h265e_rc.h"
#include "jpege_rc.h"
#include "vp8e_rc.h"

const RcImplApi *rc_api_ops[] = {
	&default_h264e,
	&default_h265e,
	&default_jpege,
	&default_vp8e,
};

typedef struct MppRcImpl_t {
	void *ctx;
	const RcImplApi *api;
	RcCfg cfg;

	RcFpsCfg fps;
	RK_S32 frm_cnt;

	RK_U32 frm_send;
	RK_U32 frm_done;
} MppRcImpl;

RK_U32 rc_debug = 0;
module_param(rc_debug, uint, 0644);
MODULE_PARM_DESC(rc_debug, "bits rc debug information");

const static char default_rc_api[] = "default";

MPP_RET rc_init(RcCtx * ctx, MppCodingType type, const char **request_name)
{
	MPP_RET ret = MPP_NOK;
	MppRcImpl *p = NULL;
	const char *name = NULL;
	RcImplApi *api = NULL;
	RK_U32 i = 0;
	// mpp_env_get_u32("rc_debug", &rc_debug, 0);

	if (NULL == request_name || NULL == *request_name)
		name = default_rc_api;
	else
		name = *request_name;

	rc_dbg_func("enter type %x name %s\n", type, name);

	for (i = 0; i < MPP_ARRAY_ELEMS(rc_api_ops); i++) {
		if (rc_api_ops[i]->type == type)
			api = (RcImplApi *) rc_api_ops[i];
	}

	mpp_assert(api);

	if (api) {
		void *rc_ctx = mpp_calloc_size(void, api->ctx_size);
		p = mpp_calloc(MppRcImpl, 1);

		if (NULL == p || NULL == rc_ctx) {
			mpp_err_f("failed to create context size %d\n",
				  api->ctx_size);
			MPP_FREE(p);
			MPP_FREE(rc_ctx);
			ret = MPP_ERR_MALLOC;
		} else {
			p->ctx = rc_ctx;
			p->api = api;
			p->frm_cnt = -1;
			if (request_name && *request_name)
				mpp_log("using rc impl %s\n", api->name);
			ret = MPP_OK;
		}
	}

	*ctx = p;
	if (request_name)
		*request_name = name;

	rc_dbg_func("leave %p\n", p);

	return ret;
}

MPP_RET rc_deinit(RcCtx ctx)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;
	MPP_RET ret = MPP_OK;

	rc_dbg_func("enter %p\n", ctx);

	if (api && api->deinit && p->ctx) {
		ret = api->deinit(p->ctx);
		MPP_FREE(p->ctx);
	}

	MPP_FREE(p);

	rc_dbg_func("leave %p\n", ctx);

	return ret;
}

MPP_RET rc_update_usr_cfg(RcCtx ctx, RcCfg * cfg)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;
	MPP_RET ret = MPP_OK;

	rc_dbg_func("enter %p\n", ctx);

	p->cfg = *cfg;
	p->fps = cfg->fps;

	if (api && api->init && p->ctx)
		api->init(p->ctx, &p->cfg);

	rc_dbg_func("leave %p\n", ctx);

	return ret;
}

MPP_RET rc_frm_check_drop(RcCtx ctx, EncRcTask * task)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;
	MPP_RET ret = MPP_OK;

	rc_dbg_func("enter %p\n", ctx);

	if (api && api->check_drop && p->ctx && task) {
		ret = api->check_drop(p->ctx, task);
		return ret;
	} else {
		RcFpsCfg *cfg = &p->fps;
		RK_S32 frm_cnt = p->frm_cnt;
		RK_S32 rate_in = cfg->fps_in_num * cfg->fps_out_denorm;
		RK_S32 rate_out = cfg->fps_out_num * cfg->fps_in_denorm;
		RK_S32 drop = 0;

		mpp_assert(cfg->fps_in_denorm >= 1);
		mpp_assert(cfg->fps_out_denorm >= 1);
		mpp_assert(rate_in >= rate_out);

		// frame counter is inited to (rate_in - rate_out)  to encode first frame
		if (frm_cnt < 0)
			frm_cnt = rate_in - rate_out;

		frm_cnt += rate_out;

		if (frm_cnt < rate_in)
			drop = 1;
		else
			frm_cnt -= rate_in;

		p->frm_cnt = frm_cnt;
		task->frm.drop = drop;
	}

	rc_dbg_func("leave %p drop %d\n", ctx, task->frm.drop);

	return ret;
}

MPP_RET rc_frm_check_reenc(RcCtx ctx, EncRcTask * task)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;

	if (!api || !api->check_reenc || !p->ctx || !task)
		return MPP_OK;

	return api->check_reenc(p->ctx, task);
}

MPP_RET rc_frm_start(RcCtx ctx, EncRcTask * task)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;

	if (!api || !api->frm_start || !p->ctx || !task)
		return MPP_OK;

	return api->frm_start(p->ctx, task);
}

MPP_RET rc_frm_end(RcCtx ctx, EncRcTask * task)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;

	if (!api || !api->frm_end || !p->ctx || !task)
		return MPP_OK;

	return api->frm_end(p->ctx, task);
}

MPP_RET rc_hal_start(RcCtx ctx, EncRcTask * task)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;

	if (!api || !api->hal_start || !p->ctx || !task)
		return MPP_OK;

	return api->hal_start(p->ctx, task);
}

MPP_RET rc_hal_end(RcCtx ctx, EncRcTask * task)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;

	if (!api || !api->hal_end || !p->ctx || !task)
		return MPP_OK;

	return api->hal_end(p->ctx, task);
}

MPP_RET rc_proc_show(void *seq_file, RcCtx ctx, RK_S32 chl_id)
{
	MppRcImpl *p = (MppRcImpl *) ctx;
	const RcImplApi *api = p->api;

	if (!api || !api->proc_show || !p->ctx )
		return MPP_OK;

	api->proc_show(seq_file, p->ctx, chl_id);
	return 0;
}

