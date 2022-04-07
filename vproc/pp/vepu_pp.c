// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * author: timkingh.huang@rock-chips.com
 *
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "vepu_pp.h"
#include "vepu_pp_api.h"
#include "vepu_pp_service_api.h"


static struct vepu_pp_ctx_t g_pp_ctx;
static struct vcodec_mpibuf_fn *g_mpi_buf_fn = NULL;

void register_vmpibuf_func_to_pp(struct vcodec_mpibuf_fn *mpibuf_fn)
{
	g_mpi_buf_fn = mpibuf_fn;
}
EXPORT_SYMBOL(register_vmpibuf_func_to_pp);

void unregister_vmpibuf_func_pp(void)
{
	g_mpi_buf_fn = NULL;
}
EXPORT_SYMBOL(unregister_vmpibuf_func_pp);

static struct vcodec_mpibuf_fn * get_vmpibuf_func(void)
{
	if (IS_ERR_OR_NULL(g_mpi_buf_fn)) {
		pr_err("%s failed\n", __FUNCTION__);
		return ERR_PTR(-ENOMEM);
	} else
		return g_mpi_buf_fn;
}

static struct pp_buffer_t * pp_malloc_buffer(struct pp_chn_info_t *info, u32 size)
{
	struct vcodec_mpibuf_fn *func = get_vmpibuf_func();
	struct pp_buffer_t *pp_buf = NULL;

	pp_buf = vmalloc(sizeof(*pp_buf));
	if (IS_ERR_OR_NULL(pp_buf)) {
		pp_err("failed\n");
		return ERR_PTR(-ENOMEM);
	}

	if (func->buf_alloc) {
		pp_buf->buf = func->buf_alloc(size);
		if (pp_buf->buf) {
			if (func->buf_get_dmabuf) {
				pp_buf->buf_dma = func->buf_get_dmabuf(pp_buf->buf);
				if (pp_buf->buf_dma)
					pp_buf->iova = info->api->get_address(info->dev_srv,
									      pp_buf->buf_dma, 0);
			}
		}
	}

	return pp_buf;
}

static void pp_free_buffer(struct pp_chn_info_t *info, struct pp_buffer_t *pp_buf)
{
	struct vcodec_mpibuf_fn *func = get_vmpibuf_func();

	if (pp_buf) {
		if (pp_buf->buf_dma)
			info->api->release_address(info->dev_srv, pp_buf->buf_dma);
		if (pp_buf->buf)
			func->buf_unref(pp_buf->buf);

		vfree(pp_buf);
		pp_buf = NULL;
	}
}

static int pp_allocate_buffer(struct pp_chn_info_t *info)
{
	int w = PP_ALIGN(info->width, 32);
	int h = PP_ALIGN(info->height, 32);
	enum PP_RET ret = VEPU_PP_OK;

	//TODO: reduce buffer size
	info->buf_rfpw = pp_malloc_buffer(info, w * h / 8);
	info->buf_rfpr = pp_malloc_buffer(info, w * h / 8);
	if (IS_ERR_OR_NULL(info->buf_rfpw) ||
	    IS_ERR_OR_NULL(info->buf_rfpr)) {
		pp_err("failed\n");
		ret = VEPU_PP_NOK;
	}

	if (info->md_en) {
		info->buf_rfmwr = pp_malloc_buffer(info, w * h / 8);
		if (IS_ERR_OR_NULL(info->buf_rfmwr)) {
			pp_err("failed\n");
			ret = VEPU_PP_NOK;
		}
	}

	return ret;
}

static void pp_release_buffer(struct pp_chn_info_t *info)
{
	pp_free_buffer(info, info->buf_rfpw);
	pp_free_buffer(info, info->buf_rfpr);

	if (info->md_en)
		pp_free_buffer(info, info->buf_rfmwr);
}

int vepu_pp_create_chn(int chn, struct pp_chn_attr *attr)
{
	struct pp_chn_info_t *info = NULL;

	pr_info("%s %d\n", __FUNCTION__, chn);
	if (chn >= MAX_CHN_NUM) {
		pp_err("vepu pp create channel id %d error\n", chn);
		return VEPU_PP_NOK;
	}

	info = &g_pp_ctx.chn_info[chn];

	memset(info, 0, sizeof(*info));
	info->chn = chn;
	info->width = attr->width;
	info->height = attr->height;
	info->smear_en = attr->smear_en;
	info->weightp_en = attr->weightp_en;
	info->md_en = attr->md_en;
	info->od_en = attr->od_en;
	info->down_scale_en = attr->down_scale_en;
	info->api = &pp_srv_api;

	info->dev_srv = vmalloc(info->api->ctx_size);
	if (info->dev_srv == NULL) {
		pp_err("vepu pp vmalloc failed\n");
		return VEPU_PP_NOK;
	} else
		memset(info->dev_srv, 0, info->api->ctx_size);

	info->api->init(info->dev_srv, MPP_DEVICE_RKVENC_PP);

	return pp_allocate_buffer(info);
}
EXPORT_SYMBOL(vepu_pp_create_chn);

int vepu_pp_destroy_chn(int chn)
{
	struct pp_chn_info_t *info = NULL;

	pr_info("%s %d\n", __FUNCTION__, chn);

	if (chn >= MAX_CHN_NUM) {
		pp_err("vepu pp destroy channel id %d error\n", chn);
		return VEPU_PP_NOK;
	}

	info = &g_pp_ctx.chn_info[chn];

	info->api->deinit(info->dev_srv);

	pp_release_buffer(info);

	if (info->dev_srv) {
		vfree(info->dev_srv);
		info->dev_srv = NULL;
	}

	return VEPU_PP_OK;
}
EXPORT_SYMBOL(vepu_pp_destroy_chn);

static void pp_set_src_addr(struct pp_chn_info_t *info, struct pp_com_cfg *cfg)
{
	struct pp_param_t *p = &info->param;
	u32 adr_src0, adr_src1, adr_src2;
	u32 width = info->width, height = info->height;

	adr_src0 = info->api->get_address(info->dev_srv, cfg->src_buf, 0);

	switch (cfg->fmt) {
	case RKVENC_F_YCbCr_420_P: {
		adr_src1 = adr_src0 + width * height;
		adr_src2 = adr_src1 + width * height / 4;
		break;
	}
	case RKVENC_F_YCbCr_420_SP: {
		adr_src1 = adr_src0 + width * height;
		adr_src2 = adr_src1;
		break;
	}
	default: {
	}
	}

	p->adr_src0 = adr_src0;
	p->adr_src1 = adr_src1;
	p->adr_src2 = adr_src2;
}

static void pp_set_common_addr(struct pp_chn_info_t *info, struct pp_com_cfg *cfg)
{
	struct pp_param_t *p = &info->param;

	//TODO: md_interval > 1
	if (cfg->frm_cnt % 2 == 0) {
		p->adr_rfpw = info->buf_rfpw->iova;
		p->adr_rfpr0 = info->buf_rfpr->iova;
		p->adr_rfpr1 = p->adr_rfpr0;
	} else {
		p->adr_rfpw = info->buf_rfpr->iova;
		p->adr_rfpr0 = info->buf_rfpw->iova;
		p->adr_rfpr1 = p->adr_rfpr0;
	}
}

static void vepu_pp_set_param(struct pp_chn_info_t *info, enum pp_cmd cmd, void *param)
{
	struct pp_param_t *p = &info->param;

	switch (cmd) {
	case PP_CMD_SET_COMMON_CFG: {
		struct pp_com_cfg *cfg = (struct pp_com_cfg *)param;
		int frm_cnt = cfg->frm_cnt;
		int gop = cfg->gop;

		p->enc_pic_fmt.src_from_isp = !info->down_scale_en;
		p->enc_pic_fmt.ref_pic0_updt_en = (info->smear_en || info->weightp_en) &&
						  ((frm_cnt % gop) != (gop - 1));
		p->enc_pic_fmt.ref_pic1_updt_en = (info->md_en && (frm_cnt % cfg->md_interval == 0)) ||
						  (info->od_en && (frm_cnt % cfg->od_interval == 0));

		p->enc_pic_rsl.pic_wd8_m1 = info->width / 8 - 1;
		p->enc_pic_rsl.pic_hd8_m1 = info->height / 8 - 1;

		p->vsp_pic_con.src_cfmt = cfg->fmt;

		p->vsp_pic_fill.pic_wfill = 0;
		p->vsp_pic_fill.pic_hfill = 0;

		p->vsp_pic_ofst.pic_ofst_x = 0;
		p->vsp_pic_ofst.pic_ofst_y = 0;
		p->vsp_pic_strd0.src_strd0 = info->width;
		p->vsp_pic_strd1.src_strd1 = info->width / 2;

		p->smr_con_base.smear_cur_frm_en = info->smear_en && (frm_cnt % gop);
		p->smr_con_base.smear_ref_frm_en = p->smr_con_base.smear_cur_frm_en && ((frm_cnt % gop) != 1);
		p->smr_sto_strd = ((p->enc_pic_rsl.pic_wd8_m1 + 4) / 4 + 7) / 8 * 16;

		p->wp_con_comb0 = info->weightp_en && (frm_cnt % gop);

		p->md_con_base.cur_frm_en = info->md_en && (frm_cnt % cfg->md_interval == 0) && (frm_cnt > 0);
		p->md_con_base.ref_frm_en = p->md_con_base.cur_frm_en && (frm_cnt > cfg->md_interval);

		{
			//TODO:
			u32 od_en = info->od_en && (frm_cnt % cfg->od_interval == 0);
			u32 od_background_en = (frm_cnt > 0);
			u32 od_sad_comp_en = 1;
			p->od_con_base = od_en | (od_background_en << 1) | (od_sad_comp_en << 2);
		}

		pp_set_src_addr(info, cfg);
		pp_set_common_addr(info, cfg);
		break;
	}
	case PP_CMD_SET_MD_CFG: {
		struct pp_md_cfg *cfg = (struct pp_md_cfg *)param;
		p->md_con_base.switch_sad = cfg->switch_sad;
		p->md_con_base.thres_sad = cfg->thres_sad;
		p->md_con_base.thres_move = cfg->thres_move;

		p->md_fly_chk.night_mode_en = cfg->night_mode;
		p->md_fly_chk.flycatkin_flt_en = cfg->filter_switch;
		p->md_fly_chk.thres_dust_move = 3;
		p->md_fly_chk.thres_dust_blk = 3;
		p->md_fly_chk.thres_dust_chng = 50;
		p->md_sto_strd = 64; /* ?? */

		p->adr_rfmw = info->buf_rfmwr->iova;
		p->adr_rfmr = info->buf_rfmwr->iova;
		p->adr_md_base = info->api->get_address(info->dev_srv, cfg->mdw_buf, 0);
		break;
	}
	case PP_CMD_SET_OD_CFG: {
		struct pp_od_cfg *cfg = (struct pp_od_cfg *)param;
		p->od_con_cmplx.od_thres_complex = cfg->thres_complex;
		p->od_con_cmplx.od_thres_area_complex = cfg->thres_area_complex;
		p->od_con_sad.od_thres_sad = 7;
		p->od_con_sad.od_thres_area_sad = cfg->thres_area_complex;
		break;
	}
	default: {
	}
	}
}

int vepu_pp_control(int chn, enum pp_cmd cmd, void *param)
{
	struct pp_chn_info_t *info = NULL;

	if (chn >= MAX_CHN_NUM) {
		pp_err("vepu pp control channel id %d error\n", chn);
		return VEPU_PP_NOK;
	}

	info = &g_pp_ctx.chn_info[chn];

	if (cmd == PP_CMD_SET_COMMON_CFG || cmd == PP_CMD_SET_MD_CFG ||
	    cmd == PP_CMD_SET_OD_CFG)
		vepu_pp_set_param(info, cmd, param);

	if (cmd == PP_CMD_RUN_SYNC) {
		{
			struct dev_reg_wr_t reg_wr;
			reg_wr.data_ptr = &info->param;
			reg_wr.size = sizeof(info->param);
			reg_wr.offset = 0;

			if (info->api->reg_wr)
				info->api->reg_wr(info->dev_srv, &reg_wr);
		}

		{
			struct dev_reg_rd_t reg_rd;
			reg_rd.data_ptr = &info->output;
			reg_rd.size = sizeof(info->output);
			reg_rd.offset = 0;

			if (info->api->reg_rd)
				info->api->reg_rd(info->dev_srv, &reg_rd);
		}

		if (info->api->cmd_send)
			info->api->cmd_send(info->dev_srv);
		if (info->api->cmd_poll)
			info->api->cmd_poll(info->dev_srv);
	}

	if (cmd == PP_CMD_GET_OD_OUTPUT) {
		struct pp_od_out *out = (struct pp_od_out *)param;

		out->od_flg = info->output.od_out_flag;
		out->pix_sum = info->output.od_out_pix_sum;
		pr_info("od_flg %d pix_sum 0x%08x\n", out->od_flg, out->pix_sum);
	}

	pr_debug("vepu pp control cmd 0x%08x finished\n", cmd);

	return 0;
}
EXPORT_SYMBOL(vepu_pp_control);

static int __init vepu_pp_init(void)
{
	pr_info("vepu_pp init\n");
	return 0;
}

static void __exit vepu_pp_exit(void)
{
	pr_info("vepu_pp exit\n");
}

module_init(vepu_pp_init);
module_exit(vepu_pp_exit);


MODULE_LICENSE("GPL");
