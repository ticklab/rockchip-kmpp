/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/cdev.h>

#include "mpp_vcodec_chan.h"
#include "mpp_vcodec_base.h"
#include "mpp_vcodec_flow.h"
#include "mpp_vcodec_intf.h"

#include "mpp_log.h"
#include "mpp_enc.h"
#include "mpp_vcodec_thread.h"
#include "rk_venc_cfg.h"

void mpp_enc_cfg_setup(int chan_id, MppEncCfg cfg)
{
	 RK_U32 width = 1280;
	RK_U32 height = 720;
	RK_U32 hor_stride = 1280;
	RK_U32 ver_stride = 720;
	RK_S32 fps_in_flex = 0;
	RK_S32 fps_in_den = 1;
	RK_S32 fps_in_num = 25;
	RK_S32 fps_out_flex = 0;
	RK_S32 fps_out_den = 1;
	RK_S32 fps_out_num = 25;
	RK_S32 bps = 0;
	RK_S32 bps_max = 0;
	RK_S32 bps_min = 0;
	RK_S32 rc_mode = MPP_ENC_RC_MODE_CBR;
	RK_U32 gop_len = 60;
	MppCodingType type = MPP_VIDEO_CodingHEVC;
	 if (!bps)
		bps = width * height / 8 * (fps_out_num / fps_out_den);
	mpp_enc_cfg_set_s32(cfg, "prep:width", width);
	mpp_enc_cfg_set_s32(cfg, "prep:height", height);
	mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
	mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
	mpp_enc_cfg_set_s32(cfg, "prep:format", 0);
	 mpp_enc_cfg_set_s32(cfg, "rc:mode", rc_mode);

	    /* fix input / output frame rate */
	    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", fps_in_flex);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps_in_num);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", fps_in_den);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", fps_out_flex);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps_out_num);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", fps_out_den);
	mpp_enc_cfg_set_s32(cfg, "rc:gop",
			     gop_len ? gop_len : fps_out_num * 2);

	    /* drop frame or not when bitrate overflow */
	    mpp_enc_cfg_set_u32(cfg, "rc:drop_mode",
				MPP_ENC_RC_DROP_FRM_DISABLED);
	mpp_enc_cfg_set_u32(cfg, "rc:drop_thd", 20);	/* 20% of max bps */
	mpp_enc_cfg_set_u32(cfg, "rc:drop_gap", 1);	/* Do not continuous drop frame */

	    /* setup bitrate for different rc_mode */
	    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
	switch (rc_mode) {
	case MPP_ENC_RC_MODE_FIXQP:{

			    /* do not setup bitrate on FIXQP mode */
		}
		break;
	case MPP_ENC_RC_MODE_CBR:{

			    /* CBR mode has narrow bound */
			    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",
						bps_max ? bps_max : bps * 17 /
						16);
			mpp_enc_cfg_set_s32(cfg, "rc:bps_min",
					     bps_min ? bps_min : bps * 15 / 16);
		}
		break;
	case MPP_ENC_RC_MODE_VBR:
	case MPP_ENC_RC_MODE_AVBR:{

			    /* VBR mode has wide bound */
			    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",
						bps_max ? bps_max : bps * 17 /
						16);
			mpp_enc_cfg_set_s32(cfg, "rc:bps_min",
					     bps_min ? bps_min : bps * 1 / 16);
		}
		break;
	default:{

			    /* default use CBR mode */
			    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",
						bps_max ? bps_max : bps * 17 /
						16);
			mpp_enc_cfg_set_s32(cfg, "rc:bps_min",
					     bps_min ? bps_min : bps * 15 / 16);
		}
		break;
	}

	    /* setup qp for different codec and rc_mode */
	    switch (type) {
	case MPP_VIDEO_CodingAVC:
	case MPP_VIDEO_CodingHEVC:{
			switch (rc_mode) {
			case MPP_ENC_RC_MODE_FIXQP:{
					mpp_enc_cfg_set_s32(cfg, "rc:qp_init",
							     20);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_max",
							     20);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_min",
							     20);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i",
							     20);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i",
							     20);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_ip",
							     2);
				}
				break;
			case MPP_ENC_RC_MODE_CBR:
			case MPP_ENC_RC_MODE_VBR:
			case MPP_ENC_RC_MODE_AVBR:{
					mpp_enc_cfg_set_s32(cfg, "rc:qp_init",
							     26);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_max",
							     51);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_min",
							     10);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i",
							     51);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i",
							     10);
					mpp_enc_cfg_set_s32(cfg, "rc:qp_ip",
							     2);
				}
				break;
			default:{
					mpp_err_f
					    ("unsupport encoder rc mode %d\n",
					     rc_mode);
				}
				break;
			}
		}
		break;
	case MPP_VIDEO_CodingVP8:{

			    /* vp8 only setup base qp range */
			    mpp_enc_cfg_set_s32(cfg, "rc:qp_init", 40);
			mpp_enc_cfg_set_s32(cfg, "rc:qp_max", 127);
			mpp_enc_cfg_set_s32(cfg, "rc:qp_min", 0);
			mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", 127);
			mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", 0);
			mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 6);
		}
		break;
	case MPP_VIDEO_CodingMJPEG:{

			    /* jpeg use special codec config to control qtable */
			    mpp_enc_cfg_set_s32(cfg, "jpeg:q_factor", 80);
			mpp_enc_cfg_set_s32(cfg, "jpeg:qf_max", 99);
			mpp_enc_cfg_set_s32(cfg, "jpeg:qf_min", 1);
		}
		break;
	default:{
		}
		break;
	}

	    /* setup codec  */
	    mpp_enc_cfg_set_s32(cfg, "codec:type", type);
	switch (type) {
	case MPP_VIDEO_CodingAVC:{

			    /*
			     * H.264 profile_idc parameter
			     * 66  - Baseline profile
			     * 77  - Main profile
			     * 100 - High profile
			     */
			    mpp_enc_cfg_set_s32(cfg, "h264:profile", 100);

			    /*
			     * H.264 level_idc parameter
			     * 10 / 11 / 12 / 13    - qcif@15fps / cif@7.5fps / cif@15fps / cif@30fps
			     * 20 / 21 / 22         - cif@30fps / half-D1@@25fps / D1@12.5fps
			     * 30 / 31 / 32         - D1@25fps / 720p@30fps / 720p@60fps
			     * 40 / 41 / 42         - 1080p@30fps / 1080p@30fps / 1080p@60fps
			     * 50 / 51 / 52         - 4K@30fps
			     */
			    mpp_enc_cfg_set_s32(cfg, "h264:level", 40);
			mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
			mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
			mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", 1);
		}
		break;
	case MPP_VIDEO_CodingHEVC:
	case MPP_VIDEO_CodingMJPEG:
	case MPP_VIDEO_CodingVP8:{
		}
		break;
	default:{
			mpp_err_f("unsupport encoder coding type %d\n", type);
		}
		break;
	}
	mpp_vcodec_chan_control(chan_id, MPP_CTX_ENC, MPP_ENC_SET_CFG, cfg);
}

 void enc_test(void)
{
	RK_U32 i = 0;
	struct venc_module *venc = NULL;
	struct vcodec_threads *thd;
	RK_U32 chnl_num = 3;
	MppEncCfg cfg = NULL;
	struct vcodec_attr attr;
	struct mpp_chan *chan_entry = NULL;
	mpp_enc_cfg_api_init();
	pr_info("mpp_enc_cfg_api_init ok");
	for (i = 0; i < chnl_num; i++) {
		mpp_enc_cfg_init(&cfg);
		attr.chan_id = i;
		attr.type = MPP_CTX_ENC;
		attr.coding = MPP_VIDEO_CodingHEVC;
		mpp_vcodec_chan_create(&attr);
		mpp_enc_cfg_setup(i, cfg);
		mpp_vcodec_chan_start(i, MPP_CTX_ENC);
		mpp_enc_cfg_deinit(cfg);
		cfg = NULL;
	}
	chan_entry = mpp_vcodec_get_chan_entry(0, MPP_CTX_ENC);
	venc = mpp_vcodec_get_enc_module_entry();
	thd = venc->thd;
}

 void test_end(void)
{
	RK_U32 i = 0;
	RK_U32 chnl_num = 1;
	for (i = 0; i < chnl_num; i++) {
		mpp_vcodec_chan_stop(i, MPP_CTX_ENC);
		mpp_vcodec_chan_destory(i, MPP_CTX_ENC);
	}
	mpp_enc_cfg_api_deinit();
}


