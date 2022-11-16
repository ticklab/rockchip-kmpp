// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_DEVICE_H__
#define __MPP_DEVICE_H__

#include "mpp_err.h"
#include "mpp_dev_defs.h"

struct dma_buf;

typedef enum MppDevIoctlCmd_e {
	/* hardware operation setup config */
	MPP_DEV_REG_WR,
	MPP_DEV_REG_RD,
	MPP_DEV_REG_OFFSET,
	MPP_DEV_RCB_INFO,
	MPP_DEV_SET_INFO,

	MPP_DEV_CMD_SEND,
	MPP_DEV_CMD_POLL,

	MPP_DEV_IOCTL_CMD_BUTT,
} MppDevIoctlCmd;

/* for MPP_DEV_REG_WR */
typedef struct MppDevRegWrCfg_t {
	void *reg;
	RK_U32 size;
	RK_U32 offset;
} MppDevRegWrCfg;

/* for MPP_DEV_REG_RD */
typedef struct MppDevRegRdCfg_t {
	void *reg;
	RK_U32 size;
	RK_U32 offset;
} MppDevRegRdCfg;

/* for MPP_DEV_REG_OFFSET */
typedef struct MppDevRegOffsetCfg_t {
	RK_U32 reg_idx;
	RK_U32 offset;
} MppDevRegOffsetCfg;

/* for MPP_DEV_RCB_INFO */
typedef struct MppDevRcbInfoCfg_t {
	RK_U32 reg_idx;
	RK_U32 size;
} MppDevRcbInfoCfg;

/* for MPP_DEV_SET_INFO */
typedef struct MppDevSetInfoCfg_t {
	RK_U32 type;
	RK_U32 flag;
	RK_U64 data;
} MppDevInfoCfg;

typedef struct MppDevApi_t {
	const char *name;
	RK_U32 ctx_size;
	MPP_RET(*init) (void *ctx, MppClientType type);
	MPP_RET(*deinit) (void *ctx);

	/* config the cmd on preparing */
	MPP_RET(*reg_wr) (void *ctx, MppDevRegWrCfg * cfg);
	MPP_RET(*reg_rd) (void *ctx, MppDevRegRdCfg * cfg);
	MPP_RET(*reg_offset) (void *ctx, MppDevRegOffsetCfg * cfg);
	MPP_RET(*rcb_info) (void *ctx, MppDevRcbInfoCfg * cfg);
	MPP_RET(*set_info) (void *ctx, MppDevInfoCfg * cfg);

	/* send cmd to hardware */
	MPP_RET(*cmd_send) (void *ctx);

	/* poll cmd from hardware */
	MPP_RET(*cmd_poll) (void *ctx);
	RK_U32(*get_address) (void *ctx, struct dma_buf * buf, RK_U32 offset);
	void (*chnl_register) (void *ctx, void *func, RK_S32 chan_id);
	struct device *(*chnl_get_dev)(void *ctx);
} MppDevApi;

typedef void *MppDev;

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET mpp_dev_init(MppDev * ctx, MppClientType type);
MPP_RET mpp_dev_deinit(MppDev ctx);

MPP_RET mpp_dev_ioctl(MppDev ctx, RK_S32 cmd, void *param);

/* special helper function for large address offset config */
MPP_RET mpp_dev_set_reg_offset(MppDev dev, RK_S32 index, RK_U32 offset);

RK_U32 mpp_dev_get_iova_address(MppDev ctx, MppBuffer mpp_buf,
				RK_U32 reg_idx);

RK_U32 mpp_dev_get_iova_address2(MppDev ctx, struct dma_buf *dma_buf,
				 RK_U32 reg_idx);

RK_U32 mpp_dev_get_mpi_ioaddress(MppDev ctx, MpiBuf mpi_buf,
				 RK_U32 offset);

void mpp_dev_chnl_register(MppDev ctx, void *func, RK_S32 chan_id);

struct device * mpp_get_dev(MppDev ctx);


#ifdef __cplusplus
}
#endif
#endif				/* __MPP_DEVICE_H__ */
