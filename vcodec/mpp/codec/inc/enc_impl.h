// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __ENC_IMPL_H__
#define __ENC_IMPL_H__

#include "enc_impl_api.h"

typedef void *EncImpl;

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET enc_impl_init(EncImpl * impl, EncImplCfg * cfg);
MPP_RET enc_impl_deinit(EncImpl impl);

MPP_RET enc_impl_proc_cfg(EncImpl impl, MpiCmd cmd, void *para);
MPP_RET enc_impl_gen_hdr(EncImpl impl, MppPacket pkt);

MPP_RET enc_impl_start(EncImpl impl, HalEncTask * task);
MPP_RET enc_impl_proc_dpb(EncImpl impl, HalEncTask * task);
MPP_RET enc_impl_proc_hal(EncImpl impl, HalEncTask * task);

MPP_RET enc_impl_add_prefix(EncImpl impl, MppPacket pkt,
			    RK_S32 * length, RK_U8 uuid[16],
			    const void *data, RK_S32 size);

MPP_RET enc_impl_sw_enc(EncImpl impl, HalEncTask * task);

void enc_impl_proc_debug(void *seq_file, EncImpl impl,
			 RK_S32 chl_id);
#ifdef __cplusplus
}
#endif
#endif /*__MPP_ENC_IMPL_H__*/
