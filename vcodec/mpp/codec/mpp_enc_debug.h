// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __MPP_ENC_DEBUG_H__
#define __MPP_ENC_DEBUG_H__

#include "mpp_log.h"
extern RK_U32 mpp_enc_debug;

#define MPP_ENC_DBG_FUNCTION            (0x00000001)
#define MPP_ENC_DBG_CONTROL             (0x00000002)
#define MPP_ENC_DBG_STATUS              (0x00000010)
#define MPP_ENC_DBG_DETAIL              (0x00000020)
#define MPP_ENC_DBG_RESET               (0x00000040)
#define MPP_ENC_DBG_NOTIFY              (0x00000080)
#define MPP_ENC_DBG_REENC               (0x00000100)

#define MPP_ENC_DBG_FRM_STATUS          (0x00010000)

#define mpp_enc_dbg(flag, fmt, ...)     _mpp_dbg(mpp_enc_debug, flag, fmt, ## __VA_ARGS__)
#define mpp_enc_dbg_f(flag, fmt, ...)   _mpp_dbg_f(mpp_enc_debug, flag, fmt, ## __VA_ARGS__)

#define enc_dbg_func(fmt, ...)          mpp_enc_dbg_f(MPP_ENC_DBG_FUNCTION, fmt, ## __VA_ARGS__)
#define enc_dbg_ctrl(fmt, ...)          mpp_enc_dbg_f(MPP_ENC_DBG_CONTROL, fmt, ## __VA_ARGS__)
#define enc_dbg_status(fmt, ...)        mpp_enc_dbg_f(MPP_ENC_DBG_STATUS, fmt, ## __VA_ARGS__)
#define enc_dbg_detail(fmt, ...)        mpp_enc_dbg_f(MPP_ENC_DBG_DETAIL, fmt, ## __VA_ARGS__)
#define enc_dbg_notify(fmt, ...)        mpp_enc_dbg_f(MPP_ENC_DBG_NOTIFY, fmt, ## __VA_ARGS__)
#define enc_dbg_reenc(fmt, ...)         mpp_enc_dbg_f(MPP_ENC_DBG_REENC, fmt, ## __VA_ARGS__)
#define enc_dbg_frm_status(fmt, ...)    mpp_enc_dbg_f(MPP_ENC_DBG_FRM_STATUS, fmt, ## __VA_ARGS__)

#endif /* __MPP_ENC_DEBUG_H__ */
