// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *  Herman Chen <herman.chen@rock-chips.com>
 *
 */
#ifndef __RK_EXPORT_FUNC_H__
#define __RK_EXPORT_FUNC_H__

struct dma_buf;
struct mpi_obj;

struct mpp_frame_infos {
	RK_U32  width;
	RK_U32  height;
	RK_U32  hor_stride;
	RK_U32  ver_stride;
	RK_U32  hor_stride_pixel;
	RK_U32  offset_x;
	RK_U32  offset_y;
	RK_U32  fmt;
	RK_U32  fd;
	RK_U64  pts;
	RK_S32  jpeg_chan_id;
	void    *osd_buf;
	RK_S32  mpi_buf_id;
	void    *jpg_combo_osd_buf;
	RK_U32  is_gray;
	RK_U32  is_full;
	RK_U32  phy_addr;
	RK_U64  dts;
	void    *pp_info;
	RK_U32  res[2];
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
	struct mpi_buf *(*dma_buf_import)(struct dma_buf *dma_buf, struct mpp_frame_infos *info,
					  int chan_id);
	void (*get_buf_frm_info)(struct mpi_buf *buf, struct mpp_frame_infos *info, RK_S32 chan_id);
	struct mpi_buf_pool *(*buf_pool_create)(size_t buf_size, unsigned int num_bufs);
	int (*buf_pool_destroy)(struct mpi_buf_pool *pool);
	struct mpi_buf *(*buf_pool_request_buf)(struct mpi_buf_pool *pool);
	int (*buf_pool_get_free_num)(struct mpi_buf_pool *pool);
};

struct vcodec_set_dev_fn {
	void *(*bind) (void *out_param);
	int (*unbind) (void *ctx);
	int (*msg_callback)(struct mpi_obj *obj, int event, void *args);

};

struct vcodec_mpidev_fn {
	struct mpi_dev *(*create_dev)(const char *name, struct vcodec_set_dev_fn *dev_fn);
	int (*destory_dev)(struct mpi_dev *dev);
	int (*handle_message)(struct mpi_obj *obj, int event, void *args);
	void *(*get_chnl_ctx)(struct mpi_obj *obj);
	int (*get_chnl_id)(void *out_parm);
	int (*get_chnl_type)(void *out_parm);
	int (*set_intra_info)(RK_S32 chn_id, RK_U64 dts, RK_U64 pts, RK_U32 is_intra);
	int (*notify_drop_frm)(RK_S32 chn_id);
};

struct vcodec_mppdev_svr_fn {
	struct mpp_session *(*chnl_open)(int client_type);
	int (*chnl_register)(struct mpp_session *session, void *fun, unsigned int chn_id);
	int (*chnl_release)(struct mpp_session *session);
	int (*chnl_add_req)(struct mpp_session *session,  void *reqs);
	unsigned int (*chnl_get_iova_addr)(struct mpp_session *session,  struct dma_buf *buf,
					   unsigned int offset);
	void (*chnl_release_iova_addr)(struct mpp_session *session,  struct dma_buf *buf);

	struct device *(*mpp_chnl_get_dev)(struct mpp_session *session);
};

#define VENC_MAX_PACK_INFO_NUM 8

struct venc_pack_info {
	RK_U32      flag;
	RK_U32      temporal_id;
	RK_U32      packet_offset;
	RK_U32      packet_len;
};

struct venc_packet {
	RK_S64               u64priv_data;  //map mpi_id
	RK_U64               u64packet_addr; //packet address in kernel space
	RK_U32               len;
	RK_U32               buf_size;

	RK_U64               u64pts;
	RK_U64               u64dts;
	RK_U32               flag;
	RK_U32               temporal_id;
	RK_U32               offset;
	RK_U32               data_num;
	struct venc_pack_info       packet[8];
};

struct vsm_buf {
	unsigned int phy_addr;
	void *vir_addr;
	int buf_size;
};

struct vcodec_vsm_buf_fn {
	int (*GetBuf) (struct vsm_buf *buf, int type, int flag, int chan_id);
	int (*PutBuf)(struct vsm_buf *buf, struct venc_packet *packet, int flag, int chan_id);
	int (*FreeBuf)(struct vsm_buf *buf, int chan_id);
};

extern void vsm_buf_register_fn2vcocdec (struct vcodec_vsm_buf_fn *vsm_fn);


struct vcodec_mpidev_fn *get_mpidev_ops(void);
struct vcodec_mpibuf_fn *get_mpibuf_ops(void);
struct vcodec_vsm_buf_fn *get_vsm_ops(void);

extern void vmpi_register_fn2vcocdec (struct vcodec_mpidev_fn *mpidev_fn,
				      struct vcodec_mpibuf_fn *mpibuf_f);
extern void vmpi_unregister_fn2vcocdec(void);
extern struct vcodec_mppdev_svr_fn *get_mppdev_svr_ops(void);

extern int mpp_vcodec_clear_buf_resource(void);

#endif
