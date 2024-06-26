// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_device"

#include <linux/string.h>
#include <linux/errno.h>

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_buffer.h"
//#include "mpp_device_debug.h"
#include "mpp_service_api.h"
#include "rk_export_func.h"
#include "mpp_service.h"

typedef struct MppDevImpl_t {
	MppClientType type;

	void *ctx;
	const MppDevApi *api;
} MppDevImpl;

RK_U32 mpp_device_debug = 0;

MPP_RET mpp_dev_init(MppDev * ctx, MppClientType type)
{
	const MppDevApi *api = NULL;
	MppDevImpl *impl = NULL;
	void *impl_ctx = NULL;
	if (NULL == ctx) {
		mpp_err_f("found NULL input ctx\n");
		return MPP_ERR_NULL_PTR;
	}
	// mpp_env_get_u32("mpp_device_debug", &mpp_device_debug, 0);
	*ctx = NULL;
	api = &mpp_service_api;
	impl = mpp_calloc(MppDevImpl, 1);
	impl_ctx = mpp_calloc_size(void, api->ctx_size);
	if (NULL == impl || NULL == impl_ctx) {
		mpp_err_f("malloc failed impl %p impl_ctx %p\n", impl,
			  impl_ctx);
		MPP_FREE(impl);
		MPP_FREE(impl_ctx);
		return MPP_ERR_MALLOC;
	}

	impl->ctx = impl_ctx;
	impl->api = api;
	impl->type = type;
	*ctx = impl;

	return api->init(impl_ctx, type);
}

MPP_RET mpp_dev_deinit(MppDev ctx)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == p) {
		mpp_err_f("found NULL input ctx\n");
		return MPP_ERR_NULL_PTR;
	}

	if (p->api && p->api->deinit && p->ctx)
		ret = p->api->deinit(p->ctx);

	MPP_FREE(p->ctx);
	MPP_FREE(p);

	return ret;
}

MPP_RET mpp_dev_ioctl(MppDev ctx, RK_S32 cmd, void *param)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;
	MPP_RET ret = MPP_OK;

	if (NULL == p || NULL == api)
		return ret;

	switch (cmd) {
	case MPP_DEV_REG_WR: {
		if (api->reg_wr)
			ret = api->reg_wr(impl_ctx, param);
	}
	break;
	case MPP_DEV_REG_RD: {
		if (api->reg_rd)
			ret = api->reg_rd(impl_ctx, param);
	}
	break;
	case MPP_DEV_REG_OFFSET: {
		if (api->reg_offset)
			ret = api->reg_offset(impl_ctx, param);
	}
	break;
	case MPP_DEV_RCB_INFO: {
		if (api->rcb_info)
			ret = api->rcb_info(impl_ctx, param);
	}
	break;
	case MPP_DEV_SET_INFO: {
		if (api->set_info)
			ret = api->set_info(impl_ctx, param);
	}
	break;
	case MPP_DEV_CMD_SEND: {
		if (api->cmd_send)
			ret = api->cmd_send(impl_ctx);
	}
	break;
	case MPP_DEV_CMD_POLL: {
		if (api->cmd_poll)
			ret = api->cmd_poll(impl_ctx);
	}
	break;
	case MPP_DEV_CMD_RUN_TASK: {
		if (api->run_task)
			ret = api->run_task(impl_ctx);
	}
	break;
	default: {
		mpp_err_f("invalid cmd %d\n", cmd);
	}
	break;
	}

	return ret;
}

MPP_RET mpp_dev_set_reg_offset(MppDev dev, RK_S32 index, RK_U32 offset)
{
	MppDevRegOffsetCfg trans_cfg;

	trans_cfg.reg_idx = index;
	trans_cfg.offset = offset;

	mpp_dev_ioctl(dev, MPP_DEV_REG_OFFSET, &trans_cfg);

	return MPP_OK;
}

RK_U32 mpp_dev_get_iova_address(MppDev ctx, MppBuffer mpp_buf, RK_U32 reg_idx)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;
	struct dma_buf *dma_buf = NULL;
	RK_S32 phy_addr = 0;
	if (!mpp_buf) {
		mpp_err_f("input NULL");
		return -EINVAL;
	}

	phy_addr = mpp_buffer_get_phy(mpp_buf);
	if (phy_addr == -1) {
		dma_buf = mpp_buffer_get_dma(mpp_buf);
		mpp_assert(dma_buf);
		if (api->get_address)
			phy_addr = api->get_address(impl_ctx, dma_buf, reg_idx);

		if (phy_addr != -1)
			mpp_buffer_set_phy(mpp_buf, phy_addr);
	}
	return (RK_U32)phy_addr;
}

RK_U32 mpp_dev_get_iova_address2(MppDev ctx, struct dma_buf *dma_buf, RK_U32 reg_idx)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;
	mpp_assert(dma_buf);
	if (api->get_address)
		return api->get_address(impl_ctx, dma_buf, reg_idx);
	return -EINVAL;
}

RK_U32 mpp_dev_get_mpi_ioaddress(MppDev ctx, MpiBuf mpi_buf, RK_U32 offset)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;
	struct dma_buf *dma_buf = NULL;
	RK_S32 phy_addr = -1;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();

	if (!mpi_buf) {
		mpp_err_f("input NULL");
		return -EINVAL;
	}

	if (mpibuf_fn && mpibuf_fn->buf_get_paddr)
		phy_addr = mpibuf_fn->buf_get_paddr(mpi_buf);
	if (phy_addr == -1) {
		dma_buf = mpi_buf_get_dma(mpi_buf);
		mpp_assert(dma_buf);
		if (api->get_address)
			return api->get_address(impl_ctx, dma_buf, offset);
	}

	return (RK_U32)phy_addr;
}

void mpp_dev_chnl_register(MppDev ctx, void *func, RK_S32 chan_id)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;

	if (api->chnl_register)
		api->chnl_register(impl_ctx, func, chan_id);
	return;

}

struct device * mpp_get_dev(MppDev ctx)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;
	if (api->chnl_get_dev)
		return api->chnl_get_dev(impl_ctx);
	return NULL;
}

RK_S32 mpp_dev_chnl_check_running(MppDev ctx)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;

	if (api->chnl_check_running)
		return api->chnl_check_running(impl_ctx);

	return 0;
}

RK_S32 mpp_dev_chnl_unbind_jpeg_task(MppDev ctx)
{
	MppDevImpl *p = (MppDevImpl *) ctx;
	const MppDevApi *api = p->api;
	void *impl_ctx = p->ctx;

	if (api->control)
		return api->control(impl_ctx, MPP_CMD_UNBIND_JPEG_TASK, NULL);

	return 0;
}

