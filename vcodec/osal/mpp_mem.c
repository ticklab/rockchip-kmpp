// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_mem"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include "rk_type.h"
#include "mpp_err.h"

#include "mpp_log.h"
#include "mpp_mem.h"

// mpp_mem_debug bit mask
#define MEM_DEBUG_EN            (0x00000001)
// NOTE: runtime log need debug enable
#define MEM_RUNTIME_LOG         (0x00000002)
#define MEM_NODE_LOG            (0x00000004)
#define MEM_EXT_ROOM            (0x00000010)
#define MEM_POISON              (0x00000020)

// default memory align size is set to 32
#define MEM_MAX_INDEX           (0x7fffffff)
#define MEM_ALIGN               32
#define MEM_ALIGN_MASK          (MEM_ALIGN - 1)
#define MEM_ALIGNED(x)          (((x) + MEM_ALIGN) & (~MEM_ALIGN_MASK))
#define MEM_HEAD_ROOM(debug)    ((debug & MEM_EXT_ROOM) ? (MEM_ALIGN) : (0))
#define MEM_NODE_MAX            (1024)
#define MEM_FREE_MAX            (512)
#define MEM_LOG_MAX             (1024)
#define MEM_CHECK_MARK          (0xdd)
#define MEM_HEAD_MASK           (0xab)
#define MEM_TAIL_MASK           (0xcd)

void *mpp_osal_malloc(const char *caller, size_t size)
{
	RK_U32 debug = 0;
	size_t size_align = MEM_ALIGNED(size);
	size_t size_real =
		(debug & MEM_EXT_ROOM) ? (size_align +
					  2 * MEM_ALIGN) : (size_align);

	return vmalloc(size_real);

}

void *mpp_osal_calloc(const char *caller, size_t size)
{
	void *ptr = mpp_osal_malloc(caller, size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void *mpp_osal_realloc(const char *caller, void *ptr, size_t size)
{
	//RK_U32 debug = 0;
	void *ret = NULL;
	/*  size_t size_align = MEM_ALIGNED(size);
	   size_t size_real = (debug & MEM_EXT_ROOM) ? (size_align + 2 * MEM_ALIGN) :
	   (size_align);
	   void *ptr_real = (RK_U8 *)ptr - MEM_HEAD_ROOM(debug); */

	if (NULL == ptr)
		return mpp_osal_malloc(caller, size);

	if (0 == size) {
		mpp_err("warning: realloc %p to zero size\n", ptr);
		return NULL;
	}

	return ret;
}

void mpp_osal_free(const char *caller, void *ptr)
{
	RK_U32 debug = 0;
	if (NULL == ptr)
		return;

	if (!debug) {
		vfree(ptr);
		return;
	}
}
