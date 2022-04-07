// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __MPP_MEM_H__
#define __MPP_MEM_H__

#include "rk_type.h"
#include "mpp_err.h"

#define mpp_malloc_with_caller(type, count, caller)  \
    (type*)mpp_osal_malloc(caller, sizeof(type) * (count))

#define mpp_malloc(type, count)  \
    (type*)mpp_osal_malloc(__FUNCTION__, sizeof(type) * (count))

#define mpp_malloc_size(type, size)  \
    (type*)mpp_osal_malloc(__FUNCTION__, size)

#define mpp_calloc_size(type, size)  \
    (type*)mpp_osal_calloc(__FUNCTION__, size)

#define mpp_calloc(type, count)  \
    (type*)mpp_osal_calloc(__FUNCTION__, sizeof(type) * (count))

#define mpp_realloc(ptr, type, count) \
    (type*)mpp_osal_realloc(__FUNCTION__, ptr, sizeof(type) * (count))

#define mpp_free(ptr) \
    mpp_osal_free(__FUNCTION__, ptr)

#define MPP_FREE(ptr)   do { if(ptr) mpp_free(ptr); ptr = NULL; } while (0)
#define MPP_FCLOSE(fp)  do { if(fp)  fclose(fp);     fp = NULL; } while (0)

#ifdef __cplusplus
extern "C" {
#endif

void *mpp_osal_malloc(const char *caller, size_t size);
void *mpp_osal_calloc(const char *caller, size_t size);
void *mpp_osal_realloc(const char *caller, void *ptr, size_t size);
void mpp_osal_free(const char *caller, void *ptr);

#ifdef __cplusplus
}
#endif
#endif /*__MPP_MEM_H__*/
