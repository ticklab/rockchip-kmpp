// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __H265E_PS_H__
#define __H265E_PS_H__

#include "h265e_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

	MPP_RET h265e_set_sps(H265eCtx * ctx, h265_sps * sps, h265_vps * vps);
	MPP_RET h265e_set_pps(H265eCtx * ctx, h265_pps * pps, h265_sps * sps);
	MPP_RET h265e_set_vps(H265eCtx * ctx, h265_vps * vps);

#ifdef __cplusplus
}
#endif
#endif
