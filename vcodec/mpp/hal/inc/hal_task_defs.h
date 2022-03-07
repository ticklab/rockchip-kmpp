// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __HAL_TASK_DEFS__
#define __HAL_TASK_DEFS__

#include "rk_type.h"

typedef void *HalTaskHnd;
typedef void *HalTaskGroup;

typedef enum HalTaskStatus_e {
	MPP_TASK_IDLE,
	MPP_TASK_PROCESSING,
	MPP_TASK_PROC_DONE,
	MPP_TASK_BUTT,
} HalTaskStatus;

/*
 * modified by parser and encoder
 *
 * number   : the number of the data pointer array element
 * data     : the address of the pointer array, parser will add its data here
 */
typedef struct MppSyntax_t {
	RK_U32 number;
	void *data;
} MppSyntax;

#endif /* __HAL_TASK_DEFS__ */
