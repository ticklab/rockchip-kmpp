// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_CFG_H__
#define __MPP_CFG_H__

#include "rk_type.h"
#include "mpp_err.h"

typedef enum CfgType_e {
	CFG_FUNC_TYPE_S32,
	CFG_FUNC_TYPE_U32,
	CFG_FUNC_TYPE_S64,
	CFG_FUNC_TYPE_U64,
	CFG_FUNC_TYPE_Ptr,
	CFG_FUNC_TYPE_St,
	CFG_FUNC_TYPE_BUTT,
} CfgType;

typedef struct CfgDataInfo_s {
	CfgType type: 4;
	RK_U32 size: 12;
	RK_U32 offset: 16;
} CfgDataInfo;

typedef struct MppCfgApi_t {
	const char *name;
	CfgDataInfo info;
	CfgDataInfo update;
	void *api_set;
	void *api_get;
} MppCfgApi;

typedef MPP_RET(*CfgSetS32) (void *cfg, MppCfgApi * api, RK_S32 val);
typedef MPP_RET(*CfgGetS32) (void *cfg, MppCfgApi * api, RK_S32 * val);
typedef MPP_RET(*CfgSetU32) (void *cfg, MppCfgApi * api, RK_U32 val);
typedef MPP_RET(*CfgGetU32) (void *cfg, MppCfgApi * api, RK_U32 * val);
typedef MPP_RET(*CfgSetS64) (void *cfg, MppCfgApi * api, RK_S64 val);
typedef MPP_RET(*CfgGetS64) (void *cfg, MppCfgApi * api, RK_S64 * val);
typedef MPP_RET(*CfgSetU64) (void *cfg, MppCfgApi * api, RK_U64 val);
typedef MPP_RET(*CfgGetU64) (void *cfg, MppCfgApi * api, RK_U64 * val);
typedef MPP_RET(*CfgSetPtr) (void *cfg, MppCfgApi * api, void *val);
typedef MPP_RET(*CfgGetPtr) (void *cfg, MppCfgApi * api, void **val);
typedef MPP_RET(*CfgSetSt) (void *cfg, MppCfgApi * api, void *val);
typedef MPP_RET(*CfgGetSt) (void *cfg, MppCfgApi * api, void *val);

#ifdef  __cplusplus
extern "C" {
#endif

extern const char *cfg_type_names[];
MPP_RET check_cfg_api_info(MppCfgApi * api, CfgType type);

#ifdef  __cplusplus
}
#endif
#endif /*__MPP_CFG_H__*/
