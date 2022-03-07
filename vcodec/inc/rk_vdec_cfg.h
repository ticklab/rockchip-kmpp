// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __RK_VDEC_CFG_H__
#define __RK_VDEC_CFG_H__

#include "rk_type.h"
#include "mpp_err.h"

typedef void* MppDecCfg;

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET mpp_dec_cfg_init(MppDecCfg *cfg);
MPP_RET mpp_dec_cfg_deinit(MppDecCfg cfg);

MPP_RET mpp_dec_cfg_set_s32(MppDecCfg cfg, const char *name, RK_S32 val);
MPP_RET mpp_dec_cfg_set_u32(MppDecCfg cfg, const char *name, RK_U32 val);
MPP_RET mpp_dec_cfg_set_s64(MppDecCfg cfg, const char *name, RK_S64 val);
MPP_RET mpp_dec_cfg_set_u64(MppDecCfg cfg, const char *name, RK_U64 val);
MPP_RET mpp_dec_cfg_set_ptr(MppDecCfg cfg, const char *name, void *val);

MPP_RET mpp_dec_cfg_get_s32(MppDecCfg cfg, const char *name, RK_S32 *val);
MPP_RET mpp_dec_cfg_get_u32(MppDecCfg cfg, const char *name, RK_U32 *val);
MPP_RET mpp_dec_cfg_get_s64(MppDecCfg cfg, const char *name, RK_S64 *val);
MPP_RET mpp_dec_cfg_get_u64(MppDecCfg cfg, const char *name, RK_U64 *val);
MPP_RET mpp_dec_cfg_get_ptr(MppDecCfg cfg, const char *name, void **val);

void mpp_dec_cfg_show(void);

#ifdef __cplusplus
}
#endif

#endif /*__RK_VDEC_CFG_H__*/
