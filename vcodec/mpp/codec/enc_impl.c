// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define  MODULE_TAG "enc_impl"

#include <linux/string.h>

#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_maths.h"

#include "h264e_api_v2.h"
#include "jpege_api_v2.h"
#include "h265e_api.h"
//#include "vp8e_api_v2.h"
#include "enc_impl.h"

/*
 * all encoder controller static register here
 */
static const EncImplApi *enc_apis[] = {
#ifdef HAVE_H264E
	&api_h264e,
#endif
#ifdef HAVE_H265E
	&api_h265e,
#endif
#ifdef HAVE_JPEGE
	&api_jpege,
#endif
#ifdef HAVE_VP8E
	&api_vp8e,
#endif
};

typedef struct EncImplCtx_t {
	EncImplCfg cfg;
	const EncImplApi *api;
	void *ctx;
} EncImplCtx;

MPP_RET enc_impl_init(EncImpl * impl, EncImplCfg * cfg)
{
	RK_U32 i;
	const EncImplApi **apis = enc_apis;
	RK_U32 api_cnt = MPP_ARRAY_ELEMS(enc_apis);

	if (NULL == impl || NULL == cfg) {
		mpp_err_f("found NULL input controller %p config %p\n", impl,
			  cfg);
		return MPP_ERR_NULL_PTR;
	}

	*impl = NULL;

	for (i = 0; i < api_cnt; i++) {
		const EncImplApi *api = apis[i];

		if (cfg->coding == api->coding) {
			EncImplCtx *p = mpp_calloc(EncImplCtx, 1);
			void *ctx = mpp_calloc_size(void, api->ctx_size);
			MPP_RET ret = MPP_OK;

			if (NULL == ctx || NULL == p) {
				mpp_err_f("failed to alloc encoder context\n");
				mpp_free(p);
				mpp_free(ctx);
				return MPP_ERR_MALLOC;
			}

			ret = api->init(ctx, cfg);
			if (MPP_OK != ret) {
				mpp_err_f("failed to init controller\n");
				mpp_free(p);
				mpp_free(ctx);
				return ret;
			}

			p->api = api;
			p->ctx = ctx;
			memcpy(&p->cfg, cfg, sizeof(p->cfg));
			*impl = p;
			return MPP_OK;
		}
	}

	return MPP_NOK;
}

MPP_RET enc_impl_deinit(EncImpl impl)
{
	EncImplCtx *p = NULL;

	if (NULL == impl) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;
	if (p->api->deinit)
		p->api->deinit(p->ctx);

	mpp_free(p->ctx);
	mpp_free(p);
	return MPP_OK;
}

MPP_RET enc_impl_proc_cfg(EncImpl impl, MpiCmd cmd, void *para)
{
	EncImplCtx *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == impl) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;
	if (p->api->proc_cfg)
		ret = p->api->proc_cfg(p->ctx, cmd, para);

	return ret;
}

MPP_RET enc_impl_gen_hdr(EncImpl impl, MppPacket pkt)
{
	EncImplCtx *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == impl) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;
	if (p->api->gen_hdr)
		ret = p->api->gen_hdr(p->ctx, pkt);

	return ret;
}

MPP_RET enc_impl_start(EncImpl impl, HalEncTask * task)
{
	EncImplCtx *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == impl) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;
	if (p->api->start)
		ret = p->api->start(p->ctx, task);

	return ret;
}

MPP_RET enc_impl_proc_dpb(EncImpl impl, HalEncTask * task)
{
	EncImplCtx *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == impl) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;
	if (p->api->proc_dpb)
		ret = p->api->proc_dpb(p->ctx, task);

	return ret;
}

MPP_RET enc_impl_proc_hal(EncImpl impl, HalEncTask * task)
{
	EncImplCtx *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == impl || NULL == task) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;
	if (p->api->proc_hal)
		ret = p->api->proc_hal(p->ctx, task);

	return ret;
}

MPP_RET enc_impl_add_prefix(EncImpl impl, MppPacket pkt, RK_S32 * length,
			    RK_U8 uuid[16], const void *data, RK_S32 size)
{
	EncImplCtx *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == pkt || NULL == data) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;

	if (NULL == p->api->add_prefix)
		return ret;

	if (p->api->add_prefix)
		ret = p->api->add_prefix(pkt, length, uuid, data, size);

	return ret;
}

MPP_RET enc_impl_sw_enc(EncImpl impl, HalEncTask * task)
{
	EncImplCtx *p = NULL;
	MPP_RET ret = MPP_OK;

	if (NULL == impl || NULL == task) {
		mpp_err_f("found NULL input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (EncImplCtx *) impl;
	if (p->api->sw_enc)
		ret = p->api->sw_enc(p->ctx, task);

	return ret;
}

void enc_impl_proc_debug(void *seq_file, EncImpl impl, RK_S32 chl_id)
{
	EncImplCtx *p = NULL;

	if (NULL == impl || NULL == seq_file) {
		mpp_err_f("found NULL input\n");
		return ;
	}

	p = (EncImplCtx *) impl;
	if (p->api->proc_debug)
		p->api->proc_debug(seq_file, p->ctx, chl_id);

	return;
}

