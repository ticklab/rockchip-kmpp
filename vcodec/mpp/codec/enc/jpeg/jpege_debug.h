// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __JPEGE_DEBUG_H__
#define __JPEGE_DEBUG_H__

#include "mpp_log.h"

#define JPEGE_DBG_FUNCTION          (0x00000001)
#define JPEGE_DBG_INPUT             (0x00000010)
#define JPEGE_DBG_OUTPUT            (0x00000020)
#define JPEGE_DBG_CTRL              (0x00000040)

#define jpege_dbg(flag, fmt, ...)   _mpp_dbg(jpege_debug, flag, fmt, ## __VA_ARGS__)
#define jpege_dbg_f(flag, fmt, ...) _mpp_dbg_f(jpege_debug, flag, fmt, ## __VA_ARGS__)

#define jpege_dbg_func(fmt, ...)    jpege_dbg_f(JPEGE_DBG_FUNCTION, fmt, ## __VA_ARGS__)
#define jpege_dbg_input(fmt, ...)   jpege_dbg(JPEGE_DBG_INPUT, fmt, ## __VA_ARGS__)
#define jpege_dbg_output(fmt, ...)  jpege_dbg(JPEGE_DBG_OUTPUT, fmt, ## __VA_ARGS__)
#define jpege_dbg_ctrl(fmt, ...)    jpege_dbg(JPEGE_DBG_CTRL, fmt, ## __VA_ARGS__)

extern RK_U32 jpege_debug;

#endif /* __JPEGE_DEBUG_H__ */
