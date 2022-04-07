// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * author: timkingh.huang@rock-chips.com
 *
 */

#ifndef __VEPU_PP_COMMON_H__
#define __VEPU_PP_COMMON_H__

#include<linux/kernel.h>

struct dma_buf;

#define pp_err(fmt, args...)					\
		pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

#if __SIZEOF_POINTER__ == 4
#define REQ_DATA_PTR(ptr) ((u32)ptr)
#elif __SIZEOF_POINTER__ == 8
#define REQ_DATA_PTR(ptr) ((u64)ptr)
#endif

#define MPP_DEVICE_RKVENC_PP  (25)

/* define flags for mpp_request */
#define MPP_FLAGS_MULTI_MSG         (0x00000001)
#define MPP_FLAGS_LAST_MSG          (0x00000002)
#define MPP_FLAGS_REG_FD_NO_TRANS   (0x00000004)
#define MPP_FLAGS_SCL_FD_NO_TRANS   (0x00000008)
#define MPP_FLAGS_REG_OFFSET_ALONE  (0x00000010)
#define MPP_FLAGS_SECURE_MODE       (0x00010000)

#define PP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))

enum MppServiceCmdType_e {
	MPP_CMD_QUERY_BASE = 0,
	MPP_CMD_PROBE_HW_SUPPORT = MPP_CMD_QUERY_BASE + 0,
	MPP_CMD_QUERY_HW_ID = MPP_CMD_QUERY_BASE + 1,
	MPP_CMD_QUERY_CMD_SUPPORT = MPP_CMD_QUERY_BASE + 2,
	MPP_CMD_QUERY_BUTT,

	MPP_CMD_INIT_BASE = 0x100,
	MPP_CMD_INIT_CLIENT_TYPE = MPP_CMD_INIT_BASE + 0,
	MPP_CMD_INIT_DRIVER_DATA = MPP_CMD_INIT_BASE + 1,
	MPP_CMD_INIT_TRANS_TABLE = MPP_CMD_INIT_BASE + 2,
	MPP_CMD_INIT_BUTT,

	MPP_CMD_SEND_BASE = 0x200,
	MPP_CMD_SET_REG_WRITE = MPP_CMD_SEND_BASE + 0,
	MPP_CMD_SET_REG_READ = MPP_CMD_SEND_BASE + 1,
	MPP_CMD_SET_REG_ADDR_OFFSET = MPP_CMD_SEND_BASE + 2,
	MPP_CMD_SET_RCB_INFO = MPP_CMD_SEND_BASE + 3,
	MPP_CMD_SEND_BUTT,

	MPP_CMD_POLL_BASE = 0x300,
	MPP_CMD_POLL_HW_FINISH = MPP_CMD_POLL_BASE + 0,
	MPP_CMD_POLL_BUTT,

	MPP_CMD_CONTROL_BASE = 0x400,
	MPP_CMD_RESET_SESSION = MPP_CMD_CONTROL_BASE + 0,
	MPP_CMD_TRANS_FD_TO_IOVA = MPP_CMD_CONTROL_BASE + 1,
	MPP_CMD_RELEASE_FD = MPP_CMD_CONTROL_BASE + 2,
	MPP_CMD_SEND_CODEC_INFO = MPP_CMD_CONTROL_BASE + 3,
	MPP_CMD_CONTROL_BUTT,

	MPP_CMD_BUTT,
};

enum PP_RET {
	VEPU_PP_NOK = -1,
	VEPU_PP_OK = 0,
};

struct dev_reg_wr_t {
	void *data_ptr;
	u32 size;
	u32 offset;
};

struct dev_reg_rd_t {
	void *data_ptr;
	u32 size;
	u32 offset;
};

struct pp_srv_api_t {
	const char *name;
	int ctx_size;
	enum PP_RET(*init) (void *ctx, int type);
	enum PP_RET(*deinit) (void *ctx);
	enum PP_RET(*reg_wr) (void *ctx, struct dev_reg_wr_t *cfg);
	enum PP_RET(*reg_rd) (void *ctx, struct dev_reg_rd_t *cfg);
	enum PP_RET(*cmd_send) (void *ctx);

	/* poll cmd from hardware */
	enum PP_RET(*cmd_poll) (void *ctx);
	u32 (*get_address) (void *ctx, struct dma_buf *buf, u32 offset);
	void (*release_address) (void *ctx, struct dma_buf *buf);
};



#endif
