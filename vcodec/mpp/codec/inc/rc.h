// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __RC_H__
#define __RC_H__

#include "mpp_err.h"

#include "mpp_rc_api.h"

/*
 * Mpp rate control principle
 *
 * 1. Rate control information is composed by these parts:
 *    a. configuration from mpp user.
 *    b. frame level target bitrate calculated by different RC model.
 *    c. block level hardware related configuration to hardware.
 *    d. statistic information from hardware after one frame is encoded.
 *
 * 2. All the above informatioin is storaged and managed by RcDataCtx in mpp
 *    framework. All these information is shared in encoder and hal module.
 *
 * 3. User (no matter developer or end user) defined rate control should based
 *    on these Rate control information and implement its own rate control
 *    strategy module by providing their own RcImplApi.
 */

/*
 * Relation between mpp_enc / enc_impl / hal / RcApi / RcImplApi / RcData
 *
 *  +-----------+
 *  |  mpp_enc  |
 *  +-----+-----+
 *        |
 *  +-----+-----+   +-----------+   +-----------+
 *  |  enc_impl +--->   RcApi   +---> RcImplApi |
 *  +-----+-----+   +-----+-----+   +-----+-----+
 *        |      \ /      |               |
 *        |       +       |               |
 *        |      / \      |               |
 *  +-----v-----+   +-----v-----+         |
 *  |    hal    +--->  RcData   <---------+
 *  +-----------+   +-----------+
 */

typedef void *RcCtx;

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET rc_init(RcCtx * ctx, MppCodingType type,
		const char **request_name);
MPP_RET rc_deinit(RcCtx ctx);

/* update rc control  */
MPP_RET rc_update_usr_cfg(RcCtx ctx, RcCfg * cfg);

/* Frame rate convertion */
MPP_RET rc_frm_check_drop(RcCtx ctx, EncRcTask * task);
/* Frame reenc check */
MPP_RET rc_frm_check_reenc(RcCtx ctx, EncRcTask * task);

/* Frame level rate and quality control */
MPP_RET rc_frm_start(RcCtx ctx, EncRcTask * task);
MPP_RET rc_frm_end(RcCtx ctx, EncRcTask * task);

MPP_RET rc_hal_start(RcCtx ctx, EncRcTask * task);
MPP_RET rc_hal_end(RcCtx ctx, EncRcTask * task);

MPP_RET rc_proc_show(void *seq_file, RcCtx ctx, RK_S32 chl_id);
#ifdef __cplusplus
}
#endif
#endif				/* __RC_H__ */
