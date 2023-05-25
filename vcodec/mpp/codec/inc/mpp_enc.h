// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_ENC_H__
#define __MPP_ENC_H__

#include "rk_type.h"
#include "mpp_err.h"
#include "rk_mpi_cmd.h"

/*
 * Configure of encoder is separated into four parts.
 *
 * 1. Rate control parameter
 *    This is quality and bitrate request from user.
 *    For controller only
 *
 * 2. Data source MppFrame parameter
 *    This is data source buffer information.
 *    For both controller and hal
 *
 * 3. Video codec infomation
 *    This is user custormized stream information.
 *    For hal only
 *
 * 4. Extra parameter
 *    including:
 *    PreP  : encoder Preprocess configuration
 *    ROI   : Region Of Interest
 *    OSD   : On Screen Display
 *    MD    : Motion Detection
 *    extra : SEI for h.264 / Exif for mjpeg
 *    For hal only
 *
 * The module transcation flow is as follows:
 *
 *                 +                      +
 *     User        |      Mpi/Mpp         |         EncImpl
 *                 |                      |            Hal
 *                 |                      |
 * +----------+    |    +---------+       |       +------------+
 * |          |    |    |         +-----RcCfg----->            |
 * |  RcCfg   +--------->         |       |       | EncImpl |
 * |          |    |    |         |   +-Frame----->            |
 * +----------+    |    |         |   |   |       +---+-----^--+
 *                 |    |         |   |   |           |     |
 *                 |    |         |   |   |           |     |
 * +----------+    |    |         |   |   |        syntax   |
 * |          |    |    |         |   |   |           |     |
 * | MppFrame +--------->  MppEnc +---+   |           |   result
 * |          |    |    |         |   |   |           |     |
 * +----------+    |    |         |   |   |           |     |
 *                 |    |         |   |   |       +---v-----+--+
 *                 |    |         |   +-Frame----->            |
 * +----------+    |    |         |       |       |            |
 * |          |    |    |         +---CodecCfg---->    Hal     |
 * | CodecCfg +--------->         |       |       |            |
 * |          |    |    |         <-----Extra----->            |
 * +----------+    |    +---------+       |       +------------+
 *                 |                      |
 *                 |                      |
 *                 +                      +
 *
 * The function call flow is shown below:
 *
 *  mpi                      mpp_enc         controller                  hal
 *   +                          +                 +                       +
 *   |                          |                 |                       |
 *   |                          |                 |                       |
 *   +----------init------------>                 |                       |
 *   |                          |                 |                       |
 *   |                          |                 |                       |
 *   |         PrepCfg          |                 |                       |
 *   +---------control---------->     PrepCfg     |                       |
 *   |                          +-----control----->                       |
 *   |                          |                 |        PrepCfg        |
 *   |                          +--------------------------control-------->
 *   |                          |                 |                    allocate
 *   |                          |                 |                     buffer
 *   |                          |                 |                       |
 *   |          RcCfg           |                 |                       |
 *   +---------control---------->      RcCfg      |                       |
 *   |                          +-----control----->                       |
 *   |                          |              rc_init                    |
 *   |                          |                 |                       |
 *   |                          |                 |                       |
 *   |         CodecCfg         |                 |                       |
 *   +---------control---------->                 |        CodecCfg       |
 *   |                          +--------------------------control-------->
 *   |                          |                 |                    generate
 *   |                          |                 |                    sps/pps
 *   |                          |                 |     Get extra info    |
 *   |                          +--------------------------control-------->
 *   |      Get extra info      |                 |                       |
 *   +---------control---------->                 |                       |
 *   |                          |                 |                       |
 *   |                          |                 |                       |
 *   |         ROICfg           |                 |                       |
 *   +---------control---------->                 |        ROICfg         |
 *   |                          +--------------------------control-------->
 *   |                          |                 |                       |
 *   |         OSDCfg           |                 |                       |
 *   +---------control---------->                 |        OSDCfg         |
 *   |                          +--------------------------control-------->
 *   |                          |                 |                       |
 *   |          MDCfg           |                 |                       |
 *   +---------control---------->                 |         MDCfg         |
 *   |                          +--------------------------control-------->
 *   |                          |                 |                       |
 *   |      Set extra info      |                 |                       |
 *   +---------control---------->                 |     Set extra info    |
 *   |                          +--------------------------control-------->
 *   |                          |                 |                       |
 *   |           task           |                 |                       |
 *   +----------encode---------->      task       |                       |
 *   |                          +-----encode------>                       |
 *   |                          |              encode                     |
 *   |                          |                 |        syntax         |
 *   |                          +--------------------------gen_reg-------->
 *   |                          |                 |                       |
 *   |                          |                 |                       |
 *   |                          +---------------------------start--------->
 *   |                          |                 |                       |
 *   |                          |                 |                       |
 *   |                          +---------------------------wait---------->
 *   |                          |                 |                       |
 *   |                          |    callback     |                       |
 *   |                          +----------------->                       |
 *   +--OSD-MD--encode---------->                 |                       |
 *   |             .            |                 |                       |
 *   |             .            |                 |                       |
 *   |             .            |                 |                       |
 *   +--OSD-MD--encode---------->                 |                       |
 *   |                          |                 |                       |
 *   +----------deinit---------->                 |                       |
 *   +                          +                 +                       +
 */

typedef void *MppEnc;

typedef struct MppEncInitCfg_t {
	MppCodingType	coding;
	RK_S32		online;
	RK_U32		buf_size;
	RK_U32      max_strm_cnt;
	RK_U32		ref_buf_shared;
	RK_U32		smart_en;
	struct      hal_shared_buf *shared_buf;
	RK_U32		qpmap_en;
	RK_U32		chan_id;
	RK_U32		motion_static_switch_en;
	RK_U32      only_smartp;
} MppEncInitCfg;

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET mpp_enc_init(MppEnc * ctx, MppEncInitCfg * cfg);
MPP_RET mpp_enc_deinit(MppEnc ctx);

MPP_RET mpp_enc_start(MppEnc ctx);
MPP_RET mpp_enc_stop(MppEnc ctx);
MPP_RET mpp_enc_run_task(MppEnc ctx);
RK_S32 mpp_enc_check_hw_running(MppEnc ctx);
RK_S32 mpp_enc_unbind_jpeg_task(MppEnc ctx);
bool mpp_enc_check_is_int_process(MppEnc ctx);

MPP_RET mpp_enc_control(MppEnc ctx, MpiCmd cmd, void *param);
MPP_RET mpp_enc_notify(MppEnc ctx, RK_U32 flag);
MPP_RET mpp_enc_reset(MppEnc ctx);
MPP_RET mpp_enc_oneframe(MppEnc ctx, MppFrame frame,
			 MppPacket * packet);
MPP_RET mpp_enc_cfg_reg(MppEnc ctx, MppFrame frame);	//no block
MPP_RET mpp_enc_hw_start(MppEnc ctx, MppEnc jpeg_ctx);	//no block

MPP_RET mpp_enc_int_process(MppEnc ctx, MppEnc jpeg_ctx, MppPacket * packet,
			    MppPacket * jpeg_packet);
MPP_RET mpp_enc_register_chl(MppEnc ctx, void *func, RK_S32 chan_id);
void    mpp_enc_proc_debug(void *seq_file, MppEnc ctx, RK_U32 chl_id);
RK_S32  mpp_enc_check_pkt_pool(MppEnc ctx);
void mpp_enc_deinit_frame(MppEnc ctx);

#ifdef __cplusplus
}
#endif
#endif /*__MPP_ENC_H__*/
