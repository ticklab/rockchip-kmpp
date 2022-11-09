// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_enc"

#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>

#include "mpp_time.h"
#include "mpp_maths.h"
#include "mpp_mem.h"

#include "mpp_frame_impl.h"
#include "mpp_packet_impl.h"
#include "mpp_enc_refs_setup.h"
#include "mpp_enc_debug.h"
#include "mpp_enc_cfg_impl.h"
#include "mpp_enc_impl.h"
#include "mpp_packet.h"
#include "mpp_2str.h"
#include "rk_export_func.h"

typedef union EncTaskWait_u {
	RK_U32 val;
	struct {
		RK_U32 enc_frm_in : 1; // 0x0001 MPP_ENC_NOTIFY_FRAME_ENQUEUE
		RK_U32 reserv0002 : 1; // 0x0002
		RK_U32 reserv0004 : 1; // 0x0004
		RK_U32 enc_pkt_out : 1; // 0x0008 MPP_ENC_NOTIFY_PACKET_ENQUEUE

		RK_U32 reserv0010 : 1; // 0x0010
		RK_U32 reserv0020 : 1; // 0x0020
		RK_U32 reserv0040 : 1; // 0x0040
		RK_U32 reserv0080 : 1; // 0x0080

		RK_U32 reserv0100 : 1; // 0x0100
		RK_U32 reserv0200 : 1; // 0x0200
		RK_U32 reserv0400 : 1; // 0x0400
		RK_U32 reserv0800 : 1; // 0x0800

		RK_U32 reserv1000 : 1; // 0x1000
		RK_U32 reserv2000 : 1; // 0x2000
		RK_U32 reserv4000 : 1; // 0x4000
		RK_U32 reserv8000 : 1; // 0x8000
	};
} EncTaskWait;

/* encoder internal work flow */
typedef union EncTaskStatus_u {
	RK_U32 val;
	struct {
		RK_U32 task_in_rdy : 1;
		RK_U32 task_out_rdy : 1;

		RK_U32 frm_pkt_rdy : 1;

		RK_U32 hal_task_reset_rdy : 1; // reset hal task to start
		RK_U32 rc_check_frm_drop : 1; // rc  stage
		RK_U32 pkt_buf_rdy : 1; // prepare pkt buf

		RK_U32 enc_start : 1; // enc stage
		RK_U32 refs_force_update : 1; // enc stage
		RK_U32 low_delay_again : 1; // enc stage low delay output again

		RK_U32 enc_backup : 1; // enc stage
		RK_U32 enc_restore : 1; // reenc flow start point
		RK_U32 enc_proc_dpb : 1; // enc stage
		RK_U32 rc_frm_start : 1; // rc  stage
		RK_U32 check_type_reenc : 1; // flow checkpoint if reenc -> enc_restore
		RK_U32 enc_proc_hal : 1; // enc stage
		RK_U32 hal_get_task : 1; // hal stage
		RK_U32 rc_hal_start : 1; // rc  stage
		RK_U32 hal_gen_reg : 1; // hal stage
		RK_U32 hal_start : 1; // hal stage
		RK_U32 hal_wait : 1; // hal stage NOTE: special in low delay mode
		RK_U32 rc_hal_end : 1; // rc  stage
		RK_U32 hal_ret_task : 1; // hal stage
		RK_U32 enc_update_hal : 1; // enc stage
		RK_U32 rc_frm_end : 1; // rc  stage
		RK_U32 rc_reenc : 1; // need reenc
	};
} EncTaskStatus;

typedef struct EncTask_t {
	HalTaskHnd hnd;

	RK_S32 seq_idx;
	EncTaskStatus status;
	EncTaskWait wait;
	HalTaskInfo info;
} EncTask;

static RK_U8 uuid_version[16] = {
	0x3d, 0x07, 0x6d, 0x45, 0x73, 0x0f, 0x41, 0xa8,
	0xb1, 0xc4, 0x25, 0xd7, 0x97, 0x6b, 0xf1, 0xac,
};

static RK_U8 uuid_rc_cfg[16] = {
	0xd7, 0xdc, 0x03, 0xc3, 0xc5, 0x6f, 0x40, 0xe0,
	0x8e, 0xa9, 0x17, 0x1a, 0xd2, 0xef, 0x5e, 0x23,
};

static RK_U8 uuid_usr_data[16] = {
	0xfe, 0x39, 0xac, 0x4c, 0x4a, 0x8e, 0x4b, 0x4b,
	0x85, 0xd9, 0xb2, 0xa2, 0x4f, 0xa1, 0x19, 0x5b,
};

#if 0
static RK_U8 uuid_debug_info[16] = {
	0x57, 0x68, 0x97, 0x80, 0xe7, 0x0c, 0x4b, 0x65,
	0xa9, 0x06, 0xae, 0x29, 0x94, 0x11, 0xcd, 0x9a
};
#endif
static void reset_hal_enc_task(HalEncTask *task)
{
	memset(task, 0, sizeof(*task));
}

static void reset_enc_rc_task(EncRcTask *task)
{
	memset(task, 0, sizeof(*task));
}

static void reset_enc_task(MppEncImpl *enc)
{
	enc->packet = NULL;
	enc->frame = NULL;

	enc->frm_buf = NULL;
	enc->pkt_buf = NULL;

	/* NOTE: clear add_by flags */
	enc->hdr_status.val = enc->hdr_status.ready;
}

static void update_enc_hal_info(MppEncImpl *enc)
{
	//    MppDevInfoCfg data[32];
	//    RK_S32 size = sizeof(data);
	//    RK_S32 i;

	if (NULL == enc->hal_info || NULL == enc->dev)
		return;

	//hal_info_from_enc_cfg(enc->hal_info, &enc->cfg);

	//hal_info_get(enc->hal_info, data, &size);
#if 0
	if (size) {
		size /= sizeof(data[0]);
		for (i = 0; i < size; i++)
			mpp_dev_ioctl(enc->dev, MPP_DEV_SET_INFO, &data[i]);
	}
#endif
}

static void update_hal_info_fps(MppEncImpl *enc)
{
	RK_S32 time_diff = ((RK_S32)(enc->time_end - enc->time_base) / 1000);

	enc->real_fps = (enc->frame_count * 1000 * 10) / time_diff;
	enc->time_base = enc->time_end;
	enc->frame_count = 0;
}

static void check_hal_task_pkt_len(HalEncTask *task, const char *reason)
{
	RK_U32 task_length = task->length;
	RK_U32 packet_length = mpp_packet_get_length(task->packet);

	if (task_length != packet_length) {
		mpp_err_f(
			"%s check failed: task length is not match to packet length %d vs %d\n",
			reason, task_length, packet_length);
	}
}

static RK_S32 check_codec_to_resend_hdr(MppEncCodecCfg *codec)
{
	switch (codec->coding) {
	case MPP_VIDEO_CodingAVC: {
		if (codec->h264.change)
			return 1;
	} break;
	case MPP_VIDEO_CodingHEVC: {
		if (codec->h265.change)
			return 1;
	} break;
	case MPP_VIDEO_CodingVP8:
	case MPP_VIDEO_CodingMJPEG:
	default: {
	} break;
	}
	return 0;
}

static RK_S32 check_resend_hdr(MpiCmd cmd, void *param, MppEncCfgSet *cfg)
{
	RK_S32 resend = 0;
	static const char *resend_reason[] = {
		"unchanged",
		"codec/prep cfg change",
		"rc cfg change rc_mode/fps/gop",
		"set cfg change input/format ",
		"set cfg change rc_mode/fps/gop",
		"set cfg change codec",
	};

	if (cfg->codec.coding == MPP_VIDEO_CodingMJPEG ||
	    cfg->codec.coding == MPP_VIDEO_CodingVP8)
		return 0;

	do {
		if (cmd == MPP_ENC_SET_CODEC_CFG ||
		    cmd == MPP_ENC_SET_PREP_CFG ||
		    cmd == MPP_ENC_SET_IDR_FRAME) {
			resend = 1;
			break;
		}

		if (cmd == MPP_ENC_SET_RC_CFG) {
			RK_U32 change = *(RK_U32 *)param;
			RK_U32 check_flag = MPP_ENC_RC_CFG_CHANGE_RC_MODE |
					    MPP_ENC_RC_CFG_CHANGE_FPS_IN |
					    MPP_ENC_RC_CFG_CHANGE_FPS_OUT |
					    MPP_ENC_RC_CFG_CHANGE_GOP;

			if (change & check_flag) {
				resend = 2;
				break;
			}
		}

		if (cmd == MPP_ENC_SET_CFG) {
			RK_U32 change = cfg->prep.change;
			RK_U32 check_flag = MPP_ENC_PREP_CFG_CHANGE_INPUT |
					    MPP_ENC_PREP_CFG_CHANGE_FORMAT |
					    MPP_ENC_PREP_CFG_CHANGE_ROTATION;

			if (change & check_flag) {
				resend = 3;
				break;
			}

			change = cfg->rc.change;
			check_flag = MPP_ENC_RC_CFG_CHANGE_RC_MODE |
				     MPP_ENC_RC_CFG_CHANGE_FPS_IN |
				     MPP_ENC_RC_CFG_CHANGE_FPS_OUT |
				     MPP_ENC_RC_CFG_CHANGE_GOP;

			if (change & check_flag) {
				resend = 4;
				break;
			}
			if (check_codec_to_resend_hdr(&cfg->codec)) {
				resend = 5;
				break;
			}
		}
	} while (0);

	if (resend)
		mpp_log("send header for %s\n", resend_reason[resend]);

	return resend;
}

static RK_S32 check_rc_cfg_update(MpiCmd cmd, MppEncCfgSet *cfg)
{
	if (cmd == MPP_ENC_SET_RC_CFG || cmd == MPP_ENC_SET_PREP_CFG ||
	    cmd == MPP_ENC_SET_REF_CFG)
		return 1;

	if (cmd == MPP_ENC_SET_CFG) {
		RK_U32 change = cfg->prep.change;
		RK_U32 check_flag = MPP_ENC_PREP_CFG_CHANGE_INPUT |
				    MPP_ENC_PREP_CFG_CHANGE_FORMAT;

		if (change & check_flag)
			return 1;

		change = cfg->rc.change;
		check_flag = MPP_ENC_RC_CFG_CHANGE_ALL &
			     (~MPP_ENC_RC_CFG_CHANGE_QUALITY);

		if (change & check_flag)
			return 1;
	}

	return 0;
}

static RK_S32 check_rc_gop_update(MpiCmd cmd, MppEncCfgSet *cfg)
{
	if (((cmd == MPP_ENC_SET_RC_CFG) || (cmd == MPP_ENC_SET_CFG)) &&
	    (cfg->rc.change & MPP_ENC_RC_CFG_CHANGE_GOP))
		return 1;

	return 0;
}

static RK_S32 check_hal_info_update(MpiCmd cmd)
{
	if (cmd == MPP_ENC_SET_CFG || cmd == MPP_ENC_SET_RC_CFG ||
	    cmd == MPP_ENC_SET_CODEC_CFG || cmd == MPP_ENC_SET_PREP_CFG ||
	    cmd == MPP_ENC_SET_REF_CFG)
		return 1;

	return 0;
}

MPP_RET mpp_enc_proc_rc_cfg(MppEncRcCfg *dst, MppEncRcCfg *src)
{
	MPP_RET ret = MPP_OK;
	RK_U32 change = src->change;

	if (change) {
		MppEncRcCfg bak = *dst;

		if (change & MPP_ENC_RC_CFG_CHANGE_RC_MODE)
			dst->rc_mode = src->rc_mode;

		if (change & MPP_ENC_RC_CFG_CHANGE_QUALITY)
			dst->quality = src->quality;

		if (change & MPP_ENC_RC_CFG_CHANGE_BPS) {
			dst->bps_target = src->bps_target;
			dst->bps_max = src->bps_max;
			dst->bps_min = src->bps_min;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_FPS_IN) {
			dst->fps_in_flex = src->fps_in_flex;
			dst->fps_in_num = src->fps_in_num;
			dst->fps_in_denorm = src->fps_in_denorm;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_FPS_OUT) {
			dst->fps_out_flex = src->fps_out_flex;
			dst->fps_out_num = src->fps_out_num;
			dst->fps_out_denorm = src->fps_out_denorm;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_GOP)
			dst->gop = src->gop;

		if (change & MPP_ENC_RC_CFG_CHANGE_MAX_REENC)
			dst->max_reenc_times = src->max_reenc_times;

		if (change & MPP_ENC_RC_CFG_CHANGE_DROP_FRM) {
			dst->drop_mode = src->drop_mode;
			dst->drop_threshold = src->drop_threshold;
			dst->drop_gap = src->drop_gap;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_PRIORITY) {
			if (src->rc_priority >= MPP_ENC_RC_PRIORITY_BUTT) {
				mpp_err("invalid rc_priority %d should be[%d, %d] \n",
					src->rc_priority,
					MPP_ENC_RC_BY_BITRATE_FIRST,
					MPP_ENC_RC_PRIORITY_BUTT);
				ret = MPP_ERR_VALUE;
			}
			dst->rc_priority = src->rc_priority;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_SUPER_FRM) {
			if (src->super_mode >= MPP_ENC_RC_SUPER_FRM_BUTT) {
				mpp_err("invalid super_mode %d should be[%d, %d] \n",
					src->super_mode,
					MPP_ENC_RC_SUPER_FRM_NONE,
					MPP_ENC_RC_SUPER_FRM_BUTT);
				ret = MPP_ERR_VALUE;
			}
			dst->super_mode = src->super_mode;
			dst->super_i_thd = src->super_i_thd;
			dst->super_p_thd = src->super_p_thd;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_DEBREATH) {
			dst->debreath_en = src->debreath_en;
			dst->debre_strength = src->debre_strength;
			if (dst->debreath_en && dst->debre_strength > 35) {
				mpp_err("invalid debre_strength should be[%d, %d] \n",
					0, 35);
				ret = MPP_ERR_VALUE;
			}
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_MAX_I_PROP)
			dst->max_i_prop = src->max_i_prop;

		if (change & MPP_ENC_RC_CFG_CHANGE_MIN_I_PROP)
			dst->min_i_prop = src->min_i_prop;

		if (change & MPP_ENC_RC_CFG_CHANGE_INIT_IP_RATIO)
			dst->init_ip_ratio = src->init_ip_ratio;

		if (change & MPP_ENC_RC_CFG_CHANGE_QP_INIT)
			dst->qp_init = src->qp_init;

		if (change & MPP_ENC_RC_CFG_CHANGE_QP_RANGE) {
			dst->qp_min = src->qp_min;
			dst->qp_max = src->qp_max;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_QP_RANGE_I) {
			dst->qp_min_i = src->qp_min_i;
			dst->qp_max_i = src->qp_max_i;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_QP_MAX_STEP)
			dst->qp_max_step = src->qp_max_step;

		if (change & MPP_ENC_RC_CFG_CHANGE_QP_IP)
			dst->qp_delta_ip = src->qp_delta_ip;

		if (change & MPP_ENC_RC_CFG_CHANGE_QP_VI)
			dst->qp_delta_vi = src->qp_delta_vi;


		if (change & MPP_ENC_RC_CFG_CHANGE_FM_LV_QP) {
			dst->fm_lvl_qp_min_i = src->fm_lvl_qp_min_i;
			dst->fm_lvl_qp_min_p = src->fm_lvl_qp_min_p;
			dst->fm_lvl_qp_max_i = src->fm_lvl_qp_max_i;
			dst->fm_lvl_qp_max_p = src->fm_lvl_qp_max_p;
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_HIER_QP) {
			dst->hier_qp_en = src->hier_qp_en;
			memcpy(dst->hier_qp_delta, src->hier_qp_delta,
			       sizeof(src->hier_qp_delta));
			memcpy(dst->hier_frame_num, src->hier_frame_num,
			       sizeof(src->hier_frame_num));
		}

		if (change & MPP_ENC_RC_CFG_CHANGE_ST_TIME)
			dst->stats_time = src->stats_time;

		// parameter checking
		if (dst->rc_mode >= MPP_ENC_RC_MODE_BUTT) {
			mpp_err("invalid rc mode %d should be RC_MODE_VBR or RC_MODE_CBR\n",
				src->rc_mode);
			ret = MPP_ERR_VALUE;
		}
		if (dst->quality >= MPP_ENC_RC_QUALITY_BUTT) {
			mpp_err("invalid quality %d should be from QUALITY_WORST to QUALITY_BEST\n",
				dst->quality);
			ret = MPP_ERR_VALUE;
		}
		if (dst->rc_mode != MPP_ENC_RC_MODE_FIXQP) {
			if ((dst->bps_target >= 100 * SZ_1M ||
			     dst->bps_target <= 1 * SZ_1K) ||
			    (dst->bps_max >= 100 * SZ_1M ||
			     dst->bps_max <= 1 * SZ_1K) ||
			    (dst->bps_min >= 100 * SZ_1M ||
			     dst->bps_min <= 1 * SZ_1K)) {
				mpp_err("invalid bit per second %d [%d:%d] out of range 1K~100M\n",
					dst->bps_target, dst->bps_min,
					dst->bps_max);
				ret = MPP_ERR_VALUE;
			}
		}
		// if I frame min/max is not set use normal case
		if (dst->qp_min_i <= 0)
			dst->qp_min_i = dst->qp_min;
		if (dst->qp_max_i <= 0)
			dst->qp_max_i = dst->qp_max;
		if (dst->qp_min < 0 || dst->qp_max < 0 ||
		    dst->qp_min > dst->qp_max || dst->qp_min_i < 0 ||
		    dst->qp_max_i < 0 || dst->qp_min_i > dst->qp_max_i ||
		    (dst->qp_init > 0 && (dst->qp_init > dst->qp_max_i ||
					  dst->qp_init < dst->qp_min_i))) {
			mpp_err("invalid qp range: init %d i [%d:%d] p [%d:%d]\n",
				dst->qp_init, dst->qp_min_i, dst->qp_max_i,
				dst->qp_min, dst->qp_max);

			dst->qp_init = bak.qp_init;
			dst->qp_min_i = bak.qp_min_i;
			dst->qp_max_i = bak.qp_max_i;
			dst->qp_min = bak.qp_min;
			dst->qp_max = bak.qp_max;

			mpp_err("restore qp range: init %d i [%d:%d] p [%d:%d]\n",
				dst->qp_init, dst->qp_min_i, dst->qp_max_i,
				dst->qp_min, dst->qp_max);
		}
		if (dst->qp_delta_ip < 0) {
			mpp_err("invalid qp delta ip %d restore to %d\n",
				dst->qp_delta_ip, bak.qp_delta_ip);
			dst->qp_delta_ip = bak.qp_delta_ip;
		}
		if (dst->qp_delta_vi < 0) {
			mpp_err("invalid qp delta vi %d restore to %d\n",
				dst->qp_delta_vi, bak.qp_delta_vi);
			dst->qp_delta_vi = bak.qp_delta_vi;
		}
		if (dst->qp_max_step < 0) {
			mpp_err("invalid qp max step %d restore to %d\n",
				dst->qp_max_step, bak.qp_max_step);
			dst->qp_max_step = bak.qp_max_step;
		}
		if (dst->stats_time && dst->stats_time > 60) {
			mpp_err("warning: bitrate statistic time %d is larger than 60s\n",
				dst->stats_time);
		}

		dst->change |= change;

		if (ret) {
			mpp_err_f("failed to accept new rc config\n");
			*dst = bak;
		} else {
			mpp_log("MPP_ENC_SET_RC_CFG bps %d [%d : %d] fps [%d:%d] gop %d\n",
				dst->bps_target, dst->bps_min, dst->bps_max,
				dst->fps_in_num, dst->fps_out_num, dst->gop);
		}
	}

	return ret;
}

MPP_RET mpp_enc_proc_hw_cfg(MppEncHwCfg *dst, MppEncHwCfg *src)
{
	MPP_RET ret = MPP_OK;
	RK_U32 change = src->change;

	if (change) {
		MppEncHwCfg bak = *dst;

		if (change & MPP_ENC_HW_CFG_CHANGE_QP_ROW)
			dst->qp_delta_row = src->qp_delta_row;

		if (change & MPP_ENC_HW_CFG_CHANGE_QP_ROW_I)
			dst->qp_delta_row_i = src->qp_delta_row_i;

		if (change & MPP_ENC_HW_CFG_CHANGE_AQ_THRD_I)
			memcpy(dst->aq_thrd_i, src->aq_thrd_i,
			       sizeof(dst->aq_thrd_i));

		if (change & MPP_ENC_HW_CFG_CHANGE_AQ_THRD_P)
			memcpy(dst->aq_thrd_p, src->aq_thrd_p,
			       sizeof(dst->aq_thrd_p));

		if (change & MPP_ENC_HW_CFG_CHANGE_AQ_STEP_I)
			memcpy(dst->aq_step_i, src->aq_step_i,
			       sizeof(dst->aq_step_i));

		if (change & MPP_ENC_HW_CFG_CHANGE_AQ_STEP_P)
			memcpy(dst->aq_step_p, src->aq_step_p,
			       sizeof(dst->aq_step_p));

		if (dst->qp_delta_row < 0 || dst->qp_delta_row_i < 0) {
			mpp_err("invalid hw qp delta row [%d:%d]\n",
				dst->qp_delta_row_i, dst->qp_delta_row);
			ret = MPP_ERR_VALUE;
		}

		dst->change |= change;

		if (ret) {
			mpp_err_f("failed to accept new hw config\n");
			*dst = bak;
		}
	}

	return ret;
}

static MPP_RET mpp_enc_proc_user_data(MppEncImpl *enc, void *param)
{
	MppEncUserData *user_data = (MppEncUserData *)param;
	RK_U32 i = 0;

	if (!enc->rb_userdata.free_cnt) {
		mpp_err("user data is overflow");
		return MPP_NOK;
	}

	i = enc->rb_userdata.write_pos % MAX_USRDATA_CNT;

	if (user_data->len > 1024) {
		mpp_err("usr data is big then 1k byte len %d", user_data->len);
		return MPP_NOK;
	}

	if (copy_from_user(&enc->rb_userdata.data[i], user_data->pdata,
			   user_data->len))
		return -EFAULT;
	enc->rb_userdata.len[i] = user_data->len;
	enc->rb_userdata.write_pos = i + 1;
	enc->rb_userdata.free_cnt--;
	return MPP_OK;
}

static MPP_RET mpp_enc_proc_ref_cfg(MppEncImpl *enc, void *param)
{
	MppEncRefCfg src = NULL;
	MppEncRefCfg dst = enc->cfg.ref_cfg;
	MppEncRefParam *ref_p = (MppEncRefParam *)param;
	MPP_RET ret = MPP_OK;

	if (ref_p->cfg_mode && NULL == src)
		mpp_enc_ref_cfg_init(&src);

	if (NULL == dst) {
		mpp_enc_ref_cfg_init(&dst);
		enc->cfg.ref_cfg = dst;
	}

	switch (ref_p->cfg_mode) {
	case REF_IPPP: {
		src = mpp_enc_ref_default();
	} break;
	case REF_TSVC1:
	case REF_TSVC2:
	case REF_TSVC3: {
		mpi_enc_gen_ref_cfg(src, ref_p->cfg_mode);
	} break;
	case REF_VI: {
		mpi_enc_gen_smart_gop_ref_cfg(src, ref_p);
	} break;
	case REF_HIR_SKIP: {
		mpi_enc_gen_hir_skip_ref(src, ref_p);
	} break;
	default: {
		mpp_err("ref param_error");
	} break;
	}
	if (src) {
		ret = mpp_enc_ref_cfg_copy(dst, src);
		if (ret)
			mpp_err_f("failed to copy ref cfg ret %d\n", ret);

		ret = mpp_enc_refs_set_cfg(enc->refs, dst);
		if (ret)
			mpp_err_f("failed to set ref cfg ret %d\n", ret);

		if (mpp_enc_refs_update_hdr(enc->refs))
			enc->hdr_status.val = 0;

		if (ref_p->cfg_mode && src)
			mpp_enc_ref_cfg_deinit(&src);
	}
	memcpy(&enc->cfg.ref_param, param, sizeof(MppEncRefParam));

	return ret;
}

MPP_RET mpp_enc_unref_osd_buf(MppEncOSDData3 *osd)
{
	RK_U32 i = 0;
	if (!osd || !osd->change)
		return MPP_OK;

	for (i = 0; i < osd->num_region; i++) {
		MppEncOSDRegion3 *rgn = &osd->region[i];
		if (rgn->osd_buf.buf)
			mpi_buf_unref(rgn->osd_buf.buf);

		if (rgn->inv_cfg.inv_buf.buf)
			mpi_buf_unref(rgn->inv_cfg.inv_buf.buf);
	}
	return MPP_OK;
}
MPP_RET mpp_enc_proc_export_osd_buf(MppEncOSDData3 *osd)
{
	RK_U32 i = 0;
	struct mpi_buf *buf = NULL;
	struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
	struct dma_buf *dmabuf = NULL;
	struct mpp_frame_infos info;
	memset(&info, 0, sizeof(info));

	if (!mpibuf_fn || !mpibuf_fn->dma_buf_import) {
		mpp_err_f("mpibuf_ops get fail");
		return -1;
	}

	for (i = 0; i < osd->num_region; i++) {
		MppEncOSDRegion3 *rgn = &osd->region[i];

		if (rgn->osd_buf.fd > 0) {
			dmabuf = dma_buf_get(rgn->osd_buf.fd);
			if (!IS_ERR(dmabuf)) {
				buf = mpibuf_fn->dma_buf_import(dmabuf, &info, -1);
				rgn->osd_buf.buf = buf;
				dma_buf_put(dmabuf);
			} else
				mpp_err("osd buf dma_buf_get fd %d failed\n",
					rgn->osd_buf.fd);
		}

		if (rgn->inv_cfg.inv_buf.fd > 0) {
			dmabuf = dma_buf_get(rgn->inv_cfg.inv_buf.fd);
			if (!IS_ERR(dmabuf)) {
				buf = mpibuf_fn->dma_buf_import(dmabuf, &info, -1);
				rgn->inv_cfg.inv_buf.buf = buf;
				dma_buf_put(dmabuf);
			} else
				mpp_err("osd inv buf dma_buf_get fd %d failed\n",
					rgn->inv_cfg.inv_buf.fd);
		}
	}
	return MPP_OK;
}

MPP_RET mpp_enc_proc_tune_cfg(MppEncFineTuneCfg *dst, MppEncFineTuneCfg *src)
{
	MPP_RET ret = MPP_OK;
	RK_U32 change = src->change;

	if (change) {
		MppEncFineTuneCfg bak = *dst;

		if (change & MPP_ENC_TUNE_CFG_CHANGE_SCENE_MODE)
			dst->scene_mode = src->scene_mode;

		if (dst->scene_mode < MPP_ENC_SCENE_MODE_DEFAULT ||
		    dst->scene_mode >= MPP_ENC_SCENE_MODE_BUTT) {
			mpp_err("invalid scene mode %d not in range [%d:%d]\n", dst->scene_mode,
				MPP_ENC_SCENE_MODE_DEFAULT, MPP_ENC_SCENE_MODE_BUTT - 1);
			ret = MPP_ERR_VALUE;
		}

		dst->change |= change;

		if (ret) {
			mpp_err_f("failed to accept new tuning config\n");
			*dst = bak;
		}
	}

	return ret;
}


MPP_RET mpp_enc_proc_cfg(MppEncImpl *enc, MpiCmd cmd, void *param)
{
	MPP_RET ret = MPP_OK;

	switch (cmd) {
	case MPP_ENC_SET_CFG: {
		MppEncCfgImpl *impl = (MppEncCfgImpl *)param;
		MppEncCfgSet *src = &impl->cfg;
		RK_U32 change = src->base.change;
		mpp_log("MPP_ENC_SET_CFG in \n");

		/* get base cfg here */
		if (change) {
			MppEncCfgSet *dst = &enc->cfg;

			if (change & MPP_ENC_BASE_CFG_CHANGE_LOW_DELAY)
				dst->base.low_delay = src->base.low_delay;

			src->base.change = 0;
		}

		/* process rc cfg at mpp_enc module */
		if (src->rc.change) {
			ret = mpp_enc_proc_rc_cfg(&enc->cfg.rc, &src->rc);
			src->rc.change = 0;
		}

		/* process hardware cfg at mpp_enc module */
		if (src->hw.change) {
			ret = mpp_enc_proc_hw_cfg(&enc->cfg.hw, &src->hw);
			src->hw.change = 0;
		}

		/* process hardware cfg at mpp_enc module */
		if (src->tune.change) {
			ret = mpp_enc_proc_tune_cfg(&enc->cfg.tune, &src->tune);
			src->tune.change = 0;
		}

		/* Then process the rest config */
		ret = enc_impl_proc_cfg(enc->impl, cmd, param);
	} break;
	case MPP_ENC_SET_RC_CFG: {
		MppEncRcCfg *src = (MppEncRcCfg *)param;
		if (src)
			ret = mpp_enc_proc_rc_cfg(&enc->cfg.rc, src);
	} break;
	case MPP_ENC_SET_IDR_FRAME: {
		enc->frm_cfg.force_idr++;
	} break;
	case MPP_ENC_GET_HDR_SYNC:
	case MPP_ENC_GET_EXTRA_INFO: {
		/*
			 * NOTE: get stream header should use user's MppPacket
			 * If we provide internal MppPacket to external user
			 * we do not known when the buffer usage is finished.
			 * So encoder always write its header to external buffer
			 * which is provided by user.
			 */
		if (!enc->hdr_status.ready) {
			enc_impl_gen_hdr(enc->impl, enc->hdr_pkt);
			enc->hdr_len = mpp_packet_get_length(enc->hdr_pkt);
			enc->hdr_status.ready = 1;
		}

		if (cmd == MPP_ENC_GET_EXTRA_INFO) {
			mpp_err("Please use MPP_ENC_GET_HDR_SYNC instead of unsafe MPP_ENC_GET_EXTRA_INFO\n");
			mpp_err("NOTE: MPP_ENC_GET_HDR_SYNC needs MppPacket input\n");

			*(MppPacket *)param = enc->hdr_pkt;
		} else
			mpp_packet_copy((MppPacket)param, enc->hdr_pkt);

		enc->hdr_status.added_by_ctrl = 1;
	} break;
	case MPP_ENC_PRE_ALLOC_BUFF: {
		/* deprecated control */
		mpp_log("deprecated MPP_ENC_PRE_ALLOC_BUFF control\n");
	} break;
	case MPP_ENC_SET_HEADER_MODE: {
		if (param) {
			MppEncHeaderMode mode = *((MppEncHeaderMode *)param);

			if (mode < MPP_ENC_HEADER_MODE_BUTT) {
				enc->hdr_mode = mode;
				enc_dbg_ctrl("header mode set to %d\n", mode);
			} else {
				mpp_err_f("invalid header mode %d\n", mode);
				ret = MPP_NOK;
			}
		} else {
			mpp_err_f("invalid NULL ptr on setting header mode\n");
			ret = MPP_NOK;
		}
	} break;
	case MPP_ENC_SET_SEI_CFG: {
		if (param) {
			MppEncSeiMode mode = *((MppEncSeiMode *)param);

			if (mode <= MPP_ENC_SEI_MODE_ONE_FRAME) {
				enc->sei_mode = mode;
				enc_dbg_ctrl("sei mode set to %d\n", mode);
			} else {
				mpp_err_f("invalid sei mode %d\n", mode);
				ret = MPP_NOK;
			}
		} else {
			mpp_err_f("invalid NULL ptr on setting header mode\n");
			ret = MPP_NOK;
		}
	} break;
	case MPP_ENC_SET_REF_CFG: {
		ret = mpp_enc_proc_ref_cfg(enc, param);
	} break;
	case MPP_ENC_SET_OSD_DATA_CFG: {
		MppEncCfgSet *cfg = &enc->cfg;
		mpp_enc_unref_osd_buf(&cfg->osd);
		memcpy(&cfg->osd, param, sizeof(cfg->osd));
		mpp_enc_proc_export_osd_buf(&cfg->osd);
	} break;
	case MPP_ENC_SET_ROI_CFG: {
		MppEncCfgSet *cfg = &enc->cfg;
		memcpy(&cfg->roi, param, sizeof(cfg->roi));
	} break;
	case MPP_ENC_INSRT_USERDATA: {
		ret = mpp_enc_proc_user_data(enc, param);
	} break;
	default: {
		ret = enc_impl_proc_cfg(enc->impl, cmd, param);
	} break;
	}

	if (check_resend_hdr(cmd, param, &enc->cfg)) {
		enc->frm_cfg.force_flag |= ENC_FORCE_IDR;
		enc->hdr_status.val = 0;
	}
	if (check_rc_cfg_update(cmd, &enc->cfg))
		enc->rc_api_user_cfg = 1;
	if (check_rc_gop_update(cmd, &enc->cfg))
		mpp_enc_refs_set_rc_igop(enc->refs, enc->cfg.rc.gop);

	if (check_hal_info_update(cmd))
		enc->hal_info_updated = 0;

	return ret;
}

static const char *name_of_rc_mode[] = {
	"vbr",
	"cbr",
	"fixqp",
	"avbr",
};

static void update_rc_cfg_log(MppEncImpl *impl, const char *fmt, ...)
{
	RK_S32 size = impl->rc_cfg_size;
	RK_S32 length = impl->rc_cfg_length;
	char *base = impl->rc_cfg_info + length;

	va_list args;
	va_start(args, fmt);

	length += vsnprintf(base, size - length, fmt, args);
	if (length >= size)
		mpp_log_f("rc cfg log is full\n");

	impl->rc_cfg_length = length;

	va_end(args);
}

static void update_user_datas(MppEncImpl *enc, MppPacket packet,
			      HalEncTask *hal_task)
{
	if (enc->rb_userdata.free_cnt >= MAX_USRDATA_CNT)
		return;

	while (enc->rb_userdata.free_cnt < MAX_USRDATA_CNT) {
		RK_U32 i = enc->rb_userdata.read_pos % MAX_USRDATA_CNT;
		RK_S32 length = 0;

		enc_impl_add_prefix(enc->impl, packet, &length, uuid_usr_data,
				    &enc->rb_userdata.data[i],
				    enc->rb_userdata.len[i]);

		hal_task->sei_length += length;
		hal_task->length += length;
		enc->rb_userdata.free_cnt++;
		enc->rb_userdata.read_pos = i + 1;
	}
}

static void set_rc_cfg(RcCfg *cfg, MppEncCfgSet *cfg_set)
{
	MppEncRcCfg *rc = &cfg_set->rc;
	MppEncPrepCfg *prep = &cfg_set->prep;
	MppEncCodecCfg *codec = &cfg_set->codec;
	MppEncRefCfgImpl *ref = (MppEncRefCfgImpl *)cfg_set->ref_cfg;
	MppEncCpbInfo *info = &ref->cpb_info;
	RK_S32 fps = (!!rc->fps_in_denorm) ? (rc->fps_in_num / rc->fps_in_denorm) : 1;
	RK_S32 status_time = 4 * ((!!fps) ? (rc->gop / fps) : 8);
	if (status_time > 8)
		status_time = 8;
	cfg->width = prep->width;
	cfg->height = prep->height;

	switch (rc->rc_mode) {
	case MPP_ENC_RC_MODE_CBR: {
		cfg->mode = RC_CBR;
	} break;
	case MPP_ENC_RC_MODE_VBR: {
		cfg->mode = RC_VBR;
	} break;
	case MPP_ENC_RC_MODE_AVBR: {
		cfg->mode = RC_AVBR;
	} break;
	case MPP_ENC_RC_MODE_FIXQP: {
		cfg->mode = RC_FIXQP;
	} break;
	default: {
		cfg->mode = RC_AVBR;
	} break;
	}

	cfg->fps.fps_in_flex = rc->fps_in_flex;
	cfg->fps.fps_in_num = rc->fps_in_num;
	cfg->fps.fps_in_denorm = rc->fps_in_denorm;
	cfg->fps.fps_out_flex = rc->fps_out_flex;
	cfg->fps.fps_out_num = rc->fps_out_num;
	cfg->fps.fps_out_denorm = rc->fps_out_denorm;
	cfg->igop = rc->gop;
	cfg->max_i_bit_prop = rc->max_i_prop;
	cfg->min_i_bit_prop = rc->min_i_prop;
	cfg->init_ip_ratio = rc->init_ip_ratio;
	cfg->fm_lv_min_quality = rc->fm_lvl_qp_min_p;
	cfg->fm_lv_min_i_quality = rc->fm_lvl_qp_min_i;
	cfg->fm_lv_max_quality = rc->fm_lvl_qp_max_p;
	cfg->fm_lv_max_i_quality = rc->fm_lvl_qp_max_i;
	cfg->bps_target = rc->bps_target;
	cfg->bps_max = rc->bps_max;
	cfg->bps_min = rc->bps_min;

	cfg->hier_qp_cfg.hier_qp_en = rc->hier_qp_en;
	memcpy(cfg->hier_qp_cfg.hier_frame_num, rc->hier_frame_num,
	       sizeof(rc->hier_frame_num));
	memcpy(cfg->hier_qp_cfg.hier_qp_delta, rc->hier_qp_delta,
	       sizeof(rc->hier_qp_delta));

	mpp_assert(rc->fps_out_num);
	cfg->stats_time = rc->stats_time ? rc->stats_time : status_time;
	cfg->stats_time = mpp_clip(cfg->stats_time, 1, 60);

	/* quality configure */
	switch (codec->coding) {
	case MPP_VIDEO_CodingAVC:
	case MPP_VIDEO_CodingHEVC:
	case MPP_VIDEO_CodingVP8: {
		cfg->init_quality = rc->qp_init;
		cfg->max_quality = rc->qp_max;
		cfg->min_quality = rc->qp_min;
		cfg->max_i_quality = rc->qp_max_i ? rc->qp_max_i : rc->qp_max;
		cfg->min_i_quality = rc->qp_min_i ? rc->qp_min_i : rc->qp_min;
		cfg->i_quality_delta = rc->qp_delta_ip;
		cfg->vi_quality_delta = rc->qp_delta_vi;
	} break;
	case MPP_VIDEO_CodingMJPEG: {
		MppEncJpegCfg *jpeg = &codec->jpeg;

		cfg->init_quality = jpeg->q_factor;
		cfg->max_quality = jpeg->qf_max;
		cfg->min_quality = jpeg->qf_min;
		cfg->max_i_quality = jpeg->qf_max;
		cfg->min_i_quality = jpeg->qf_min;
	} break;
	default: {
		mpp_err_f("unsupport coding type %d\n", codec->coding);
	} break;
	}

	cfg->layer_bit_prop[0] = 256;
	cfg->layer_bit_prop[1] = 0;
	cfg->layer_bit_prop[2] = 0;
	cfg->layer_bit_prop[3] = 0;

	cfg->max_reencode_times = rc->max_reenc_times;
	cfg->drop_mode = rc->drop_mode;
	cfg->drop_thd = rc->drop_threshold;
	cfg->drop_gap = rc->drop_gap;

	cfg->super_cfg.rc_priority = rc->rc_priority;
	cfg->super_cfg.super_mode = rc->super_mode;
	cfg->super_cfg.super_i_thd = rc->super_i_thd;
	cfg->super_cfg.super_p_thd = rc->super_p_thd;

	cfg->debreath_cfg.enable = rc->debreath_en;
	cfg->debreath_cfg.strength = rc->debre_strength;

	if (info->st_gop) {
		cfg->vgop = info->st_gop;
		if (cfg->vgop >= rc->fps_out_num / rc->fps_out_denorm &&
		    cfg->vgop < cfg->igop) {
			cfg->gop_mode = SMART_P;
			if (!cfg->vi_quality_delta)
				cfg->vi_quality_delta = 2;
		}
	}

	if (codec->coding == MPP_VIDEO_CodingAVC ||
	    codec->coding == MPP_VIDEO_CodingHEVC) {
		mpp_log("mode %s bps [%d:%d:%d] fps %s [%d/%d] -> %s [%d/%d] gop i [%d] v [%d]\n",
			name_of_rc_mode[cfg->mode], rc->bps_min, rc->bps_target,
			rc->bps_max, cfg->fps.fps_in_flex ? "flex" : "fix",
			cfg->fps.fps_in_num, cfg->fps.fps_in_denorm,
			cfg->fps.fps_out_flex ? "flex" : "fix",
			cfg->fps.fps_out_num, cfg->fps.fps_out_denorm,
			cfg->igop, cfg->vgop);
	}
}

MPP_RET mpp_enc_proc_rc_update(MppEncImpl *enc)
{
	MPP_RET ret = MPP_OK;

	// check and update rate control config
	if (enc->rc_api_user_cfg) {
		MppEncCfgSet *cfg = &enc->cfg;
		MppEncRcCfg *rc_cfg = &cfg->rc;
		MppEncPrepCfg *prep_cfg = &cfg->prep;
		RcCfg usr_cfg;

		enc_dbg_detail("rc update cfg start\n");

		memset(&usr_cfg, 0, sizeof(usr_cfg));
		set_rc_cfg(&usr_cfg, cfg);

		if (enc->online || enc->ref_buf_shared)
			usr_cfg.shared_buf_en = 1;
		if (enc->motion_static_switch_en)
			usr_cfg.motion_static_switch_en = 1;

		ret = rc_update_usr_cfg(enc->rc_ctx, &usr_cfg);
		rc_cfg->change = 0;
		prep_cfg->change = 0;

		enc_dbg_detail("rc update cfg done\n");
		enc->rc_api_user_cfg = 0;

		enc->rc_cfg_length = enc->rc_cfg_pos;
		enc->gop_mode = usr_cfg.gop_mode;
		update_rc_cfg_log(
			enc, "%s-b:%d[%d:%d]-g:%d-q:%d:[%d:%d]:[%d:%d]:%d\n",
			name_of_rc_mode[usr_cfg.mode], usr_cfg.bps_target,
			usr_cfg.bps_min, usr_cfg.bps_max, usr_cfg.igop,
			usr_cfg.init_quality, usr_cfg.min_quality,
			usr_cfg.max_quality, usr_cfg.min_i_quality,
			usr_cfg.max_i_quality, usr_cfg.i_quality_delta);
	}

	return ret;
}

#define ENC_RUN_FUNC2(func, ctx, task, enc, ret)                               \
	ret = func(ctx, task);                                                 \
	if (ret) {                                                             \
		mpp_err("enc %p " #func ":%-4d failed return %d", enc,         \
			__LINE__, ret);                                        \
		goto TASK_DONE;                                                \
	}

#define ENC_RUN_FUNC3(func, ctx, task, jpeg_task, enc, ret)                    \
	ret = func(ctx, task, jpeg_task);                                      \
	if (ret) {                                                             \
		mpp_err("enc %p " #func ":%-4d failed return %d", enc,         \
			__LINE__, ret);                                        \
		goto TASK_DONE;                                                \
	}

static MPP_RET mpp_enc_check_frm_pkt(MppEncImpl *enc)
{
	MPP_RET ret = MPP_OK;
	enc->frm_buf = NULL;
	if (NULL == enc->packet) {
		ret = mpp_packet_new(&enc->packet);
		if (ret)
			return ret;

	}

	if (enc->frame) {
		RK_U32 hor_stride = 0, ver_stride = 0;
		RK_S64 pts = mpp_frame_get_pts(enc->frame);
		RK_S64 dts = mpp_frame_get_dts(enc->frame);
		MppBuffer frm_buf = mpp_frame_get_buffer(enc->frame);
		MppEncPrepCfg *prep = &enc->cfg.prep;
		hor_stride = mpp_frame_get_hor_stride(enc->frame);
		ver_stride = mpp_frame_get_ver_stride(enc->frame);
		if (hor_stride != prep->hor_stride ||
		    ver_stride != prep->ver_stride)
			mpp_err("frame stride set equal cfg stride");
		enc->task_pts = pts;
		enc->frm_buf = frm_buf;

		mpp_packet_set_pts(enc->packet, pts);
		mpp_packet_set_dts(enc->packet, dts);

		if (mpp_frame_get_eos(enc->frame))
			mpp_packet_set_eos(enc->packet);
		else
			mpp_packet_clr_eos(enc->packet);
	}

	return (NULL == enc->frame || NULL == enc->frm_buf) ? MPP_NOK : MPP_OK;
}

MPP_RET mpp_enc_alloc_output_from_bufpool(MppEncImpl *enc)
{
	MPP_RET ret = mpp_enc_check_frm_pkt(enc);
	if (ret)
		return ret;
	mpp_packet_set_length(enc->packet, 0);
	if (NULL == enc->pkt_buf) {
		/* NOTE: set buffer w * h * 1.5 to avoid buffer overflow */
		MppEncPrepCfg *prep = &enc->cfg.prep;
		RK_U32 width = MPP_ALIGN(prep->width, 16);
		RK_U32 height = MPP_ALIGN(prep->height, 16);
		RK_U32 size = (enc->coding == MPP_VIDEO_CodingMJPEG) ?
			      (width * height * 3 / 2) :
			      (width * height);

		MppPacketImpl *pkt = (MppPacketImpl *)enc->packet;
		MppBuffer buffer = NULL;
		struct vcodec_mpibuf_fn *mpibuf_fn = get_mpibuf_ops();
		mpp_assert(size);
		if (mpibuf_fn) {
			if (!enc->strm_pool && mpibuf_fn->buf_pool_create) {
				enc->strm_pool =
					mpibuf_fn->buf_pool_create(size, 2);
			}
			if (enc->strm_pool && mpibuf_fn->buf_pool_request_buf) {
				MppBufferInfo info;
				struct mpi_buf *buf =
					mpibuf_fn->buf_pool_request_buf(
						enc->strm_pool);
				memset(&info, 0, sizeof(info));
				if (buf) {
					info.hnd = buf;
					info.size = size;
					mpp_buffer_import(&buffer, &info);
					if (mpibuf_fn->buf_unref)
						mpibuf_fn->buf_unref(buf);
				}
			}
		}
		if (!buffer)
			mpp_buffer_get(NULL, &buffer, size);

		mpp_assert(buffer);
		// enc->pkt_buf = buffer;
		enc->pkt_buf = &pkt->buf;
		pkt->buf.buf = buffer;
		pkt->buf.mpi_buf_id = mpp_buffer_get_mpi_buf_id(buffer);
		pkt->buf.start_offset = 0;
		pkt->buf.size = mpp_buffer_get_size(buffer);
		pkt->data = mpp_buffer_get_ptr(buffer);
		pkt->pos = pkt->data;
		pkt->size = mpp_buffer_get_size(buffer);
		pkt->length = 0;
		pkt->buffer = buffer;

		enc_dbg_detail("create output pkt %p buf %p\n", enc->packet,
			       buffer);
	} else {
		enc_dbg_detail("output to pkt %p buf %p pos %p length %d\n",
			       enc->packet, enc->pkt_buf,
			       mpp_packet_get_pos(enc->packet),
			       (RK_U32)mpp_packet_get_length(enc->packet));
	}
	return MPP_OK;
}

static RK_U32 mpp_enc_check_next_frm_type(MppEncImpl *enc)
{
	EncTask *task = (EncTask *)enc->enc_task;
	RK_U32 is_intra = 0;

	if (mpp_enc_refs_next_frm_is_intra(enc->refs) || (task->seq_idx == 1) ||
	    mpp_enc_refs_next_frm_is_kpfrm(enc->refs))
		is_intra =  1;
	return is_intra;
}


MPP_RET mpp_enc_alloc_output_from_ringbuf(MppEncImpl *enc)
{
	MPP_RET ret = MPP_OK;
	MppBuffer buffer = NULL;
	MppEncPrepCfg *prep = &enc->cfg.prep;
	RK_U32 width = MPP_ALIGN(prep->width, 16);
	RK_U32 height = MPP_ALIGN(prep->height, 16);
	RK_U32 size = (enc->coding == MPP_VIDEO_CodingMJPEG) ?
		      (width * height) :
		      (width * height / 2);

	RK_U32 is_intra = mpp_enc_check_next_frm_type(enc);

	if (enc->ring_pool && !enc->ring_pool->init_done && !get_vsm_ops()) {
		if (!enc->ring_buf_size)
			enc->ring_buf_size = size;
		enc->ring_buf_size = MPP_MAX(enc->ring_buf_size, SZ_16K);
		enc->ring_buf_size = MPP_ALIGN(enc->ring_buf_size, SZ_4K);
		if (enc->shared_buf->stream_buf)
			buffer = enc->shared_buf->stream_buf;
		else {
			mpp_ring_buffer_get(NULL, &buffer, enc->ring_buf_size);
			if (!buffer) {
				mpp_err("ring buf get mpp_buf fail \n");
				return MPP_NOK;
			}
		}
		ring_buf_init(enc->ring_pool, buffer, enc->max_strm_cnt);
	}
	ret = mpp_packet_new_ring_buf(&enc->packet, enc->ring_pool, 0, is_intra, enc->chan_id);
	if (ret) {
		if (ret == MPP_ERR_NULL_PTR)
			enc->pkt_fail_cnt++ ;
		else
			enc->ringbuf_fail_cnt++;
		return ret;
	}
	{
		MppPacketImpl *pkt = (MppPacketImpl *)enc->packet;
		enc->pkt_buf = &pkt->buf;
	}
	ret = mpp_enc_check_frm_pkt(enc);
	return ret;
}

//#define USE_RING_BUF

static MPP_RET mpp_enc_alloc_output(MppEncImpl *enc)
{
	MPP_RET ret = MPP_OK;
#ifdef USE_RING_BUF
	ret = mpp_enc_alloc_output_from_ringbuf(enc);
#else
	ret = mpp_enc_alloc_output_from_bufpool(enc);
#endif
	return ret;
}

static MPP_RET mpp_enc_proc_two_pass(MppEncImpl *enc, EncTask *task)
{
	MPP_RET ret = MPP_OK;

	if (mpp_enc_refs_next_frm_is_intra(enc->refs)) {
		EncRcTask *rc_task = &enc->rc_task;
		EncFrmStatus frm_bak = rc_task->frm;
		EncRcTaskInfo rc_info = rc_task->info;
		EncCpbStatus *cpb = &rc_task->cpb;
		EncFrmStatus *frm = &rc_task->frm;
		HalEncTask *hal_task = &task->info.enc;
		EncImpl impl = enc->impl;
		MppEncHal hal = enc->enc_hal;
		MppPacket packet = hal_task->packet;
		RK_S32 task_len = hal_task->length;
		RK_S32 hw_len = hal_task->hw_length;
		RK_S32 pkt_len = mpp_packet_get_length(packet);

		enc_dbg_detail("task %d two pass mode enter\n", frm->seq_idx);
		rc_task->info = enc->rc_info_prev;

		enc_dbg_detail("task %d enc proc dpb\n", frm->seq_idx);
		mpp_enc_refs_get_cpb_pass1(enc->refs, cpb);

		enc_dbg_frm_status("frm %d start ***********************************\n", cpb->curr.seq_idx);
		ENC_RUN_FUNC2(enc_impl_proc_dpb, impl, hal_task, enc, ret);

		enc_dbg_detail("task %d enc proc hal\n", frm->seq_idx);
		ENC_RUN_FUNC2(enc_impl_proc_hal, impl, hal_task, enc, ret);

		enc_dbg_detail("task %d hal get task\n", frm->seq_idx);
		ENC_RUN_FUNC2(mpp_enc_hal_get_task, hal, hal_task, enc, ret);

		enc_dbg_detail("task %d hal generate reg\n", frm->seq_idx);
		ENC_RUN_FUNC2(mpp_enc_hal_gen_regs, hal, hal_task, enc, ret);

		enc_dbg_detail("task %d hal start\n", frm->seq_idx);
		ENC_RUN_FUNC3(mpp_enc_hal_start, hal, hal_task, NULL, enc,
			      ret);

		mpp_err("task %d hal wait\n", frm->seq_idx);
		ENC_RUN_FUNC2(mpp_enc_hal_wait, hal, hal_task, enc, ret);

		//enc_dbg_detail("task %d hal ret task\n", frm->seq_idx);
		ENC_RUN_FUNC3(mpp_enc_hal_ret_task, hal, hal_task, NULL, enc,
			      ret);

		//recover status & packet
		mpp_packet_set_length(packet, pkt_len);
		hal_task->hw_length = hw_len;
		hal_task->length = task_len;

		*frm = frm_bak;
		rc_task->info = rc_info;

		enc_dbg_detail("task %d two pass mode leave\n", frm->seq_idx);
	}
TASK_DONE:
	return ret;
}

static void mpp_enc_rc_info_backup(MppEncImpl *enc)
{
	if (enc->online || !enc->cfg.rc.debreath_en || enc->ref_buf_shared)
		return;

	enc->rc_info_prev = enc->rc_task.info;
}

static MPP_RET mpp_enc_normal_cfg(MppEncImpl *enc, EncTask *task)
{
	EncImpl impl = enc->impl;
	MppEncHal hal = enc->enc_hal;
	EncRcTask *rc_task = &enc->rc_task;
	MppEncHeaderStatus *hdr_status = &enc->hdr_status;
	EncCpbStatus *cpb = &rc_task->cpb;
	EncFrmStatus *frm = &rc_task->frm;
	HalEncTask *hal_task = &task->info.enc;
	MppPacket packet = hal_task->packet;
	MppEncRefFrmUsrCfg *frm_cfg = &enc->frm_cfg;
	MPP_RET ret = MPP_OK;
	if (enc->qpmap_en) {
		RK_U32 i;
		hal_task->mv_info = enc->mv_info;
		hal_task->qpmap = enc->qpmap;
		for (i = 0; i < 3; i++)
			hal_task->mv_flag[i] = enc->mv_flag[i];
		hal_task->mv_index = &enc->mv_index;
		hal_task->qp_out = enc->qp_out;
	}
	if (!enc->online && enc->cfg.rc.debreath_en && !enc->ref_buf_shared) {
		if (!frm_cfg->force_flag) {
			ret = mpp_enc_proc_two_pass(enc, task);
			if (ret)
				return ret;
		}
	}
	enc_dbg_detail("task %d enc proc dpb\n", frm->seq_idx);
	mpp_enc_refs_get_cpb(enc->refs, cpb);

	enc_dbg_frm_status("frm %d start ***********************************\n",
			   cpb->curr.seq_idx);
	ENC_RUN_FUNC2(enc_impl_proc_dpb, impl, hal_task, enc, ret);

	enc_dbg_frm_status("frm %d compare\n", cpb->curr.seq_idx);
	enc_dbg_frm_status("seq_idx      %d vs %d\n", frm->seq_idx,
			   cpb->curr.seq_idx);
	enc_dbg_frm_status("is_idr       %d vs %d\n", frm->is_idr,
			   cpb->curr.is_idr);
	enc_dbg_frm_status("is_intra     %d vs %d\n", frm->is_intra,
			   cpb->curr.is_intra);
	enc_dbg_frm_status("is_non_ref   %d vs %d\n", frm->is_non_ref,
			   cpb->curr.is_non_ref);
	enc_dbg_frm_status("is_lt_ref    %d vs %d\n", frm->is_lt_ref,
			   cpb->curr.is_lt_ref);
	enc_dbg_frm_status("lt_idx       %d vs %d\n", frm->lt_idx,
			   cpb->curr.lt_idx);
	enc_dbg_frm_status("temporal_id  %d vs %d\n", frm->temporal_id,
			   cpb->curr.temporal_id);
	enc_dbg_frm_status("frm %d done  ***********************************\n",
			   cpb->curr.seq_idx);

	enc_dbg_detail("task %d rc frame start\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_frm_start, enc->rc_ctx, rc_task, enc, ret);
	enc_dbg_detail("task %d rc frame start ok \n", frm->seq_idx);

	// 16. generate header before hardware stream
	if (enc->hdr_mode == MPP_ENC_HEADER_MODE_EACH_IDR && frm->is_intra &&
	    !hdr_status->added_by_change && !hdr_status->added_by_ctrl &&
	    !hdr_status->added_by_mode) {
		enc_dbg_detail("task %d IDR header length %d\n", frm->seq_idx,
			       enc->hdr_len);

		mpp_packet_append(packet, enc->hdr_pkt);

		hal_task->header_length = enc->hdr_len;
		hal_task->length += enc->hdr_len;
		hdr_status->added_by_mode = 1;
	}
	// check for header adding
	check_hal_task_pkt_len(hal_task, "header adding");

	/* 17. Add all prefix info before encoding */
	if (frm->is_idr && enc->sei_mode >= MPP_ENC_SEI_MODE_ONE_SEQ) {
		RK_S32 length = 0;

		enc_impl_add_prefix(impl, packet, &length, uuid_version,
				    enc->version_info, enc->version_length);

		hal_task->sei_length += length;
		hal_task->length += length;

		length = 0;
		enc_impl_add_prefix(impl, packet, &length, uuid_rc_cfg,
				    enc->rc_cfg_info, enc->rc_cfg_length);

		hal_task->sei_length += length;
		hal_task->length += length;
	}

	update_user_datas(enc, packet, hal_task);

	// check for user data adding
	check_hal_task_pkt_len(hal_task, "user data adding");

	enc_dbg_detail("task %d enc proc hal\n", frm->seq_idx);
	ENC_RUN_FUNC2(enc_impl_proc_hal, impl, hal_task, enc, ret);

	enc_dbg_detail("task %d hal get task\n", frm->seq_idx);
	ENC_RUN_FUNC2(mpp_enc_hal_get_task, hal, hal_task, enc, ret);

	enc_dbg_detail("task %d rc hal start\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_hal_start, enc->rc_ctx, rc_task, enc, ret);

	enc_dbg_detail("task %d hal generate reg\n", frm->seq_idx);
	ENC_RUN_FUNC2(mpp_enc_hal_gen_regs, hal, hal_task, enc, ret);

	//  mpp_stopwatch_record(hal_task->stopwatch, "encode hal start");
//	enc_dbg_detail("task %d hal start\n", frm->seq_idx);
//	ENC_RUN_FUNC3(mpp_enc_hal_start, hal, hal_task, NULL, enc, ret);
//   mpp_stopwatch_record(hal_task->stopwatch, " hal wait");
TASK_DONE:
	if (ret)
		enc->cfg_fail_cnt++;
	return ret;
}

static MPP_RET mpp_enc_end(MppEncImpl *enc, EncTask *task, EncTask *jpeg_task)
{
	MppEncHal hal = enc->enc_hal;
	EncRcTask *rc_task = &enc->rc_task;
	EncFrmStatus *frm = &rc_task->frm;
	HalEncTask *hal_task = &task->info.enc;
	HalEncTask *jpeg_hal_task = NULL;
	MPP_RET ret = MPP_OK;
	if (jpeg_task)
		jpeg_hal_task = &jpeg_task->info.enc;
	//  mpp_stopwatch_record(hal_task->stopwatch, "encode hal finish");

	enc_dbg_detail("task %d rc hal end\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_hal_end, enc->rc_ctx, rc_task, enc, ret);

	enc_dbg_detail("task %d hal ret task\n", frm->seq_idx);
	ENC_RUN_FUNC3(mpp_enc_hal_ret_task, hal, hal_task, jpeg_hal_task, enc,
		      ret);

	enc_dbg_detail("task %d rc frame check reenc\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_frm_check_reenc, enc->rc_ctx, rc_task, enc, ret);
TASK_DONE:
	return ret;
}

static void mpp_enc_clr_rc_cb_info(EncRcTask *rc_task)
{
	EncRcTaskInfo *hal_rc_ret = (EncRcTaskInfo *) &rc_task->info;
	RK_S32          bit_target = hal_rc_ret->bit_target;
	RK_S32          bit_max = hal_rc_ret->bit_max;
	RK_S32          bit_min = hal_rc_ret->bit_min;
	RK_S32          quality_target = hal_rc_ret->quality_target;
	RK_S32          quality_max = hal_rc_ret->quality_max;
	RK_S32          quality_min = hal_rc_ret->quality_min;
	memset(hal_rc_ret, 0, sizeof(EncRcTaskInfo));
	hal_rc_ret->bit_target = bit_target;
	hal_rc_ret->bit_max = bit_max;
	hal_rc_ret->bit_min = bit_min;
	hal_rc_ret->quality_target =  quality_target;
	hal_rc_ret->quality_max = quality_max;
	hal_rc_ret->quality_min = quality_min;
	return;
}

static MPP_RET mpp_enc_reenc_simple(MppEncImpl *enc, EncTask *task)
{
	//    MppEncImpl *enc = (MppEncImpl *)mpp->mEnc;
	MppEncHal hal = enc->enc_hal;
	EncRcTask *rc_task = &enc->rc_task;
	EncFrmStatus *frm = &rc_task->frm;
	HalEncTask *hal_task = &task->info.enc;

	MPP_RET ret = MPP_OK;
	enc_dbg_func("enter\n");

	mpp_enc_clr_rc_cb_info(rc_task);
	enc_dbg_detail("task %d enc proc hal\n", frm->seq_idx);
	ENC_RUN_FUNC2(enc_impl_proc_hal, enc->impl, hal_task, enc, ret);

	enc_dbg_detail("task %d hal get task\n", frm->seq_idx);
	ENC_RUN_FUNC2(mpp_enc_hal_get_task, hal, hal_task, enc, ret);

	enc_dbg_detail("task %d rc hal start\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_hal_start, enc->rc_ctx, rc_task, enc, ret);

	enc_dbg_detail("task %d hal generate reg\n", frm->seq_idx);
	ENC_RUN_FUNC2(mpp_enc_hal_gen_regs, hal, hal_task, enc, ret);

	enc_dbg_detail("task %d reenc %d times %d\n", frm->seq_idx,
		       frm->reencode, frm->reencode_times);
	enc_dbg_func("leave\n");

TASK_DONE:
	return ret;
}

static MPP_RET mpp_enc_reenc_drop(MppEncImpl *enc, EncTask *task)
{
	//    MppEncImpl *enc = (MppEncImpl *)mpp->mEnc;
	EncRcTask *rc_task = &enc->rc_task;
	EncRcTaskInfo *info = &rc_task->info;
	EncFrmStatus *frm = &rc_task->frm;
	HalEncTask *hal_task = &task->info.enc;
	MPP_RET ret = MPP_OK;

	enc_dbg_func("enter\n");
	mpp_enc_refs_rollback(enc->refs);

	info->bit_real = hal_task->length;
	info->quality_real = info->quality_target;

	enc_dbg_detail("task %d rc frame end\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_frm_end, enc->rc_ctx, rc_task, enc, ret);
	enc->qp_out = rc_task->qp_out;

TASK_DONE:
	enc_dbg_func("leave\n");
	return ret;
}

static MPP_RET mpp_enc_reenc_force_pskip(MppEncImpl *enc, EncTask *task)
{
	//    MppEncImpl *enc = (MppEncImpl *)mpp->mEnc;
	EncImpl impl = enc->impl;
	MppEncRefFrmUsrCfg *frm_cfg = &enc->frm_cfg;
	EncRcTask *rc_task = &enc->rc_task;
	EncCpbStatus *cpb = &rc_task->cpb;
	EncFrmStatus *frm = &rc_task->frm;
	HalEncTask *hal_task = &task->info.enc;
	MPP_RET ret = MPP_OK;

	enc_dbg_func("enter\n");

	frm_cfg->force_pskip++;
	frm_cfg->force_flag |= ENC_FORCE_PSKIP;

	/* NOTE: in some condition the pskip should not happen */

	mpp_enc_refs_rollback(enc->refs);
	mpp_enc_refs_set_usr_cfg(enc->refs, frm_cfg);

	enc_dbg_detail("task %d enc proc dpb\n", frm->seq_idx);
	mpp_enc_refs_get_cpb(enc->refs, cpb);

	enc_dbg_frm_status("frm %d start ***********************************\n",
			   cpb->curr.seq_idx);
	ENC_RUN_FUNC2(enc_impl_proc_dpb, impl, hal_task, enc, ret);

	enc_dbg_detail("task %d enc sw enc start\n", frm->seq_idx);
	ENC_RUN_FUNC2(enc_impl_sw_enc, impl, hal_task, enc, ret);

	enc_dbg_detail("task %d rc frame end\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_frm_end, enc->rc_ctx, rc_task, enc, ret);
	enc->qp_out = rc_task->qp_out;

TASK_DONE:
	enc_dbg_func("leave\n");
	return ret;
}

static void mpp_enc_terminate_task(MppEncImpl *enc, EncTask *task)
{
	//HalEncTask *hal_task = &task->info.enc;
	//   EncFrmStatus *frm = &enc->rc_task.frm;

	//    mpp_stopwatch_record(hal_task->stopwatch, "encode task done");

	if (enc->frame) {
		mpp_frame_deinit(&enc->frame);
		enc->frame = NULL;
	}

	if (enc->packet) {
		/* setup output packet and meta data */
		mpp_packet_set_length(enc->packet, 0);
		mpp_packet_ring_buf_put_used(enc->packet, enc->chan_id, enc->dev);
		mpp_packet_deinit(&enc->packet);
	}

	reset_enc_task(enc);
	task->status.val = 0;
}

MPP_RET mpp_enc_impl_alloc_task(MppEncImpl *enc)
{
	enc->enc_task = (void *)mpp_calloc(EncTask, 1);
	if (!enc->enc_task)
		return MPP_NOK;
	enc->init_time = mpp_time();
	enc->time_base = mpp_time();
	enc->time_end = mpp_time();
	return MPP_OK;
}

MPP_RET mpp_enc_impl_free_task(MppEncImpl *enc)
{
	if (enc->enc_task) {
		mpp_free(enc->enc_task);
		enc->enc_task = NULL;
	}
	return MPP_OK;
}

MPP_RET mpp_enc_impl_get_roi_osd(MppEncImpl *enc, MppFrame frame)
{
	if (enc->cfg.roi.change) {
		memcpy(&enc->cur_roi, &enc->cfg.roi, sizeof(enc->cur_roi));
		enc->cfg.roi.change = 0;
	}

	if (enc->cfg.osd.change) {
		mpp_enc_unref_osd_buf(&enc->cur_osd);
		memcpy(&enc->cur_osd, &enc->cfg.osd, sizeof(enc->cur_osd));
		enc->cfg.osd.change = 0;
	}

	if (!frame)
		return MPP_OK;
	if (enc->cur_roi.change)
		mpp_frame_add_roi(frame, &enc->cur_roi);

	if (enc->cur_osd.change)
		mpp_frame_add_osd(frame, &enc->cur_osd);
	return MPP_OK;
}

static MPP_RET mpp_enc_check_frm_valid(MppEncImpl *enc)
{
	if (enc->frame) {
		RK_U32 hor_stride = 0, ver_stride = 0;
		RK_U32 width = 0, height = 0;
		MppEncPrepCfg *prep = &enc->cfg.prep;
		hor_stride = mpp_frame_get_hor_stride(enc->frame);
		ver_stride = mpp_frame_get_ver_stride(enc->frame);
		width = mpp_frame_get_width(enc->frame);
		height = mpp_frame_get_height(enc->frame);
		if (prep->rotation == MPP_ENC_ROT_90 || prep->rotation == MPP_ENC_ROT_270)
			MPP_SWAP(RK_U32, width, height);
		if (hor_stride != prep->hor_stride ||
		    ver_stride != prep->ver_stride ||
		    width < prep->width ||
		    height < prep->height) {
			mpp_log("frame info no equal set drop: frame [%d, %d, %d, %d], prep [%d, %d, %d, %d]",
				width, height, hor_stride, ver_stride, prep->width, prep->height,
				prep->hor_stride, prep->ver_stride);
			return MPP_NOK;
		}
	}
	return  MPP_OK;
}

MPP_RET mpp_enc_impl_reg_cfg(MppEnc ctx, MppFrame frame)
{
	MppEncImpl *enc = (MppEncImpl *)ctx;
	MPP_RET ret = MPP_OK;
	EncTask *task = (EncTask *)enc->enc_task;
	EncRcTask *rc_task = &enc->rc_task;
	EncFrmStatus *frm = &rc_task->frm;
	MppEncRefFrmUsrCfg *frm_cfg = &enc->frm_cfg;
	MppEncHeaderStatus *hdr_status = &enc->hdr_status;
	EncTaskStatus *status = &task->status;
	HalEncTask *hal_task = &task->info.enc;
	MppStopwatch stopwatch = NULL;

	if (status->rc_reenc) { //online will no support reenc
		mpp_enc_reenc_simple(enc, task);
		return MPP_OK;
	}

	enc->frame = frame;
	enc->packet = NULL;

	if (mpp_enc_check_frm_valid(enc) != MPP_OK) {
		ret = MPP_NOK;
		goto TASK_DONE;
	}
	reset_hal_enc_task(hal_task);
	reset_enc_rc_task(rc_task);
	frm->seq_idx = task->seq_idx++;

	hal_task->rc_task = rc_task;
	hal_task->frm_cfg = frm_cfg;
	hal_task->stopwatch = stopwatch;

	rc_task->frame = enc->frame;
	enc_dbg_detail("task seq idx %d start\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_frm_check_drop, enc->rc_ctx, rc_task, enc, ret);
	status->rc_check_frm_drop = 1;
	enc_dbg_detail("task %d drop %d\n", frm->seq_idx, frm->drop);

	// when the frame should be dropped just return empty packet
	if (frm->drop) {
		hal_task->valid = 0;
		hal_task->length = 0;
		ret = MPP_NOK;
		goto TASK_DONE;
	}
	hal_task->valid = 1;
	mpp_assert(hal_task->valid);
	ret = mpp_enc_alloc_output(enc);
	if (ret)
		goto TASK_DONE;
	hal_task->frame = enc->frame;
	hal_task->input = enc->frm_buf;
	hal_task->packet = enc->packet;
	hal_task->output = enc->pkt_buf;

	status->pkt_buf_rdy = 1;

	hal_task->output = enc->pkt_buf;
	mpp_assert(enc->packet);

	mpp_enc_impl_get_roi_osd(enc, enc->frame);

	// 11. check hal info update
	if (!enc->hal_info_updated) {
		update_enc_hal_info(enc);
		enc->hal_info_updated = 1;
	}
	// 12. generate header before hardware stream
	if (!hdr_status->ready) {
		/* config cpb before generating header */

		enc_impl_gen_hdr(enc->impl, enc->hdr_pkt);
		enc->hdr_len = mpp_packet_get_length(enc->hdr_pkt);
		hdr_status->ready = 1;

		enc_dbg_detail("task %d update header length %d\n",
			       frm->seq_idx, enc->hdr_len);

		mpp_packet_append(enc->packet, enc->hdr_pkt);
		hal_task->header_length = enc->hdr_len;
		hal_task->length += enc->hdr_len;
		hdr_status->added_by_change = 1;

		enc_dbg_detail("added_by_change \n");
	}
	enc_dbg_detail("check_hal_task_pkt_len \n");

	check_hal_task_pkt_len(hal_task, "gen_hdr and adding");

	enc_dbg_detail("task %d enc start\n", frm->seq_idx);
	ENC_RUN_FUNC2(enc_impl_start, enc->impl, hal_task, enc, ret);

	if (frm_cfg->force_flag)
		mpp_enc_refs_set_usr_cfg(enc->refs, frm_cfg);

	mpp_enc_refs_stash(enc->refs);
	ENC_RUN_FUNC2(mpp_enc_normal_cfg, enc, task, enc, ret);
	frm_cfg->force_flag = 0;
TASK_DONE:
	if (ret)
		mpp_enc_terminate_task(enc, task);

	return ret;
}

MPP_RET mpp_enc_impl_hw_start(MppEnc ctx, MppEnc jpeg_ctx)
{
	MppEncImpl *enc = (MppEncImpl *)ctx;
	EncTask *task = (EncTask *)enc->enc_task;
	EncRcTask *rc_task = &enc->rc_task;
	EncFrmStatus *frm = &rc_task->frm;
	MppEncHal hal = enc->enc_hal;
	HalEncTask *hal_task = &task->info.enc;
	MPP_RET ret = MPP_OK;
	HalEncTask *jpeg_hal_task = NULL;
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();
	MppEncCfgSet *cfg = &enc->cfg;

	if (jpeg_ctx) {
		MppEncImpl *jpeg_enc = (MppEncImpl *)jpeg_ctx;
		EncTask *jpeg_task = (EncTask *)jpeg_enc->enc_task;
		jpeg_hal_task = &jpeg_task->info.enc;
	}

	enc_dbg_detail("task %d hal start\n", frm->seq_idx);
	ENC_RUN_FUNC3(mpp_enc_hal_start, hal, hal_task, jpeg_hal_task, enc,
		      ret);

	if (mpidev_fn && mpidev_fn->set_intra_info) {
		RK_U64 dts = mpp_frame_get_dts(hal_task->frame);
		RK_U64 pts = mpp_frame_get_pts(hal_task->frame);
		RK_U32 is_intra = (cfg->codec.coding == MPP_VIDEO_CodingMJPEG || frm->is_intra);
		mpidev_fn->set_intra_info(enc->chan_id, dts, pts, is_intra);
	}


TASK_DONE:
	if (ret) {
		mpp_enc_terminate_task(enc, task);
		if (jpeg_ctx) {
			MppEncImpl *jpeg_enc = (MppEncImpl *)jpeg_ctx;
			EncTask *jpeg_task = (EncTask *)jpeg_enc->enc_task;
			mpp_enc_terminate_task(jpeg_enc, jpeg_task);
		}
		enc->cfg_fail_cnt++;
	}
	return ret;
}
static MPP_RET mpp_enc_comb_end_jpeg(MppEnc ctx, MppPacket *packet)
{
	MppEncImpl *enc = (MppEncImpl *)ctx;
	MPP_RET ret = MPP_OK;
	EncTask *task = (EncTask *)enc->enc_task;
	EncRcTask *rc_task = &enc->rc_task;
	HalEncTask *hal_task = &task->info.enc;
	EncFrmStatus *frm = &rc_task->frm;
	MppEncHal hal = enc->enc_hal;
	struct vcodec_mpidev_fn *mpidev_fn = get_mpidev_ops();

	hal_task->length -= hal_task->hw_length;
	ENC_RUN_FUNC3(mpp_enc_hal_ret_task, hal, hal_task, NULL, enc, ret);
	ENC_RUN_FUNC2(rc_hal_end, enc->rc_ctx, rc_task, enc, ret);
	enc_dbg_detail("task %d hal wait\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_hal_end, enc->rc_ctx, rc_task, enc, ret);
	enc_dbg_detail("task %d rc enc->frame end\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_frm_end, enc->rc_ctx, rc_task, enc, ret);
	enc->qp_out = rc_task->qp_out;
	enc->time_end = mpp_time();
	enc->frame_count++;

	if (mpidev_fn && mpidev_fn->set_intra_info) {
		RK_U64 dts = mpp_frame_get_dts(hal_task->frame);
		RK_U64 pts = mpp_frame_get_pts(hal_task->frame);
		mpidev_fn->set_intra_info(enc->chan_id, dts, pts, 1);
	}

	if (enc->dev && enc->time_base && enc->time_end &&
	    ((enc->time_end - enc->time_base) >= (RK_S64)(1000 * 1000)))
		update_hal_info_fps(enc);

	frm->reencode = 0;
	frm->reencode_times = 0;
TASK_DONE:

	/* setup output packet and meta data */
	if (ret) {
		enc->frame_force_drop++;
		mpp_packet_set_length(enc->packet, 0);
		mpp_packet_ring_buf_put_used(enc->packet, enc->chan_id, enc->dev);
		mpp_packet_deinit(&enc->packet);
	} else {
		mpp_packet_set_length(enc->packet, hal_task->length);
		if (frm->is_intra)
			mpp_packet_set_flag(enc->packet, MPP_PACKET_FLAG_INTRA); //set as key frame
		mpp_packet_set_temporal_id(enc->packet, frm->temporal_id);
		if (mpp_packet_ring_buf_put_used(enc->packet, enc->chan_id, enc->dev))
			mpp_err_f("ring_buf_put_used fail \n");
	}

	*packet = enc->packet;

	/*
	 * First return output packet.
	 * Then enqueue task back to input port.
	 * Final user will release the mpp_frame they had input.
	 */
	enc_dbg_detail("task %d enqueue packet pts %lld\n", frm->seq_idx,
		       enc->task_pts);
	if (enc->frame)
		mpp_frame_deinit(&enc->frame);
	reset_enc_task(enc);
	task->status.val = 0;
	return ret;
}

MPP_RET mpp_enc_impl_int(MppEnc ctx, MppEnc jpeg_ctx, MppPacket *packet,
			 MppPacket *jpeg_packet)
{
	MppEncImpl *enc = (MppEncImpl *)ctx;
	MppEncHal hal = enc->enc_hal;
	EncTask *task = (EncTask *)enc->enc_task;
	EncRcTask *rc_task = &enc->rc_task;
	HalEncTask *hal_task = &task->info.enc;
	EncFrmStatus *frm = &rc_task->frm;
	EncTaskStatus *status = &task->status;
	EncTask *jpeg_task = NULL;
	MPP_RET ret = MPP_OK;
	if (jpeg_ctx) {
		MppEncImpl *jpeg_enc = (MppEncImpl *)jpeg_ctx;
		jpeg_task = (EncTask *)jpeg_enc->enc_task;
	}
	enc_dbg_detail("task %d hal wait\n", frm->seq_idx);
	ENC_RUN_FUNC2(mpp_enc_hal_wait, hal, hal_task, enc, ret);
	ENC_RUN_FUNC3(mpp_enc_end, enc, task, jpeg_task, enc, ret);
	if (frm->reencode &&
	    frm->reencode_times < enc->cfg.rc.max_reenc_times) {
		hal_task->length -= hal_task->hw_length;
		hal_task->hw_length = 0;
		status->rc_reenc = 1;

		if (enc->online || enc->ref_buf_shared) {
			enc_dbg_detail("shared status can't reenc drop request idr\n");
			ret = MPP_NOK;
			goto TASK_DONE;
		}

		enc_dbg_detail("task %d reenc %d times %d\n", frm->seq_idx,
			       frm->reencode, frm->reencode_times);

		if (frm->drop) {
			mpp_enc_reenc_drop(enc, task);
			status->rc_reenc = 0;
		}

		if (frm->force_pskip && !frm->is_idr && !frm->is_lt_ref) {
			mpp_enc_reenc_force_pskip(enc, task);
			status->rc_reenc = 0;
		}
		if (status->rc_reenc) {
			if (jpeg_ctx)
				mpp_enc_comb_end_jpeg(jpeg_ctx, jpeg_packet);
			return MPP_OK;
		}
	}
	enc_dbg_detail("task %d rc enc->frame end\n", frm->seq_idx);
	ENC_RUN_FUNC2(rc_frm_end, enc->rc_ctx, rc_task, enc, ret);
	enc->qp_out = rc_task->qp_out;
	enc->time_end = mpp_time();
	enc->frame_count++;

	if (enc->dev && enc->time_base && enc->time_end &&
	    ((enc->time_end - enc->time_base) >= (RK_S64)(1000 * 1000)))
		update_hal_info_fps(enc);

	frm->reencode = 0;
	frm->reencode_times = 0;

TASK_DONE:

	//mpp_stopwatch_record(hal_task->stopwatch, "encode task done");
	if (ret) {
		enc->frame_force_drop++;
		enc->frm_cfg.force_flag |= ENC_FORCE_IDR;
		enc->hdr_status.val = 0;
		mpp_packet_set_length(enc->packet, 0);
		mpp_packet_ring_buf_put_used(enc->packet, enc->chan_id, enc->dev);
		mpp_packet_deinit(&enc->packet);
	} else {
		/* setup output packet and meta data */
		mpp_packet_set_length(enc->packet, hal_task->length);
		if (frm->is_intra)
			mpp_packet_set_flag(enc->packet, MPP_PACKET_FLAG_INTRA); //set as key frame
		mpp_packet_set_temporal_id(enc->packet, frm->temporal_id);
		if (mpp_packet_ring_buf_put_used(enc->packet, enc->chan_id, enc->dev))
			mpp_err_f("ring_buf_put_used fail \n");
	}
	*packet = enc->packet;
	/*
	 * First return output packet.
	 * Then enqueue task back to input port.
	 * Final user will release the mpp_frame they had input.
	 */
	enc_dbg_detail("task %d enqueue packet pts %lld\n", frm->seq_idx,
		       enc->task_pts);

	if (enc->frame)
		mpp_frame_deinit(&enc->frame);
	mpp_enc_rc_info_backup(enc);
	reset_enc_task(enc);
	task->status.val = 0;
	if (jpeg_ctx)
		mpp_enc_comb_end_jpeg(jpeg_ctx, jpeg_packet);
	return ret;
}

void mpp_enc_impl_poc_debug_info(void *seq_file, MppEnc ctx, RK_U32 chl_id)
{
	MppEncImpl *enc = (MppEncImpl *)ctx;
	MppEncCfgSet *cfg = &enc->cfg;
	EncTask *task = (EncTask *)enc->enc_task;
	struct seq_file *seq = (struct seq_file *)seq_file;
	seq_puts(
		seq,
		"\n--------venc chn attr 1---------------------------------------------------------------------------\n");
	seq_printf(seq, "%8s%8s%8s%6s%9s%10s%10s%6s\n", "ID", "Width", "Height",
		   "Type", "ByFrame", "Sequence", "GopMode", "Prio");

	seq_printf(seq, "%8d%8u%8u%6s%9s%10u%10s%6d\n", chl_id, cfg->prep.width,
		   cfg->prep.height, strof_coding_type(cfg->codec.coding), "y",
		   task->seq_idx, strof_gop_mode(enc->gop_mode), 0);

	seq_puts(
		seq,
		"\n--------venc chn attr 2---------------------------------------------------------------------------\n");
	seq_printf(seq, "%8s%8s%8s%8s%12s%12s%12s%12s%10s\n", "ID", "VeStr", "SrcFr",
		   "TarFr", "Timeref", "PixFmt", "RealFps*10", "rotation", "mirror");
	seq_printf(seq, "%8d%8s%8d%8d%12x%12s%12u%12s%10s\n", chl_id, "y",
		   cfg->rc.fps_in_num / cfg->rc.fps_in_denorm,
		   cfg->rc.fps_out_num / cfg->rc.fps_out_denorm,
		   (RK_U32)enc->init_time, strof_pixel_fmt(cfg->prep.format),
		   enc->real_fps, strof_rotation(cfg->prep.rotation),
		   strof_bool(cfg->prep.mirroring));

	seq_puts(
		seq,
		"\n--------ring buf status---------------------------------------------------------------------------\n");

	seq_printf(seq, "%8s%8s%8s%8s%10s%10s%10s%10s\n", "ID", "w_pos", "r_pos",
		   "usd_len", "total_len", "min_size", "l_w_pos", "l_r_pos");
	seq_printf(seq, "%8d%8d%8d%8d%10d%10d%10d%10d\n", chl_id, enc->ring_pool->w_pos,
		   enc->ring_pool->r_pos, enc->ring_pool->use_len, enc->ring_pool->len
		   , enc->ring_pool->min_buf_size, enc->ring_pool->l_w_pos, enc->ring_pool->l_r_pos);

	seq_puts(
		seq,
		"\n--------hw status---------------------------------------------------------------------------------\n");
	seq_printf(seq, "%8s%8s%12s%14s%14s%14s%16s\n", "ID", "hw_run", "enc_status", "pkt_fail_cnt",
		   "ring_fail_cnt",
		   "cfg_fail_cnt", "start_fail_cnt");
	seq_printf(seq, "%8d%8d%12d%14u%14u%14u%16u\n", chl_id, enc->hw_run, enc->enc_status,
		   enc->pkt_fail_cnt,
		   enc->ringbuf_fail_cnt,
		   enc->cfg_fail_cnt, enc->start_fail_cnt);

	enc_impl_proc_debug(seq_file, enc->impl, chl_id);
	rc_proc_show(seq_file, enc->rc_ctx, chl_id);
}
