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
};

struct pp_md_cfg {
	struct dma_buf *mdw_buf;
	int resi_thd0;
};

struct pp_od_cfg {
	int stride;
};

struct pp_od_out {
	int od_flg;
	int pix_sum;
};

enum pp_cmd {
	PP_CMD_BASE = 0,
	PP_CMD_SET_COMMON_CFG,
	PP_CMD_SET_MD_CFG,
	PP_CMD_SET_OD_CFG,

	PP_CMD_RUN_SYNC,

	PP_CMD_GET_OD_OUTPUT,
};

struct vcodec_mpibuf_fn {
	struct mpi_buf *(*buf_alloc)(size_t size);
	void *(*buf_map)(struct mpi_buf *buf);
	int  (*buf_unmap)(struct mpi_buf *buf);
	void (*buf_ref)(struct mpi_buf *buf);
	void (*buf_unref)(struct mpi_buf *buf);
	struct dma_buf *(*buf_get_dmabuf)(struct mpi_buf *buf);
	struct mpi_queue *(*buf_queue_create)(int cnt);
	int (*buf_queue_destroy)( struct mpi_queue *queue);
	int (*buf_queue_push)(struct mpi_queue *queue, struct mpi_buf *buf);
	struct mpi_buf *(*buf_queue_pop)(struct mpi_queue *queue);
	struct mpi_buf *(*dma_buf_import)(struct dma_buf *dma_buf,struct mpp_frame_infos *info);
	void (*get_buf_frm_info)(struct mpi_buf *buf, struct mpp_frame_infos *info);
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
