// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __MPP_ENC_CFG_IMPL_H__
#define __MPP_ENC_CFG_IMPL_H__

#include "mpp_trie.h"
#include "mpp_enc_cfg.h"

typedef struct MppEncCfgImpl_t {
	MppEncCfgSet cfg;
	RK_S32 size;
	MppTrie api;
} MppEncCfgImpl;

#endif /*__MPP_ENC_CFG_IMPL_H__*/
