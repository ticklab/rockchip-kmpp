/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#ifndef __ROCKCHIP_MPP_VCODEC_EVENT_H__
#define __ROCKCHIP_MPP_VCODEC_EVENT_H__

#include <linux/types.h>

#include "mpp_vcodec_intf.h"

struct vcodec_event_msg_head {
	u16 size;
	u16 version;
	enum vcodec_event_id msg_id;
	u64 reserve;
	u64 send_id;
	u64 recv_id;
};

/* channel event */
struct vcodec_msg_chan_creates {
	struct vcodec_event_msg_head head;

	u32 lowdelay_input;
	u32 lowdelay_output;
} vcodec_msg_chan_create;

struct vcodec_msg_chan_destroy {
	struct vcodec_event_msg_head head;
} vcodec_msg_chan_destroy;

struct vcodec_msg_chan_reset {
	struct vcodec_event_msg_head head;
} vcodec_msg_chan_reset;

struct vcodec_msg_chan_control {
	struct vcodec_event_msg_head head;

	/* generate control config */
	void *cfg;
} vcodec_msg_chan_control;

/* encoder data flow control event */
struct vcodec_msg_chan_start {
	struct vcodec_event_msg_head head;
} vcodec_msg_chan_start;

struct vcodec_msg_chan_stop {
	struct vcodec_event_msg_head head;
} vcodec_msg_chan_stop;

struct vcodec_msg_chan_pause {
	struct vcodec_event_msg_head head;
} vcodec_msg_chan_pause;

struct vcodec_msg_chan_resume {
	struct vcodec_event_msg_head head;
} vcodec_msg_chan_resume;

/* encoder data flow event */
struct vcodec_msg_enc_in_frm_cfg_rdy {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_in_frm_cfg_rdy;

struct vcodec_msg_enc_in_frm_start {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_in_frm_start;

struct vcodec_msg_enc_in_frm_early_end {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_in_frm_early_end;

struct vcodec_msg_enc_in_frm_end {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_in_frm_end;

struct vcodec_msg_enc_in_block {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_in_block;

struct vcodec_msg_enc_out_strm_q_full {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_out_strm_q_full;

struct vcodec_msg_enc_out_strm_buf_rdy {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_out_strm_buf_rdy;

struct vcodec_msg_enc_out_strm_end {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_out_strm_end;

struct vcodec_msg_enc_out_block {
	struct vcodec_event_msg_head head;
} vcodec_msg_enc_out_block;

#endif
