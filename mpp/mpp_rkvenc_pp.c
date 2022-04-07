// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * author: timkingh.huang@rock-chips.com
 *
 */
#include <asm/cacheflush.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/proc_fs.h>
#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define PP_DRIVER_NAME		"mpp_vepu_pp"

#define	VEPU_PP_SESSION_MAX_BUFFERS		20

#define PP_REG_RO_VALID_INT_STS(x)		((x) & (BIT(0)))
#define PP_REG_RO_BUS_ERROR_STS(x)		((x) & (BIT(2) | BIT(3) | BIT(4)))

enum {
	VEPU_PP_ENC_STRT = 0x10,
	VEPU_PP_INT_EN = 0x20,
	VEPU_PP_INT_MSK = 0x24,
	VEPU_PP_INT_CLR = 0x28,
	VEPU_PP_INT_STA = 0x2C,
	VEPU_PP_ENC_CON_CKG = 0x30,

	VEPU_PP_ENC_PIC_FMT = 0x34,
	VEPU_PP_ENC_PIC_RSL = 0x38,

	VEPU_PP_BASE_ADR_RFPW = 0x3C,
	VEPU_PP_BASE_ADR_RFPR0 = 0x40,
	VEPU_PP_BASE_ADR_RFPR1 = 0x44,
	VEPU_PP_BASE_ADR_RFSW = 0x48,
	VEPU_PP_BASE_ADR_RFSR = 0x4C,
	VEPU_PP_BASE_ADR_RFMW = 0x50,
	VEPU_PP_BASE_ADR_RFMR = 0x54,

	VEPU_PP_BRSP_CHECK_EN = 0x58,
	VEPU_PP_DTRNS_MAP = 0x5C,
	VEPU_PP_ENC_WDG_CNT = 0x60,

	VEPU_PP_VSP_PIC_CON = 0x100,
	VEPU_PP_VSP_PIC_FILL = 0x104,
	VEPU_PP_VSP_PIC_OFST = 0x108,
	VEPU_PP_VSP_ADR_SRC0 = 0x10C,
	VEPU_PP_VSP_ADR_SRC1 = 0x110,
	VEPU_PP_VSP_ADR_SRC2 = 0x114,
	VEPU_PP_VSP_PIC_STRD0 = 0x118,
	VEPU_PP_VSP_PIC_STRD1 = 0x11C,
	VEPU_PP_VSP_PIC_UDFY = 0x120,
	VEPU_PP_VSP_PIC_UDFU = 0x124,
	VEPU_PP_VSP_PIC_UDFV = 0x128,
	VEPU_PP_VSP_PIC_UDFO = 0x12C,

	VEPU_PP_SMR_BASE_ADR = 0x200,
	VEPU_PP_SMR_CON_BASE = 0x204,
	VEPU_PP_SMR_CON_STO_STRIDE = 0x238,
	VEPU_PP_WP_CON_COMB0 = 0x300,
	VEPU_PP_WP_CON_COMB6 = 0x318,

	VEPU_PP_MD_BASE_ADR = 0x400,
	VEPU_PP_MD_CON_BASE = 0x404,
	VEPU_PP_MD_CON_FLY_CHECK = 0x408,
	VEPU_PP_MD_CON_STO_STRIDE = 0x40C,

	VEPU_PP_OD_CON_BASE = 0x500,
	VEPU_PP_OD_CON_CMPLX = 0x504,
	VEPU_PP_OD_CON_SAD = 0x508,

	VEPU_PP_WP_OUT_PAR_Y = 0x800,
	VEPU_PP_OD_OUT_PIX_SUM = 0x814
};

#define to_pp_task(task)		\
		container_of(task, struct pp_task, mpp_task)
#define to_vepu_pp_dev(dev)		\
		container_of(dev, struct vepu_pp_dev, mpp)

struct vepu_pp_params {
	u32 enc_pic_fmt; /* 0x34 */
	u32 enc_pic_rsl; /* 0x38 */
	u32 vsp_pic_con; /* 0x100 */
	u32 vsp_pic_fill; /* 0x104 */
	u32 vsp_pic_ofst; /* 0x108 */
	u32 vsp_pic_strd0; /* 0x118 */
	u32 vsp_pic_strd1; /* 0x11C */
	u32 vsp_pic_udfy; /* 0x120 */
	u32 vsp_pic_udfu; /* 0x124 */
	u32 vsp_pic_udfv; /* 0x128 */
	u32 vsp_pic_udfo; /* 0x12C */

	u32 smr_con_base; /* 0x204 */
	u32 smr_resi_thd0; /* 0x208 */
	u32 smr_resi_thd1;
	u32 smr_resi_thd2;
	u32 smr_resi_thd3;
	u32 smr_madp_thd0; /* 0x218 */
	u32 smr_madp_thd1;
	u32 smr_madp_thd2;
	u32 smr_madp_thd3;
	u32 smr_madp_thd4;
	u32 smr_madp_thd5;
	u32 smr_cnt_thd0; /* 0x230 */
	u32 smr_cnt_thd1;
	u32 smr_sto_strd; /* 0x238 */

	u32 wp_con_comb0; /* 0x300 */
	u32 wp_con_comb1;
	u32 wp_con_comb2;
	u32 wp_con_comb3;
	u32 wp_con_comb4;
	u32 wp_con_comb5;
	u32 wp_con_comb6;

	u32 md_con_base; /* 0x404 */
	u32 md_fly_chk;
	u32 md_sto_strd;

	u32 od_con_base; /* 0x500 */
	u32 od_con_cmplx;
	u32 od_con_sad;

	/* 0x600 ~ 0x734 */
	u32 osd_en[8];
	u32 osd_cfgs[78];

	/* buffer address */
	u32 adr_rfpw; /* 0x3C */
	u32 adr_rfpr0; /* 0x40 */
	u32 adr_rfpr1; /* 0x44 */
	u32 adr_rfsw; /* 0x48 */
	u32 adr_rfsr; /* 0x4C */
	u32 adr_rfmw; /* 0x50 */
	u32 adr_rfmr; /* 0x54 */
	u32 adr_src0; /* 0x10C */
	u32 adr_src1; /* 0x110 */
	u32 adr_src2; /* 0x114 */
	u32 adr_smr_base; /* 0x200 */
	u32 adr_md_base; /* 0x400 */
};

struct vepu_pp_output {
	u32 wp_out_par_y; /* 0x800 */
	u32 wp_out_par_u;
	u32 wp_out_par_v;
	u32 wp_out_pic_mean;
	u32 od_out_flag;
	u32 od_out_pix_sum; /* 0x814 */
};

struct pp_task {
	struct mpp_task mpp_task;
	struct mpp_hw_info *hw_info;

	enum MPP_CLOCK_MODE clk_mode;
	struct vepu_pp_params params;
	struct vepu_pp_output output;

	struct reg_offset_info off_inf;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct vepu_pp_dev {
	struct mpp_dev mpp;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info sclk_info;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_s;
};

static int vepu_pp_extract_task_msg(struct pp_task *task,
				    struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;

	for (i = 0; i < msgs->req_cnt; i++) {
		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			memcpy(&task->params, req->data, req->size);
		} break;
		case MPP_CMD_SET_REG_READ: {
			memcpy(&task->r_reqs[task->r_req_cnt++],
			       req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_ADDR_OFFSET: {
			mpp_extract_reg_offset_info(&task->off_inf, req);
		} break;
		default:
			break;
		}
	}
	mpp_debug(DEBUG_TASK_INFO, "w_req_cnt %d, r_req_cnt %d\n",
		  task->w_req_cnt, task->r_req_cnt);

	return 0;
}

static void *vepu_pp_alloc_task(struct mpp_session *session,
				struct mpp_task_msgs *msgs)
{
	int ret;
	struct pp_task *task = NULL;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task_init(session, &task->mpp_task);
	/* extract reqs for current task */
	ret = vepu_pp_extract_task_msg(task, msgs);
	if (ret)
		goto fail;

	task->clk_mode = CLK_MODE_NORMAL;

	mpp_debug_leave();

	return &task->mpp_task;

fail:
	mpp_task_finalize(session, &task->mpp_task);
	kfree(task);
	return NULL;
}

static void vepu_pp_config(struct mpp_dev *mpp, struct pp_task *task)
{
	//struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);
	struct vepu_pp_params *cfg = &task->params;

	mpp_write_relaxed(mpp, VEPU_PP_INT_EN, 0x3F);
	mpp_write_relaxed(mpp, VEPU_PP_INT_MSK, 0);
	mpp_write_relaxed(mpp, VEPU_PP_INT_CLR, 0x3F);
	mpp_write_relaxed(mpp, VEPU_PP_ENC_CON_CKG, 0x7);
	mpp_write_relaxed(mpp, VEPU_PP_BRSP_CHECK_EN, 0x1F);

	mpp_write_relaxed(mpp, VEPU_PP_DTRNS_MAP, 0);
	mpp_write_relaxed(mpp, VEPU_PP_ENC_WDG_CNT, 0xFFFF);

	mpp_write_relaxed(mpp, VEPU_PP_VSP_ADR_SRC0, cfg->adr_src0);
	mpp_write_relaxed(mpp, VEPU_PP_VSP_ADR_SRC1, cfg->adr_src1);
	mpp_write_relaxed(mpp, VEPU_PP_VSP_ADR_SRC2, cfg->adr_src2);

	mpp_write_relaxed(mpp, VEPU_PP_BASE_ADR_RFPW, cfg->adr_rfpw);
	mpp_write_relaxed(mpp, VEPU_PP_BASE_ADR_RFPR0, cfg->adr_rfpr0);
	mpp_write_relaxed(mpp, VEPU_PP_BASE_ADR_RFPR1, cfg->adr_rfpr1);
	mpp_write_relaxed(mpp, VEPU_PP_BASE_ADR_RFSW, cfg->adr_rfsw);
	mpp_write_relaxed(mpp, VEPU_PP_BASE_ADR_RFSR, cfg->adr_rfsr);
	mpp_write_relaxed(mpp, VEPU_PP_BASE_ADR_RFMW, cfg->adr_rfmw);
	mpp_write_relaxed(mpp, VEPU_PP_BASE_ADR_RFMR, cfg->adr_rfmr);

	mpp_write_relaxed(mpp, VEPU_PP_ENC_PIC_FMT, cfg->enc_pic_fmt);
	mpp_write_relaxed(mpp, VEPU_PP_ENC_PIC_RSL, cfg->enc_pic_rsl);

	mpp_write_relaxed(mpp, VEPU_PP_VSP_PIC_CON, cfg->vsp_pic_con);
	mpp_write_relaxed(mpp, VEPU_PP_VSP_PIC_FILL, cfg->vsp_pic_fill);
	mpp_write_relaxed(mpp, VEPU_PP_VSP_PIC_OFST, cfg->vsp_pic_ofst);
	mpp_write_relaxed(mpp, VEPU_PP_VSP_PIC_STRD0, cfg->vsp_pic_strd0);
	mpp_write_relaxed(mpp, VEPU_PP_VSP_PIC_STRD1, cfg->vsp_pic_strd1);
}

static void vepu_pp_smear_cfg(struct mpp_dev *mpp, struct pp_task *task)
{
	struct vepu_pp_params *cfg = &task->params;

	if (cfg->smr_con_base & 1) {
		u32 *pval = &cfg->smr_con_base;
		u32 reg_num = (VEPU_PP_SMR_CON_STO_STRIDE - VEPU_PP_SMR_CON_BASE) / 4 + 1;
		int k;

		mpp_write_relaxed(mpp, VEPU_PP_SMR_BASE_ADR, cfg->adr_smr_base);

		for (k = 0; k < reg_num; ++k)
			mpp_write_relaxed(mpp, VEPU_PP_SMR_CON_BASE + 4 * k, pval[k]);
	}
}

static void vepu_pp_wp_cfg(struct mpp_dev *mpp, struct pp_task *task)
{
	struct vepu_pp_params *cfg = &task->params;

	if (cfg->wp_con_comb0 & 1) {
		u32 *pval = &cfg->wp_con_comb0;
		u32 reg_num = (VEPU_PP_WP_CON_COMB6 - VEPU_PP_WP_CON_COMB0) / 4 + 1;
		int k;

		for (k = 0; k < reg_num; ++k)
			mpp_write_relaxed(mpp, VEPU_PP_WP_CON_COMB0 + 4 * k, pval[k]);
	}
}

static void vepu_pp_md_cfg(struct mpp_dev *mpp, struct pp_task *task)
{
	struct vepu_pp_params *cfg = &task->params;

	if (cfg->md_con_base & 1) {
		mpp_write_relaxed(mpp, VEPU_PP_MD_BASE_ADR, cfg->adr_md_base);
		mpp_write_relaxed(mpp, VEPU_PP_MD_CON_BASE, cfg->md_con_base);
		mpp_write_relaxed(mpp, VEPU_PP_MD_CON_FLY_CHECK, cfg->md_fly_chk);
		mpp_write_relaxed(mpp, VEPU_PP_MD_CON_STO_STRIDE, cfg->md_sto_strd);
	}
}

static void vepu_pp_od_cfg(struct mpp_dev *mpp, struct pp_task *task)
{
	struct vepu_pp_params *cfg = &task->params;

	if (cfg->od_con_base & 1) {
		mpp_write_relaxed(mpp, VEPU_PP_OD_CON_BASE, cfg->od_con_base);
		mpp_write_relaxed(mpp, VEPU_PP_OD_CON_CMPLX, cfg->od_con_cmplx);
		mpp_write_relaxed(mpp, VEPU_PP_OD_CON_SAD, cfg->od_con_sad);
	}
}

static void vepu_pp_osd_cfg(struct mpp_dev *mpp, struct pp_task *task)
{
}

static int vepu_pp_run(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task)
{
	struct pp_task *task = NULL;

	mpp_debug_enter();

	task = to_pp_task(mpp_task);

	/* init current task */
	mpp->cur_task = mpp_task;

	vepu_pp_config(mpp, task);
	vepu_pp_smear_cfg(mpp, task);
	vepu_pp_wp_cfg(mpp, task);
	vepu_pp_md_cfg(mpp, task);
	vepu_pp_od_cfg(mpp, task);
	vepu_pp_osd_cfg(mpp, task);

	/* Last, flush the registers */
	wmb();
	/* start vepu_pp */
	mpp_write(mpp, VEPU_PP_ENC_STRT, 1);

	mpp_debug_leave();

	return 0;
}

static int vepu_pp_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, VEPU_PP_INT_STA);
	mpp_write(mpp, VEPU_PP_INT_CLR, 0x3F);

	if (!PP_REG_RO_VALID_INT_STS(mpp->irq_status))
		return IRQ_NONE;

	return IRQ_WAKE_THREAD;
}

static int vepu_pp_isr(struct mpp_dev *mpp)
{
	struct mpp_task *mpp_task = NULL;
	struct pp_task *task = NULL;
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);

	mpp_task = mpp->cur_task;
	task = to_pp_task(mpp_task);
	if (!task) {
		dev_err(pp->mpp.dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
		  task->irq_status);

	if (PP_REG_RO_BUS_ERROR_STS(task->irq_status))
		atomic_inc(&mpp->reset_request);

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int vepu_pp_finish(struct mpp_dev *mpp,
			  struct mpp_task *mpp_task)
{
	struct pp_task *task = to_pp_task(mpp_task);
	struct vepu_pp_output *output = &task->output;
	u32 reg_num = (VEPU_PP_OD_OUT_PIX_SUM - VEPU_PP_WP_OUT_PAR_Y) / 4 + 1;
	u32 *pval = &output->wp_out_par_y;
	int k;

	mpp_debug_enter();

	for (k = 0; k < reg_num; ++k)
		pval[k] = mpp_read(mpp, VEPU_PP_WP_OUT_PAR_Y + k * 4);

	mpp_debug_leave();

	return 0;
}

static int vepu_pp_result(struct mpp_dev *mpp,
			  struct mpp_task *mpp_task,
			  struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct pp_task *task = to_pp_task(mpp_task);

	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];
		memcpy(req->data, (u8 *)&task->output, req->size);
	}

	return 0;
}

static int vepu_pp_free_task(struct mpp_session *session,
			     struct mpp_task *mpp_task)
{
	struct pp_task *task = to_pp_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int vepu_pp_procfs_remove(struct mpp_dev *mpp)
{
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);

	if (pp->procfs) {
		proc_remove(pp->procfs);
		pp->procfs = NULL;
	}

	return 0;
}

static int vepu_pp_procfs_init(struct mpp_dev *mpp)
{
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);

	pp->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(pp->procfs)) {
		mpp_err("failed on mkdir\n");
		pp->procfs = NULL;
		return -EIO;
	}
	mpp_procfs_create_u32("aclk", 0644,
			      pp->procfs, &pp->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      pp->procfs, &mpp->session_max_buffers);

	return 0;
}
#else
static inline int vepu_pp_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int vepu_pp_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int vepu_pp_init(struct mpp_dev *mpp)
{
	int ret;
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_IEP2];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &pp->aclk_info, "aclk_vepu_pp");
	if (ret)
		mpp_err("failed on clk_get aclk\n");
	ret = mpp_get_clk_info(mpp, &pp->hclk_info, "hclk_vepu_pp");
	if (ret)
		mpp_err("failed on clk_get hclk\n");
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&pp->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);

	pp->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "rst_a_pp");
	if (!pp->rst_a)
		mpp_err("No aclk reset resource define\n");
	pp->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "rst_h_pp");
	if (!pp->rst_h)
		mpp_err("No hclk reset resource define\n");

	return 0;
}

static int vepu_pp_clk_on(struct mpp_dev *mpp)
{
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);

	mpp_clk_safe_enable(pp->aclk_info.clk);
	mpp_clk_safe_enable(pp->hclk_info.clk);
	mpp_clk_safe_enable(pp->sclk_info.clk);

	return 0;
}

static int vepu_pp_clk_off(struct mpp_dev *mpp)
{
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);

	mpp_clk_safe_disable(pp->aclk_info.clk);
	mpp_clk_safe_disable(pp->hclk_info.clk);
	mpp_clk_safe_disable(pp->sclk_info.clk);

	return 0;
}

static int vepu_pp_set_freq(struct mpp_dev *mpp,
			    struct mpp_task *mpp_task)
{
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);
	struct pp_task *task = to_pp_task(mpp_task);

	mpp_clk_set_rate(&pp->aclk_info, task->clk_mode);

	return 0;
}

static int vepu_pp_reset(struct mpp_dev *mpp)
{
	struct vepu_pp_dev *pp = to_vepu_pp_dev(mpp);

	if (pp->rst_a && pp->rst_h && pp->rst_s) {
		/* Don't skip this or iommu won't work after reset */
		rockchip_pmu_idle_request(mpp->dev, true);
		mpp_safe_reset(pp->rst_a);
		mpp_safe_reset(pp->rst_h);
		mpp_safe_reset(pp->rst_s);
		udelay(5);
		mpp_safe_unreset(pp->rst_a);
		mpp_safe_unreset(pp->rst_h);
		mpp_safe_unreset(pp->rst_s);
		rockchip_pmu_idle_request(mpp->dev, false);
	}

	return 0;
}

static struct mpp_hw_ops vepu_pp_hw_ops = {
	.init = vepu_pp_init,
	.clk_on = vepu_pp_clk_on,
	.clk_off = vepu_pp_clk_off,
	.set_freq = vepu_pp_set_freq,
	.reset = vepu_pp_reset,
};

static struct mpp_dev_ops vepu_pp_dev_ops = {
	.alloc_task = vepu_pp_alloc_task,
	.run = vepu_pp_run,
	.irq = vepu_pp_irq,
	.isr = vepu_pp_isr,
	.finish = vepu_pp_finish,
	.result = vepu_pp_result,
	.free_task = vepu_pp_free_task,
};

static struct mpp_hw_info vepu_pp_hw_info = {
	.reg_id = -1,
};

static const struct mpp_dev_var vepu_pp_data = {
	.device_type = MPP_DEVICE_RKVENC_PP,
	.hw_ops = &vepu_pp_hw_ops,
	.dev_ops = &vepu_pp_dev_ops,
	.hw_info = &vepu_pp_hw_info,
};

static const struct of_device_id mpp_vepu_pp_match[] = {
	{
		.compatible = "rockchip,rkvenc-pp-rv1106",
		.data = &vepu_pp_data,
	},
	{},
};

static int vepu_pp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vepu_pp_dev *pp = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dev_info(dev, "probe device\n");
	pp = devm_kzalloc(dev, sizeof(struct vepu_pp_dev), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	mpp = &pp->mpp;
	platform_set_drvdata(pdev, pp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_vepu_pp_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq,
					mpp_dev_isr_sched,
					IRQF_SHARED,
					dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		return -EINVAL;
	}

	mpp->session_max_buffers = VEPU_PP_SESSION_MAX_BUFFERS;
	vepu_pp_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int vepu_pp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vepu_pp_dev *pp = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&pp->mpp);
	vepu_pp_procfs_remove(&pp->mpp);

	return 0;
}

static void vepu_pp_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct vepu_pp_dev *pp = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &pp->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->task_count,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_vepu_pp_driver = {
	.probe = vepu_pp_probe,
	.remove = vepu_pp_remove,
	.shutdown = vepu_pp_shutdown,
	.driver = {
		.name = PP_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_vepu_pp_match),
	},
};
EXPORT_SYMBOL(rockchip_vepu_pp_driver);
