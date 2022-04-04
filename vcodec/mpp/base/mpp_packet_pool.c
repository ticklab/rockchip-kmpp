// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_packet_pool"

#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "mpp_log.h"
#include "mpp_err.h"
#include "mpp_mem.h"
#include "mpp_packet_impl.h"
#include "mpp_packet_pool.h"

struct MppPacketPool {
	RK_U32 max_pool_size;
	struct mutex used_lock;
	struct mutex unused_lock;
	struct list_head used_list;
	struct list_head unused_list;
	atomic_t cur_pool_size;
	atomic_t used_cnt;
	atomic_t unused_cnt;
};

struct MppPacketPoolImpl {
	struct list_head pool_list;
	MppPacketImpl impl;
};

//static const char *module_name = MODULE_TAG;
static struct MppPacketPool mpp_packet_pool;

MPP_RET mpp_packet_pool_init(RK_U32 max_cnt)
{
	struct MppPacketPool *ctx = &mpp_packet_pool;
	memset(ctx, 0, sizeof(struct MppPacketPool));

	ctx->max_pool_size = max_cnt;
	atomic_set(&ctx->unused_cnt, 0);
	atomic_set(&ctx->used_cnt, 0);
	atomic_set(&ctx->cur_pool_size, 0);

	INIT_LIST_HEAD(&ctx->used_list);
	INIT_LIST_HEAD(&ctx->unused_list);
	mutex_init(&ctx->used_lock);
	mutex_init(&ctx->unused_lock);
	return MPP_OK;
}

MppPacketImpl *mpp_packet_mem_alloc(void)
{
	struct MppPacketPool *ctx = &mpp_packet_pool;
	struct MppPacketPoolImpl *p = NULL;

	if (atomic_read(&ctx->unused_cnt) > 0) {
		mutex_lock(&ctx->unused_lock);
		p = list_first_entry_or_null(&ctx->unused_list,
					     struct MppPacketPoolImpl,
					     pool_list);

		list_del_init(&p->pool_list);
		atomic_dec(&ctx->unused_cnt);
		mutex_lock(&ctx->used_lock);
		list_move_tail(&p->pool_list, &ctx->used_list);
		atomic_inc(&ctx->used_cnt);
		mutex_unlock(&ctx->used_lock);
		mutex_unlock(&ctx->unused_lock);
		memset(&p->impl, 0, sizeof(MppPacketImpl));

	} else {
		mutex_lock(&ctx->used_lock);
		if (atomic_read(&ctx->cur_pool_size) > ctx->max_pool_size) {
			mutex_unlock(&ctx->used_lock);
			return NULL;
		}
		p = (struct MppPacketPoolImpl *)mpp_calloc(struct
							   MppPacketPoolImpl,
							   1);
		if (NULL == p) {
			mpp_err_f("malloc failed\n");
			mutex_unlock(&ctx->used_lock);
			return NULL;
		}
		INIT_LIST_HEAD(&p->pool_list);
		list_add_tail(&p->pool_list, &ctx->used_list);
		atomic_inc(&ctx->used_cnt);
		mutex_unlock(&ctx->used_lock);
	}

	return &p->impl;
}

MPP_RET mpp_packet_mem_free(MppPacketImpl * p)
{
	struct MppPacketPool *ctx = &mpp_packet_pool;
	struct MppPacketPoolImpl *packet = NULL, *n;

	mutex_lock(&ctx->used_lock);
	list_for_each_entry_safe(packet, n, &ctx->used_list, pool_list) {
		if (&packet->impl == p) {
			list_del_init(&packet->pool_list);
			atomic_dec(&ctx->used_cnt);
			mutex_lock(&ctx->unused_lock);
			list_move_tail(&packet->pool_list, &ctx->unused_list);
			mutex_unlock(&ctx->unused_lock);
			atomic_inc(&ctx->unused_cnt);
			break;
		}
	}
	mutex_unlock(&ctx->used_lock);

	return MPP_OK;
}

MPP_RET mpp_packet_pool_deinit(void)
{
	struct MppPacketPool *ctx = &mpp_packet_pool;
	struct MppPacketPoolImpl *packet = NULL, *n;

	mutex_lock(&ctx->unused_lock);
	list_for_each_entry_safe(packet, n, &ctx->unused_list, pool_list) {
		list_del_init(&packet->pool_list);
		MPP_FREE(packet);
		atomic_dec(&ctx->unused_cnt);
		atomic_dec(&ctx->cur_pool_size);
	}
	mutex_unlock(&ctx->unused_lock);

	mutex_lock(&ctx->used_lock);
	list_for_each_entry_safe(packet, n, &ctx->used_list, pool_list) {
		list_del_init(&packet->pool_list);
		atomic_dec(&ctx->used_cnt);
		atomic_dec(&ctx->cur_pool_size);
		MPP_FREE(packet);
	}
	mutex_unlock(&ctx->used_lock);
	return MPP_OK;
}
