// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_frame"

#include <linux/string.h>
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_maths.h"
#include "mpp_frame_impl.h"

static const char *module_name = MODULE_TAG;

static void setup_mpp_frame_name(MppFrameImpl * frame)
{
	frame->name = module_name;
}

MPP_RET check_is_mpp_frame(void *frame)
{
	if (frame && ((MppFrameImpl *) frame)->name == module_name)
		return MPP_OK;

	mpp_err_f("pointer %p failed on check\n", frame);
	mpp_abort();

	return MPP_NOK;
}

MPP_RET mpp_frame_init(MppFrame * frame)
{
	MppFrameImpl *p = NULL;

	if (NULL == frame) {
		mpp_err_f("invalid NULL pointer input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = mpp_calloc(MppFrameImpl, 1);
	if (NULL == p) {
		mpp_err_f("malloc failed\n");
		return MPP_ERR_NULL_PTR;
	}

	setup_mpp_frame_name(p);
	*frame = p;

	return MPP_OK;
}

MPP_RET mpp_frame_deinit(MppFrame * frame)
{
	MppFrameImpl *p = NULL;

	if (NULL == frame || check_is_mpp_frame(*frame)) {
		mpp_err_f("invalid NULL pointer input\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (MppFrameImpl *) * frame;
	if (p->buffer)
		mpp_buffer_put(p->buffer);

	if (p->osd) {
		MppEncOSDData3 *osd_data = (MppEncOSDData3 *)p->osd;
		RK_U32 i = 0;

		for (i = 0; i < osd_data->num_region; i++) {
			if (osd_data->region[i].osd_buf.buf)
				mpi_buf_unref(osd_data->region[i].osd_buf.buf);

			if (osd_data->region[i].inv_cfg.inv_buf.buf)
				mpi_buf_unref(osd_data->region[i].inv_cfg.inv_buf.buf);
		}
	}

	if (p->pp_info) {
		//mpi_buf_unref(pp_info.smear);
	}

	mpp_free(*frame);
	*frame = NULL;

	return MPP_OK;
}

MppFrame mpp_frame_get_next(MppFrame frame)
{
	MppFrameImpl *p = NULL;

	if (check_is_mpp_frame(frame))
		return NULL;

	p = (MppFrameImpl *) frame;

	return (MppFrame) p->next;
}

MPP_RET mpp_frame_set_next(MppFrame frame, MppFrame next)
{
	MppFrameImpl *p = NULL;

	if (check_is_mpp_frame(frame))
		return MPP_ERR_UNKNOW;

	p = (MppFrameImpl *) frame;
	p->next = (MppFrameImpl *) next;

	return MPP_OK;
}

MppBuffer mpp_frame_get_buffer(MppFrame frame)
{
	MppFrameImpl *p = NULL;

	if (check_is_mpp_frame(frame))
		return NULL;

	p = (MppFrameImpl *) frame;

	return (MppFrame) p->buffer;
}

void mpp_frame_set_buffer(MppFrame frame, MppBuffer buffer)
{
	MppFrameImpl *p = NULL;

	if (check_is_mpp_frame(frame))
		return;

	p = (MppFrameImpl *) frame;
	if (p->buffer != buffer) {
		if (buffer)
			mpp_buffer_inc_ref(buffer);

		if (p->buffer)
			mpp_buffer_put(p->buffer);

		p->buffer = buffer;
	}
}

RK_S32 mpp_frame_has_meta(const MppFrame frame)
{
	MppFrameImpl *p = NULL;

	if (check_is_mpp_frame(frame))
		return 0;

	p = (MppFrameImpl *) frame;

	return 0;
}

MPP_RET mpp_frame_add_roi(MppFrame frame, MppRoi roi)
{
	MppFrameImpl *p = (MppFrameImpl *)frame;

	if (check_is_mpp_frame(frame) || !roi)
		return MPP_ERR_NULL_PTR;

	// memcpy(&p->roi, ptr, sizeof(MppEncROICfg));
	p->roi = roi;

	return 0;
}

MppRoi mpp_frame_get_roi(MppFrame frame)
{
	MppFrameImpl *p = (MppFrameImpl *)frame;

	if (check_is_mpp_frame(frame))
		return NULL;

	return (MppRoi)p->roi;
}

MPP_RET mpp_frame_add_osd(MppFrame frame, MppOsd osd)
{
	MppFrameImpl *p = (MppFrameImpl *)frame;
	MppEncOSDData3 *osd_data = NULL;
	RK_U32 i = 0;

	if (check_is_mpp_frame(frame) || !osd)
		return MPP_ERR_NULL_PTR;

	p->osd = osd;
	osd_data = (MppEncOSDData3 *)osd;

	for (i = 0; i < osd_data->num_region; i++) {
		if (osd_data->region[i].osd_buf.buf)
			mpi_buf_ref(osd_data->region[i].osd_buf.buf);
		if (osd_data->region[i].inv_cfg.inv_buf.buf)
			mpi_buf_ref(osd_data->region[i].inv_cfg.inv_buf.buf);
	}

	return 0;
}

MppOsd mpp_frame_get_osd(MppFrame frame)
{
	MppFrameImpl *p = (MppFrameImpl *)frame;

	if (check_is_mpp_frame(frame))
		return NULL;

	return (MppOsd)p->osd;

}

MPP_RET mpp_frame_add_ppinfo(MppFrame frame, MppPpInfo pp_info)
{
	MppFrameImpl *p = (MppFrameImpl *)frame;

	if (check_is_mpp_frame(frame) || !pp_info)
		return MPP_ERR_NULL_PTR;

	p->pp_info = pp_info;
	//    mpi_buf_ref(pp_info.smear);

	return 0;
}

MppPpInfo mpp_frame_get_ppinfo(MppFrame frame)
{
	MppFrameImpl *p = (MppFrameImpl *)frame;

	if (check_is_mpp_frame(frame))
		return NULL;

	return (MppPpInfo)p->pp_info;
}

void mpp_frame_set_stopwatch_enable(MppFrame frame, RK_S32 enable)
{
	if (check_is_mpp_frame(frame))
		return;
#if 0
	MppFrameImpl *p = (MppFrameImpl *) frame;
	if (enable && NULL == p->stopwatch) {
		char name[32];

		snprintf(name, sizeof(name) - 1, "frm %8llx", p->pts);
		p->stopwatch = mpp_stopwatch_get(name);
		if (p->stopwatch)
			mpp_stopwatch_set_show_on_exit(p->stopwatch, 1);
	} else if (!enable && p->stopwatch) {
		mpp_stopwatch_put(p->stopwatch);
		p->stopwatch = NULL;
	}
#endif
	return;
}

MppStopwatch mpp_frame_get_stopwatch(const MppFrame frame)
{
	if (check_is_mpp_frame(frame))
		return NULL;

	/*  MppFrameImpl *p = (MppFrameImpl *)frame;
	   return p->stopwatch; */
	return NULL;
}

MPP_RET mpp_frame_copy(MppFrame dst, MppFrame src)
{
	MppFrameImpl *p = NULL;
	if (NULL == dst || check_is_mpp_frame(src)) {
		mpp_err_f("invalid input dst %p src %p\n", dst, src);
		return MPP_ERR_UNKNOW;
	}

	memcpy(dst, src, sizeof(MppFrameImpl));
	p = (MppFrameImpl *) dst;

	p->osd = NULL; //osd part no process

	/*if (p->osd) {
			MppEncOSDData3 *osd_data = NULL;
			RK_U32 i = 0;
			osd_data = (MppEncOSDData3 *)p->osd;
			for (i = 0; i < osd_data->num_region; i++) {
				if (osd_data->region[i].osd_buf.buf) {
					mpi_buf_ref(osd_data->region[i].osd_buf.buf);
				}

				if (osd_data->region[i].inv_cfg.inv_buf.buf) {
					mpi_buf_ref(osd_data->region[i].inv_cfg.inv_buf.buf);
				}
			}
	}*/
//    if (p->meta)
//        mpp_meta_inc_ref(p->meta);

	return MPP_OK;
}

MPP_RET mpp_frame_info_cmp(MppFrame frame0, MppFrame frame1)
{
	MppFrameImpl *f0 = NULL;
	MppFrameImpl *f1 = NULL;

	if (check_is_mpp_frame(frame0) || check_is_mpp_frame(frame0)) {
		mpp_err_f("invalid NULL pointer input\n");
		return MPP_ERR_NULL_PTR;
	}

	f0 = (MppFrameImpl *) frame0;
	f1 = (MppFrameImpl *) frame1;

	if ((f0->width == f1->width) &&
	    (f0->height == f1->height) &&
	    (f0->hor_stride == f1->hor_stride) &&
	    (f0->ver_stride == f1->ver_stride) &&
	    (f0->fmt == f1->fmt) && (f0->buf_size == f1->buf_size))
		return MPP_OK;

	return MPP_NOK;
}

RK_U32 mpp_frame_get_fbc_offset(MppFrame frame)
{
	MppFrameImpl *p = NULL;

	if (check_is_mpp_frame(frame))
		return 0;

	p = (MppFrameImpl *) frame;

	if (MPP_FRAME_FMT_IS_FBC(p->fmt)) {
		RK_U32 fbc_version = p->fmt & MPP_FRAME_FBC_MASK;
		RK_U32 fbc_offset = 0;

		if (fbc_version == MPP_FRAME_FBC_AFBC_V1) {
			fbc_offset = MPP_ALIGN(MPP_ALIGN(p->width, 16) *
					       MPP_ALIGN(p->height, 16) / 16,
					       SZ_4K);
		} else if (fbc_version == MPP_FRAME_FBC_AFBC_V2)
			fbc_offset = 0;
		p->fbc_offset = fbc_offset;
	}

	return p->fbc_offset;
}

RK_U32 mpp_frame_get_fbc_stride(MppFrame frame)
{
	MppFrameImpl *p = NULL;

	if (check_is_mpp_frame(frame))
		return 0;

	p = (MppFrameImpl *) frame;

	return MPP_ALIGN(p->width, 16);
}

/*
 * object access function macro
 */
#define MPP_FRAME_ACCESSORS(type, field)		\
    type mpp_frame_get_##field(const MppFrame s)	\
    {							\
        check_is_mpp_frame((MppFrameImpl*)s);		\
							\
        return ((MppFrameImpl*)s)->field;		\
    }							\
    void mpp_frame_set_##field(MppFrame s, type v)	\
    {							\
        check_is_mpp_frame((MppFrameImpl*)s);		\
							\
        ((MppFrameImpl*)s)->field = v;			\
    }

MPP_FRAME_ACCESSORS(RK_U32, width)
MPP_FRAME_ACCESSORS(RK_U32, height)
MPP_FRAME_ACCESSORS(RK_U32, hor_stride_pixel)
MPP_FRAME_ACCESSORS(RK_U32, hor_stride)
MPP_FRAME_ACCESSORS(RK_U32, ver_stride)
MPP_FRAME_ACCESSORS(RK_U32, offset_x)
MPP_FRAME_ACCESSORS(RK_U32, offset_y)
MPP_FRAME_ACCESSORS(RK_U32, mode)
MPP_FRAME_ACCESSORS(RK_U32, discard)
MPP_FRAME_ACCESSORS(RK_U32, viewid)
MPP_FRAME_ACCESSORS(RK_U32, poc)
MPP_FRAME_ACCESSORS(RK_S64, pts)
MPP_FRAME_ACCESSORS(RK_S64, dts)
MPP_FRAME_ACCESSORS(RK_U32, eos)
MPP_FRAME_ACCESSORS(RK_U32, info_change)
MPP_FRAME_ACCESSORS(MppFrameColorRange, color_range)
MPP_FRAME_ACCESSORS(MppFrameColorPrimaries, color_primaries)
MPP_FRAME_ACCESSORS(MppFrameColorTransferCharacteristic, color_trc)
MPP_FRAME_ACCESSORS(MppFrameColorSpace, colorspace)
MPP_FRAME_ACCESSORS(MppFrameChromaLocation, chroma_location)
MPP_FRAME_ACCESSORS(MppFrameFormat, fmt)
MPP_FRAME_ACCESSORS(MppFrameRational, sar)
MPP_FRAME_ACCESSORS(MppFrameMasteringDisplayMetadata, mastering_display)
MPP_FRAME_ACCESSORS(MppFrameContentLightMetadata, content_light)
MPP_FRAME_ACCESSORS(size_t, buf_size)
MPP_FRAME_ACCESSORS(RK_U32, errinfo)
MPP_FRAME_ACCESSORS(RK_U32, is_gray)
MPP_FRAME_ACCESSORS(RK_U32, is_full)
MPP_FRAME_ACCESSORS(RK_U32, phy_addr)
