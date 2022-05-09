/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#ifndef __ROCKCHIP_MPP_VCODEC_BASE_H__
#define __ROCKCHIP_MPP_VCODEC_BASE_H__

#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include"rk_type.h"
#include "mpp_err.h"
#include "mpp_packet.h"

#define MAX_ENC_NUM 8

typedef enum {
	CHAN_STATE_NULL = 0,
	CHAN_STATE_SUSPEND_PENDING,
	CHAN_STATE_SUSPEND,
	CHAN_STATE_RUN,
	CHAN_STATE_CANCEL,
	CHAN_STATE_MAX
} chan_state;

struct chan_cfg {
	RK_U8 priority;
	RK_U8 online;
	atomic_t comb_runing;
};

struct mpi_queue;
struct mpi_dev;

struct mpp_chan {
	RK_S32 chan_id;
	RK_S32 binder_chan_id;
	RK_S32 coding_type;
	MppCtxType type;
	struct chan_cfg cfg;
	chan_state state;
	RK_U32 chan_pri;
	atomic_t runing;
	RK_U32 reenc;
	spinlock_t chan_lock;
	void *handle;
	struct mpi_queue *yuv_queue;
	wait_queue_head_t wait;
	wait_queue_head_t stop_wait;
	struct mutex stream_done_lock;
	struct mutex stream_remove_lock;
	struct list_head stream_done;
	struct list_head stream_remove;
	atomic_t stream_count;
	atomic_t str_out_cnt;
	RK_U32 last_cfg_time;
	RK_S64 last_yuv_time;
	RK_S64 last_jeg_combo_start;
	RK_S64 last_jeg_combo_end;
	RK_S32 gap_time;
	RK_S32 combo_gap_time;
	RK_U32 max_width;
	RK_U32 max_height;
	RK_U32 ring_buf_size;
	struct hal_shared_buf shared_buf;
	RK_U32 max_lt_cnt;
};

struct stream_packet {
	struct list_head list;
	MppPacket *src;
	struct kref ref;
};

struct venc_module {
	void *check;
	const char *name;
	struct mpi_dev *dev;
	struct mpp_chan mpp_enc_chan_entry[MAX_ENC_NUM];
	RK_U32 num_enc;
	spinlock_t enc_lock;
	RK_U32 started_chan_num;
	RK_U32 chan_pri_tab_index;
	RK_U32 curr_high_prior_chan;
	RK_S32 chan_pri_tab[MAX_ENC_NUM][2];	/* chan_id and prio */
	struct vcodec_threads *thd;
};

struct vcodec_entry {
	struct venc_module venc;
};

RK_U32 mpp_vcodec_get_chan_num(MppCtxType type);
void mpp_vcodec_inc_chan_num(MppCtxType type);
void mpp_vcodec_dec_chan_num(MppCtxType type);
struct venc_module *mpp_vcodec_get_enc_module_entry(void);
struct mpp_chan *mpp_vcodec_get_chan_entry(RK_S32 chan_id, MppCtxType type);
void enc_chan_update_chan_prior_tab(void);
MPP_RET enc_chan_update_tab_after_enc(RK_U32 curr_chan);
void enc_chan_get_high_prior_chan(void);
int mpp_vcodec_init(void);
int mpp_vcodec_unregister_mipdev(void);
int mpp_vcodec_deinit(void);
int mpp_vcodec_chan_entry_init(struct mpp_chan *entry, MppCtxType type,
			       MppCodingType coding, void *handle);
int mpp_vcodec_chan_entry_deinit(struct mpp_chan *entry);
struct stream_packet *stream_packet_alloc(void);
void stream_packet_free(struct kref *ref);
void mpp_vcodec_stream_clear(struct mpp_chan *entry);
int mpp_vcodec_get_free_chan(MppCtxType type);
int vcodec_create_mpi_dev(void);
void enc_test(void);
int mpp_vcodec_chan_setup_hal_bufs(struct mpp_chan *entry, struct vcodec_attr *attr);


#endif
