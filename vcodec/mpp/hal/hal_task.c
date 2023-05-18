// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "hal_task"

#include <linux/string.h>

#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_list.h"

#include "hal_task.h"

typedef struct HalTaskImpl_t HalTaskImpl;
typedef struct HalTaskGroupImpl_t HalTaskGroupImpl;

struct HalTaskImpl_t {
	struct list_head list;
	HalTaskGroupImpl *group;
	RK_S32 index;
	HalTaskStatus status;
	HalTaskInfo task;
};

struct HalTaskGroupImpl_t {
	MppCtxType type;
	RK_S32 task_count;

	Mutex *lock;

	HalTaskImpl *tasks;
	struct list_head list[MPP_TASK_BUTT];
	RK_U32 count[MPP_TASK_BUTT];
};

MPP_RET hal_task_group_init(HalTaskGroup * group, RK_S32 count)
{
	HalTaskGroupImpl *p = NULL;
	HalTaskImpl *tasks = NULL;
	Mutex *lock = NULL;

	if (NULL == group) {
		mpp_err_f("found invalid input group %p count %d\n", group,
			  count);
		return MPP_ERR_UNKNOW;
	}
	do {
		p = mpp_calloc(HalTaskGroupImpl, 1);
		if (NULL == p) {
			mpp_err_f("malloc group failed\n");
			break;
		}
		lock = new Mutex();
		if (NULL == lock) {
			mpp_err_f("new lock failed\n");
			break;;
		}
		tasks = mpp_calloc(HalTaskImpl, count);
		if (NULL == tasks) {
			mpp_err_f("malloc tasks list failed\n");
			break;;
		}

		p->task_count = count;
		p->lock = lock;
		p->tasks = tasks;

		for (RK_U32 i = 0; i < MPP_TASK_BUTT; i++)
			INIT_LIST_HEAD(&p->list[i]);

		for (RK_S32 i = 0; i < count; i++) {
			INIT_LIST_HEAD(&tasks[i].list);
			tasks[i].index = i;
			tasks[i].group = p;
			tasks[i].status = MPP_TASK_IDLE;
			list_add_tail(&tasks[i].list, &p->list[MPP_TASK_IDLE]);
			p->count[MPP_TASK_IDLE]++;
		}
		*group = p;
		return MPP_OK;
	} while (0);

	MPP_FREE(p);
	if (lock)
		delete lock;
	if (tasks)
		mpp_free(tasks);

	*group = NULL;

	return MPP_NOK;
}

MPP_RET hal_task_group_deinit(HalTaskGroup group)
{
	HalTaskGroupImpl *p = NULL;

	if (NULL == group) {
		mpp_err_f("found NULL input group\n");
		return MPP_ERR_NULL_PTR;
	}

	p = (HalTaskGroupImpl *) group;
	if (p->tasks)
		mpp_free(p->tasks);
	if (p->lock)
		delete p->lock;
	mpp_free(p);

	return MPP_OK;
}

MPP_RET hal_task_get_hnd(HalTaskGroup group, HalTaskStatus status,
			 HalTaskHnd * hnd)
{
	if (NULL == group || status >= MPP_TASK_BUTT || NULL == hnd) {
		mpp_err_f("found invaid input group %p status %d hnd %p\n",
			  group, status, hnd);
		return MPP_ERR_UNKNOW;
	}

	*hnd = NULL;
	HalTaskGroupImpl *p = (HalTaskGroupImpl *) group;
	AutoMutex auto_lock(p->lock);
	struct list_head *list = &p->list[status];
	if (list_empty(list))
		return MPP_NOK;

	HalTaskImpl *task = list_entry(list->next, HalTaskImpl, list);
	mpp_assert(task->status == status);
	*hnd = task;
	return MPP_OK;
}

MPP_RET hal_task_check_empty(HalTaskGroup group, HalTaskStatus status)
{
	if (NULL == group || status >= MPP_TASK_BUTT) {
		mpp_err_f("found invaid input group %p status %d \n", group,
			  status);
		return MPP_ERR_UNKNOW;
	}
	HalTaskGroupImpl *p = (HalTaskGroupImpl *) group;
	AutoMutex auto_lock(p->lock);
	struct list_head *list = &p->list[status];
	if (list_empty(list))
		return MPP_OK;
	return MPP_NOK;
}

MPP_RET hal_task_get_count(HalTaskGroup group, HalTaskStatus status,
			   RK_U32 * count)
{
	if (NULL == group || status >= MPP_TASK_BUTT || NULL == count) {
		mpp_err_f("found invaid input group %p status %d count %p\n",
			  group, status, count);
		return MPP_ERR_UNKNOW;
	}

	HalTaskGroupImpl *p = (HalTaskGroupImpl *) group;
	AutoMutex auto_lock(p->lock);
	*count = p->count[status];
	return MPP_OK;
}

MPP_RET hal_task_hnd_set_status(HalTaskHnd hnd, HalTaskStatus status)
{
	if (NULL == hnd || status >= MPP_TASK_BUTT) {
		mpp_err_f("found invaid input hnd %p status %d\n", hnd, status);
		return MPP_ERR_UNKNOW;
	}

	HalTaskImpl *impl = (HalTaskImpl *) hnd;
	HalTaskGroupImpl *group = impl->group;
	mpp_assert(group);
	mpp_assert(impl->index < group->task_count);

	AutoMutex auto_lock(group->lock);
	list_del_init(&impl->list);
	list_add_tail(&impl->list, &group->list[status]);
	group->count[impl->status]--;
	group->count[status]++;
	impl->status = status;
	return MPP_OK;
}

MPP_RET hal_task_hnd_set_info(HalTaskHnd hnd, HalTaskInfo * task)
{
	if (NULL == hnd || NULL == task) {
		mpp_err_f("found invaid input hnd %p task %p\n", hnd, task);
		return MPP_ERR_UNKNOW;
	}

	HalTaskImpl *impl = (HalTaskImpl *) hnd;
	HalTaskGroupImpl *group = impl->group;
	mpp_assert(impl->index < group->task_count);
	AutoMutex auto_lock(group->lock);
	memcpy(&impl->task, task, sizeof(impl->task));
	return MPP_OK;
}

MPP_RET hal_task_hnd_get_info(HalTaskHnd hnd, HalTaskInfo * task)
{
	if (NULL == hnd || NULL == task) {
		mpp_err_f("found invaid input hnd %p task %p\n", hnd, task);
		return MPP_ERR_UNKNOW;
	}

	HalTaskImpl *impl = (HalTaskImpl *) hnd;
	HalTaskGroupImpl *group = impl->group;
	mpp_assert(impl->index < group->task_count);
	AutoMutex auto_lock(group->lock);
	memcpy(task, &impl->task, sizeof(impl->task));
	return MPP_OK;
}

MPP_RET hal_task_info_init(HalTaskInfo * task, MppCtxType type)
{
	if (NULL == task || type >= MPP_CTX_BUTT) {
		mpp_err_f("found invalid input task %p type %d\n", task, type);
		return MPP_ERR_UNKNOW;
	}
	if (MPP_CTX_DEC == type) {
		HalDecTask *p = &task->dec;
		p->valid = 0;
		p->flags.val = 0;
		p->flags.eos = 0;
		p->prev_status = 0;
		p->input_packet = NULL;
		p->output = -1;
		p->input = -1;
		memset(&task->dec.syntax, 0, sizeof(task->dec.syntax));
		memset(task->dec.refer, -1, sizeof(task->dec.refer));
	} else
		memset(&task->enc, 0, sizeof(task->enc));
	return MPP_OK;
}
