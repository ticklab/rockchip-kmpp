// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */

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
#define RKVENC_MAX_CORE_NUM			4
#define RV1106_FPGA_TEST 0

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

	RKVENC_FMT_OSD_BASE	= 0x1000,
	RKVENC_FMT_H264E_OSD	= RKVENC_FMT_OSD_BASE + 0,
	RKVENC_FMT_H265E_OSD	= RKVENC_FMT_OSD_BASE + 1,
	RKVENC_FMT_JPEGE_OSD	= RKVENC_FMT_OSD_BASE + 2,
	RKVENC_FMT_BUTT,
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
	}node_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	}pic_cfg;
	union {
		u32 valid : 1;
		u32 lkt_addr : 32;
	}rc_cfg;
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
	/* for ccu */
	struct rkvenc_ccu *ccu;
	struct list_head core_link;
	u32 disable_work;
	atomic_t on_work;

	/* internal rcb-memory */
	u32 sram_size;
	u32 sram_used;
	dma_addr_t sram_iova;
	u32 sram_enabled;
	struct page *rcb_page;
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
	struct dvbm_port *port;
#endif
	/* dvbm y/c top/bot addr */
	u32 ybuf_top;
	u32 ybuf_bot;
	u32 cbuf_top;
	u32 cbuf_bot;
	u32 frm_id_start;
	u32 frm_id_end;
	u32 dvbm_en;
	u32 frm_id_tmp;
	u32 multi_out;

	u32 out_addr;
	u32 out_offset;
#if RV1106_FPGA_TEST
	struct mpp_dma_buffer *out_buf;
#endif
	u32 out_buf_top_off;
	u32 out_buf_cur_off;
	u32 out_buf_pre_off;

	/* for link mode */
	u32 task_capacity;
	struct rkvenc_link_dev *link;
	enum RKVENC_MODE link_mode;
	atomic_t link_task_cnt;
	u32 link_run;
};

struct rkvenc_ccu {
	u32 core_num;
	/* lock for core attach */
	struct mutex lock;
	struct list_head core_list;
	struct mpp_dev *main_core;
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
	4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19,20,21,22,23,
	/* renc and ref wrap */
	// 24, 25, 26, 27,
	/* jpege */
	100, 101, 102, 103, 104, 105, 106,
	107, 108, 109, 110,
};

static const u16 trans_tbl_h264e_rv1106_osd[] = {
	3, 4, 12, 13, 21, 22, 30, 31,
	39, 40, 48, 49, 57, 58, 66, 67,
};

static const u16 trans_tbl_h265e_rv1106[] = {
	4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19,20,21,22,23,
	/* renc and ref wrap */
	// 24, 25, 26, 27,
	/* jpege */
	100, 101, 102, 103, 104, 105, 106,
	107, 108, 109, 110,
};

static const u16 trans_tbl_h265e_rv1106_osd[] = {
	3, 4, 12, 13, 21, 22, 30, 31,
	39, 40, 48, 49, 57, 58, 66, 67,
};

static const u16 trans_tbl_jpege_rv1106[] = {
	100, 101, 102, 103, 104, 105, 106,
	107, 108, 109, 110,
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

static int rkvenc_free_class_msg(struct rkvenc_task *task)
{
	u32 i;
	u32 reg_class = task->hw_info->reg_class;

	for (i = 0; i < reg_class; i++) {
		if (task->reg[i].data) {
			kfree(task->reg[i].data);
			task->reg[i].data = NULL;
			task->reg[i].size = 0;
		}
	}

	return 0;
}

static int rkvenc_alloc_class_msg(struct rkvenc_task *task, int class)
{
	u32 *data;
	struct rkvenc_hw_info *hw = task->hw_info;

	if (!task->reg[class].data) {
		u32 base_s = hw->reg_msg[class].base_s;
		u32 base_e = hw->reg_msg[class].base_e;
		u32 class_size = base_e - base_s + sizeof(u32);

		data = kzalloc(class_size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		task->reg[class].data = data;
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

static int rkvenc_extract_task_msg(struct mpp_session *session,
				   struct rkvenc_task *task,
				   struct mpp_task_msgs *msgs, u32 kernel_space)
{
	int ret;
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
			void *data;
			struct mpp_request *wreq;

			for (j = 0; j < hw->reg_class; j++) {
				if (!req_over_class(req, task, j))
					continue;

				wreq = &task->w_reqs[task->w_req_cnt];
				rkvenc_update_req(task, j, req, wreq);
				data = rkvenc_get_class_reg(task, wreq->offset);
				if (!data)
					goto fail;
				if (!kernel_space) {
					if (copy_from_user(data, wreq->data, wreq->size)) {
						mpp_err("copy_from_user reg failed\n");
						return -EIO;
					}
				}else {
					memcpy(data, wreq->data, wreq->size);
				}

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

fail:
	rkvenc_free_class_msg(task);

	mpp_debug_enter();
	return ret;
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
	val = class_reg[offset/sizeof(u32)];
	task->fmt = (val >> bitpos) & ((1 << bitlen) - 1);

	return 0;
}

static void *task_init(struct rkvenc_task *task){
	int i = 0;
	for(i = 0; i < RKVENC_CLASS_BUTT; i++){
		task->reg[i].valid = 0;
	}
	task->irq_status = 0;
	task->w_req_cnt = 0;
	task->r_req_cnt = 0;
	return 0;
}

static void *rkvenc_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct rkvenc_task *task = (struct rkvenc_task *) session->task;
	struct mpp_task *mpp_task;
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	if (!task)
		return NULL;

	task_init(task);
	mpp_task = &task->mpp_task;
	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
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

	if (session->k_space) {
		u32 i;
		struct rkvenc_hw_info *hw = task->hw_info;

		for (i = 0; i < hw->fd_class; i++) {
			u32 class = hw->fd_reg[i].class;
			u32 fmt = hw->fd_reg[i].base_fmt + task->fmt;
			u32 *reg = task->reg[class].data;
			mpp_get_dma_attach_mem_info(session, mpp_task, fmt, reg);
		}
	}
	task->clk_mode = CLK_MODE_NORMAL;

	mpp_debug_leave();

	return mpp_task;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	/* free class register buffer */
	rkvenc_free_class_msg(task);
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
			enc->dvbm_en = dvbm_cfg;
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
		//mpp_err("table=%px\n", table);
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

	mpp_debug_enter();

	atomic_set(&enc->on_work, 1);
	//dev_info(mpp->dev, "link_run=%d mode %d\n", enc->link_run, enc->link_mode);
	switch (enc->link_mode) {
	case RKVENC_MODE_ONEFRAME: {
		u32 i, j;
		u32 enc_start_val = 0;
		struct mpp_request msg;
		struct mpp_request *req;
		u32 dvbm_en = 0;

		for (i = 0; i < task->w_req_cnt; i++) {
			int ret;
			u32 s, e, off;
			u32 *regs;

			req = &task->w_reqs[i];
			ret = rkvenc_get_class_msg(task, req->offset, &msg);
			//mpp_err("req->offset=%d, msg.offset=%d, req->size=%d, ret=%d\n",
			//		req->offset, msg.offset, req->size, ret);
			if (ret)
				return -EINVAL;

			s = (req->offset - msg.offset) / sizeof(u32);
			e = s + req->size / sizeof(u32);
			regs = (u32 *)msg.data;
			//mpp_err("s=%d, e=%d\n", s, e);
			for (j = s; j < e; j++) {
				off = msg.offset + j * sizeof(u32);
				if (off == hw->enc_start_base) {
					enc_start_val = regs[j];
					continue;
				}
				if (off == hw->dvbm_cfg) {
					dvbm_en = regs[j];
				}
				mpp_write_relaxed(mpp, off, regs[j]);
			}
		}

		/* init current task */
		mpp->cur_task = mpp_task;

		/* Flush the register before the start the device */
		//mpp_err("enc_start_val=%08x\n", enc_start_val);
		wmb();
		if (enc->link_run)
			mpp_write(mpp, hw->enc_start_base, enc_start_val);
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
		if (dvbm_en)
			rk_dvbm_link(enc->port);
#endif
		enc->dvbm_en = dvbm_en;
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



static int rkvenc_dump_dbg(struct mpp_dev *mpp)
{
	// struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	u32 i, off;
	u32 start = 0;
	u32 end = 0x5354;

	pr_info("====== %s ======\n", __func__);

	// for (i = 0; i <= 0x480; i += 4) {
	// 	off = i;
	// 	pr_info("reg[0x%0x] = 0x%08x\n", off, mpp_read(mpp, off));
	// }
	for (i = start; i < end; i += 4) {
		off = i;
		pr_info("reg[0x%0x] = 0x%08x\n", off, mpp_read(mpp, off));
	}

	pr_info("====== %s ======\n", __func__);

	// rk_dvbm_ctrl(enc->port, DVBM_VEPU_DUMP_REGS, NULL);

	return 0;
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

#if RV1106_FPGA_TEST
	if (enc->multi_out) {
		u32 st_snum = mpp_read(mpp, 0x4034);
		u32 pkt_num = (st_snum >> 16) & 0x7f;
		u32 size = 0;
		void *src = enc->out_buf->vaddr;
		void* dst;

		dma_sync_single_for_device(mpp->dev, enc->out_addr,
					10*1024, DMA_FROM_DEVICE);
		dma_sync_single_for_cpu(mpp->dev, enc->out_addr,
					10*1024, DMA_FROM_DEVICE);
		dma_sync_single_for_device(mpp->dev, enc->out_buf->iova,
					enc->out_buf_top_off, DMA_FROM_DEVICE);
		dma_sync_single_for_cpu(mpp->dev, enc->out_buf->iova,
					enc->out_buf_top_off, DMA_FROM_DEVICE);
		dst = phys_to_virt(enc->out_addr);
		if (test_bit(0, &mpp->irq_status))
			size = mpp_read(mpp, 0x4000) - enc->out_offset;
		else
			size = pkt_num * 1024 - enc->out_offset;

		if ((test_bit(4, &mpp->irq_status) && size > 0) || (size == enc->out_buf_top_off)) {
			//pr_info("buf overflow pkt_num %d\n", pkt_num);
			size -= 1024;
		}
		//pr_info("pkt_num %d size %d pre %d offset %d max %d\n",
		//	pkt_num, size, enc->out_buf_pre_off, enc->out_offset,
		//	enc->out_buf_top_off);
		//pr_info("w : r = 0x%08x : 0x%08x\n", mpp_read(mpp, 0x402c), mpp_read(mpp, 0x2bc));
		if (size > 0) {
			if ((enc->out_buf_pre_off + size) >= enc->out_buf_top_off) {
				u32 size1 = enc->out_buf_top_off - enc->out_buf_pre_off;

				memcpy(dst + enc->out_offset, src + enc->out_buf_pre_off, size1);
				enc->out_buf_pre_off = 0;
				enc->out_offset += size1;
				memcpy(dst + enc->out_offset, src + enc->out_buf_pre_off, size - size1);
				enc->out_offset += (size - size1);
				enc->out_buf_pre_off += (size - size1);
			} else {
				memcpy(phys_to_virt(enc->out_addr) + enc->out_offset, src + enc->out_buf_pre_off, size);
				enc->out_buf_pre_off += size;
				enc->out_offset += size;
			}
			dma_sync_single_for_device(mpp->dev, enc->out_addr,
						10*1024, DMA_FROM_DEVICE);
			dma_sync_single_for_cpu(mpp->dev, enc->out_addr,
						10*1024, DMA_FROM_DEVICE);
			dma_sync_single_for_device(mpp->dev, enc->out_buf->iova,
						enc->out_buf_top_off, DMA_FROM_DEVICE);
			dma_sync_single_for_cpu(mpp->dev, enc->out_buf->iova,
						enc->out_buf_top_off, DMA_FROM_DEVICE);
			// if (test_bit(4, &mpp->irq_status)) {
			// 	// u32 old_adr = mpp_read(mpp, 0x2b0);
			// 	// enc->out_buf_top_off += 1*1024;
			// 	// mpp_write(mpp, 0x2b0, enc->out_buf->iova + enc->out_buf_top_off);
			// 	// pr_info("update top addr 0x%08x -> 0x%08x", old_adr, mpp_read(mpp, 0x2b0));
			// 	mpp_write(mpp, 0x2bc, enc->out_buf->iova + enc->out_buf_pre_off + 0xd);
			// } else
				mpp_write(mpp, 0x2bc, enc->out_buf->iova + enc->out_buf_pre_off + 0xd);
		}
		//pr_info("st_snum 0x%08x si_len 0x%08x size 0x%08x\n", st_snum,
		//	mpp_read(mpp, 0x4038), mpp_read(mpp, 0x4000));
		if (!test_bit(0, &mpp->irq_status))
			return IRQ_NONE;
	}
#endif
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
	tb_reg[int_sta_off/ sizeof(u32)] = irq_status;

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
	if (list_empty(&mpp->queue->running_list) && enc->dvbm_en) {
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
				task->irq_status = mpp->irq_status;2
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
		if(mpp->irq_status & 0x100 || mpp->dump_regs)
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

	mpp_debug_enter();

	switch (enc->link_mode) {
	case RKVENC_MODE_ONEFRAME:
	case RKVENC_MODE_LINK_ONEFRAME: {
		u32 i, j;
		u32 *reg;
		struct mpp_request *req;
		struct mpp_request msg;

		if(mpp->irq_status & 0x100 || mpp->dump_regs)
			rkvenc_dump_dbg(mpp);

		for (i = 0; i < task->r_req_cnt; i++) {
			int ret;
			int s, e;

			req = &task->r_reqs[i];
			ret = rkvenc_get_class_msg(task, req->offset, &msg);
			if (ret)
				return -EINVAL;
			s = (req->offset - msg.offset) / sizeof(u32);
			e = s + req->size / sizeof(u32);
			reg = (u32 *)msg.data;
			//mpp_err("msg.offset=%08x, s=%d, e=%d\n", msg.offset, s ,e);
			for (j = s; j < e; j++)
				reg[j] = mpp_read_relaxed(mpp, msg.offset + j * sizeof(u32));

		}
		/* revert hack for irq status */
		reg = rkvenc_get_class_reg(task, task->hw_info->int_sta_base);
		if (reg)
			*reg = task->irq_status;
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
		if (enc->dvbm_en)
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
		u32 i;
		u32 *reg;
		struct mpp_request *req;

		for (i = 0; i < task->r_req_cnt; i++) {
			req = &task->r_reqs[i];
			reg = rkvenc_get_class_reg(task, req->offset);
			if (!reg)
				return -EINVAL;
			if (!mpp_task->session->k_space) {
				if (copy_to_user(req->data, reg, req->size)) {
					mpp_err("copy_to_user reg fail\n");
					return -EIO;
				}
			}else{
				memcpy(req->data, (u8 *)reg, req->size);
			}
		}
	}break;
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
			}else{
				memcpy(req->data, (u8 *)reg, req->size);
			}
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

	mpp_task_finalize(session, mpp_task);
	//rkvenc_free_class_msg(task);
	//kfree(task);

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
	if (session && session->task) {
		struct rkvenc_task *task = (struct rkvenc_task *)session->task;

		rkvenc_free_class_msg(task);
		kfree(task);
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
	session->task = task;
	return 0;

fail:
	if (session->priv)
		kfree(session->priv);
	if (task) {
		rkvenc_free_class_msg(task);
		kfree(task);
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
		} else {
			seq_printf(seq, "%8s|", (const char *)"null");
		}
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
	mpp_procfs_create_u32("stream_top", 0644,
			      enc->procfs, &enc->out_buf_top_off);

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
	//mpp_err("xxxx link_mode=%d\n", enc->link_mode);

	mpp_debug_leave();

	return 0;
}

static int rkvenc2_alloc_rcbbuf(struct platform_device *pdev, struct rkvenc_dev *enc)
{
	int ret;
	u32 vals[2];
	dma_addr_t iova;
	u32 sram_used, sram_size;
	struct device_node *sram_np;
	struct resource sram_res;
	resource_size_t sram_start, sram_end;
	struct iommu_domain *domain;
	struct device *dev = &pdev->dev;

	/* get rcb iova start and size */
	ret = device_property_read_u32_array(dev, "rockchip,rcb-iova", vals, 2);
	if (ret) {
		dev_err(dev, "could not find property rcb-iova\n");
		return ret;
	}
	iova = PAGE_ALIGN(vals[0]);
	sram_used = PAGE_ALIGN(vals[1]);
	if (!sram_used) {
		dev_err(dev, "sram rcb invalid.\n");
		return -EINVAL;
	}
	/* alloc reserve iova for rcb */
	ret = iommu_dma_reserve_iova(dev, iova, sram_used);
	if (ret) {
		dev_err(dev, "alloc rcb iova error.\n");
		return ret;
	}
	/* get sram device node */
	sram_np = of_parse_phandle(dev->of_node, "rockchip,sram", 0);
	if (!sram_np) {
		dev_err(dev, "could not find phandle sram\n");
		return -ENODEV;
	}
	/* get sram start and size */
	ret = of_address_to_resource(sram_np, 0, &sram_res);
	of_node_put(sram_np);
	if (ret) {
		dev_err(dev, "find sram res error\n");
		return ret;
	}
	/* check sram start and size is PAGE_SIZE align */
	sram_start = round_up(sram_res.start, PAGE_SIZE);
	sram_end = round_down(sram_res.start + resource_size(&sram_res), PAGE_SIZE);
	if (sram_end <= sram_start) {
		dev_err(dev, "no available sram, phy_start %pa, phy_end %pa\n",
			&sram_start, &sram_end);
		return -ENOMEM;
	}
	sram_size = sram_end - sram_start;
	sram_size = sram_used < sram_size ? sram_used : sram_size;
	/* iova map to sram */
	domain = enc->mpp.iommu_info->domain;
	ret = iommu_map(domain, iova, sram_start, sram_size, IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		dev_err(dev, "sram iommu_map error.\n");
		return ret;
	}
	/* alloc dma for the remaining buffer, sram + dma */
	if (sram_size < sram_used) {
		struct page *page;
		size_t page_size = PAGE_ALIGN(sram_used - sram_size);

		page = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(page_size));
		if (!page) {
			dev_err(dev, "unable to allocate pages\n");
			ret = -ENOMEM;
			goto err_sram_map;
		}
		/* iova map to dma */
		ret = iommu_map(domain, iova + sram_size, page_to_phys(page),
				page_size, IOMMU_READ | IOMMU_WRITE);
		if (ret) {
			dev_err(dev, "page iommu_map error.\n");
			__free_pages(page, get_order(page_size));
			goto err_sram_map;
		}
		enc->rcb_page = page;
	}

	enc->sram_size = sram_size;
	enc->sram_used = sram_used;
	enc->sram_iova = iova;
	enc->sram_enabled = -1;
	dev_info(dev, "sram_start %pa\n", &sram_start);
	dev_info(dev, "sram_iova %pad\n", &enc->sram_iova);
	dev_info(dev, "sram_size %u\n", enc->sram_size);
	dev_info(dev, "sram_used %u\n", enc->sram_used);

	return 0;

err_sram_map:
	iommu_unmap(domain, iova, sram_size);

	return ret;
}
#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)
static int rkvenc_callback(void* ctx, enum dvbm_cb_event event, void* arg)
{
	struct rkvenc_dev *enc = (struct rkvenc_dev *)ctx;

	if (!enc)
		return 0;

	switch (event) {
	case DVBM_VEPU_NOTIFY_ADDR: {
		struct dvbm_addr_cfg *cfg = (struct dvbm_addr_cfg*)arg;

		enc->ybuf_top = cfg->ybuf_top;
		enc->ybuf_bot = cfg->ybuf_bot;
		enc->cbuf_top = cfg->cbuf_top;
		enc->cbuf_bot = cfg->cbuf_bot;

		//pr_info("%s y/c t: 0x%08x 0x%08x y/c b: 0x%08x 0x%08x\n",
		//	__func__, enc->ybuf_top, enc->cbuf_top, enc->ybuf_bot, enc->cbuf_bot);
	} break;
	case DVBM_VEPU_REQ_CONNECT : {
		u32 connect = *(u32*)arg;
		unsigned long val = mpp_read(&enc->mpp, 0x18);

		if (!connect)
			clear_bit(24, &val);
		// pr_info("val 0x%08x\n", val);
		mpp_write(&enc->mpp, 0x18, val);
	} break;
	case DVBM_VEPU_NOTIFY_FRM_STR: {
		enc->frm_id_start = *(u32*)arg;
	} break;
	case DVBM_VEPU_NOTIFY_FRM_END: {
		enc->frm_id_end = *(u32*)arg;
	} break;
	default : {
	} break;
	}
	return 0;
}
#endif

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
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret)
		return ret;

	rkvenc2_alloc_rcbbuf(pdev, enc);

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
		if (!np_dvbm || !of_device_is_available(np_dvbm)) {
			mpp_err("failed to get device node\n");
		} else {
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
#if RV1106_FPGA_TEST
	enc->out_buf = mpp_dma_alloc(dev, 50 * 1024);
	enc->out_buf_top_off = 3 * 1024;
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
#if RV1106_FPGA_TEST
	if (enc->out_buf)
		mpp_dma_free(enc->out_buf);
#endif
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