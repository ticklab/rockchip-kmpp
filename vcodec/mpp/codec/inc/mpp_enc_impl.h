// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_ENC_IMPL_H__
#define __MPP_ENC_IMPL_H__
#include <linux/semaphore.h>
#include "enc_impl.h"
#include "mpp_enc_hal.h"
#include "mpp_enc_ref.h"
#include "mpp_enc_refs.h"

#include "rc.h"
#include "hal_info.h"
#define  MAX_USRDATA_CNT 4

typedef union MppEncHeaderStatus_u {
	RK_U32 val;
	struct {
		RK_U32 ready: 1;

		RK_U32 added_by_ctrl: 1;
		RK_U32 added_by_mode: 1;
		RK_U32 added_by_change: 1;
	};
} MppEncHeaderStatus;

typedef struct RcApiStatus_t {
	RK_U32 rc_api_inited: 1;
	RK_U32 rc_api_updated: 1;
	RK_U32 rc_api_user_cfg: 1;
} RcApiStatus;

typedef struct MppUserDataRb_t {
	RK_U8 read_pos;
	RK_U8 write_pos;
	RK_U8 free_cnt;
	RK_U8 len[MAX_USRDATA_CNT];
	RK_U8 data[MAX_USRDATA_CNT][1024];
} MppUserDataRb;


typedef struct MppEncImpl_t {
	MppCodingType coding;
	EncImpl impl;
	MppEncHal enc_hal;

	/* device from hal */
	MppDev dev;
	HalInfo hal_info;
	RK_S64 time_base;
	RK_S64 time_end;
	RK_S64 init_time;
	RK_S32 frame_count;
	RK_S32 frame_force_drop;
	RK_S32 hal_info_updated;

	/*
	 * Rate control plugin parameters
	 */
	RcCtx rc_ctx;
	EncRcTask rc_task;

	/*
	 * thread input / output context
	 */
	MppFrame frame;
	MppPacket packet;
	RK_U32 low_delay_part_mode;

	/* base task information */
	RK_U32 task_idx;
	RK_S64 task_pts;
	MppBuffer frm_buf;
	ring_buf  *pkt_buf;

	RK_U32 reset_flag;

	// legacy support for MPP_ENC_GET_EXTRA_INFO
	MppPacket hdr_pkt;
	void *hdr_buf;
	RK_U32 hdr_len;
	MppEncHeaderStatus hdr_status;
	MppEncHeaderMode hdr_mode;
	MppEncSeiMode sei_mode;

	/* information for debug prefix */
	const char *version_info;
	RK_S32 version_length;
	char *rc_cfg_info;
	RK_S32 rc_cfg_pos;
	RK_S32 rc_cfg_length;
	RK_S32 rc_cfg_size;
	RK_U32 rc_api_user_cfg;
	void *enc_task;

	/* cpb parameters */
	MppEncRefs refs;
	MppEncRefFrmUsrCfg frm_cfg;

	/* two-pass deflicker parameters */
	EncRcTaskInfo       rc_info_prev;
	/* Encoder configure set */
	MppEncCfgSet cfg;
	GopMode  gop_mode;
	RK_U32   real_fps;
	RK_U32   stop_flag;
	RK_U32   hw_run;
	RK_U32   enc_status;
	RK_U32   online;
	RK_U32   ref_buf_shared;
	RK_U32   qpmap_en;
	struct semaphore enc_sem;
	struct mpi_buf_pool *strm_pool;
	MppEncROICfg cur_roi;
	MppEncOSDData3 cur_osd;
	MppUserDataRb rb_userdata;
	ring_buf_pool *ring_pool;
	RK_U32 ring_buf_size;
	RK_U32 max_strm_cnt;
	RK_U32 pkt_fail_cnt;
	RK_U32 ringbuf_fail_cnt;
	RK_U32 cfg_fail_cnt;
	RK_U32 start_fail_cnt;
	struct hal_shared_buf *shared_buf;
	MppBuffer mv_info;
	MppBuffer qpmap;
	RK_U8 *mv_flag;
	RK_U32 qp_out;
	RK_U32		chan_id;
	RK_U32 motion_static_switch_en;
} MppEncImpl;

enum enc_status {
	ENC_STATUS_CFG_IN,
	ENC_STATUS_CFG_DONE,
	ENC_STATUS_START_IN,
	ENC_STATUS_START_DONE,
	ENC_STATUS_INT_IN,
	ENC_STATUS_INT_DONE,
	ENC_STATUS_BUTT,
};


#ifdef __cplusplus
extern "C" {
#endif
MPP_RET mpp_enc_impl_reg_cfg(MppEnc ctx, MppFrame frame);
MPP_RET mpp_enc_proc_cfg(MppEncImpl * enc, MpiCmd cmd, void *param);
MPP_RET mpp_enc_impl_alloc_task(MppEncImpl * enc);
MPP_RET mpp_enc_impl_free_task(MppEncImpl * enc);
MPP_RET mpp_enc_proc_rc_update(MppEncImpl * enc);
MPP_RET mpp_enc_impl_int(MppEnc ctx, MppEnc jpeg_ctx, MppPacket * packet, MppPacket * jpeg_packet);
MPP_RET mpp_enc_impl_hw_start(MppEnc ctx, MppEnc jpeg_ctx);
void    mpp_enc_impl_poc_debug_info(void *seq_file, MppEnc ctx, RK_U32 chl_id);
MPP_RET mpp_enc_unref_osd_buf(MppEncOSDData3 *osd);
#ifdef __cplusplus
}
#endif
#endif /*__MPP_ENC_IMPL_H__*/
