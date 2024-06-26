/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 */
#ifndef __ROCKCHIP_MPP_VCODEC_FLOW_H__
#define __ROCKCHIP_MPP_VCODEC_FLOW_H__

int mpp_vcodec_enc_routine(void *param);
void *mpp_vcodec_dec_routine(void *param);
void mpp_vcodec_enc_int_handle(int chan_id);
int mpp_vcodec_enc_run_task(RK_U32 chan_id);

#endif
