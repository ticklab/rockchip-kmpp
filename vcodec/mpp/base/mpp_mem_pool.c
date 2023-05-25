// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "mpp_mem_pool"

#include <linux/spinlock.h>
#include <linux/seq_file.h>

#include "mpp_log.h"
#include "mpp_err.h"
#include "mpp_mem.h"
#include "mpp_mem_pool.h"

typedef struct MppMemPoolNode_t {
	void                *check;
	struct list_head    list;
	void                *ptr;
	size_t              size;
} MppMemPoolNode;

typedef struct MppMemPoolImpl_t {
	void                *check;
	const char    	    *name;
	size_t              size;
	spinlock_t	    lock;
	struct list_head    used;
	struct list_head    unused;
	RK_S32              used_count;
	RK_S32              unused_count;
	RK_U32		    max_cnt;
} MppMemPoolImpl;

#define mem_pool_dbg_flow(fmt, ...) mpp_dbg(0, fmt, ## __VA_ARGS__)

MppMemPool mpp_mem_get_pool_f(const char *caller, const char *name, size_t size, RK_U32 max_cnt)
{
	MppMemPoolImpl *pool = mpp_malloc(MppMemPoolImpl, 1);

	if (NULL == pool)
		return NULL;

	spin_lock_init(&pool->lock);

	pool->check = pool;
	pool->size = size;
	pool->used_count = 0;
	pool->unused_count = 0;
	pool->name = name;
	pool->max_cnt = max_cnt;

	INIT_LIST_HEAD(&pool->used);
	INIT_LIST_HEAD(&pool->unused);
	mem_pool_dbg_flow("create %s pool %d used:unused [%d:%d] from %s\n",
			  name, pool->size, pool->used_count, pool->unused_count, caller);

	return pool;
}

void mpp_mem_put_pool_f(const char *caller, MppMemPool pool)
{
	MppMemPoolImpl *impl = (MppMemPoolImpl *)pool;
	MppMemPoolNode *node, *m;
	unsigned long flags;

	if (impl != impl->check) {
		mpp_err_f("invalid mem impl %p check %p\n", impl, impl->check);
		return;
	}

	spin_lock_irqsave(&impl->lock, flags);

	mem_pool_dbg_flow("pool %d get used:unused [%d:%d] from %s\n", impl->size,
			  impl->used_count, impl->unused_count, caller);
	if (!list_empty(&impl->unused)) {
		list_for_each_entry_safe(node, m, &impl->unused, list) {
			MPP_FREE(node);
			impl->unused_count--;
		}
	}

	if (!list_empty(&impl->used)) {
		mpp_err_f("found %d used buffer size %d\n",
			  impl->used_count, impl->size);

		list_for_each_entry_safe(node, m, &impl->used, list) {
			MPP_FREE(node);
			impl->used_count--;
		}
	}

	if (impl->used_count || impl->unused_count)
		mpp_err_f("pool size %d found leaked buffer used:unused [%d:%d]\n",
			  impl->size, impl->used_count, impl->unused_count);

	spin_unlock_irqrestore(&impl->lock, flags);

	mpp_free(impl);
}

void *mpp_mem_pool_get_f(const char *caller, MppMemPool pool)
{
	MppMemPoolImpl *impl = (MppMemPoolImpl *)pool;
	MppMemPoolNode *node = NULL;
	void* ptr = NULL;
	unsigned long flags;

	spin_lock_irqsave(&impl->lock, flags);

	if (!list_empty(&impl->unused)) {
		node = list_first_entry_or_null(&impl->unused, MppMemPoolNode, list);
		if (node) {
			list_del_init(&node->list);
			list_add_tail(&node->list, &impl->used);
			impl->unused_count--;
			impl->used_count++;
			ptr = node->ptr;
			node->check = node;
			goto DONE;
		}
	}

	if ((impl->unused_count + impl->used_count) >= impl->max_cnt) {
		mpp_log("pool %d reach max cnt %d\n", impl->size, impl->max_cnt);
		goto DONE;
	}
	spin_unlock_irqrestore(&impl->lock, flags);

	node = mpp_malloc_size(MppMemPoolNode, sizeof(MppMemPoolNode) + impl->size);
	if (NULL == node) {
		mpp_err_f("failed to create node from %s pool\n", impl->name);
		return ptr;
	}

	spin_lock_irqsave(&impl->lock, flags);
	node->check = node;
	node->ptr = (void *)(node + 1);
	node->size = impl->size;
	INIT_LIST_HEAD(&node->list);
	list_add_tail(&node->list, &impl->used);
	impl->used_count++;
	ptr = node->ptr;

DONE:
	mem_pool_dbg_flow("pool %d get used:unused [%d:%d] from %s\n", impl->size,
			  impl->used_count, impl->unused_count, caller);
	spin_unlock_irqrestore(&impl->lock, flags);
	if (node)
		memset(node->ptr, 0, node->size);

	return ptr;
}

void mpp_mem_pool_put_f(const char *caller, MppMemPool pool, void *p)
{
	MppMemPoolImpl *impl = (MppMemPoolImpl *)pool;
	MppMemPoolNode *node = (MppMemPoolNode *)((RK_U8 *)p - sizeof(MppMemPoolNode));
	unsigned long flags;

	if (impl != impl->check) {
		mpp_err_f("invalid mem pool %p check %p\n", impl, impl->check);
		return;
	}

	if (node != node->check) {
		mpp_err_f("invalid mem pool ptr %p node %p check %p\n",
			  p, node, node->check);
		return;
	}

	spin_lock_irqsave(&impl->lock, flags);

	list_del_init(&node->list);
	list_add(&node->list, &impl->unused);
	impl->used_count--;
	impl->unused_count++;
	node->check = NULL;

	mem_pool_dbg_flow("pool %d put used:unused [%d:%d] from %s\n", impl->size,
			  impl->used_count, impl->unused_count, caller);

	spin_unlock_irqrestore(&impl->lock, flags);
}

void mpp_mem_pool_info_show(void *seq_file, MppMemPool pool)
{
	MppMemPoolImpl *impl = (MppMemPoolImpl*)pool;
	struct seq_file *seq = (struct seq_file *)seq_file;

	seq_printf(seq,
		   "\n--------%s pool---------------------------------------------------------------------------\n",
		   impl->name);
	seq_printf(seq, "%12s|%12s|%12s|%12s\n", "unused_cnt", "used_cnt", "total_cnt", "max_cnt");
	seq_printf(seq, "%12u|%12u|%12u|%12u\n",
		   impl->unused_count, impl->used_count, impl->unused_count + impl->used_count, impl->max_cnt);
}
