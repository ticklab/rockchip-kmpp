// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * author: timkingh.huang@rock-chips.com
 *
 */

#include <linux/string.h>

#include "vepu_pp_common.h"

#define PP_MAX_REQ_NUM  (16)

struct dma_buf;

struct vcodec_mppdev_svr_fn {
	struct mpp_session *(*chnl_open)(int client_type);
	int (*chnl_register)(struct mpp_session *session, void *fun, unsigned int chn_id);
	int (*chnl_release)(struct mpp_session *session);
	int (*chnl_add_req)(struct mpp_session *session,  void *reqs);
	unsigned int (*chnl_get_iova_addr)(struct mpp_session *session,  struct dma_buf *buf,
					   unsigned int offset);
	void (*chnl_release_iova_addr)(struct mpp_session *session,  struct dma_buf *buf);
};

struct mpp_req_t {
	u32 cmd;
	u32 flag;
	u32 size;
	u32 offset;
	u64 data_ptr;
};

struct vepu_pp_dev_srv {
	int client_type;

	struct mpp_session *chnl;

	int req_cnt;
	int reg_offset_count;
	struct mpp_req_t reqs[PP_MAX_REQ_NUM];

	struct vcodec_mppdev_svr_fn *mppdev_ops;
};

extern struct vcodec_mppdev_svr_fn *get_mppdev_svr_ops(void);

enum PP_RET pp_service_init(void *ctx, int type)
{
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;
	enum PP_RET ret = VEPU_PP_OK;

	p->mppdev_ops = get_mppdev_svr_ops();
	if (!p->mppdev_ops) {
		pr_err("get_mppdev_svr_ops fail");
		return VEPU_PP_NOK;
	}
	if (p->mppdev_ops->chnl_open)
		p->chnl = p->mppdev_ops->chnl_open(type);

	if (!p->chnl) {
		ret = VEPU_PP_NOK;
		pr_err("mpp_chnl_open fail");
	}
	return ret;
}

enum PP_RET pp_service_deinit(void *ctx)
{
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;
	if (p->chnl && p->mppdev_ops->chnl_release)
		p->mppdev_ops->chnl_release(p->chnl);

	return VEPU_PP_OK;
}

enum PP_RET pp_service_reg_wr(void *ctx, struct dev_reg_wr_t *cfg)
{
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;
	struct mpp_req_t *mpp_req = &p->reqs[p->req_cnt];

	if (!p->req_cnt)
		memset(p->reqs, 0, sizeof(p->reqs));

	mpp_req->cmd = MPP_CMD_SET_REG_WRITE;
	mpp_req->flag = 0;
	mpp_req->size = cfg->size;
	mpp_req->offset = cfg->offset;
	mpp_req->data_ptr = REQ_DATA_PTR(cfg->data_ptr);
	p->req_cnt++;

	return VEPU_PP_OK;
}

enum PP_RET pp_service_reg_rd(void *ctx, struct dev_reg_rd_t *cfg)
{
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;
	struct mpp_req_t *mpp_req = &p->reqs[p->req_cnt];

	if (!p->req_cnt)
		memset(p->reqs, 0, sizeof(p->reqs));

	mpp_req->cmd = MPP_CMD_SET_REG_READ;
	mpp_req->flag = 0;
	mpp_req->size = cfg->size;
	mpp_req->offset = cfg->offset;
	mpp_req->data_ptr = REQ_DATA_PTR(cfg->data_ptr);
	p->req_cnt++;

	return VEPU_PP_OK;
}

enum PP_RET pp_service_cmd_send(void *ctx)
{
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;
	enum PP_RET ret = VEPU_PP_OK;

	if (p->req_cnt <= 0 || p->req_cnt > PP_MAX_REQ_NUM) {
		pp_err("ctx %p invalid request count %d\n", ctx, p->req_cnt);
		return VEPU_PP_NOK;
	}

	/* setup flag for multi message request */
	if (p->req_cnt > 1) {
		int i;

		for (i = 0; i < p->req_cnt; i++)
			p->reqs[i].flag |= MPP_FLAGS_MULTI_MSG;
	}
	p->reqs[p->req_cnt - 1].flag |= MPP_FLAGS_LAST_MSG;
	p->reqs[p->req_cnt - 1].flag |= MPP_FLAGS_REG_FD_NO_TRANS;
	if (p->mppdev_ops->chnl_add_req)
		ret = p->mppdev_ops->chnl_add_req(p->chnl, &p->reqs[0]);
	p->req_cnt = 0;
	p->reg_offset_count = 0;
	return ret;
}

enum PP_RET pp_service_cmd_poll(void *ctx)
{
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;
	struct mpp_req_t dev_req;
	enum PP_RET ret = 0;
	memset(&dev_req, 0, sizeof(dev_req));
	dev_req.cmd = MPP_CMD_POLL_HW_FINISH;
	dev_req.flag |= MPP_FLAGS_LAST_MSG;
	if (p->mppdev_ops->chnl_add_req)
		p->mppdev_ops->chnl_add_req(p->chnl, &dev_req);
	return ret;
}

u32 pp_service_iova_address(void *ctx, struct dma_buf * buf, u32 offset)
{
	u32 iova_address = 0;
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;

	if (p->mppdev_ops->chnl_get_iova_addr)
		iova_address =
			p->mppdev_ops->chnl_get_iova_addr(p->chnl, buf, offset);
	return iova_address;
}

void pp_service_iova_release(void *ctx, struct dma_buf *buf)
{
	struct vepu_pp_dev_srv *p = (struct vepu_pp_dev_srv *) ctx;

	if (p->mppdev_ops->chnl_release_iova_addr)
		p->mppdev_ops->chnl_release_iova_addr(p->chnl, buf);
}

const struct pp_srv_api_t pp_srv_api = {
	"vepu_pp",
	sizeof(struct vepu_pp_dev_srv),
	pp_service_init,
	pp_service_deinit,
	pp_service_reg_wr,
	pp_service_reg_rd,
	pp_service_cmd_send,
	pp_service_cmd_poll,
	pp_service_iova_address,
	pp_service_iova_release
};
