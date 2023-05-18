// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 *
 */

#define pr_fmt(fmt) "vcodec_thread: " fmt

#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/module.h>
#include <linux/sched/prio.h>
#include <uapi/linux/sched/types.h>

#include "mpp_vcodec_thread.h"

#define THREAD_DBG_FLOW     (0x00000001)

#define thread_dbg(flag, fmt, args...) \
    do { \
        if (unlikely(thread_debug & flag)) \
            pr_info(fmt, ##args); \
    } while (0)

#define thread_dbg_flow(fmt, args...)   thread_dbg(THREAD_DBG_FLOW, fmt, ##args)

unsigned int thread_debug;
module_param(thread_debug, uint, 0644);
MODULE_PARM_DESC(thread_debug,
		 "bit switch for vcodec thread debug information");

static const char *const str_thd_state[] = {
	"invalid",
	"ready",
	"running",
	"paused",
};

static const char *state_to_str(enum vcodec_threads_status status)
{
	return (status < VCODEC_THREADS_BUTT) ? str_thd_state[status] : NULL;
}

static int check_vcodec_threads(struct vcodec_threads *thds, const char *fmt)
{
	if (!thds) {
		pr_err("NULL invalid vcodec threads on %s\n", fmt);
		return -EINVAL;
	}

	if (thds->check != thds || !thds->module) {
		pr_err("%p invalid vcodec threads check %p module %p at %s\n",
		       thds, thds->check, thds->module, fmt);
		return -EINVAL;
	}

	return 0;
}

static void vcodec_thread_worker(struct kthread_work *work_s)
{
	struct vcodec_thread *thd =
		container_of(work_s, struct vcodec_thread, work);
	struct vcodec_threads *thds = thd->thds;
	u64 curr_us = ktime_get_raw_ns();

	thread_dbg_flow("enter worker\n");

	do {
		thd->sum_gap += curr_us - thd->last_us;
		thd->last_us = curr_us;

		if (thds->cfg.callback) {
			set_bit(thd->thd_id, &thds->run_state);
			thd->ret = thds->cfg.callback(thds->cfg.param);
			clear_bit(thd->thd_id, &thds->run_state);
		} else
			thd->ret = 0;

		curr_us = ktime_get_raw_ns();
		thd->sum_run += curr_us - thd->last_us;
		thd->last_us = curr_us;

		thd->run_cnt++;
	} while (thd->ret > 0);

	thread_dbg_flow("leave worker\n");
}

static int vcodec_thread_prepare(struct vcodec_threads *thds)
{
	int count = thds->cfg.count;
	vcodec_work_func_t callback = thds->cfg.callback;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 10 };
	int i;

	if (!count || count > VCODEC_MAX_WORK_THREAD || !callback) {
		pr_err("invalid count %d callback %p", count, callback);
		return -EINVAL;
	}

	thread_dbg_flow("enter prepare\n");
	WARN_ON(thds->worker);

	thds->worker = kcalloc(count, sizeof(*thds->worker), GFP_KERNEL);
	if (!thds->worker)
		return -ENOMEM;

	thread_dbg_flow("prepare count %d\n", count);
	for (i = 0; i < count; i++) {
		struct vcodec_thread *thd = &thds->worker[i];

		thd->thds = thds;
		thd->thd_id = i;
		thd->last_us = ktime_get_raw_ns();

		kthread_init_worker(&thd->worker);
		thd->kworker_task = kthread_run(kthread_worker_fn, &thd->worker,
						"vcodec_thread_%d", i);
		kthread_init_work(&thd->work, vcodec_thread_worker);
		sched_setscheduler(thd->kworker_task, SCHED_FIFO, &param);
		thread_dbg_flow("prepare thread %d done\n", i);
	}
	thread_dbg_flow("leave prepare\n");

	return 0;
}

static void vcodec_thread_reset(struct vcodec_threads *thds)
{
	thread_dbg_flow("enter reset\n");
	if (thds->worker) {
		if (thds->cfg.count) {
			int i;

			for (i = 0; i < thds->cfg.count; i++) {
				struct vcodec_thread *thd = &thds->worker[i];

				thread_dbg_flow("stop thread %d\n", i);
				if (thd->kworker_task) {
					kthread_flush_worker(&thd->worker);
					kthread_stop(thd->kworker_task);
					thd->kworker_task = NULL;
				}
			}
		}
		kfree(thds->worker);
	}
	thread_dbg_flow("leave reset\n");
}

struct vcodec_threads *vcodec_thread_create(struct vcodec_module *module)
{
	struct vcodec_threads *thds = kzalloc(sizeof(*thds), GFP_KERNEL);

	if (thds) {
		/* setup init single thread pool without callback */
		thds->set.count = 1;
		thds->change = VCODEC_THREADS_CHANGE_COUNT | VCODEC_THREADS_CHANGE_CALLBACK;
		thds->check = thds;
		thds->module = module;
		thds->status = VCODEC_THREADS_INVALID;
		spin_lock_init(&thds->lock);
	}

	return thds;
}

int vcodec_thread_destroy(struct vcodec_threads *thds)
{
	unsigned long lock_flags = 0;

	if (check_vcodec_threads(thds, "destroy"))
		return -EINVAL;


	spin_lock_irqsave(&thds->lock, lock_flags);

	thds->status = VCODEC_THREADS_INVALID;

	vcodec_thread_reset(thds);

	thds->check = NULL;
	thds->module = NULL;

	spin_unlock_irqrestore(&thds->lock, lock_flags);

	kfree(thds);

	return 0;
}

int vcodec_thread_set_count(struct vcodec_threads *thds, int count)
{
	unsigned long lock_flags = 0;

	if (check_vcodec_threads(thds, "set count"))
		return -EINVAL;

	thread_dbg_flow("enter set count %d\n", count);

	spin_lock_irqsave(&thds->lock, lock_flags);
	if (thds->set.count != count) {
		thds->set.count = count;
		thds->change |= VCODEC_THREADS_CHANGE_COUNT;
	}

	spin_unlock_irqrestore(&thds->lock, lock_flags);
	thread_dbg_flow("leave set count\n");

	return 0;
}

int vcodec_thread_set_callback(struct vcodec_threads *thds,
			       vcodec_work_func_t callback, void *param)
{
	unsigned long lock_flags = 0;

	if (check_vcodec_threads(thds, "set callback"))
		return -EINVAL;

	thread_dbg_flow("enter set callback\n");

	spin_lock_irqsave(&thds->lock, lock_flags);
	if (thds->set.callback != callback || thds->set.param != param) {
		thds->set.callback = callback;
		thds->set.param = param;
		thds->change |= VCODEC_THREADS_CHANGE_CALLBACK;
	}

	spin_unlock_irqrestore(&thds->lock, lock_flags);
	thread_dbg_flow("leave set callback\n");

	return 0;
}

int vcodec_thread_start(struct vcodec_threads *thds)
{
	int ret = 0;

	if (check_vcodec_threads(thds, "start"))
		return -EINVAL;

	thread_dbg_flow("enter start with change %x\n", thds->change);

	if (thds->status != VCODEC_THREADS_INVALID
	    && thds->status != VCODEC_THREADS_READY) {
		pr_err("%p can not start at status %s", thds,
		       state_to_str(thds->status));

		return -EINVAL;
	}

	if (thds->change) {
		int change = thds->change;

		vcodec_thread_reset(thds);

		if (change & VCODEC_THREADS_CHANGE_COUNT)
			thds->cfg.count = thds->set.count;

		if (change & VCODEC_THREADS_CHANGE_CALLBACK) {
			thds->cfg.callback = thds->set.callback;
			thds->cfg.param = thds->set.param;
		}
		thds->change = 0;

		ret = vcodec_thread_prepare(thds);
	}

	thds->status = VCODEC_THREADS_RUNNING;

	thread_dbg_flow("leave start\n");

	return ret;
}

int vcodec_thread_stop(struct vcodec_threads *thds)
{
	unsigned long lock_flags = 0;

	if (check_vcodec_threads(thds, "stop"))
		return -EINVAL;

	thread_dbg_flow("enter stop\n");

	if (thds->status != VCODEC_THREADS_RUNNING
	    && thds->status != VCODEC_THREADS_PAUSED) {
		pr_err("%p can not stop at status %s", thds,
		       state_to_str(thds->status));
		return -EINVAL;
	}

	if (thds->cfg.count && thds->worker) {
		int i;

		for (i = 0; i < thds->cfg.count; i++) {
			struct vcodec_thread *thd = &thds->worker[i];

			kthread_flush_worker(&thd->worker);
		}
	}

	spin_lock_irqsave(&thds->lock, lock_flags);
	thds->status = VCODEC_THREADS_READY;
	spin_unlock_irqrestore(&thds->lock, lock_flags);
	thread_dbg_flow("leave stop\n");

	return 0;
}

int vcodec_thread_pause(struct vcodec_threads *thds)
{
	unsigned long lock_flags = 0;

	if (check_vcodec_threads(thds, "pause"))
		return -EINVAL;

	spin_lock_irqsave(&thds->lock, lock_flags);
	if (thds->status != VCODEC_THREADS_RUNNING) {
		pr_err("%p can not pause at status %s", thds,
		       state_to_str(thds->status));

		spin_unlock_irqrestore(&thds->lock, lock_flags);
		return -EINVAL;
	}

	thds->status = VCODEC_THREADS_PAUSED;

	spin_unlock_irqrestore(&thds->lock, lock_flags);
	return 0;
}

int vcodec_thread_resume(struct vcodec_threads *thds)
{
	unsigned long lock_flags = 0;

	if (check_vcodec_threads(thds, "resume"))
		return -EINVAL;


	spin_lock_irqsave(&thds->lock, lock_flags);
	if (thds->status != VCODEC_THREADS_PAUSED) {
		pr_err("%p can not resume at status %s", thds,
		       state_to_str(thds->status));

		spin_unlock_irqrestore(&thds->lock, lock_flags);
		return -EINVAL;
	}

	thds->status = VCODEC_THREADS_RUNNING;

	spin_unlock_irqrestore(&thds->lock, lock_flags);

	return 0;
}

int vcodec_thread_trigger(struct vcodec_threads *thds)
{
	unsigned long lock_flags = 0;

	if (check_vcodec_threads(thds, "stop"))
		return -EINVAL;

	spin_lock_irqsave(&thds->lock, lock_flags);
	thread_dbg_flow("enter trigger %llu\n", thds->queue_cnt);

	if (thds->status != VCODEC_THREADS_RUNNING) {
		pr_err("%p can not trigger at status %s", thds,
		       state_to_str(thds->status));
		spin_unlock_irqrestore(&thds->lock, lock_flags);
		return -EINVAL;
	}

	if (thds->cfg.count && thds->worker) {
		bool cnt = 0;
		int i;

		if (thds->cfg.count > 1) {
			for (i = 0; i < thds->cfg.count; i++) {
				if (!test_bit(i, &thds->run_state)) {
					struct vcodec_thread *thd = &thds->worker[i];

					cnt = kthread_queue_work(&thd->worker, &thd->work);
					thread_dbg_flow ("queue work %d ret %d\n", i, cnt);
					thd->queue_cnt += cnt;
					break;
				}
			}
		} else {
			struct vcodec_thread *thd = &thds->worker[0];

			cnt = kthread_queue_work(&thd->worker, &thd->work);
			thread_dbg_flow("queue work %d ret %d\n", 0, cnt);
			thd->queue_cnt += cnt;
		}

		if (cnt) {
			thds->queue_cnt++;
			thds->miss_cnt = 0;
		} else
			thds->miss_cnt++;
	}

	thread_dbg_flow("leave trigger %llu:%llu\n", thds->queue_cnt,
			thds->miss_cnt);

	spin_unlock_irqrestore(&thds->lock, lock_flags);

	return 0;
}
