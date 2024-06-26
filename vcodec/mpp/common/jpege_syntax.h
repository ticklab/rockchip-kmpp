// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __JPEGE_SYNTAX_H__
#define __JPEGE_SYNTAX_H__

#include "mpp_frame.h"
#include "rk_venc_cmd.h"

#define QUANTIZE_TABLE_SIZE 64
/*
 *  from RFC435 spec.
 */
extern const RK_U8 jpege_luma_quantizer[QUANTIZE_TABLE_SIZE];
extern const RK_U8 jpege_chroma_quantizer[QUANTIZE_TABLE_SIZE];

typedef struct JpegeSyntax_t {
	RK_U32              width;
	RK_U32              height;
	RK_U32              hor_stride;
	RK_U32              ver_stride;
	RK_U32              mcu_w;
	RK_U32              mcu_h;
	MppFrameFormat      format;
	MppFrameColorSpace  color;
	MppEncRotationCfg   rotation;
	RK_S32              mirroring;
	RK_U32              offset_x;
	RK_U32              offset_y;

	/* For quantization table */
	RK_U32              quality;
	RK_S32              q_factor;
	RK_S32              qf_min;
	RK_S32              qf_max;
	RK_U8               *qtable_y;
	RK_U8               *qtable_c;

	/*
	 * For color conversion
	 *
	 * 0 = bt601
	 * 1 = bt709
	 * 2 = user defined
	 */
	RK_U32              color_conversion_type;
	RK_U32              coeffA;
	RK_U32              coeffB;
	RK_U32              coeffC;
	RK_U32              coeffE;
	RK_U32              coeffF;

	/* For slice encoding mode */
	RK_U32              slice_enable;
	RK_U32              slice_size_mb_rows;
	RK_U32              restart_interval;

	/*
	 * For unit type and density
	 *
	 * units_type   0 - no unit
	 *              1 - dots per inch
	 *              2 - dots per cm
	 *
	 * X/Y density  specify the pixel aspect ratio
	 */
	RK_U32              units_type;
	RK_U32              density_x;
	RK_U32              density_y;

	/* For comment header */
	RK_U32              comment_length;
	RK_U8               *comment_data;

	/* For jpeg low delay slice encoding */
	RK_U32              low_delay;
	RK_U32              part_rows;
	RK_U32              restart_ri;
} JpegeSyntax;

typedef struct JpegeFeedback_t {
	RK_U32 hw_status;       /* zero -> correct; non-zero -> error */
	RK_U32 stream_length;
} JpegeFeedback;

#endif

