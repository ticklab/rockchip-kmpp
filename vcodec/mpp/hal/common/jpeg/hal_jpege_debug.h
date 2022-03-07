// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __HAL_JPEGE_DEBUG_H__
#define __HAL_JPEGE_DEBUG_H__

#include "mpp_log.h"

#define HAL_JPEGE_DBG_FUNCTION          (0x00000001)
#define HAL_JPEGE_DBG_SIMPLE            (0x00000002)
#define HAL_JPEGE_DBG_DETAIL            (0x00000004)
#define HAL_JPEGE_DBG_INPUT             (0x00000010)
#define HAL_JPEGE_DBG_OUTPUT            (0x00000020)

#define hal_jpege_dbg(flag, fmt, ...)   _mpp_dbg(hal_jpege_debug, flag, fmt, ## __VA_ARGS__)
#define hal_jpege_dbg_f(flag, fmt, ...) _mpp_dbg_f(hal_jpege_debug, flag, fmt, ## __VA_ARGS__)

#define hal_jpege_dbg_func(fmt, ...)    hal_jpege_dbg_f(HAL_JPEGE_DBG_FUNCTION, fmt, ## __VA_ARGS__)
#define hal_jpege_dbg_simple(fmt, ...)  hal_jpege_dbg(HAL_JPEGE_DBG_SIMPLE, fmt, ## __VA_ARGS__)
#define hal_jpege_dbg_detail(fmt, ...)  hal_jpege_dbg(HAL_JPEGE_DBG_DETAIL, fmt, ## __VA_ARGS__)
#define hal_jpege_dbg_input(fmt, ...)   hal_jpege_dbg(HAL_JPEGE_DBG_INPUT, fmt, ## __VA_ARGS__)
#define hal_jpege_dbg_output(fmt, ...)  hal_jpege_dbg(HAL_JPEGE_DBG_OUTPUT, fmt, ## __VA_ARGS__)

#define hal_jpege_enter()               hal_jpege_dbg_func("(%d) enter\n", __LINE__);
#define hal_jpege_leave()               hal_jpege_dbg_func("(%d) leave\n", __LINE__);

extern RK_U32 hal_jpege_debug;

#endif /* __HAL_JPEGE_DEBUG_H__ */
