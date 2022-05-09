/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#ifndef __ROCKCHIP_MPP_VCODEC_CHAN_H__
#define __ROCKCHIP_MPP_VCODEC_CHAN_H__
#include "rk_type.h"

int mpp_vcodec_chan_create(struct vcodec_attr *attr);
int mpp_vcodec_chan_destory(int chan_id, MppCtxType type);
int mpp_vcodec_chan_start(int chan_id, MppCtxType type);
int mpp_vcodec_chan_stop(int chan_id, MppCtxType type);
int mpp_vcodec_chan_pause(int chan_id, MppCtxType type);
int mpp_vcodec_chan_resume(int chan_id, MppCtxType type);
int mpp_vcodec_chan_control(int chan_id, MppCtxType type, int cmd, void *arg);
int mpp_vcodec_chan_push_frm(int chan_id, void *param);
int mpp_vcodec_chan_get_stream(int chan_id, MppCtxType type,
			       struct venc_packet *enc_packet);

int mpp_vcodec_chan_put_stream(int chan_id, MppCtxType type,
			       struct venc_packet *enc_packet);

/* return channel id to run. return -1 for no task to run */
int mpp_vcodec_schedule(void);

#endif
