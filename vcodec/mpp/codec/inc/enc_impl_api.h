// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __ENC_IMPL_API_H__
#define __ENC_IMPL_API_H__

#include "rk_mpi_cmd.h"

#include "hal_task.h"
#include "mpp_enc_cfg.h"
#include "mpp_enc_refs.h"
#include "mpp_dev_defs.h"

/*
 * the reset wait for extension
 */
typedef struct EncImplCfg_t {
	// input
	MppCodingType coding;
	MppClientType type;
	MppEncCfgSet *cfg;
	MppEncRefs refs;

	// output
	RK_S32 task_count;
} EncImplCfg;

/*
 * EncImplApi is the data structure provided from different encoders
 *
 * They will be static register to mpp_enc for scaning
 * name         - encoder name
 * coding       - encoder coding type
 * ctx_size     - encoder context size, mpp_dec will use this to malloc memory
 * flag         - reserve
 *
 * init         - encoder initialization function
 * deinit       - encoder de-initialization function
 * proc_cfg     - encoder processs control function
 * gen_hdr      - encoder generate hearder function
 * proc_dpb     - encoder dpb process function (approach one frame)
 * proc_hal     - encoder prepare hal info function
 * add_prefix   - encoder generate user data / sei to packet as prefix
 */
typedef struct EncImplApi_t {
	char *name;
	MppCodingType coding;
	RK_U32 ctx_size;
	RK_U32 flag;

	MPP_RET(*init) (void *ctx, EncImplCfg * ctrlCfg);
	MPP_RET(*deinit) (void *ctx);

	MPP_RET(*proc_cfg) (void *ctx, MpiCmd cmd, void *param);
	MPP_RET(*gen_hdr) (void *ctx, MppPacket pkt);

	MPP_RET(*start) (void *ctx, HalEncTask * task);
	MPP_RET(*proc_dpb) (void *ctx, HalEncTask * task);
	MPP_RET(*proc_hal) (void *ctx, HalEncTask * task);

	MPP_RET(*add_prefix) (MppPacket pkt, RK_S32 * length, RK_U8 uuid[16],
			      const void *data, RK_S32 size);

	MPP_RET(*sw_enc) (void *ctx, HalEncTask * task);

	void(*proc_debug)(void *seq_file, void *ctx, RK_S32 chl_id);
} EncImplApi;

#endif /*__ENC_IMPL_API_H__*/
