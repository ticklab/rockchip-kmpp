// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */

#define pr_fmt(fmt) "rkvenc_540c: " fmt

#include <asm/cacheflush.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/proc_fs.h>
#include <linux/pm_runtime.h>
#include <linux/nospec.h>
#include <linux/workqueue.h>
#include <linux/dma-iommu.h>
#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_ipa.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
#include <soc/rockchip/rockchip_dvbm.h>
#endif
#include "mpp_debug.h"
#include "mpp_iommu.h"
#include "mpp_common.h"
#define RKVENC_DRIVER_NAME			"mpp_rkvenc_540c"

#define	RKVENC_SESSION_MAX_BUFFERS		40

/* irq status definition */
#define RKVENC_DVBM_DISCONNECT		(BIT(15))
#define RKVENC_JPEG_OVERFLOW		(BIT(13))
#define RKVENC_VIDEO_OVERFLOW		(BIT(4))
#define RKVENC_ENC_DONE_STATUS		(BIT(0))
#define REC_FBC_DIS_CLASS_OFFSET	(36)

#define RKVENC_VIDEO_BSBS	(0x2b8)
#define RKVENC_VIDEO_BSBR	(0x2bc)
#define RKVENC_JPEG_BSBS	(0x40c)
#define RKVENC_JPEG_BSBR	(0x408)

#define to_rkvenc_info(info)		\
		container_of(info, struct rkvenc_hw_info, hw)
#define to_rkvenc_task(ctx)		\
		container_of(ctx, struct rkvenc_task, mpp_task)
#define to_rkvenc_dev(dev)		\
		container_of(dev, struct rkvenc_dev, mpp)

enum RKVENC_MODE {
	RKVENC_MODE_ONEFRAME		= 0,
	RKVENC_MODE_LINK_ADD		= 1,
	RKVENC_MODE_LINK_ONEFRAME	= 2,

	RKVDEC_MODE_BUTT,
};

enum RKVENC_FORMAT_TYPE {
	RKVENC_FMT_BASE		= 0x0000,
	RKVENC_FMT_H264E	= RKVENC_FMT_BASE + 0,
	RKVENC_FMT_H265E	= RKVENC_FMT_BASE + 1,
	RKVENC_FMT_JPEGE	= RKVENC_FMT_BASE + 2,
	RKVENC_FMT_BUTT,

	RKVENC_FMT_OSD_BASE	= RKVENC_FMT_BUTT,
	RKVENC_FMT_H264E_OSD	= RKVENC_FMT_OSD_BASE + 0,
	RKVENC_FMT_H265E_OSD	= RKVENC_FMT_OSD_BASE + 1,
	RKVENC_FMT_JPEGE_OSD	= RKVENC_FMT_OSD_BASE + 2,
	RKVENC_FMT_OSD_BUTT,
};

enum RKVENC_CLASS_TYPE {
	RKVENC_CLASS_CTL	= 0,	/* base */
	RKVENC_CLASS_PIC	= 1,	/* picture configure */
	RKVENC_CLASS_RC		= 2,	/* rate control */
	RKVENC_CLASS_PAR	= 3,	/* parameter */
	RKVENC_CLASS_SQI	= 4,	/* subjective Adjust */
	RKVENC_CLASS_SCL	= 5,	/* scaling list */
	RKVENC_CLASS_OSD	= 6,	/* osd */
	RKVENC_CLASS_ST		= 7,	/* status */
	RKVENC_CLASS_DEBUG	= 8,	/* debug */
	RKVENC_CLASS_BUTT,
};

enum RKVENC_CLASS_FD_TYPE {
	RKVENC_CLASS_FD_BASE	= 0,	/* base */
	RKVENC_CLASS_FD_OSD	= 1,	/* osd */
	RKVENC_CLASS_FD_BUTT,
};

struct rkvenc_reg_msg {
	u32 base_s;
	u32 base_e;
	/* class base for link */
	u32 link_s;
	/* class end for link */
	u32 link_e;
	/* class bytes for link */
	u32 link_len;
};

struct rkvenc_hw_info {
	struct mpp_hw_info hw;
	/* for register range check */
	u32 reg_class;
	struct rkvenc_reg_msg reg_msg[RKVENC_CLASS_BUTT];
	/* for fd translate */
	u32 fd_class;
	struct {
		u32 class;
		u32 base_fmt;
	} fd_reg[RKVENC_CLASS_FD_BUTT];
	/* for get format */
	struct {
		u32 class;
		u32 base;
		u32 bitpos;
		u32 bitlen;
	} fmt_reg;
	/* register info */
	u32 enc_start_base;
	u32 enc_clr_base;
	u32 int_en_base;
	u32 int_mask_base;
	u32 int_clr_base;
	u32 int_sta_base;
	u32 enc_wdg_base;
	u32 err_mask;
	u32 st_enc;
	u32 dvbm_cfg;
	/* for link */
	u32 link_stop_mask;
	u32 link_addr_base;
	u32 link_status_base;
	u32 link_node_base;
};

struct rkvenc_task {
	struct mpp_task mpp_task;
	int fmt;
	struct rkvenc_hw_info *hw_info;

	/* class register */
	struct {
		u32 valid;
		u32 *data;
		u32 size;
	} reg[RKVENC_CLASS_BUTT];
	/* register offset info */
	struct reg_offset_info off_inf;

	enum MPP_CLOCK_MODE clk_mode;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
	struct mpp_dma_buffer *table;
	u32 task_no;
};

struct rkvenc_link_header {
	struct {
		u32 node_core_id    : 2;
		u32 node_int        : 1;
		u32 reserved        : 1;
		u32 task_id         : 12;
		u32 reserved1       : 16;
	} node_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	} pic_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	} rc_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	} param_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	} sqi_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	} scal_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	} osd_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	} status_cfg;
	union {
		u32 valid : 1;
		u32 next_addr : 32;
	} next_node;
};

struct rkvenc_link_status {
	u32 cfg_done_num : 8;
	u32 cfg_num : 8;
	u32 int_num : 8;
	u32 enc_num : 8;
};

struct rkvenc_link_dev {
	struct rkvenc_dev *enc;
	u32 table_num;
	struct mpp_dma_buffer *table;
	u32 class_off[RKVENC_CLASS_BUTT];

	/* list for used link table */
	struct list_head used_list;
	/* list for unused link table */
	struct list_head unused_list;
	/* lock for the above table lists */
	struct mutex list_mutex;
};

struct rkvenc2_session_priv {
	struct rw_semaphore rw_sem;
	u32 dvbm_en;
	u32 dvbm_link;
	/* codec info from user */
	struct {
		/* show mode */
		u32 flag;
		/* item data */
		u64 val;
	} codec_info[ENC_INFO_BUTT];
};

struct rkvenc_dev {
	struct mpp_dev mpp;
	struct rkvenc_hw_info *hw_info;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info core_clk_info;
	u32 default_max_load;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_core;
	atomic_t on_work;

#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct dvbm_port *port;
	u32 dvbm_overflow;
#endif
	u32 line_cnt;
	u32 frm_id_start;
	u32 frm_id_end;

	/* for link mode */
	u32 task_capacity;
	struct rkvenc_link_dev *link;
	enum RKVENC_MODE link_mode;
	atomic_t link_task_cnt;
	u32 link_run;
};

static struct rkvenc_hw_info rkvenc_rv1106_hw_info = {
	.hw = {
		.reg_num = 254,
		.reg_id = 0,
		.reg_en = 4,
		.reg_start = 160,
		.reg_end = 253,
	},
	.reg_class = RKVENC_CLASS_BUTT,
	.reg_msg[RKVENC_CLASS_CTL] = {
		.base_s = 0x0000,
		.base_e = 0x0120,
	},
	.reg_msg[RKVENC_CLASS_PIC] = {
		.base_s = 0x0270,
		.base_e = 0x0480,
		.link_len = 532,
	},
	.reg_msg[RKVENC_CLASS_RC] = {
		.base_s = 0x1000,
		.base_e = 0x110c,
		.link_len = 272,
	},
	.reg_msg[RKVENC_CLASS_PAR] = {
		.base_s = 0x1700,
		.base_e = 0x19cc,
		.link_len = 720,
	},
	.reg_msg[RKVENC_CLASS_SQI] = {
		.base_s = 0x2000,
		.base_e = 0x20fc,
		.link_len = 256,
	},
	.reg_msg[RKVENC_CLASS_SCL] = {
		.base_s = 0x21e0,
		.base_e = 0x2dfc,
		.link_len = 3104,
	},
	.reg_msg[RKVENC_CLASS_OSD] = {
		.base_s = 0x3000,
		.base_e = 0x326c,
		.link_len = 624,
	},
	.reg_msg[RKVENC_CLASS_ST] = {
		.base_s = 0x4000,
		.base_e = 0x424c,
		.link_len = 592,
	},
	.reg_msg[RKVENC_CLASS_DEBUG] = {
		.base_s = 0x5000,
		.base_e = 0x5354,
	},
	.fd_class = RKVENC_CLASS_FD_BUTT,
	.fd_reg[RKVENC_CLASS_FD_BASE] = {
		.class = RKVENC_CLASS_PIC,
		.base_fmt = RKVENC_FMT_BASE,
	},
	.fd_reg[RKVENC_CLASS_FD_OSD] = {
		.class = RKVENC_CLASS_OSD,
		.base_fmt = RKVENC_FMT_OSD_BASE,
	},
	.fmt_reg = {
		.class = RKVENC_CLASS_PIC,
		.base = 0x0300,
		.bitpos = 0,
		.bitlen = 2,
	},
	.enc_start_base = 0x0010,
	.enc_clr_base = 0x0014,
	.int_en_base = 0x0020,
	.int_mask_base = 0x0024,
	.int_clr_base = 0x0028,
	.int_sta_base = 0x002c,
	.enc_wdg_base = 0x0038,
	.err_mask = 0x27d0,
	.st_enc = 0x4020,
	.dvbm_cfg = 0x60,
	.link_stop_mask = 0x0140,
	.link_addr_base = 0x0070,
	.link_status_base = 0x4024,
	.link_node_base = 0x4028,
};

/*
 * file handle translate information for v2
 */
/* rv1106 trans table */
static const u16 trans_tbl_h264e_rv1106[] = {
	7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23,
	/* jpege */
	100, 101, 102, 103,
};

static const u16 trans_tbl_h264e_rv1106_osd[] = {
	3, 4, 12, 13, 21, 22, 30, 31,
	39, 40, 48, 49, 57, 58, 66, 67,
};

static const u16 trans_tbl_h265e_rv1106[] = {
	7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23,
	/* jpege */
	100, 101, 102, 103,
};

static const u16 trans_tbl_h265e_rv1106_osd[] = {
	3, 4, 12, 13, 21, 22, 30, 31,
	39, 40, 48, 49, 57, 58, 66, 67,
};

static const u16 trans_tbl_jpege_rv1106[] = {
	100, 101, 102, 103,
};

static const u16 trans_tbl_jpege_osd_rv1106[] = {
	81, 82, 90, 91, 99, 100, 108, 109,
	117, 118, 126, 127, 135, 136, 144, 145,
};

static struct mpp_trans_info trans_rkvenc_rv1106[] = {
	[RKVENC_FMT_H264E] = {
		.count = ARRAY_SIZE(trans_tbl_h264e_rv1106),
		.table = trans_tbl_h264e_rv1106,
	},
	[RKVENC_FMT_H264E_OSD] = {
		.count = ARRAY_SIZE(trans_tbl_h264e_rv1106_osd),
		.table = trans_tbl_h264e_rv1106_osd,
	},
	[RKVENC_FMT_H265E] = {
		.count = ARRAY_SIZE(trans_tbl_h265e_rv1106),
		.table = trans_tbl_h265e_rv1106,
	},
	[RKVENC_FMT_H265E_OSD] = {
		.count = ARRAY_SIZE(trans_tbl_h265e_rv1106_osd),
		.table = trans_tbl_h265e_rv1106_osd,
	},
	[RKVENC_FMT_JPEGE] = {
		.count = ARRAY_SIZE(trans_tbl_jpege_rv1106),
		.table = trans_tbl_jpege_rv1106,
	},
	[RKVENC_FMT_JPEGE_OSD] = {
		.count = ARRAY_SIZE(trans_tbl_jpege_osd_rv1106),
		.table = trans_tbl_jpege_osd_rv1106,
	},
};

static int rkvenc_reset(struct mpp_dev *mpp);
static bool req_over_class(struct mpp_request *req,
			   struct rkvenc_task *task, int class)
{
	bool ret;
	u32 base_s, base_e, req_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	base_s = hw->reg_msg[class].base_s;
	base_e = hw->reg_msg[class].base_e;
	req_e = req->offset + req->size - sizeof(u32);

	ret = (req->offset <= base_e && req_e >= base_s) ? true : false;

	return ret;
}

static int rkvenc_invalid_class_msg(struct rkvenc_task *task)
{
	u32 i;
	u32 reg_class = task->hw_info->reg_class;

	for (i = 0; i < reg_class; i++) {
		if (task->reg[i].data) {
			//kfree(task->reg[i].data);
			task->reg[i].data = NULL;
			task->reg[i].valid = 0;
		}
	}

	return 0;
}

static int rkvenc_alloc_class_msg(struct rkvenc_task *task, int class)
{
	struct rkvenc_hw_info *hw = task->hw_info;

	if (!task->reg[class].data) {
		u32 base_s = hw->reg_msg[class].base_s;
		u32 base_e = hw->reg_msg[class].base_e;
		u32 class_size = base_e - base_s + sizeof(u32);

		/*data = kzalloc(class_size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;*/
		task->reg[class].data = NULL;
		task->reg[class].size = class_size;
	}

	return 0;
}

static int rkvenc_update_req(struct rkvenc_task *task, int class,
			     struct mpp_request *req_in,
			     struct mpp_request *req_out)
{
	u32 base_s, base_e, req_e, s, e;
	struct rkvenc_hw_info *hw = task->hw_info;

	base_s = hw->reg_msg[class].base_s;
	base_e = hw->reg_msg[class].base_e;
	req_e = req_in->offset + req_in->size - sizeof(u32);
	s = max(req_in->offset, base_s);
	e = min(req_e, base_e);

	req_out->offset = s;
	req_out->size = e - s + sizeof(u32);
	req_out->data = (u8 *)req_in->data + (s - req_in->offset);

	if (req_in->offset < base_s || req_e > base_e)
		mpp_err("warning over class, req off 0x%08x size %d\n",
			req_in->offset, req_in->size);

	return 0;
}

static int rkvenc_get_class_msg(struct rkvenc_task *task,
				u32 addr, struct mpp_request *msg)
{
	int i;
	bool found = false;
	u32 base_s, base_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	if (!msg)
		return -EINVAL;

	memset(msg, 0, sizeof(*msg));
	for (i = 0; i < hw->reg_class; i++) {
		base_s = hw->reg_msg[i].base_s;
		base_e = hw->reg_msg[i].base_e;
		if (addr >= base_s && addr < base_e) {
			found = true;
			msg->offset = base_s;
			msg->size = task->reg[i].size;
			msg->data = task->reg[i].data;
			break;
		}
	}

	return (found ? 0 : (-EINVAL));
}

static u32 *rkvenc_get_class_reg(struct rkvenc_task *task, u32 addr)
{
	int i;
	u8 *reg = NULL;
	u32 base_s, base_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	for (i = 0; i < hw->reg_class; i++) {
		base_s = hw->reg_msg[i].base_s;
		base_e = hw->reg_msg[i].base_e;
		if (addr >= base_s && addr < base_e) {
			reg = (u8 *)task->reg[i].data + (addr - base_s);
			break;
		}
	}

	return (u32 *)reg;
}

static int rkvenc_set_class_reg(struct rkvenc_task *task, u32 addr, u32 *data)
{
	int i;
	u32 base_s, base_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	for (i = 0; i < hw->reg_class; i++) {
		base_s = hw->reg_msg[i].base_s;
		base_e = hw->reg_msg[i].base_e;
		if (addr >= base_s && addr < base_e) {
			if ((addr - base_s) == 0)
				task->reg[i].data = data;
			break;
		}
	}
	return 0;
}


static int rkvenc_extract_task_msg(struct mpp_session *session,
				   struct rkvenc_task *task,
				   struct mpp_task_msgs *msgs, u32 kernel_space)
{
	u32 i, j;
	struct mpp_request *req;
	struct rkvenc_hw_info *hw = task->hw_info;

	mpp_debug_enter();

	for (i = 0; i < msgs->req_cnt; i++) {
		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			//void *data;
			struct mpp_request *wreq;

			for (j = 0; j < hw->reg_class; j++) {
				if (!req_over_class(req, task, j))
					continue;

				wreq = &task->w_reqs[task->w_req_cnt];
				rkvenc_update_req(task, j, req, wreq);
				rkvenc_set_class_reg(task, wreq->offset, wreq->data);
				task->reg[j].valid = 1;
				task->w_req_cnt++;
			}
		} break;
		case MPP_CMD_SET_REG_READ: {
			struct mpp_request *rreq;

			for (j = 0; j < hw->reg_class; j++) {
				if (!req_over_class(req, task, j))
					continue;

				rreq = &task->r_reqs[task->r_req_cnt];
				rkvenc_update_req(task, j, req, rreq);
				task->reg[j].valid = 1;
				task->r_req_cnt++;
			}
		} break;
		case MPP_CMD_SET_REG_ADDR_OFFSET: {
			mpp_extract_reg_offset_info(&task->off_inf, req);
		} break;
		default:
			break;
		}
	}
	mpp_debug(DEBUG_TASK_INFO, "w_req_cnt=%d, r_req_cnt=%d\n",
		  task->w_req_cnt, task->r_req_cnt);

	mpp_debug_enter();
	return 0;
}

static int rkvenc_task_get_format(struct mpp_dev *mpp,
				  struct rkvenc_task *task)
{
	u32 offset, val;

	struct rkvenc_hw_info *hw = task->hw_info;
	u32 class = hw->fmt_reg.class;
	u32 *class_reg = task->reg[class].data;
	u32 class_size = task->reg[class].size;
	u32 class_base = hw->reg_msg[class].base_s;
	u32 bitpos = hw->fmt_reg.bitpos;
	u32 bitlen = hw->fmt_reg.bitlen;

	if (!class_reg || !class_size)
		return -EINVAL;

	offset = hw->fmt_reg.base - class_base;
	val = class_reg[offset / sizeof(u32)];
	task->fmt = (val >> bitpos) & ((1 << bitlen) - 1);

	return 0;
}

static void *task_init(struct rkvenc_task *task)
{
	int i = 0;
	for (i = 0; i < RKVENC_CLASS_BUTT; i++)
		task->reg[i].valid = 0;
	task->irq_status = 0;
	task->w_req_cnt = 0;
	task->r_req_cnt = 0;
	return 0;
}

void rkvenc_dump_dbg(struct mpp_dev *mpp)
{
	u32 i;

	if (!unlikely(mpp_dev_debug & DEBUG_DUMP_ERR_REG))
		return;
	pr_info("=== %s ===\n", __func__);
	for (i = 0; i < RKVENC_CLASS_BUTT; i++) {
		u32 j, s, e;

		s = rkvenc_rv1106_hw_info.reg_msg[i].base_s;
		e = rkvenc_rv1106_hw_info.reg_msg[i].base_e;
		/* if fmt is jpeg, skip unused class */
#if 0
		if ((i == RKVENC_CLASS_RC) ||
		    (i == RKVENC_CLASS_PAR) ||
		    (i == RKVENC_CLASS_SQI))
			continue;
		if (i == RKVENC_CLASS_SCL)
			s = 0x2c80;
		if (i == RKVENC_CLASS_OSD)
			s = 0x3138;
#endif
		for (j = s; j <= e; j += 4)
			pr_info("reg[0x%0x] = 0x%08x\n", j, mpp_read(mpp, j));
	}
	pr_info("=== %s ===\n", __func__);
}

#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
#define VEPU_LINE_CNT_UNMARK	(~GENMASK(13, 0))
#define VEPU_LINE_CNT		GENMASK(13, 0)
#define VEPU_FRAME_CNT		GENMASK(21, 14)
#define VEPU_FRAME_CNT_OFF	(14)
#define VEPU_LDLY_REG		(0x18)
#define VEPU_DVBM_ID_REG	(0x308)
#define VEPU_DBG_WRK		(0x5004)
#define VEPU_DBG_POS0		(0x5008)
#define VEPU_DBG_POS1		(0x500c)

static int rkvenc_check_overflow(struct rkvenc_dev *enc, struct dvbm_isp_frm_info *info,
				 u32 cur_fcnt, u32 cur_lcnt)
{
	u32 pos0_y = mpp_read(&enc->mpp, VEPU_DBG_POS0) >> 16;
	u32 pos1_y = mpp_read(&enc->mpp, VEPU_DBG_POS1) >> 16;
	u32 vepu_wrk = mpp_read(&enc->mpp, VEPU_DBG_WRK);
	u32 vepu_lcnt = 16 * max(pos0_y, pos1_y);

	/* 1. check whether isp frame is cur enc frame or not */
	if (cur_fcnt != info->frame_cnt || cur_lcnt >= info->line_cnt) {
		/* 1.1 check overflow */
		if (info->line_cnt > 0 && cur_fcnt != info->frame_cnt) {
			if (info->line_cnt >= info->wrap_line) {
				pr_err("overflow vepu[frm: %d line: %d %d] isp[frm: %d line: %d]\n",
				       cur_fcnt, vepu_lcnt, cur_lcnt,
				       info->frame_cnt, info->line_cnt);
				enc->dvbm_overflow = 1;
			}
			rkvenc_dump_dbg(&enc->mpp);
		}
		/* 1.2 hack for isp interruption disorder */
		if (cur_lcnt < info->max_line_cnt) {
			u32 val = mpp_read(&enc->mpp, VEPU_LDLY_REG);

			val &= VEPU_LINE_CNT_UNMARK;
			val |= info->max_line_cnt;
			mpp_write(&enc->mpp, VEPU_LDLY_REG, val);
			wmb();
			pr_err("line cnt disorder set max %d [%d %d]->[%d %d]\n",
			       info->max_line_cnt, cur_fcnt, cur_lcnt, info->frame_cnt, info->line_cnt);
		}
		return 1;
	}

	/* check overflow */
	if (atomic_read(&enc->on_work)) {
		if ((info->line_cnt - vepu_lcnt) > info->wrap_line) {
			pr_err("overflow vepu[frm: %d line: %d %d] isp[frm: %d line: %d] wrk 0x%08x\n",
			       cur_fcnt, vepu_lcnt, cur_lcnt,
			       info->frame_cnt, info->line_cnt, vepu_wrk);
			enc->dvbm_overflow = 1;
		}
	} else {
		if (info->line_cnt > info->wrap_line) {
			pr_err("overflow vepu[frm: %d line: %d %d] isp[frm: %d line: %d] wrk 0x%08x\n",
			       cur_fcnt, vepu_lcnt, cur_lcnt,
			       info->frame_cnt, info->line_cnt, vepu_wrk);
			enc->dvbm_overflow = 1;
		}
	}

	return 0;
}

static int rkvenc_callback(void* ctx, enum dvbm_cb_event event, void* arg)
{
	struct rkvenc_dev *enc = (struct rkvenc_dev *)ctx;

	if (!enc)
		return 0;

	switch (event) {
	case DVBM_VEPU_REQ_CONNECT : {
		u32 connect = *(u32*)arg;
		unsigned long val = mpp_read(&enc->mpp, 0x18);

		if (!connect)
			clear_bit(24, &val);
		mpp_write(&enc->mpp, 0x18, val);
	} break;
	case DVBM_VEPU_NOTIFY_FRM_STR: {
		enc->frm_id_start = *(u32*)arg;
	} break;
	case DVBM_VEPU_NOTIFY_FRM_END: {
		enc->frm_id_end = *(u32*)arg;
	} break;
	case DVBM_VEPU_NOTIFY_FRM_INFO: {
		struct dvbm_isp_frm_info *info = (struct dvbm_isp_frm_info*)arg;
		u32 val;
		u32 cur_fcnt;
		u32 cur_lcnt;

		/* 1. get cur vepu frame info */
		val = mpp_read(&enc->mpp, VEPU_LDLY_REG);
		cur_fcnt = (val & VEPU_FRAME_CNT) >> VEPU_FRAME_CNT_OFF;
		cur_lcnt = val & VEPU_LINE_CNT;

		/* 2. check overflow */
		if (rkvenc_check_overflow(enc, info, cur_fcnt, cur_lcnt))
			return 0;
		/* 3. config frame info to vepu */
		val &= VEPU_LINE_CNT_UNMARK;
		val |= info->line_cnt;
		enc->line_cnt = info->line_cnt;
		mpp_write(&enc->mpp, VEPU_LDLY_REG, val);
		wmb();
		mpp_dbg_dvbm("frame cnt %d line cnt %d\n",
			     info->frame_cnt, info->line_cnt);
		if (val != mpp_read(&enc->mpp, VEPU_LDLY_REG)) {
			pr_err("set frame info failed! [%d %d] -> [%d %d]\n",
			       cur_fcnt, cur_lcnt, info->frame_cnt, info->line_cnt);
		}
	} break;
	case DVBM_VEPU_NOTIFY_DUMP: {
		rkvenc_dump_dbg(&enc->mpp);
	} break;
	default : {
	} break;
	}
	return 0;
}

static void update_online_info(struct mpp_dev *mpp)
{
	struct dvbm_isp_frm_info info;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	u32 val;
	u32 cur_fcnt;
	u32 cur_lcnt;

	rk_dvbm_ctrl(enc->port, DVBM_VEPU_GET_FRAME_INFO, &info);
	/* 1. get cur vepu frame info */
	val = mpp_read(&enc->mpp, VEPU_LDLY_REG);
	cur_fcnt = (val & VEPU_FRAME_CNT) >> VEPU_FRAME_CNT_OFF;
	cur_lcnt = val & VEPU_LINE_CNT;

	if (cur_lcnt >= info.line_cnt)
		return;
	/* 2. check overflow */
	if (rkvenc_check_overflow(enc, &info, cur_fcnt, cur_lcnt))
		return;
	/* 3. update infos */
	val &= (~GENMASK(21, 0));
	val |= ((info.frame_cnt << 14) | info.line_cnt);
	mpp_write(mpp, VEPU_LDLY_REG, val);

	val = mpp_read(mpp, VEPU_DVBM_ID_REG);
	val = (val >> 8) << 8;
	val |= info.frame_cnt;
	mpp_write(mpp, VEPU_DVBM_ID_REG, val);
	mpp_dbg_dvbm("%s frame cnt %d line cnt %d\n",
		     __func__, info.frame_cnt, info.line_cnt);
}
#endif

static void *rkvenc_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct rkvenc_task *task = NULL;
	struct mpp_task *mpp_task;
	struct mpp_dev *mpp = session->mpp;
	u32 i = 0;
	mpp_debug_enter();


	for (i = 0; i < MAX_TASK_CNT; i++) {
		task = (struct rkvenc_task *)session->task[i];
		if (!task->mpp_task.state)
			break;
	}

	if (!task)
		return NULL;

	task_init(task);
	mpp_task = &task->mpp_task;
	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
	mpp_task->clbk_en = 1;
	task->hw_info = to_rkvenc_info(mpp_task->hw_info);
	/* extract reqs for current task */
	ret = rkvenc_extract_task_msg(session, task, msgs, session->k_space);
	if (ret)
		goto free_task;
	mpp_task->reg = task->reg[0].data;
	/* get format */
	ret = rkvenc_task_get_format(mpp, task);
	if (ret)
		goto free_task;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		u32 i, j;
		int cnt;
		u32 off;
		const u16 *tbl;
		struct rkvenc_hw_info *hw = task->hw_info;

		for (i = 0; i < hw->fd_class; i++) {
			u32 class = hw->fd_reg[i].class;
			u32 fmt = hw->fd_reg[i].base_fmt + task->fmt;
			u32 *reg = task->reg[class].data;
			u32 ss = hw->reg_msg[class].base_s / sizeof(u32);

			if (!reg)
				continue;
			ret = mpp_translate_reg_address(session, mpp_task, fmt, reg, NULL);
			if (ret)
				goto fail;

			cnt = mpp->var->trans_info[fmt].count;
			tbl = mpp->var->trans_info[fmt].table;
			for (j = 0; j < cnt; j++) {
				off = mpp_query_reg_offset_info(&task->off_inf, tbl[j] + ss);
				mpp_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n", tbl[j] + ss, off);
				reg[tbl[j]] += off;
			}
		}
	}
	/* check rec fbc is disable */
	{
		u32 val = task->reg[RKVENC_CLASS_PIC].data[REC_FBC_DIS_CLASS_OFFSET];
		if (val & 0x80000000)
			mpp_task->clbk_en = 0;
	}

	/*if (session->k_space) {
		u32 i;
		struct rkvenc_hw_info *hw = task->hw_info;

		for (i = 0; i < hw->fd_class; i++) {
			u32 class = hw->fd_reg[i].class;
			u32 fmt = hw->fd_reg[i].base_fmt + task->fmt;
			u32 *reg = task->reg[class].data;
			mpp_get_dma_attach_mem_info(session, mpp_task, fmt, reg);
		}
	}*/
	task->clk_mode = CLK_MODE_NORMAL;

	mpp_debug_leave();

	return mpp_task;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	/* free class register buffer */
free_task:
	kfree(task);

	return NULL;
}

static int rkvenc_link_fill_table(struct rkvenc_link_dev *link,
				  struct rkvenc_task *task,
				  struct mpp_dma_buffer *table)
{
	u32 i, off, s, e, len, si, di;
	struct rkvenc_link_header *hdr;
	struct rkvenc_reg_msg *msg;
	u32 *tb_reg = (u32 *)table->vaddr;
	struct rkvenc_hw_info *hw = task->hw_info;
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct rkvenc2_session_priv *priv =
		(struct rkvenc2_session_priv *)task->mpp_task.session;
#endif
	//mpp_err("table->iova=%pad \n", &table->iova);
	/* set class data addr valid */
	hdr = (struct rkvenc_link_header *)table->vaddr;
	hdr->node_cfg.node_int = 1;
	// hdr->base_cfg.valid = task->reg[RKVENC_CLASS_BASE].valid;
	hdr->pic_cfg.valid = task->reg[RKVENC_CLASS_PIC].valid;
	hdr->rc_cfg.valid = task->reg[RKVENC_CLASS_RC].valid;
	hdr->param_cfg.valid = task->reg[RKVENC_CLASS_PAR].valid;
	hdr->sqi_cfg.valid = task->reg[RKVENC_CLASS_SQI].valid;
	hdr->scal_cfg.valid = task->reg[RKVENC_CLASS_SCL].valid;
	hdr->osd_cfg.valid = task->reg[RKVENC_CLASS_OSD].valid;
	hdr->status_cfg.valid = task->reg[RKVENC_CLASS_ST].valid;
	hdr->next_node.valid = 1;
	// mpp_err("hdr->base_cfg.valid=%d\n", hdr->base_cfg.valid);
	// mpp_err("hdr->pic_cfg.valid=%d\n", hdr->pic_cfg.valid);
	// mpp_err("hdr->rc_cfg.valid=%d\n", hdr->rc_cfg.valid);
	// mpp_err("hdr->param_cfg.valid=%d\n", hdr->param_cfg.valid);
	// mpp_err("hdr->sqi_cfg.valid=%d\n", hdr->sqi_cfg.valid);
	// mpp_err("hdr->scal_cfg.valid=%d\n", hdr->scal_cfg.valid);
	// mpp_err("hdr->osd_cfg.valid=%d\n", hdr->osd_cfg.valid);
	// mpp_err("hdr->status_cfg.valid=%d\n", hdr->status_cfg.valid);
	// mpp_err("hdr->next_node.valid=%d\n", hdr->next_node.valid);

	/* set register data */
	for (i = 0; i < hw->reg_class; i++) {
		msg = &hw->reg_msg[i];
		//mpp_err("class=%d, link_len=%d, reg_valid=%d, reg_size=%d\n",
		//	i, msg->link_len, task->reg[i].valid, task->reg[i].size);
		if (!msg->link_len || !task->reg[i].valid)
			continue;
		off = link->class_off[i];
		s = msg->link_s ? msg->link_s : msg->base_s;
		e = msg->link_e ? msg->link_e : msg->base_e;
		len = e - s + sizeof(u32);

		di = off / sizeof(u32);
		si = (s - msg->base_s) / sizeof(u32);
		//mpp_err("off=%d, s=%08x, e=%08x, len=%d, di=%d, si=%d, size=%d\n",
		//	off, s, e, len, di, si, task->reg[i].size);
		memcpy(&tb_reg[di], &task->reg[i].data[si], len);
	}
	/* memset status regs for read */
	msg = &hw->reg_msg[RKVENC_CLASS_ST];
	di = link->class_off[RKVENC_CLASS_ST] / sizeof(u32);
	memset(&tb_reg[di], 0, msg->link_len);

#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	{
		u32 dvbm_cfg = task->reg[RKVENC_CLASS_CTL].data[24];
		struct rkvenc_dev *enc = link->enc;

		di = link->class_off[RKVENC_CLASS_PIC] / sizeof(u32);
		if (dvbm_cfg) {
			priv->dvbm_en = dvbm_cfg;
			rk_dvbm_link(enc->port);
		}
	}
#endif
	dma_sync_single_for_device(link->enc->mpp.dev, table->iova,
				   table->size, DMA_FROM_DEVICE);
	dma_sync_single_for_cpu(link->enc->mpp.dev, table->iova,
				table->size, DMA_FROM_DEVICE);
	return 0;
}

static void *rkvenc_prepare(struct mpp_dev *mpp,
			    struct mpp_task *mpp_task)
{
	struct mpp_task *out_task = NULL;
	struct mpp_taskqueue *queue = mpp->queue;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	if (atomic_read(&mpp->reset_request) > 0)
		goto done;
	if (atomic_read(&mpp_task->session->release_request) > 0)
		goto done;

	switch (enc->link_mode) {
	case RKVENC_MODE_ONEFRAME: {
		unsigned long flags;
		spin_lock_irqsave(&queue->running_lock, flags);
		out_task = list_empty(&queue->running_list) ? mpp_task : NULL;
		spin_unlock_irqrestore(&queue->running_lock, flags);
	} break;
	case RKVENC_MODE_LINK_ONEFRAME: {
		struct mpp_dma_buffer *table;
		struct rkvenc_link_dev *link = enc->link;
		unsigned long flags;

		spin_lock_irqsave(&queue->running_lock, flags);
		out_task = list_empty(&queue->running_list) ? mpp_task : NULL;
		spin_unlock_irqrestore(&queue->running_lock, flags);
		if (!out_task)
			goto done;

		/* fill task entry */
		mutex_lock(&link->list_mutex);
		table = list_first_entry_or_null(&link->unused_list,
						 struct mpp_dma_buffer, link);
		if (table) {
			rkvenc_link_fill_table(link, task, table);
			// mutex_lock(&link->list_mutex);
			list_move_tail(&table->link, &link->used_list);
			// mutex_unlock(&link->list_mutex);
			task->table = table;
			out_task = mpp_task;
		}
		mutex_unlock(&link->list_mutex);
	} break;
	case RKVENC_MODE_LINK_ADD: {
		struct mpp_dma_buffer *table;
		struct rkvenc_link_dev *link = enc->link;

		if (test_bit(TASK_STATE_LINK_FILLED, &mpp_task->state)) {
			out_task = mpp_task;
			mpp_err("xxxx task filled, mpp_state=%lx\n", mpp_task->state);
			goto done;
		}
		/* fill task entry */
		mutex_lock(&link->list_mutex);
		table = list_first_entry_or_null(&link->unused_list,
						 struct mpp_dma_buffer, link);
		//mpp_err("table=%px\n", table);
		if (table) {
			rkvenc_link_fill_table(link, task, table);
			// mutex_lock(&link->list_mutex);
			list_move_tail(&table->link, &link->used_list);
			// mutex_unlock(&link->list_mutex);
			task->table = table;
			out_task = mpp_task;
			set_bit(TASK_STATE_LINK_FILLED, &mpp_task->state);
		}
		mutex_unlock(&link->list_mutex);
	} break;
	default:
		break;
	}
done:
	mpp_debug_leave();

	return out_task;
}

static int rkvenc_run_start_link(struct mpp_dev *mpp,
				 struct rkvenc_task *task)
{
	u32 j, *regs;
	u32 base_s, base_e, s, e;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_hw_info *hw = enc->hw_info;

	/* inital hardware */
	base_s = hw->reg_msg[RKVENC_CLASS_CTL].base_s;
	base_e = hw->reg_msg[RKVENC_CLASS_CTL].base_e;
	s = base_s / sizeof(u32);
	e = base_e / sizeof(u32);
	regs = (u32 *)task->reg[RKVENC_CLASS_CTL].data;
	//mpp_err("s=%d, e=%d\n", s, e);
	for (j = s; j <= e; j++) {
		if (j == hw->hw.reg_en)
			continue;
		mpp_write(mpp, j * sizeof(u32), regs[j]);
	}

	/* set interrupt enable */
	mpp_write(mpp, hw->int_en_base, 0xffff);
	/* set interrupt enable */
	// mpp_write_relaxed(mpp, hw->int_mask_base, 0x3FF);
	/* set link start addr */
	mpp_write(mpp, hw->link_addr_base, task->table->iova);

	return 0;
}

static int rkvenc_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);
	struct rkvenc_hw_info *hw = enc->hw_info;
	struct rkvenc2_session_priv *priv = mpp_task->session->priv;

	mpp_debug_enter();

	enc->line_cnt = 0;
	switch (enc->link_mode) {
	case RKVENC_MODE_ONEFRAME: {
		u32 i, j;
		u32 enc_start_val = 0;
		struct mpp_request msg;
		struct mpp_request *req;
		u32 dvbm_en = 0;
		u32 st_ppl = 0;

		for (i = 0; i < task->w_req_cnt; i++) {
			int ret;
			u32 s, e, off;
			u32 *regs;

			req = &task->w_reqs[i];
			ret = rkvenc_get_class_msg(task, req->offset, &msg);
			if (ret)
				return -EINVAL;

			s = (req->offset - msg.offset) / sizeof(u32);
			e = s + req->size / sizeof(u32);
			regs = (u32 *)msg.data;
			for (j = s; j < e; j++) {
				off = msg.offset + j * sizeof(u32);
				if (off == hw->enc_start_base) {
					enc_start_val = regs[j];
					continue;
				}
				if (off == hw->dvbm_cfg)
					dvbm_en = regs[j];
				else
					mpp_write_relaxed(mpp, off, regs[j]);
			}
		}

		/* init current task */
		mpp->cur_task = mpp_task;

		mpp_debug(DEBUG_RUN, "%s session %d task %d enc_pic %d jpeg_cfg %08x dvbm_en %d\n",
			  __func__, mpp_task->session->index, mpp_task->task_index, task->fmt, mpp_read(mpp, 0x47c), dvbm_en);

		/* check enc status before start */
		st_ppl = mpp_read(mpp, 0x5004);

		if (st_ppl & BIT(10))
			mpp_err("enc started status %08x\n", st_ppl);
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
		if (dvbm_en) {
			enc->dvbm_overflow = 0;
			rk_dvbm_link(enc->port);
			update_online_info(mpp);
			priv->dvbm_link = 1;
			mpp_write_relaxed(mpp, hw->dvbm_cfg, dvbm_en);
		}
		priv->dvbm_en = dvbm_en;
#endif
		/* Flush the register before the start the device */
		wmb();
		if (!dvbm_en)
			mpp_write(mpp, hw->enc_start_base, enc_start_val);
		atomic_set(&enc->on_work, 1);
	} break;
	case RKVENC_MODE_LINK_ONEFRAME: {
		atomic_set(&enc->link_task_cnt, 0);
		rkvenc_run_start_link(mpp, task);
		task->task_no = atomic_inc_return(&enc->link_task_cnt);
		/* init current task */
		mpp->cur_task = mpp_task;
		wmb();
		/* link mode and one frame */
		if (enc->link_run)
			mpp_write(mpp, hw->enc_start_base, 0x201);
	} break;
	case RKVENC_MODE_LINK_ADD: {
		u32 link_status = mpp_read(mpp, hw->link_status_base);
		//struct rkvenc_link_status *status = (struct rkvenc_link_status *)&link_status;

		//mpp_err("num_cfg_done=%d, num_cfg=%d, num_int=%d, num_enc_done=%d\n",
		//	status->cfg_done_num, status->cfg_num, status->int_num, status->enc_num);
		if (!link_status) {
			atomic_set(&enc->link_task_cnt, 0);
			rkvenc_run_start_link(mpp, task);
			task->task_no = atomic_inc_return(&enc->link_task_cnt);
			wmb();
			/* link start mode */
			if (enc->link_run)
				mpp_write(mpp, hw->enc_start_base, 0x201);
		} else {
			task->task_no = atomic_inc_return(&enc->link_task_cnt);
			wmb();
			/* link add mode */
			if (enc->link_run)
				mpp_write(mpp, hw->enc_start_base, 0x301);
		}
	} break;
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvenc_check_bs_overflow(struct mpp_dev *mpp)
{
	u32 w_adr, r_adr;
	int ret = 0;

	if (!(mpp->irq_status & RKVENC_ENC_DONE_STATUS)) {
		if (mpp->irq_status & RKVENC_JPEG_OVERFLOW) {
			/* the w/r address need to be read in reversed*/
			r_adr = mpp_read(mpp, RKVENC_JPEG_BSBS);
			w_adr = mpp_read(mpp, RKVENC_JPEG_BSBR);
			mpp_write(mpp, RKVENC_JPEG_BSBS, w_adr + 16);
			mpp_write(mpp, RKVENC_JPEG_BSBR, r_adr + 0xc);
			mpp->overflow_status = mpp->irq_status;
			pr_err("jpeg overflow\n");
			ret = 1;
		}
		if (mpp->irq_status & RKVENC_VIDEO_OVERFLOW) {
			w_adr = mpp_read(mpp, RKVENC_VIDEO_BSBS);
			r_adr = mpp_read(mpp, RKVENC_VIDEO_BSBR);
			mpp_write(mpp, RKVENC_VIDEO_BSBS, w_adr + 16);
			mpp_write(mpp, RKVENC_VIDEO_BSBR, r_adr + 0xc);
			mpp->overflow_status = mpp->irq_status;
			pr_err("video overflow\n");
			ret = 1;
		}
	}
	return ret;
}

static void rkvenc_clear_dvbm_info(struct mpp_dev *mpp)
{
	u32 dvbm_info, dvbm_en;

	mpp_write(mpp, 0x60, 0);
	mpp_write(mpp, 0x18, 0);
	dvbm_info = mpp_read(mpp, 0x18);
	dvbm_en = mpp_read(mpp, 0x60);
	if (dvbm_info || dvbm_en)
		pr_err("clear dvbm info failed 0x%08x 0x%08x\n",
		       dvbm_info, dvbm_en);
}

static int rkvenc_irq(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_hw_info *hw = enc->hw_info;

	mpp_debug_enter();

	mpp->irq_status = mpp_read(mpp, hw->int_sta_base);
	//pr_info("irq_status: %s %08x\n", __func__, (u32)mpp->irq_status);
	if (!mpp->irq_status)
		return IRQ_NONE;
	mpp_write(mpp, hw->int_mask_base, 0x100);
	mpp_write(mpp, hw->int_clr_base, mpp->irq_status);
	mpp_write(mpp, hw->int_sta_base, 0);
	if (mpp->irq_status == RKVENC_DVBM_DISCONNECT)
		return IRQ_HANDLED;
	if (rkvenc_check_bs_overflow(mpp))
		return IRQ_HANDLED;
	enc->line_cnt = mpp_read(mpp, 0x18) & 0x3fff;
	rkvenc_clear_dvbm_info(mpp);
	mpp_debug_leave();

	return IRQ_WAKE_THREAD;
}

static int rkvenc_link_set_int_sta(struct rkvenc_link_dev *link,
				   struct rkvenc_task *task,
				   u32 irq_status)
{
	u32 *tb_reg;
	u32 int_sta_off;
	struct rkvenc_hw_info *hw = task->hw_info;

	tb_reg = (u32 *)task->table->vaddr;
	int_sta_off = link->class_off[RKVENC_CLASS_CTL] + hw->int_sta_base;
	tb_reg[int_sta_off / sizeof(u32)] = irq_status;

	return 0;
}

static int rkvenc_link_isr(struct mpp_dev *mpp,
			   struct rkvenc_dev *enc,
			   struct rkvenc_link_dev *link)
{
	struct mpp_task *mpp_task = NULL, *n;
	struct rkvenc_task *task = NULL;
	struct rkvenc_link_status *status;
	struct rkvenc_hw_info *hw = enc->hw_info;
	struct rkvenc2_session_priv *priv =
		(struct rkvenc2_session_priv *)task->mpp_task.session;

	//u32 node_iova = mpp_read_relaxed(mpp, hw->link_node_base);
	//u32 irq_status = mpp_read_relaxed(mpp, hw->int_sta_base);
	u32 link_status = mpp_read_relaxed(mpp, hw->link_status_base);

	status = (struct rkvenc_link_status *)&link_status;
	//mpp_err("node_iova=%08x, mpp->irq_status=%08x, irq_status=%08x, num_cfg_done=%d, num_cfg=%d, num_int=%d, num_enc_done=%d\n",
	//	node_iova, (u32)mpp->irq_status, irq_status, status->cfg_done_num, status->cfg_num, status->int_num, status->enc_num);

	/* deal with the task done */
	// mutex_lock(&mpp->queue->running_lock);
	list_for_each_entry_safe(mpp_task, n,
				 &mpp->queue->running_list,
				 queue_link) {
		int task_diff;

		task = to_rkvenc_task(mpp_task);
		task_diff = (task->task_no + 256 - status->enc_num) % 256;
		//mpp_err("task->task_no=%d, enc_num=%d, task_diff=%d\n", task->task_no, status->enc_num, task_diff);
		if (task_diff > 0)
			break;

		//mpp_err("cancle_delayed_work %px\n", mpp_task);
		cancel_delayed_work(&mpp_task->timeout_work);
		mpp_time_diff(mpp_task);
		/* set task done ready */
		task->irq_status = mpp->irq_status;
		mpp_debug(DEBUG_IRQ_STATUS, "link_irq_status: %08x\n", task->irq_status);
		rkvenc_link_set_int_sta(link, task, task->irq_status);
		if (!atomic_read(&mpp_task->abort_request)) {
			set_bit(TASK_STATE_DONE, &mpp_task->state);
			/* Wake up the GET thread */
			wake_up(&mpp_task->wait);
		}
		mutex_lock(&link->list_mutex);
		list_move_tail(&task->table->link, &link->unused_list);
		mutex_unlock(&link->list_mutex);

		set_bit(TASK_STATE_FINISH, &mpp_task->state);
		list_del_init(&mpp_task->queue_link);
		kref_put(&mpp_task->ref, mpp_free_task);
	}
	//mpp_err("list_empty=%d\n", list_empty(&mpp->queue->running_list));
	if (list_empty(&mpp->queue->running_list) && priv->dvbm_en) {
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
		rk_dvbm_unlink(enc->port);
#endif
		if (mpp->irq_status & hw->err_mask)
			rkvenc_reset(mpp);
	}
#if 0
	/* when meet error which cannot continue, hardware will stop */
	if (mpp->irq_status & hw->link_stop_mask) {
		list_for_each_entry_safe(mpp_task, n,
					 &mpp->queue->running_list,
					 queue_link) {
			int task_diff;

			task = to_rkvenc_task(mpp_task);
			//mpp_err("cancle_delayed_work %px\n", mpp_task);
			cancel_delayed_work(&mpp_task->timeout_work);
			task_diff = (task->task_no + 256 - status->enc_num) % 256;
			//mpp_err("task->task_no=%d, enc_num=%d, task_diff=%d\n", task->task_no, status->enc_num, task_diff);
			if (task_diff > 0) {
				clear_bit(TASK_STATE_HANDLE, &mpp_task->state);
				clear_bit(TASK_STATE_IRQ, &mpp_task->state);
			} else {
				task->irq_status = mpp->irq_status; 2
				rkvenc_link_set_int_sta(link, task, task->irq_status);
				if (!atomic_read(&mpp_task->abort_request)) {
					set_bit(TASK_STATE_DONE, &mpp_task->state);
					/* Wake up the GET thread */
					wake_up(&mpp_task->wait);
				}
				mutex_lock(&link->list_mutex);
				list_move_tail(&task->table->link, &link->unused_list);
				mutex_unlock(&link->list_mutex);

				set_bit(TASK_STATE_FINISH, &mpp_task->state);
				list_del_init(&mpp_task->queue_link);
				kref_put(&mpp_task->ref, mpp_free_task);
			}
		}
		/* reset hardware */
		atomic_inc(&mpp->reset_request);
		mpp_dev_reset(mpp);
	}
	//mpp_err("list_empty=%d\n", list_empty(&mpp->queue->running_list));
#endif
	// mutex_unlock(&mpp->queue->running_lock);

	mpp_debug_leave();
	return 0;
}

static int rkvenc_isr(struct mpp_dev *mpp)
{
	struct rkvenc_task *task = NULL;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_debug_enter();

	switch (enc->link_mode) {
	case RKVENC_MODE_ONEFRAME: {
		struct mpp_task *mpp_task = mpp->cur_task;
		struct rkvenc2_session_priv *priv = mpp_task->session->priv;

		if (!mpp_task)
			return IRQ_HANDLED;

		mpp_time_diff(mpp_task);
		mpp->cur_task = NULL;
		task = to_rkvenc_task(mpp_task);

		if (priv->dvbm_en) {
			/*
			* Workaround:
			* The line cnt is updated in the mcu.
			* When line cnt is set to max value 0x3fff,
			* the cur frame has overflow.
			*/
			if (enc->line_cnt == 0x3fff) {
				enc->dvbm_overflow = 1;
				dev_err(mpp->dev, "current frame has overflow\n");
			}
			if (enc->dvbm_overflow) {
				mpp->irq_status |= BIT(6);
				enc->dvbm_overflow = 0;
			}
		}
		task->irq_status = (mpp->irq_status | mpp->overflow_status);
		mpp->overflow_status = 0;
		mpp_debug(DEBUG_IRQ_STATUS, "task %d fmt %d dvbm_en %d irq_status 0x%08x\n",
			  mpp_task->task_index, task->fmt, priv->dvbm_en, task->irq_status);

		if (mpp->irq_status & enc->hw_info->err_mask) {
			dev_err(mpp->dev, "task %d fmt %d dvbm_en %d irq_status 0x%08x\n",
				mpp_task->task_index, task->fmt, priv->dvbm_en, task->irq_status);
			atomic_inc(&mpp->reset_request);
			/* dump register */
			if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
				mpp_debug(DEBUG_DUMP_ERR_REG, "irq_status: %08x\n",
					  task->irq_status);
				mpp_task_dump_hw_reg(mpp);
			}
		}
		mpp_task_finish(mpp_task->session, mpp_task);
	} break;
	case RKVENC_MODE_LINK_ONEFRAME: {
		struct mpp_task *mpp_task = mpp->cur_task;

		/* FIXME use a spin lock here */
		if (!mpp_task) {
			dev_err(mpp->dev, "no current task\n");
			return IRQ_HANDLED;
		}

		mpp_time_diff(mpp_task);
		mpp->cur_task = NULL;
		task = to_rkvenc_task(mpp_task);
		task->irq_status = mpp->irq_status;
		mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);

		if (task->irq_status & enc->hw_info->err_mask) {
			atomic_inc(&mpp->reset_request);
			/* dump register */
			if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
				mpp_debug(DEBUG_DUMP_ERR_REG, "irq_status: %08x\n",
					  task->irq_status);
				// mpp_task_dump_hw_reg(mpp);
			}
		}
		mpp_task_finish(mpp_task->session, mpp_task);
		/* move link */
		mutex_lock(&enc->link->list_mutex);
		list_move_tail(&task->table->link, &enc->link->unused_list);
		mutex_unlock(&enc->link->list_mutex);
	} break;
	case RKVENC_MODE_LINK_ADD: {
		if (mpp->irq_status & 0x100 || mpp->dump_regs)
			rkvenc_dump_dbg(mpp);
		rkvenc_link_isr(mpp, enc, enc->link);
	} break;
	default:
		break;
	}

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int rkvenc_finish(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct rkvenc2_session_priv *priv = mpp_task->session->priv;
#endif

	mpp_debug_enter();

	switch (enc->link_mode) {
	case RKVENC_MODE_ONEFRAME:
	case RKVENC_MODE_LINK_ONEFRAME: {
		u32 i, j;
		u32 *reg;
		struct mpp_request *req;

		if (mpp->irq_status & 0x100 || mpp->dump_regs)
			rkvenc_dump_dbg(mpp);

		for (i = 0; i < task->r_req_cnt; i++) {
			u32 off;

			req = &task->r_reqs[i];
			reg = (u32 *)req->data;
			for (j = 0; j < req->size / sizeof(u32); j++) {
				off =  req->offset + j * sizeof(u32);
				reg[j] = mpp_read_relaxed(mpp, off);
				if (off == task->hw_info->int_sta_base)
					reg[j] = task->irq_status;
			}
		}
		/* revert hack for irq status */
		reg = rkvenc_get_class_reg(task, task->hw_info->int_sta_base);
		if (reg)
			*reg = task->irq_status;
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
		if (priv->dvbm_en)
			rk_dvbm_unlink(enc->port);
#endif
	} break;
	default:
		break;
	}
	atomic_set(&enc->on_work, 0);

	mpp_debug_leave();

	return 0;
}

static u32 *rkvenc_link_get_class_reg(struct rkvenc_link_dev *link,
				      struct rkvenc_task *task, u32 addr)
{
	int i;
	u8 *reg = NULL;
	u32 off, base_s, base_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	for (i = 0; i < hw->reg_class; i++) {
		base_s = hw->reg_msg[i].base_s;
		base_e = hw->reg_msg[i].base_e;
		if (addr >= base_s && addr < base_e) {
			off = link->class_off[i] + addr - base_s;
			reg = (u8 *)task->table->vaddr + off;
			break;
		}
	}

	return (u32 *)reg;
}

static int rkvenc_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	switch (enc->link_mode) {
	case RKVENC_MODE_ONEFRAME:
	case RKVENC_MODE_LINK_ONEFRAME: {
	} break;
	case RKVENC_MODE_LINK_ADD: {
		u32 i;
		u32 *reg;
		struct mpp_request *req;

		for (i = 0; i < task->r_req_cnt; i++) {
			req = &task->r_reqs[i];
			reg = rkvenc_link_get_class_reg(enc->link, task, req->offset);
			if (!reg)
				return -EINVAL;
			if (!mpp_task->session->k_space) {
				if (copy_to_user(req->data, reg, req->size)) {
					mpp_err("copy_to_user reg fail\n");
					return -EIO;
				}
			} else
				memcpy(req->data, (u8 *)reg, req->size);
		}
	} break;
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvenc_free_task(struct mpp_session *session, struct mpp_task *mpp_task)
{
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	rkvenc_invalid_class_msg(task);
	return 0;
}

static int rkvenc_control(struct mpp_session *session, struct mpp_request *req)
{
	switch (req->cmd) {
	case MPP_CMD_SEND_CODEC_INFO: {
		int i;
		int cnt;
		struct codec_info_elem elem;
		struct rkvenc2_session_priv *priv;

		if (!session || !session->priv) {
			mpp_err("session info null\n");
			return -EINVAL;
		}
		priv = session->priv;

		cnt = req->size / sizeof(elem);
		cnt = (cnt > ENC_INFO_BUTT) ? ENC_INFO_BUTT : cnt;
		mpp_debug(DEBUG_IOCTL, "codec info count %d\n", cnt);
		for (i = 0; i < cnt; i++) {
			if (copy_from_user(&elem, req->data + i * sizeof(elem), sizeof(elem))) {
				mpp_err("copy_from_user failed\n");
				continue;
			}
			if (elem.type > ENC_INFO_BASE && elem.type < ENC_INFO_BUTT &&
			    elem.flag > CODEC_INFO_FLAG_NULL && elem.flag < CODEC_INFO_FLAG_BUTT) {
				elem.type = array_index_nospec(elem.type, ENC_INFO_BUTT);
				priv->codec_info[elem.type].flag = elem.flag;
				priv->codec_info[elem.type].val = elem.data;
			} else {
				mpp_err("codec info invalid, type %d, flag %d\n",
					elem.type, elem.flag);
			}
		}
	} break;
	default: {
		mpp_err("unknown mpp ioctl cmd %x\n", req->cmd);
	} break;
	}

	return 0;
}

static int rkvenc_free_session(struct mpp_session *session)
{
	if (session) {
		u32 i = 0;
		for (i = 0 ; i < MAX_TASK_CNT; i++) {
			struct rkvenc_task *task = (struct rkvenc_task *)session->task[i];
			if (task) {
				rkvenc_invalid_class_msg(task);
				kfree(task);
			}
		}
	}
	if (session) {
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
		struct rkvenc2_session_priv *priv = (struct rkvenc2_session_priv *)session->priv;
		struct rkvenc_dev *enc = to_rkvenc_dev(session->mpp);

		mpp_power_on(session->mpp);
		if (priv->dvbm_link) {
			rk_dvbm_unlink(enc->port);
			priv->dvbm_link = 0;
		}
		mpp_power_off(session->mpp);
#endif
	}
	if (session && session->priv) {
		kfree(session->priv);
		session->priv = NULL;
	}

	return 0;
}

static int rkvenc_init_session(struct mpp_session *session)
{
	struct rkvenc2_session_priv *priv;
	struct mpp_dev *mpp = NULL;
	struct rkvenc_task *task = NULL;
	int j = 0, ret = 0;
	u32 i = 0;

	if (!session) {
		mpp_err("session is null\n");
		return -EINVAL;
	}

	mpp = session->mpp;
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_rwsem(&priv->rw_sem);
	session->priv = priv;

	for (i = 0; i < MAX_TASK_CNT; i++) {
		task = kzalloc(sizeof(*task), GFP_KERNEL);
		if (!task)
			goto fail;

		task->hw_info = to_rkvenc_info(mpp->var->hw_info);
		{
			struct rkvenc_hw_info *hw = task->hw_info;

			for (j = 0; j < hw->reg_class; j++) {
				ret = rkvenc_alloc_class_msg(task, j);
				if (ret)
					goto fail;
			}
		}
		session->task[i] = task;
	}
	return 0;

fail:
	if (session->priv)
		kfree(session->priv);
	if (task) {
		rkvenc_invalid_class_msg(task);
		kfree(task);
	}
	for (i = 0 ; i < MAX_TASK_CNT; i++) {
		struct rkvenc_task *task = (struct rkvenc_task *)session->task[i];
		if (task) {
			rkvenc_invalid_class_msg(task);
			kfree(task);
		}
	}
	return -ENOMEM;
}

#ifdef CONFIG_PROC_FS
static int rkvenc_procfs_remove(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->procfs) {
		proc_remove(enc->procfs);
		enc->procfs = NULL;
	}

	return 0;
}

static int rkvenc_dump_session(struct mpp_session *session, struct seq_file *seq)
{
	int i;
	struct rkvenc2_session_priv *priv = session->priv;

	down_read(&priv->rw_sem);
	/* item name */
	seq_puts(seq, "------------------------------------------------------");
	seq_puts(seq, "------------------------------------------------------\n");
	seq_printf(seq, "|%8s|", (const char *)"session");
	seq_printf(seq, "%8s|", (const char *)"device");
	for (i = ENC_INFO_BASE; i < ENC_INFO_BUTT; i++) {
		bool show = priv->codec_info[i].flag;

		if (show)
			seq_printf(seq, "%8s|", enc_info_item_name[i]);
	}
	seq_puts(seq, "\n");
	/* item data*/
	seq_printf(seq, "|%8p|", session);
	seq_printf(seq, "%8s|", mpp_device_name[session->device_type]);
	for (i = ENC_INFO_BASE; i < ENC_INFO_BUTT; i++) {
		u32 flag = priv->codec_info[i].flag;

		if (!flag)
			continue;
		if (flag == CODEC_INFO_FLAG_NUMBER) {
			u32 data = priv->codec_info[i].val;

			seq_printf(seq, "%8d|", data);
		} else if (flag == CODEC_INFO_FLAG_STRING) {
			const char *name = (const char *)&priv->codec_info[i].val;

			seq_printf(seq, "%8s|", name);
		} else
			seq_printf(seq, "%8s|", (const char *)"null");
	}
	seq_puts(seq, "\n");
	up_read(&priv->rw_sem);

	return 0;
}

static int rkvenc_show_session_info(struct seq_file *seq, void *offset)
{
	struct mpp_session *session = NULL, *n;
	struct mpp_dev *mpp = seq->private;

	mutex_lock(&mpp->srv->session_lock);
	list_for_each_entry_safe(session, n,
				 &mpp->srv->session_list,
				 session_link) {
		if (session->device_type != MPP_DEVICE_RKVENC)
			continue;
		if (!session->priv)
			continue;
		if (mpp->dev_ops->dump_session)
			mpp->dev_ops->dump_session(session, seq);
	}
	mutex_unlock(&mpp->srv->session_lock);

	return 0;
}

static int rkvenc_procfs_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	char name[32];

	if (!mpp->dev || !mpp->dev->of_node || !mpp->dev->of_node->name ||
	    !mpp->srv || !mpp->srv->procfs)
		return -EINVAL;

	snprintf(name, sizeof(name) - 1, "%s%d",
		 mpp->dev->of_node->name, mpp->core_id);

	enc->procfs = proc_mkdir(name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(enc->procfs)) {
		mpp_err("failed on open procfs\n");
		enc->procfs = NULL;
		return -EIO;
	}
	/* for debug */
	mpp_procfs_create_u32("aclk", 0644,
			      enc->procfs, &enc->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_core", 0644,
			      enc->procfs, &enc->core_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      enc->procfs, &mpp->session_max_buffers);
	mpp_procfs_create_u32("dump_regs", 0644,
			      enc->procfs, &mpp->dump_regs);
	/* for show session info */
	proc_create_single_data("sessions-info", 0444,
				enc->procfs, rkvenc_show_session_info, mpp);
	mpp_procfs_create_u32("link_mode", 0644,
			      enc->procfs, &enc->link_mode);

	return 0;
}

#else
static inline int rkvenc_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvenc_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvenc_dump_session(struct mpp_session *session, struct seq_file *seq)
{
	return 0;
}
#endif

static int rkvenc_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	int ret = 0;

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_RKVENC];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &enc->aclk_info, "aclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get aclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &enc->hclk_info, "hclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get hclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &enc->core_clk_info, "clk_core");
	if (ret)
		mpp_err("failed on clk_get clk_core\n");
	/* Get normal max workload from dtsi */
	of_property_read_u32(mpp->dev->of_node,
			     "rockchip,default-max-load",
			     &enc->default_max_load);
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&enc->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);
	mpp_set_clk_info_rate_hz(&enc->core_clk_info, CLK_MODE_DEFAULT, 600 * MHZ);

	/* Get reset control from dtsi */
	enc->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "video_a");
	if (!enc->rst_a)
		mpp_err("No aclk reset resource define\n");
	enc->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "video_h");
	if (!enc->rst_h)
		mpp_err("No hclk reset resource define\n");
	enc->rst_core = mpp_reset_control_get(mpp, RST_TYPE_CORE, "video_core");
	if (!enc->rst_core)
		mpp_err("No core reset resource define\n");

	return 0;
}

static int rkvenc_reset(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_hw_info *hw = enc->hw_info;

	mpp_debug_enter();
	/* safe reset */
	mpp_write(mpp, hw->int_mask_base, 0x3FF);
	mpp_write(mpp, hw->enc_clr_base, 0x1);
	udelay(5);
	mpp_write(mpp, hw->enc_clr_base, 0x3);
	mpp_write(mpp, hw->int_clr_base, 0xffffffff);
	mpp_write(mpp, hw->int_sta_base, 0);
	mpp_write(mpp, hw->enc_clr_base, 0);

	/* cru reset */
	if (enc->rst_a && enc->rst_h && enc->rst_core) {
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(enc->rst_a);
		mpp_safe_reset(enc->rst_h);
		mpp_safe_reset(enc->rst_core);
		udelay(5);
		mpp_safe_unreset(enc->rst_a);
		mpp_safe_unreset(enc->rst_h);
		mpp_safe_unreset(enc->rst_core);
		mpp_pmu_idle_request(mpp, false);
	}
	rkvenc_clear_dvbm_info(mpp);

	mpp_debug_leave();

	return 0;
}

static int rkvenc_clk_on(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_clk_safe_enable(enc->aclk_info.clk);
	mpp_clk_safe_enable(enc->hclk_info.clk);
	mpp_clk_safe_enable(enc->core_clk_info.clk);

	return 0;
}

static int rkvenc_clk_off(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	clk_disable_unprepare(enc->aclk_info.clk);
	clk_disable_unprepare(enc->hclk_info.clk);
	clk_disable_unprepare(enc->core_clk_info.clk);

	return 0;
}

static int rkvenc_set_freq(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_clk_set_rate(&enc->aclk_info, task->clk_mode);
	mpp_clk_set_rate(&enc->core_clk_info, task->clk_mode);

	return 0;
}

static struct mpp_hw_ops rkvenc_hw_ops = {
	.init = rkvenc_init,
	.clk_on = rkvenc_clk_on,
	.clk_off = rkvenc_clk_off,
	.set_freq = rkvenc_set_freq,
	.reset = rkvenc_reset,
};

static struct mpp_dev_ops rkvenc_dev_ops_v2 = {
	.alloc_task = rkvenc_alloc_task,
	.prepare = rkvenc_prepare,
	.run = rkvenc_run,
	.irq = rkvenc_irq,
	.isr = rkvenc_isr,
	.finish = rkvenc_finish,
	.result = rkvenc_result,
	.free_task = rkvenc_free_task,
	.ioctl = rkvenc_control,
	.init_session = rkvenc_init_session,
	.free_session = rkvenc_free_session,
	.dump_session = rkvenc_dump_session,
};

static const struct mpp_dev_var rkvenc_rv1106_data = {
	.device_type = MPP_DEVICE_RKVENC,
	.hw_info = &rkvenc_rv1106_hw_info.hw,
	.trans_info = trans_rkvenc_rv1106,
	.hw_ops = &rkvenc_hw_ops,
	.dev_ops = &rkvenc_dev_ops_v2,
};

static const struct of_device_id mpp_rkvenc_dt_match[] = {
	{
		.compatible = "rockchip,rkv-encoder-rv1106",
		.data = &rkvenc_rv1106_data,
	},
	{},
};

static int rkvenc_link_remove(struct rkvenc_dev *enc,
			      struct rkvenc_link_dev *link)
{
	mpp_debug_enter();

	if (link) {
		struct mpp_dma_buffer *table = NULL, *n;

		mutex_lock(&link->list_mutex);
		list_for_each_entry_safe(table, n, &link->used_list, link) {
			list_del_init(&table->link);
			mpp_dma_free(table);
		}
		list_for_each_entry_safe(table, n, &link->unused_list, link) {
			list_del_init(&table->link);
			mpp_dma_free(table);
		}
		mutex_unlock(&link->list_mutex);
		link->table_num = 0;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvenc_link_alloc_table(struct rkvenc_dev *enc,
				   struct rkvenc_link_dev *link)
{
	int i;
	int ret;
	u32 table_size;
	struct rkvenc_link_header *hdr;
	struct  mpp_dma_buffer *table, *prev;
	struct rkvenc_hw_info *hw = enc->hw_info;
	struct mpp_dev *mpp = &enc->mpp;

	mpp_debug_enter();
	/* calc table class offset info */
	table_size =  roundup(sizeof(struct rkvenc_link_header), 128);
	memset(link->class_off, 0, sizeof(link->class_off));
	for (i = 0; i < hw->reg_class; i++) {
		if (!hw->reg_msg[i].link_len)
			continue;
		//mpp_err("class_off[%d]=%08x\n", i, table_size);
		link->class_off[i] = table_size;
		table_size += roundup(hw->reg_msg[i].link_len, 128);
	}
	//mpp_err("table_size=%d, dev_name=%s\n", table_size, dev_name(mpp->dev));
	/* alloc and init table node */
	link->table_num = enc->task_capacity;
	for (i = 0; i < link->table_num; i++) {
		table = mpp_dma_alloc(mpp->dev, table_size);
		if (!table) {
			dev_err(mpp->dev, "dma alloc failed\n");
			ret = -ENOMEM;
			goto err_free_node;
		}
		hdr = (struct rkvenc_link_header *)table->vaddr;
		// hdr->base_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_BASE];
		hdr->pic_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_PIC];
		hdr->rc_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_RC];
		hdr->param_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_PAR];
		hdr->sqi_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_SQI];
		hdr->scal_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_SCL];
		hdr->osd_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_OSD];
		hdr->status_cfg.lkt_addr = table->iova + link->class_off[RKVENC_CLASS_ST];

		INIT_LIST_HEAD(&table->link);
		mutex_lock(&link->list_mutex);
		/* init link next table addr */
		if (!list_empty(&link->unused_list)) {
			prev = list_last_entry(&link->unused_list, struct mpp_dma_buffer, link);
			hdr = (struct rkvenc_link_header *)prev->vaddr;
			hdr->next_node.valid = 1;
			hdr->next_node.next_addr = table->iova;
		}
		list_add_tail(&table->link, &link->unused_list);
		mutex_unlock(&link->list_mutex);
	}
	/* init first node pointer the last, then it loop */
	mutex_lock(&link->list_mutex);
	table = list_first_entry(&link->unused_list, struct mpp_dma_buffer, link);
	prev = list_last_entry(&link->unused_list, struct mpp_dma_buffer, link);
	hdr = (struct rkvenc_link_header *)prev->vaddr;
	hdr->next_node.valid = 1;
	hdr->next_node.next_addr = table->iova;
	mutex_unlock(&link->list_mutex);

	mpp_debug_leave();

	return 0;

err_free_node:
	rkvenc_link_remove(enc, link);
	return ret;
}

static int rkvenc_link_init(struct platform_device *pdev,
			    struct rkvenc_dev *enc)
{
	int ret;
	struct rkvenc_link_dev *link;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	mpp_debug_enter();

	/* read link table capacity */
	ret = of_property_read_u32(np, "rockchip,task-capacity", &enc->task_capacity);
	if (ret) {
		/* only oneframe mode */
		enc->link_mode = RKVENC_MODE_ONEFRAME;
		return ret;
	}
	if (enc->task_capacity > 1) {
		enc->link_mode = RKVENC_MODE_LINK_ADD;
		dev_info(dev, "%d task capacity link mode detected\n", enc->task_capacity);
	} else {
		enc->task_capacity = 1;
		enc->link_mode = RKVENC_MODE_LINK_ONEFRAME;
	}

	/* alloc link device data */
	link = devm_kzalloc(dev, sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	atomic_set(&enc->link_task_cnt, 0);
	mutex_init(&link->list_mutex);
	INIT_LIST_HEAD(&link->used_list);
	INIT_LIST_HEAD(&link->unused_list);
	ret = rkvenc_link_alloc_table(enc, link);
	if (ret)
		return ret;

	enc->link = link;
	link->enc = enc;

	mpp_debug_leave();

	return 0;
}

static int rkvenc_probe_default(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;

	enc = devm_kzalloc(dev, sizeof(*enc), GFP_KERNEL);
	if (!enc)
		return -ENOMEM;

	mpp = &enc->mpp;
	platform_set_drvdata(pdev, enc);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_rkvenc_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
		else {
			dev_err(dev, "dt match failed!\n");
			return -ENODEV;
		}
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq,
					mpp_dev_isr_sched,
					IRQF_SHARED,
					dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		goto failed_get_irq;
	}
	mpp->session_max_buffers = RKVENC_SESSION_MAX_BUFFERS;
	enc->hw_info = to_rkvenc_info(mpp->var->hw_info);
	rkvenc_procfs_init(mpp);
	mpp_dev_register_srv(mpp, mpp->srv);
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	{
		struct device_node *np_dvbm = NULL;
		struct platform_device *pdev_dvbm = NULL;

		np_dvbm = of_parse_phandle(dev->of_node, "dvbm", 0);
		if (!np_dvbm || !of_device_is_available(np_dvbm))
			mpp_err("failed to get device node\n");

		else {
			pdev_dvbm = of_find_device_by_node(np_dvbm);
			enc->port = rk_dvbm_get_port(pdev_dvbm, DVBM_VEPU_PORT);
			of_node_put(np_dvbm);
			if (enc->port) {
				struct dvbm_cb dvbm_cb;

				dvbm_cb.cb = rkvenc_callback;
				dvbm_cb.ctx = enc;

				rk_dvbm_set_cb(enc->port, &dvbm_cb);
			}
		}
	}
#endif
	enc->link_mode = RKVENC_MODE_ONEFRAME;
	/* init for link device */
	rkvenc_link_init(pdev, enc);
	enc->link_run = 1;

	return 0;

failed_get_irq:
	mpp_dev_remove(mpp);

	return ret;
}

static int rkvenc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;

	dev_info(dev, "probing start\n");

	ret = rkvenc_probe_default(pdev);

	dev_info(dev, "probing finish\n");

	return ret;
}

static int rkvenc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	if (enc->port) {
		rk_dvbm_put(enc->port);
		enc->port = NULL;
	}
#endif
	mpp_dev_remove(&enc->mpp);
	rkvenc_procfs_remove(&enc->mpp);
	rkvenc_link_remove(enc, enc->link);

	return 0;
}

static void rkvenc_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &enc->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->task_count,
				 val, val == 0, 1000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");

	dev_info(dev, "shutdown success\n");
}

struct platform_driver rockchip_rkvenc540c_driver = {
	.probe = rkvenc_probe,
	.remove = rkvenc_remove,
	.shutdown = rkvenc_shutdown,
	.driver = {
		.name = RKVENC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvenc_dt_match),
	},
};
