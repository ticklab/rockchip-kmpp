// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * author: timkingh.huang@rock-chips.com
 *
 */

#ifndef __VEPU_PP_API_H__
#define __VEPU_PP_API_H__

struct dma_buf;
struct mpp_frame_infos;

enum pp_fmt {
	RKVENC_F_BGRA_8888 = 0x0,
	RKVENC_F_RGB_888 = 0x1,
	RKVENC_F_RGB_565 = 0x2,

	RKVENC_F_YCbCr_422_SP = 0x4,
	RKVENC_F_YCbCr_422_P = 0x5,
	RKVENC_F_YCbCr_420_SP = 0x6,
	RKVENC_F_YCbCr_420_P = 0x7,

	RKVENC_F_YCbYCr = 0x8,
	RKVENC_F_CbYCrY = 0x9,

	RKVENC_F_YCbCr_400      = 0xA,
	RKVENC_F_RESERVED1      = 0xB,
	RKVENC_F_YCbCr_444_SP   = 0xC,
	RKVENC_F_YCbCr_444_P    = 0xD,
	RKVENC_F_BUTT           = 0xE
};

struct pp_chn_attr {
	int width;
	int height;
	int smear_en;
	int weightp_en;
	int md_en;
	int od_en;
	int down_scale_en;
	int reserved[8];
};

struct pp_com_cfg {
	int frm_cnt;
	enum pp_fmt fmt;

	int gop;
	int md_interval;
	int od_interval;

	struct dma_buf *src_buf;
	int stride0;
	int stride1;
	int reserved[8];
};

struct pp_md_cfg {
	struct dma_buf *mdw_buf;
	int switch_sad;
	int thres_sad;
	int thres_move;
	int night_mode;
	int filter_switch;
	int reserved[8];
};

struct pp_od_cfg {
	int is_background;
	int thres_complex;
	int thres_area_complex;
	int reserved[8];
};

struct pp_smear_cfg {
	struct dma_buf *smrw_buf; /* 0x200: smear write */
	int reserved[32];
};

struct pp_weightp_cfg {
	int dummy;
	int reserved[16];
};

struct pp_od_out {
	int od_flg;
	int pix_sum;
	int reserved[8];
};

struct pp_weightp_out {
	int wp_out_par_y;
	int wp_out_par_u;
	int wp_out_par_v;
	int wp_out_pic_mean;
	int reserved[8];
};

enum pp_cmd {
	PP_CMD_BASE = 0,
	PP_CMD_SET_COMMON_CFG = 0x10, /* struct pp_com_cfg */
	PP_CMD_SET_MD_CFG = 0x20, /* struct pp_md_cfg */
	PP_CMD_SET_OD_CFG = 0x30, /* struct pp_od_cfg */
	PP_CMD_SET_SMEAR_CFG = 0x40, /* struct pp_smear_cfg */
	PP_CMD_SET_WEIGHTP_CFG = 0x50, /* struct pp_weightp_cfg */

	PP_CMD_RUN_SYNC = 0x60,

	PP_CMD_GET_OD_OUTPUT = 0x70, /* struct pp_od_out */
	PP_CMD_GET_WEIGHTP_OUTPUT = 0x80, /* struct pp_weightp_out */
};

struct vcodec_mpibuf_fn {
	struct mpi_buf *(*buf_alloc)(size_t size);
	void *(*buf_map)(struct mpi_buf *buf);
	int  (*buf_unmap)(struct mpi_buf *buf);
	void (*buf_ref)(struct mpi_buf *buf);
	void (*buf_unref)(struct mpi_buf *buf);
	struct dma_buf *(*buf_get_dmabuf)(struct mpi_buf *buf);
	int (*buf_get_paddr)(struct mpi_buf *buf);
	struct mpi_queue *(*buf_queue_create)(int cnt);
	int (*buf_queue_destroy)( struct mpi_queue *queue);
	int (*buf_queue_push)(struct mpi_queue *queue, struct mpi_buf *buf);
	struct mpi_buf *(*buf_queue_pop)(struct mpi_queue *queue);
	struct mpi_buf *(*dma_buf_import)(struct dma_buf *dma_buf, struct mpp_frame_infos *info,
					  int chan_id);
	int (*get_buf_frm_info)(struct mpi_buf *buf, struct mpp_frame_infos *info);
	struct mpi_buf_pool *(*buf_pool_create)(size_t buf_size, unsigned int num_bufs);
	int (*buf_pool_destroy)(struct mpi_buf_pool *pool);
	struct mpi_buf *(*buf_pool_request_buf)(struct mpi_buf_pool *pool);
	int (*buf_pool_get_free_num)(struct mpi_buf_pool *pool);
};

extern int vepu_pp_create_chn(int chn, struct pp_chn_attr *attr);
extern int vepu_pp_destroy_chn(int chn);
extern int vepu_pp_control(int chn, enum pp_cmd cmd, void *param);
extern void register_vmpibuf_func_to_pp(struct vcodec_mpibuf_fn *mpibuf_fn);
extern void unregister_vmpibuf_func_pp(void);

#endif
