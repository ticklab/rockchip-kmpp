// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __H264E_DEBUG_H__
#define __H264E_DEBUG_H__

#include "mpp_log.h"

#define H264E_DBG_FUNCTION          (0x00000001)
#define H264E_DBG_FLOW              (0x00000002)
#define H264E_DBG_CTRL              (0x00000004)
#define H264E_DBG_DETAIL            (0x00000008)

#define H264E_DBG_SPS               (0x00000010)
#define H264E_DBG_PPS               (0x00000020)
#define H264E_DBG_SLICE             (0x00000040)
#define H264E_DBG_SEI               (0x00000080)

#define H264E_DBG_DPB               (0x00000100)
#define H264E_DBG_LIST              (0x00000200)
#define H264E_DBG_MMCO              (0x00000400)

#define h264e_dbg(flag, fmt, ...)   _mpp_dbg(h264e_debug, flag, fmt, ## __VA_ARGS__)
#define h264e_dbg_f(flag, fmt, ...) _mpp_dbg_f(h264e_debug, flag, fmt, ## __VA_ARGS__)

#define h264e_dbg_func(fmt, ...)    h264e_dbg_f(H264E_DBG_FUNCTION, fmt, ## __VA_ARGS__)
#define h264e_dbg_flow(fmt, ...)    h264e_dbg_f(H264E_DBG_FLOW, fmt, ## __VA_ARGS__)
#define h264e_dbg_ctrl(fmt, ...)    h264e_dbg_f(H264E_DBG_CTRL, fmt, ## __VA_ARGS__)
#define h264e_dbg_detail(fmt, ...)  h264e_dbg_f(H264E_DBG_DETAIL, fmt, ## __VA_ARGS__)

#define h264e_dbg_sps(fmt, ...)     h264e_dbg_f(H264E_DBG_SPS, fmt, ## __VA_ARGS__)
#define h264e_dbg_pps(fmt, ...)     h264e_dbg_f(H264E_DBG_PPS, fmt, ## __VA_ARGS__)
#define h264e_dbg_slice(fmt, ...)   h264e_dbg_f(H264E_DBG_SLICE, fmt, ## __VA_ARGS__)
#define h264e_dbg_sei(fmt, ...)     h264e_dbg_f(H264E_DBG_SEI, fmt, ## __VA_ARGS__)

#define h264e_dbg_dpb(fmt, ...)     h264e_dbg_f(H264E_DBG_DPB, fmt, ## __VA_ARGS__)
#define h264e_dbg_list(fmt, ...)    h264e_dbg_f(H264E_DBG_LIST, fmt, ## __VA_ARGS__)
#define h264e_dbg_mmco(fmt, ...)    h264e_dbg_f(H264E_DBG_MMCO, fmt, ## __VA_ARGS__)

extern RK_U32 h264e_debug;

#endif /* __H264E_DEBUG_H__ */
