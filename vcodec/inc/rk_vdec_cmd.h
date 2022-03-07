// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __RK_VDEC_CMD_H__
#define __RK_VDEC_CMD_H__

#include "rk_type.h"

/*
 * decoder query interface is only for debug usage
 */
#define MPP_DEC_QUERY_STATUS        (0x00000001)
#define MPP_DEC_QUERY_WAIT          (0x00000002)
#define MPP_DEC_QUERY_FPS           (0x00000004)
#define MPP_DEC_QUERY_BPS           (0x00000008)
#define MPP_DEC_QUERY_DEC_IN_PKT    (0x00000010)
#define MPP_DEC_QUERY_DEC_WORK      (0x00000020)
#define MPP_DEC_QUERY_DEC_OUT_FRM   (0x00000040)

#define MPP_DEC_QUERY_ALL           (MPP_DEC_QUERY_STATUS       | \
                                     MPP_DEC_QUERY_WAIT         | \
                                     MPP_DEC_QUERY_FPS          | \
                                     MPP_DEC_QUERY_BPS          | \
                                     MPP_DEC_QUERY_DEC_IN_PKT   | \
                                     MPP_DEC_QUERY_DEC_WORK     | \
                                     MPP_DEC_QUERY_DEC_OUT_FRM)

typedef struct MppDecQueryCfg_t {
	/*
	 * 32 bit query flag for query data check
	 * Each bit represent a query data switch.
	 * bit 0 - for querying decoder runtime status
	 * bit 1 - for querying decoder runtime waiting status
	 * bit 2 - for querying decoder realtime decode fps
	 * bit 3 - for querying decoder realtime input bps
	 * bit 4 - for querying decoder input packet count
	 * bit 5 - for querying decoder start hardware times
	 * bit 6 - for querying decoder output frame count
	 */
	RK_U32      query_flag;

	/* 64 bit query data output */
	RK_U32      rt_status;
	RK_U32      rt_wait;
	RK_U32      rt_fps;
	RK_U32      rt_bps;
	RK_U32      dec_in_pkt_cnt;
	RK_U32      dec_hw_run_cnt;
	RK_U32      dec_out_frm_cnt;
} MppDecQueryCfg;

#endif /*__RK_VDEC_CMD_H__*/
