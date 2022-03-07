// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#ifndef __H264E_SEI_H__
#define __H264E_SEI_H__

#include "mpp_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET h264e_sei_to_packet(MppPacket packet, RK_S32 *len, RK_S32 type,
                            RK_U8 uuid[16], const void *data, RK_S32 size);

#ifdef __cplusplus
}
#endif

#endif /* __H264E_SEI_H__ */
