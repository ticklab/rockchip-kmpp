// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG  "hal_jpege_v540c"

#define pr_fmt(fmt) MODULE_TAG ":" fmt

#include <linux/string.h>
#include <linux/dma-buf.h>

#include "mpp_mem.h"
#include "mpp_maths.h"
#include "mpp_frame_impl.h"
#include "hal_jpege_debug.h"
#include "jpege_syntax.h"
#include "hal_bufs.h"
#include "rkv_enc_def.h"
#include "vepu541_common.h"
#include "vepu540c_common.h"
#include "hal_jpege_vepu540c.h"
#include "hal_jpege_vepu540c_reg.h"
#include "hal_jpege_hdr.h"
#include "mpp_packet.h"
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
#include <soc/rockchip/rockchip_dvbm.h>
#endif

typedef struct HalJpegeRc_t {
	/* For quantization table */
	RK_S32              q_factor;
	RK_U8               *qtable_y;
	RK_U8               *qtable_c;
	RK_S32              last_quality;
} HalJpegeRc;

typedef struct jpegeV540cHalContext_t {
	MppEncHalApi api;
	MppDev dev;
	void *regs;
	void *reg_out;

	void *dump_files;

	RK_S32 frame_type;
	RK_S32 last_frame_type;

	/* @frame_cnt starts from ZERO */
	RK_U32 frame_cnt;
	Vepu540cOsdCfg osd_cfg;
	void *roi_data;
	MppEncCfgSet *cfg;

	RK_U32 enc_mode;
	RK_U32 frame_size;
	RK_S32 max_buf_cnt;
	RK_S32 hdr_status;
	void *input_fmt;
	RK_U8 *src_buf;
	RK_U8 *dst_buf;
	RK_S32 buf_size;
	RK_U32 frame_num;
	RK_S32 fbc_header_len;
	RK_U32 title_num;

	JpegeBits bits;
	JpegeSyntax syntax;
	RK_S32	online;
	RK_U32	session_run;
	HalJpegeRc hal_rc;
} jpegeV540cHalContext;

MPP_RET hal_jpege_vepu_rc(jpegeV540cHalContext *ctx, HalEncTask *task)
{
	HalJpegeRc *hal_rc = &ctx->hal_rc;
	EncRcTaskInfo *rc_info = (EncRcTaskInfo *)&task->rc_task->info;

	if (rc_info->quality_target != hal_rc->last_quality) {
		RK_U32 i = 0;
		RK_S32 q = 0;

		hal_rc->q_factor = 100 - rc_info->quality_target;
		hal_jpege_dbg_input("use qfactor=%d, rc_info->quality_target=%d\n", hal_rc->q_factor,
				    rc_info->quality_target);

		q = hal_rc->q_factor;
		if (q < 50)
			q = 5000 / q;
		else
			q = 200 - (q << 1);

		for (i = 0; i < QUANTIZE_TABLE_SIZE; i++) {
			RK_S16 lq = (jpege_luma_quantizer[i] * q + 50) / 100;
			RK_S16 cq = (jpege_chroma_quantizer[i] * q + 50) / 100;

			/* Limit the quantizers to 1 <= q <= 255 */
			hal_rc->qtable_y[i] = MPP_CLIP3(1, 255, lq);
			hal_rc->qtable_c[i] = MPP_CLIP3(1, 255, cq);
		}
	}

	return MPP_OK;
}

MPP_RET hal_jpege_vepu_init_rc(HalJpegeRc *hal_rc)
{
	memset(hal_rc, 0, sizeof(HalJpegeRc));
	hal_rc->qtable_y = mpp_malloc(RK_U8, QUANTIZE_TABLE_SIZE);
	hal_rc->qtable_c = mpp_malloc(RK_U8, QUANTIZE_TABLE_SIZE);

	if (NULL == hal_rc->qtable_y || NULL == hal_rc->qtable_c) {
		mpp_err_f("qtable is null, malloc err\n");
		return MPP_ERR_MALLOC;
	}

	return MPP_OK;
}

MPP_RET hal_jpege_vepu_deinit_rc(HalJpegeRc *hal_rc)
{
	MPP_FREE(hal_rc->qtable_y);
	MPP_FREE(hal_rc->qtable_c);

	return MPP_OK;
}

MPP_RET hal_jpege_v540c_init(void *hal, MppEncHalCfg * cfg)
{
	MPP_RET ret = MPP_OK;
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	JpegV540cRegSet *regs = NULL;
	// mpp_env_get_u32("hal_jpege_debug", &hal_jpege_debug, 0);
	hal_jpege_enter();

	ctx->reg_out = mpp_calloc(JpegV540cStatus, 1);
	ctx->regs = mpp_calloc(JpegV540cRegSet, 1);
	ctx->input_fmt = mpp_calloc(VepuFmtCfg, 1);
	ctx->cfg = cfg->cfg;

	ctx->frame_cnt = 0;
	ctx->enc_mode = 1;
	ctx->online = cfg->online;
	cfg->type = VPU_CLIENT_RKVENC;
	ret = mpp_dev_init(&cfg->dev, cfg->type);
	if (ret) {
		mpp_err_f("mpp_dev_init failed. ret: %d\n", ret);
		return ret;
	}
	regs = (JpegV540cRegSet *) ctx->regs;
	ctx->dev = cfg->dev;
	ctx->osd_cfg.reg_base = (void *)&regs->reg_osd_cfg.osd_jpeg_cfg;
	ctx->osd_cfg.dev = ctx->dev;
	ctx->osd_cfg.osd_data3 = NULL;
	jpege_bits_init(&ctx->bits);
	mpp_assert(ctx->bits);
	hal_jpege_vepu_init_rc(&ctx->hal_rc);
	hal_jpege_leave();
	return ret;
}

MPP_RET hal_jpege_v540c_deinit(void *hal)
{
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;

	hal_jpege_enter();
	jpege_bits_deinit(ctx->bits);

	hal_jpege_vepu_deinit_rc(&ctx->hal_rc);

	MPP_FREE(ctx->regs);

	MPP_FREE(ctx->reg_out);

	MPP_FREE(ctx->input_fmt);

	if (ctx->dev) {
		mpp_dev_deinit(ctx->dev);
		ctx->dev = NULL;
	}
	hal_jpege_leave();
	return MPP_OK;
}

static MPP_RET hal_jpege_vepu540c_prepare(void *hal)
{
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	VepuFmtCfg *fmt = (VepuFmtCfg *) ctx->input_fmt;

	hal_jpege_dbg_func("enter %p\n", hal);
	vepu541_set_fmt(fmt, ctx->cfg->prep.format);
	hal_jpege_dbg_func("leave %p\n", hal);

	return MPP_OK;
}
#ifdef HW_DVBM
static void vepu540c_jpeg_set_dvbm(JpegV540cRegSet *regs)
{
	RK_U32 soft_resync = 1;
	RK_U32 frame_match = 0;

	// mpp_env_get_u32("soft_resync", &soft_resync, 1);
	// mpp_env_get_u32("frame_match", &frame_match, 0);
	// mpp_env_get_u32("dvbm_en", &dvbm_en, 1);

	regs->reg_ctl.reg0024_dvbm_cfg.dvbm_en = 1;
	regs->reg_ctl.reg0024_dvbm_cfg.src_badr_sel = 0;
	regs->reg_ctl.reg0024_dvbm_cfg.vinf_frm_match = frame_match;
	regs->reg_ctl.reg0024_dvbm_cfg.vrsp_half_cycle = 15;

	regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_sel = soft_resync;
	regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_soft = 1;
	regs->reg_ctl.reg0006_vs_ldly.dvbm_inf_sel = 0;

	regs->reg_base.reg0194_dvbm_id.ch_id = 1;
	regs->reg_base.reg0194_dvbm_id.frame_id = 0;
	regs->reg_base.reg0194_dvbm_id.vrsp_rtn_en = 1;
}
#else
static void vepu540c_jpeg_set_dvbm(JpegV540cRegSet *regs)
{
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct dvbm_addr_cfg dvbm_adr;

	rk_dvbm_ctrl(NULL, DVBM_VEPU_GET_ADR, &dvbm_adr);
	regs->reg_ctl.reg0024_dvbm_cfg.dvbm_en = 1;
	regs->reg_ctl.reg0024_dvbm_cfg.src_badr_sel = 1;
	regs->reg_ctl.reg0024_dvbm_cfg.vinf_frm_match = 1;
	regs->reg_ctl.reg0024_dvbm_cfg.vrsp_half_cycle = 8;

	regs->reg_ctl.reg0006_vs_ldly.vswm_lcnt_soft = dvbm_adr.line_cnt;
	regs->reg_ctl.reg0006_vs_ldly.vswm_fcnt_soft = dvbm_adr.frame_id;
	regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_sel = 1;
	regs->reg_ctl.reg0006_vs_ldly.dvbm_ack_soft = 1;
	regs->reg_ctl.reg0006_vs_ldly.dvbm_inf_sel = 1;

	regs->reg_base.reg0194_dvbm_id.ch_id = 1;
	regs->reg_base.reg0194_dvbm_id.frame_id = dvbm_adr.frame_id;
	regs->reg_base.reg0194_dvbm_id.vrsp_rtn_en = 1;
#else
#endif
}
#endif

MPP_RET hal_jpege_v540c_gen_regs(void *hal, HalEncTask * task)
{
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	JpegV540cRegSet *regs = ctx->regs;
	jpeg_vepu540c_control_cfg *reg_ctl = &regs->reg_ctl;
	jpeg_vepu540c_base *reg_base = &regs->reg_base;
	JpegeBits bits = ctx->bits;
	const RK_U8 *qtable[2] = { NULL };
	size_t length = mpp_packet_get_length(task->packet);
	RK_U8 *buf = mpp_buffer_get_ptr(task->output->buf) + task->output->start_offset;
	size_t size = task->output->size;
	JpegeSyntax *syntax = &ctx->syntax;
	Vepu540cJpegCfg cfg;
	RK_S32 bitpos;

	hal_jpege_enter();
	cfg.enc_task = task;
	cfg.jpeg_reg_base = &reg_base->jpegReg;
	cfg.dev = ctx->dev;
	cfg.input_fmt = ctx->input_fmt;
	cfg.online = ctx->online;
	memset(regs, 0, sizeof(JpegV540cRegSet));

	/* write header to output buffer */
	jpege_bits_setup(bits, buf, (RK_U32) size);
	/* seek length bytes data */
	jpege_seek_bits(bits, length << 3);
	/* NOTE: write header will update qtable */
	if (ctx->cfg->rc.rc_mode != MPP_ENC_RC_MODE_FIXQP) {
		hal_jpege_vepu_rc(ctx, task);
		qtable[0] = ctx->hal_rc.qtable_y;
		qtable[1] = ctx->hal_rc.qtable_c;
	} else {
		qtable[0] = NULL;
		qtable[1] = NULL;
	}
	write_jpeg_header(bits, syntax, qtable);

	bitpos = jpege_bits_get_bitpos(bits);
	task->length = (bitpos + 7) >> 3;
	mpp_packet_set_length(task->packet, task->length);

	dma_buf_end_cpu_access_partial(mpp_buffer_get_dma(task->output->buf),
				       DMA_TO_DEVICE, task->output->start_offset, task->length);
	reg_ctl->reg0004_enc_strt.lkt_num = 0;
	reg_ctl->reg0004_enc_strt.vepu_cmd = ctx->enc_mode;
	reg_ctl->reg0005_enc_clr.safe_clr = 0x0;
	reg_ctl->reg0005_enc_clr.force_clr = 0x0;

	reg_ctl->reg0008_int_en.enc_done_en = 1;
	reg_ctl->reg0008_int_en.lkt_node_done_en = 1;
	reg_ctl->reg0008_int_en.sclr_done_en = 1;
	reg_ctl->reg0008_int_en.slc_done_en = 1;
	reg_ctl->reg0008_int_en.bsf_oflw_en = 1;
	reg_ctl->reg0008_int_en.brsp_otsd_en = 1;
	reg_ctl->reg0008_int_en.wbus_err_en = 1;
	reg_ctl->reg0008_int_en.rbus_err_en = 1;
	reg_ctl->reg0008_int_en.wdg_en = 1;
	reg_ctl->reg0008_int_en.lkt_err_int_en = 0;

	reg_ctl->reg0012_dtrns_map.jpeg_bus_edin = 0x7;
	reg_ctl->reg0012_dtrns_map.src_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.meiw_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.bsw_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.lktr_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.roir_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.lktw_bus_edin = 0x0;
	reg_ctl->reg0012_dtrns_map.rec_nfbc_bus_edin = 0x0;
	reg_base->reg0192_enc_pic.enc_stnd = 2;	// disable h264 or hevc

	reg_ctl->reg0013_dtrns_cfg.axi_brsp_cke = 0x0;
	reg_ctl->reg0014_enc_wdg.vs_load_thd = 0x1fffff;
	reg_ctl->reg0014_enc_wdg.rfp_load_thd = 0xff;

	if (ctx->online)
		vepu540c_jpeg_set_dvbm(regs);
	vepu540c_set_jpeg_reg(&cfg);
	vepu540c_set_osd(&ctx->osd_cfg);
	{
		RK_U16 *tbl = &regs->jpeg_table.qua_tab0[0];
		RK_U32 i, j;

		for (i = 0; i < 8; i++) {
			for (j = 0; j < 8; j++)
				tbl[i * 8 + j] = 0x8000 / qtable[0][j * 8 + i];
		}
		tbl += 64;
		for (i = 0; i < 8; i++) {
			for (j = 0; j < 8; j++)
				tbl[i * 8 + j] = 0x8000 / qtable[1][j * 8 + i];
		}
		tbl += 64;
		for (i = 0; i < 8; i++) {
			for (j = 0; j < 8; j++)
				tbl[i * 8 + j] = 0x8000 / qtable[1][j * 8 + i];
		}
	}
	task->jpeg_osd_reg = &regs->reg_osd_cfg.osd_jpeg_cfg;
	task->jpeg_tlb_reg = &regs->jpeg_table;
	ctx->frame_num++;
	hal_jpege_leave();
	return MPP_OK;
}

MPP_RET hal_jpege_v540c_start(void *hal, HalEncTask * enc_task)
{
	MPP_RET ret = MPP_OK;
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	JpegV540cRegSet *hw_regs = ctx->regs;
	JpegV540cStatus *reg_out = ctx->reg_out;
	MppDevRegWrCfg cfg;
	MppDevRegRdCfg cfg1;
	hal_jpege_enter();

	if (enc_task->flags.err) {
		mpp_err_f("enc_task->flags.err %08x, return e arly",
			  enc_task->flags.err);
		return MPP_NOK;
	}

	cfg.reg = (RK_U32 *) & hw_regs->reg_ctl;
	cfg.size = sizeof(jpeg_vepu540c_control_cfg);
	cfg.offset = VEPU540C_CTL_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	cfg.reg = &hw_regs->reg_scl;
	cfg.size = sizeof(vepu540c_jpeg_tab) + sizeof(vepu540c_scl_cfg);
	cfg.offset = VEPU540C_SCLCFG_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	cfg.reg = &hw_regs->reg_base;
	cfg.size = sizeof(jpeg_vepu540c_base);
	cfg.offset = VEPU540C_BASE_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	cfg.reg = &hw_regs->reg_osd_cfg;
	cfg.size = sizeof(vepu540c_osd_regs);
	cfg.offset = VEPU540C_OSD_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &cfg);
	if (ret) {
		mpp_err_f("set register write failed %d\n", ret);
		return ret;
	}

	cfg1.reg = &reg_out->hw_status;
	cfg1.size = sizeof(RK_U32);
	cfg1.offset = VEPU540C_REG_BASE_HW_STATUS;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_RD, &cfg1);
	if (ret) {
		mpp_err_f("set register read failed %d\n", ret);
		return ret;
	}

	cfg1.reg = &reg_out->st;
	cfg1.size = sizeof(JpegV540cStatus) - 4;
	cfg1.offset = VEPU540C_STATUS_OFFSET;

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_RD, &cfg1);
	if (ret) {
		mpp_err_f("set register read failed %d\n", ret);
		return ret;
	}

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_SEND, NULL);
	if (ret)
		mpp_err_f("send cmd failed %d\n", ret);
	ctx->session_run = 1;
	hal_jpege_leave();
	return ret;
}

//#define DUMP_DATA
static MPP_RET hal_jpege_vepu540c_status_check(void *hal)
{
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	JpegV540cStatus *elem = (JpegV540cStatus *) ctx->reg_out;

	RK_U32 hw_status = elem->hw_status;

	if (hw_status & RKV_ENC_INT_LINKTABLE_FINISH)
		hal_jpege_dbg_detail("RKV_ENC_INT_LINKTABLE_FINISH");

	if (hw_status & RKV_ENC_INT_ONE_FRAME_FINISH)
		hal_jpege_dbg_detail("RKV_ENC_INT_ONE_FRAME_FINISH");

	if (hw_status & RKV_ENC_INT_ONE_SLICE_FINISH)
		hal_jpege_dbg_detail("RKV_ENC_INT_ONE_SLICE_FINISH");

	if (hw_status & RKV_ENC_INT_SAFE_CLEAR_FINISH)
		hal_jpege_dbg_detail("RKV_ENC_INT_SAFE_CLEAR_FINISH");

	if (hw_status & RKV_ENC_INT_BIT_STREAM_OVERFLOW)
		mpp_err_f("RKV_ENC_INT_BIT_STREAM_OVERFLOW");

	if (hw_status & RKV_ENC_INT_BUS_WRITE_FULL)
		mpp_err_f("RKV_ENC_INT_BUS_WRITE_FULL");

	if (hw_status & RKV_ENC_INT_BUS_WRITE_ERROR)
		mpp_err_f("RKV_ENC_INT_BUS_WRITE_ERROR");

	if (hw_status & RKV_ENC_INT_BUS_READ_ERROR)
		mpp_err_f("RKV_ENC_INT_BUS_READ_ERROR");

	if (hw_status & RKV_ENC_INT_TIMEOUT_ERROR)
		mpp_err_f("RKV_ENC_INT_TIMEOUT_ERROR");

	return MPP_OK;
}

MPP_RET hal_jpege_v540c_wait(void *hal, HalEncTask * task)
{
	MPP_RET ret = MPP_OK;
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	HalEncTask *enc_task = task;
	JpegV540cStatus *elem = (JpegV540cStatus *) ctx->reg_out;
	hal_jpege_enter();

	if (enc_task->flags.err) {
		mpp_err_f("enc_task->flags.err %08x, return early",
			  enc_task->flags.err);
		return MPP_NOK;
	}

	ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_POLL, NULL);
	if (ret) {
		mpp_err_f("poll cmd failed %d\n", ret);
		ret = MPP_ERR_VPUHW;
	} else {
		hal_jpege_vepu540c_status_check(hal);
		task->hw_length += elem->st.jpeg_head_bits_l32;
	}

	hal_jpege_leave();
	return ret;
}

MPP_RET hal_jpege_v540c_get_task(void *hal, HalEncTask * task)
{
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	// MppFrame frame = task->frame;
	// EncFrmStatus  *frm_status = &task->rc_task->frm;
	JpegeSyntax *syntax = (JpegeSyntax *) task->syntax.data;
	hal_jpege_enter();

	memcpy(&ctx->syntax, syntax, sizeof(ctx->syntax));

	ctx->last_frame_type = ctx->frame_type;

	ctx->osd_cfg.osd_data3 = mpp_frame_get_osd(task->frame);
	if (ctx->cfg->rc.rc_mode != MPP_ENC_RC_MODE_FIXQP) {
		if (!ctx->hal_rc.q_factor) {
			task->rc_task->info.quality_target = syntax->q_factor ? (100 - syntax->q_factor) : 80;
			task->rc_task->info.quality_min = 100 - syntax->qf_max;
			task->rc_task->info.quality_max = 100 - syntax->qf_min;
			task->rc_task->frm.is_intra = 1;
		} else {
			task->rc_task->info.quality_target = ctx->hal_rc.last_quality;
			task->rc_task->info.quality_min = 100 - syntax->qf_max;
			task->rc_task->info.quality_max = 100 - syntax->qf_min;
		}
	}

	hal_jpege_leave();

	return MPP_OK;
}

MPP_RET hal_jpege_v540c_ret_task(void *hal, HalEncTask * task)
{

	EncRcTaskInfo *rc_info = &task->rc_task->info;
	jpegeV540cHalContext *ctx = (jpegeV540cHalContext *) hal;
	hal_jpege_enter();
	(void)hal;

	ctx->hal_rc.last_quality = task->rc_task->info.quality_target;
	task->length += task->hw_length;

	// setup bit length for rate control
	rc_info->bit_real = task->hw_length * 8;
	rc_info->quality_real = rc_info->quality_target;

	if (ctx->osd_cfg.osd_data3)
		vepu540c_osd_put_dma_buf(&ctx->osd_cfg);

	if (!ctx->session_run) {
		vepu540c_osd_put_dma_buf(&ctx->osd_cfg); //combo case session no run must release agin
	}
	ctx->session_run = 0;
	hal_jpege_leave();
	return MPP_OK;
}

const MppEncHalApi hal_jpege_vepu540c = {
	"hal_jpege_v540c",
	MPP_VIDEO_CodingMJPEG,
	sizeof(jpegeV540cHalContext),
	0,
	hal_jpege_v540c_init,
	hal_jpege_v540c_deinit,
	hal_jpege_vepu540c_prepare,
	hal_jpege_v540c_get_task,
	hal_jpege_v540c_gen_regs,
	hal_jpege_v540c_start,
	hal_jpege_v540c_wait,
	NULL,
	NULL,
	hal_jpege_v540c_ret_task,
};
