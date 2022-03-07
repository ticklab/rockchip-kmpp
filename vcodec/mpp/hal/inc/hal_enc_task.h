// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __HAL_ENC_TASK__
#define __HAL_ENC_TASK__

//#include "mpp_time.h"

#include "hal_task_defs.h"
#include "mpp_rc_defs.h"
#include "mpp_enc_refs.h"

#define HAL_ENC_TASK_ERR_INIT         0x00000001
#define HAL_ENC_TASK_ERR_ALLOC        0x00000010
#define HAL_ENC_TASK_ERR_EXTRAINFO    0x00000100
#define HAL_ENC_TASK_ERR_GENREG       0x00001000
#define HAL_ENC_TASK_ERR_START        0x00010000
#define HAL_ENC_TASK_ERR_WAIT         0x00100000

typedef struct HalEncTaskFlag_t {
	RK_U32 err;
} HalEncTaskFlag;

typedef struct HalEncTask_t {
	RK_U32 valid;

	// rate control data channel
	EncRcTask *rc_task;

	// cpb reference force config
	MppEncRefFrmUsrCfg *frm_cfg;

	// current tesk protocol syntax information
	MppSyntax syntax;
	MppSyntax hal_ret;

	/*
	 * Current tesk output stream buffer
	 *
	 * Usage and flow of changing task length and packet length
	 *
	 * 1. length is runtime updated for each stage.
	 *    header_length / sei_length / hw_length are for recording.
	 *
	 * 2. When writing vps/sps/pps encoder should update length.
	 *    Then length will be kept before next stage is done.
	 *    For example when vps/sps/pps were inserted and slice data need
	 *    reencoding the hal should update length at the final loop.
	 *
	 * 3. length in task and length in packet should be updated at the same
	 *    time. Encoder flow need to check these two length between stages.
	 */
	MppPacket packet;
	MppBuffer output;
	RK_S32 header_length;
	RK_S32 sei_length;
	RK_S32 hw_length;
	RK_U32 length;

	// current tesk input slot buffer
	MppFrame frame;
	MppBuffer input;

	// task stopwatch for timing
	MppStopwatch stopwatch;

	// current mv info output buffer (not used)
	MppBuffer mv_info;
	HalEncTaskFlag flags;
    RK_U32 online;
    void *jpeg_osd_reg;
    void *jpeg_tlb_reg;
} HalEncTask;

#endif /* __HAL_ENC_TASK__ */
