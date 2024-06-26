// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __HAL_DEC_TASK__
#define __HAL_DEC_TASK__

#include "hal_task_defs.h"
//#include "mpp_callback.h"

#define MAX_DEC_REF_NUM     17

typedef union HalDecTaskFlag_t {
	RK_U32 val;
	struct {
		RK_U32 eos: 1;
		RK_U32 info_change: 1;

		/*
		 * Different error flags for task
		 *
		 * parse_err :
		 * When set it means fatal error happened at parsing stage
		 * This task should not enable hardware just output a empty frame with
		 * error flag.
		 *
		 * ref_err :
		 * When set it means current task is ok but it contains reference frame
		 * with error which will introduce error pixels to this frame.
		 *
		 * used_for_ref :
		 * When set it means this output frame will be used as reference frame
		 * for further decoding. When there is error on decoding this frame
		 * if used_for_ref is set then the frame will set errinfo flag
		 * if used_for_ref is cleared then the frame will set discard flag.
		 */
		RK_U32 parse_err: 1;
		RK_U32 ref_err: 1;
		RK_U32 used_for_ref: 1;

		RK_U32 wait_done: 1;
	};
} HalDecTaskFlag;

typedef struct HalDecTask_t {
	// set by parser to signal that it is valid
	RK_U32 valid;
	HalDecTaskFlag flags;

	// previous task hardware working status
	// when hardware error happen status is not zero
	RK_U32 prev_status;
	// current tesk protocol syntax information
	MppSyntax syntax;

	// packet need to be copied to hardware buffer
	// parser will create this packet and mpp_dec will copy it to hardware bufffer
	MppPacket input_packet;

	// current task input slot index
	RK_S32 input;

	RK_S32 reg_index;
	// for test purpose
	// current tesk output slot index
	RK_S32 output;

	// current task reference slot index, -1 for unused
	RK_S32 refer[MAX_DEC_REF_NUM];
} HalDecTask;

#endif /* __HAL_DEC_TASK__ */
