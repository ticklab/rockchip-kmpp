// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_MEM_POOL_H__
#define __MPP_MEM_POOL_H__


typedef void* MppMemPool;

#ifdef __cplusplus
extern "C" {
#endif

#define mpp_mem_pool_init(name, size, max_cnt)  mpp_mem_get_pool_f(__FUNCTION__, name, size, max_cnt)
#define mpp_mem_pool_deinit(pool)               mpp_mem_put_pool_f(__FUNCTION__, pool)

#define mpp_mem_pool_get(pool)                  mpp_mem_pool_get_f(__FUNCTION__, pool)
#define mpp_mem_pool_put(pool, p)               mpp_mem_pool_put_f(__FUNCTION__, pool, p)

MppMemPool mpp_mem_get_pool_f(const char *caller, const char *name, size_t size, RK_U32 max_cnt);
void mpp_mem_put_pool_f(const char *caller, MppMemPool pool);
void *mpp_mem_pool_get_f(const char *caller, MppMemPool pool);
void mpp_mem_pool_put_f(const char *caller, MppMemPool pool, void *p);
void mpp_mem_pool_info_show(void *seq_file, MppMemPool pool);

#ifdef __cplusplus
}
#endif
#endif /*__MPP_FRAME_IMPL_H__*/
