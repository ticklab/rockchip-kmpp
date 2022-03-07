/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#ifndef __ROCKCHIP_MPP_VCODEC_INTF_H__
#define __ROCKCHIP_MPP_VCODEC_INTF_H__

/* Use 'v' as magic number */
#define VOCDEC_IOC_MAGIC		'V'
#define VOCDEC_IOC_CFG	_IOW(VOCDEC_IOC_MAGIC, 1, unsigned int)

struct vcodec_request {
	__u32 cmd;
	__u32 ctrl_cmd;
	__u32 size;
	void __user *data;
};

#define VCODEC_ID_BASE_COMMON       (0x00000000)
#define VCODEC_ID_BASE_STATE        (0x00000100)
#define VCODEC_ID_BASE_FLOW         (0x00000200)

#define VCODEC_ID_BASE_INPUT        (0x00000400)
#define VCODEC_ID_BASE_INPUT_ACK    (0x00000500)

#define VCODEC_ID_BASE_OUTPUT       (0x00000600)
#define VCODEC_ID_BASE_OUTPUT_ACK   (0x00000700)

/*
 * Event call flow definition
 *
 *
 *  prev module          vcodec module           next module
 *      |                     |                      |
 *      |                     |                      |
 *      |   input event       |                      |
 *      +-------------------->|                      |
 *      |                     |                      |
 *      |   input ack event   |                      |
 *      |<--------------------+                      |
 *      |                     |                      |
 *      |                     |   output event       |
 *      |                     +--------------------->|
 *      |                     |                      |
 *      |                     |   output ack event   |
 *      |                     +<---------------------|
 *      |                     |                      |
 *      |                     |                      |
 */

enum vcodec_event_id {
	/* channel comment event */
	VCODEC_CHAN_CREATE = VCODEC_ID_BASE_COMMON,
	VCODEC_CHAN_DESTROY,
	VCODEC_CHAN_RESET,
	VCODEC_CHAN_CONTROL,

	/* channel state change event */
	VCODEC_CHAN_START = VCODEC_ID_BASE_STATE,
	VCODEC_CHAN_STOP,
	VCODEC_CHAN_PAUSE,
	VCODEC_CHAN_RESUME,

	/* channel data flow event */
	VCODEC_CHAN_BIND = VCODEC_ID_BASE_FLOW,
	VCODEC_CHAN_UNBIND,

	/* channel input side io event from external module */
	VCODEC_CHAN_IN_FRM_RDY = VCODEC_ID_BASE_INPUT,
	VCODEC_CHAN_IN_FRM_START,
	VCODEC_CHAN_IN_FRM_EARLY_END,
	VCODEC_CHAN_IN_FRM_END,

	/* channel input side ack event from vcodec module */
	VCODEC_CHAN_IN_BLOCK = VCODEC_ID_BASE_INPUT_ACK,

	/* channel output side io event from vcodec module */
	VCODEC_CHAN_OUT_STRM_Q_FULL = VCODEC_ID_BASE_OUTPUT,
	VCODEC_CHAN_OUT_STRM_BUF_RDY,
	VCODEC_CHAN_OUT_STRM_END,

	/* channel input side ack event from external module */
	VCODEC_CHAN_OUT_BLOCK = VCODEC_ID_BASE_OUTPUT_ACK,

};

struct vcodec_event_msg_head;
struct mpi_obj;

int mpp_vcodec_event_notify(struct mpi_obj *obj,
			    struct vcodec_event_msg_head *msg, void *args);

#endif
