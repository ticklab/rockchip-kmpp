// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/of_platform.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "mpp_vcodec_chan.h"
#include "mpp_vcodec_base.h"
#include "mpp_vcodec_flow.h"
#include "mpp_vcodec_intf.h"

#include "mpp_log.h"
#include "mpp_enc.h"
#include "mpp_vcodec_thread.h"
#include "rk_venc_cfg.h"
#include "rk_export_func.h"
struct vcodec_msg {
	__u32 cmd;
	__u32 ctrl_cmd;
	__u32 size;
	__u64 data_ptr;
};
struct chanid_ctx {
	RK_S32 chan_id;
	MppCtxType type;
    void *param;
    RK_U32 size;
	atomic_t release_request;
};
static int vcodec_open(struct inode *inode, struct file *filp)
{
	struct chanid_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	filp->private_data = (void *)ctx;
	atomic_set(&ctx->release_request, 0);
	return 0;
}

static int vcodec_close(struct inode *inode, struct file *filp)
{
	struct chanid_ctx *ctx = filp->private_data;
	if (!ctx) {
		mpp_err("chan id ctx is null\n");
		return -EINVAL;
	}

	atomic_inc(&ctx->release_request);
	mpp_vcodec_chan_destory(ctx->chan_id, ctx->type);
    if (ctx->param){
        kfree(ctx->param);
        ctx->param = NULL;
        ctx->size = 0;
    }
	kfree(ctx);
	return 0;
}

static int vcodec_process_msg(struct vcodec_msg *msg,
			      struct vcodec_request *req)
{
	int ret = 0;
	req->cmd = msg->cmd;
	req->ctrl_cmd = msg->ctrl_cmd;
	req->size = msg->size;
	req->data = (void *)(unsigned long)msg->data_ptr;

	// mpp_log("cmd %x, ctrl_cmd %08x, size %d\n",
	//req->cmd, req->ctrl_cmd, req->size);
	return ret;
}

static int vcodec_process_cmd(void *private, struct vcodec_request *req)
{
	int ret = 0;
	struct chanid_ctx *ctx = private;
	int chan_id = ctx->chan_id;
	MppCtxType type = ctx->type;
	void *param = NULL;
	if (req->size > 0 && req->size > ctx->size) {
        if (ctx->param){
            kfree(ctx->param);
            ctx->param = NULL;
            ctx->size = 0;
        }
        param = kzalloc(req->size, GFP_KERNEL);
		if (!param)
			return -ENOMEM;
        ctx->param = param;
        ctx->size = req->size;
	}else{
        param = ctx->param;
    }
	switch (req->cmd) {
	case VCODEC_CHAN_CREATE:{
			struct vcodec_attr *attr = (struct vcodec_attr *)param;
			if (copy_from_user(param, req->data, req->size)) {
				ret = -EFAULT;
				goto fail;
			}
			if (req->size != sizeof(*attr)) {
				mpp_err("kernel vcodec_attr define is diff from user \n");
				ret = -EFAULT;
				goto fail;
			}
			ctx->type = attr->type;
			ret = mpp_vcodec_chan_create(attr);
			if (copy_to_user(req->data, param, req->size)) {
				mpp_err("copy_to_user failed.\n");
				return -EINVAL;
			}
			ctx->chan_id = attr->chan_id;
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_DESTROY:{
			ret = mpp_vcodec_chan_destory(chan_id, type);
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_RESET:{
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_START:{
			ret = mpp_vcodec_chan_start(chan_id, type);
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_STOP:{
			ret = mpp_vcodec_chan_stop(chan_id, type);
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_PAUSE:{
			ret = mpp_vcodec_chan_pause(chan_id, type);
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_RESUME:{
			ret = mpp_vcodec_chan_resume(chan_id, type);
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_CONTROL:{
			if (req->data) {
				if (copy_from_user(param, req->data, req->size)) {
					ret = -EFAULT;
					goto fail;
				}
			}
			ret =
			    mpp_vcodec_chan_control(chan_id, type,
						    req->ctrl_cmd, param);
			if (ret)
				goto fail;
			if (req->data) {
				if (copy_to_user(req->data, param, req->size)) {
					ret = -EFAULT;
					goto fail;
				}
			}
		}
		break;
	case VCODEC_CHAN_IN_FRM_RDY:{
			if (!req->data){
				ret = -EFAULT;
				goto fail;
			}
			if (copy_from_user(param, req->data, req->size)) {
				ret = -EFAULT;
				goto fail;
			}
			ret = mpp_vcodec_chan_push_frm(chan_id, param);
			if (ret)
				goto fail;
		}
		break;
	case VCODEC_CHAN_OUT_STRM_BUF_RDY:{
			if (!req->data){
				ret = -EFAULT;
				goto fail;
			}
			ret = mpp_vcodec_chan_get_stream(chan_id, type, param);
			if (copy_to_user(req->data, param, req->size)) {
				mpp_err("copy_to_user failed.\n");
				return -EINVAL;
			}
			if (ret)
				goto fail;
		}
		break;
    	case VCODEC_CHAN_OUT_STRM_END:{
			if (!req->data){
				ret = -EFAULT;
				goto fail;
			}
			if (copy_from_user(param, req->data, req->size)) {
				ret = -EFAULT;
				goto fail;
			}
			ret = mpp_vcodec_chan_put_stream(chan_id, type, param);
		}
		break;
	default:
		mpp_err("unknown vcode req cmd %x\n", req->cmd);
		ret = -EINVAL;
		goto fail;
	}
fail:
	return ret;
}

static long vcodec_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *msg;
	struct chanid_ctx *ctx = (struct chanid_ctx *)filp->private_data;
	struct vcodec_request req;
	msg = (void __user *)arg;
	if (!ctx) {
		mpp_err("ctx %p\n", ctx);
		return -EINVAL;
	}
	if (atomic_read(&ctx->release_request) > 0) {
		mpp_err("chl_id %d release request", ctx->chan_id);
		return -EBUSY;
	}

	switch (cmd) {
	case VOCDEC_IOC_CFG:{
			struct vcodec_msg v_msg;
			memset(&v_msg, 0, sizeof(v_msg));
			if (copy_from_user(&v_msg, msg, sizeof(v_msg)))
				return -EFAULT;
			ret = vcodec_process_msg(&v_msg, &req);
			if (ret)
				return -EFAULT;
			ret = vcodec_process_cmd(filp->private_data, &req);
			if (ret)
				return -EFAULT;
		}
		break;
	default:
		mpp_err("unknown ioctl cmd %x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

static unsigned int vcodec_poll(struct file *filp, poll_table * wait)
{
	struct chanid_ctx *ctx = filp->private_data;
	int chan_id = ctx->chan_id;
	MppCtxType type = ctx->type;
	struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(chan_id, type);
	unsigned int mask = 0;
	if (!list_empty(&chan_entry->stream_done)) {
		mask |= POLLIN | POLLRDNORM;
		return mask;
	}
	poll_wait(filp, &chan_entry->wait, wait);
	//mpp_log("poll_wait out \n");
	if (!list_empty(&chan_entry->stream_done)) {
		mask |= POLLIN | POLLRDNORM;
		//mpp_log("mask set %d \n", mask);
	}
	//	mpp_log("return mask %d \n", mask);
	return mask;
}

const struct file_operations vcodec_fops = {
	.open = vcodec_open,
    .release = vcodec_close,
    .poll = vcodec_poll,
    .unlocked_ioctl = vcodec_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vcodec_ioctl,
#endif /*  */
};

struct vcodec_dev {
	struct class *cls;
	struct device *dev;
	dev_t dev_id;
	struct cdev vco_cdev;
	struct device *child_dev;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *procfs;
	struct proc_dir_entry *vdec_procfs;
	struct proc_dir_entry *venc_procfs;
#endif
};
#define MPP_VCODEC_NAME	"vcodec"
#define MPP_ENC_NAME	"enc"
#define MPP_DEC_NAME	"dec"

#ifdef CONFIG_PROC_FS
static int vcodec_procfs_remove(struct vcodec_dev *vdev)
{
	if (vdev->procfs) {
		proc_remove(vdev->procfs);
		vdev->procfs = NULL;
	}

	if (vdev->vdec_procfs) {
		proc_remove(vdev->vdec_procfs);
		vdev->vdec_procfs = NULL;
	}

	if (vdev->procfs) {
		proc_remove(vdev->procfs);
		vdev->procfs = NULL;
	}

	return 0;
}

static int venc_proc_debug(struct seq_file *seq, void *offset)
{
	u32 i = 0;
	MppCtxType type = MPP_CTX_ENC;
	struct venc_module *venc = NULL;
	venc = mpp_vcodec_get_enc_module_entry();
    if (venc->thd) {
        seq_puts(
              seq,
              "\n--------venc thread status------------------------------------------------------------------------\n");

        seq_printf(seq, "%15s%15s%15s\n", "last_runing", "run_cnt", "que_cnt");
        seq_printf(seq, "%15lld%15lld%15lld\n", venc->thd->worker->last_us, venc->thd->worker->run_cnt,
                       venc->thd->queue_cnt);
    }

	for(i =0; i < MAX_ENC_NUM; i++) {
		struct mpp_chan *chan_entry = mpp_vcodec_get_chan_entry(i, type);
		if(chan_entry->handle){
			RK_U32 runing = atomic_read(&chan_entry->runing) > 0;
			RK_U32 comb_run = atomic_read(&chan_entry->cfg.comb_runing) > 0;
			seq_puts(
			seq,
			"\n--------venc chn runing status--------------------------------------------------------------------\n");

			seq_printf(seq, "%8s%8s%10s%10s%10s%10s%10s%14s%15s%15s\n", "ID", "runing", "combo_run", "cfg_gap", "strm_cnt",
			"strm_out", "gap_time", "cb_gap_time", "last_cb_start", "last_cb_end");

			seq_printf(seq, "%8d%8u%10u%10u%10u%10u%10u%14u%15llu%15llu\n", i, runing, comb_run, chan_entry->last_cfg_time,
			atomic_read(&chan_entry->stream_count),atomic_read(&chan_entry->str_out_cnt), chan_entry->gap_time,
				chan_entry->combo_gap_time, chan_entry->last_jeg_combo_start,  chan_entry->last_jeg_combo_end);

			mpp_enc_proc_debug(seq, chan_entry->handle, i);
		}
	}
	return 0;
}

static int vdec_proc_debug(struct seq_file *seq, void *offset)
{
   // MppCtxType type = MPP_CTX_DEC;

	return 0;
}


static int vcodec_procfs_init(struct vcodec_dev *vdev)
{

	vdev->procfs = proc_mkdir("vcodec", NULL);
	if (IS_ERR_OR_NULL(vdev->procfs)) {
		mpp_err("failed on open procfs %s\n", MPP_VCODEC_NAME);
		vdev->procfs = NULL;
		return -EIO;
	}

	vdev->venc_procfs = proc_mkdir(MPP_ENC_NAME, vdev->procfs);
	if (IS_ERR_OR_NULL(vdev->venc_procfs)) {
		mpp_err("failed on open procfs %s\n", MPP_ENC_NAME);
		vdev->venc_procfs = NULL;
		return -EIO;
	}

	vdev->vdec_procfs = proc_mkdir(MPP_DEC_NAME, vdev->procfs);
	if (IS_ERR_OR_NULL(vdev->vdec_procfs)) {
		mpp_err("failed on open procfs %s\n", MPP_DEC_NAME);
		vdev->vdec_procfs = NULL;
		return -EIO;
	}

	/* for debug */
	/* for show enc chnl info */
	proc_create_single_data("venc_info", 0444,
				vdev->venc_procfs , venc_proc_debug, NULL);

	proc_create_single_data("vdec_info", 0444,
				vdev->vdec_procfs , vdec_proc_debug, NULL);


	return 0;
}
#else
static inline int vcodec_procfs_remove(struct vcodec_dev *vdev)
{
	return 0;
}

static inline int vcodec_procfs_init(struct vcodec_dev *vdev)
{
	return 0;
}
#endif

static int vcodec_probe(struct platform_device *pdev)
{
	int ret;
	struct vcodec_dev *vdev = NULL;
	struct device *dev = &pdev->dev;

	/* create a device */
	vdev = devm_kzalloc(dev, sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;
	platform_set_drvdata(pdev, vdev);
	vdev->cls = class_create(THIS_MODULE, "vcodec_class");
	if (PTR_ERR_OR_ZERO(vdev->cls))
		return PTR_ERR(vdev->cls);
	vdev->dev = dev;
	ret = alloc_chrdev_region(&vdev->dev_id, 0, 1, "vcodec");
	if (ret) {
		dev_err(dev, "alloc dev_t failed\n");
		return ret;
	}
	cdev_init(&vdev->vco_cdev, &vcodec_fops);
	vdev->vco_cdev.owner = THIS_MODULE;
	vdev->vco_cdev.ops = &vcodec_fops;
	ret = cdev_add(&vdev->vco_cdev, vdev->dev_id, 1);
	if (ret) {
		unregister_chrdev_region(vdev->dev_id, 1);
		dev_err(dev, "add device failed\n");
		return ret;
	}
	vdev->child_dev =
		device_create(vdev->cls, dev, vdev->dev_id, NULL, "%s", "vcodec");

	vcodec_procfs_init(vdev);

	return ret;
}

static int vcodec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vcodec_dev *vdev = platform_get_drvdata(pdev);
	vcodec_procfs_remove(vdev);
	dev_info(dev, "remove device\n");
	device_destroy(vdev->cls, vdev->dev_id);
	cdev_del(&vdev->vco_cdev);
	unregister_chrdev_region(vdev->dev_id, 1);
	class_destroy(vdev->cls);
	return 0;
}

static const struct of_device_id vcodec_dt_ids[] = {
	{.compatible = "rockchip,vcodec",}, {},
};

static struct platform_driver vcodec_driver = {.probe =
	    vcodec_probe,.remove = vcodec_remove,.driver = {.name = "vcodec",
							    .of_match_table =
							    of_match_ptr
							    (vcodec_dt_ids),
							    },
};


static struct vcodec_mpidev_fn *mpidev_ops = NULL;
static struct vcodec_mpibuf_fn *mpibuf_ops = NULL;
void vmpi_register_fn2vcocdec (struct vcodec_mpidev_fn *mpidev_fn, struct vcodec_mpibuf_fn *mpibuf_fn)
{
	mpidev_ops = mpidev_fn;
	mpibuf_ops = mpibuf_fn;
	if(mpidev_ops){
		vcodec_create_mpi_dev();
	}
	return;
}
EXPORT_SYMBOL(vmpi_register_fn2vcocdec);

void vmpi_unregister_fn2vcocdec (void)
{

    mpp_vcodec_unregister_mipdev();
    mpidev_ops = NULL;
	mpibuf_ops = NULL;
	return;
}
EXPORT_SYMBOL(vmpi_unregister_fn2vcocdec);

struct vcodec_mpidev_fn *get_mpidev_ops(void){
	if (!mpidev_ops){
		mpp_err("should call vmpi_register_fn2vcocdec \n");
	}
	return mpidev_ops;
}

struct vcodec_mpibuf_fn *get_mpibuf_ops(void){
	if (!mpibuf_ops){
		mpp_err("should call vmpi_register_fn2vcocdec \n");
	}
	return mpibuf_ops;
}

static int __init vcodec_init(void)
{
	int ret;
	pr_info("init new\n");
	ret = platform_driver_register(&vcodec_driver);
	if (ret != 0) {
		printk(KERN_ERR "Platform device register failed (%d).\n", ret);
		return ret;
	}
	mpp_vcodec_init();

	//enc_test();
	return 0;
}

static void __exit vcodec_exit(void)
{

	//test_end();
	mpp_vcodec_deinit();
	platform_driver_unregister(&vcodec_driver);
	pr_info("exit\n");
} MODULE_LICENSE("GPL");

module_init(vcodec_init);
module_exit(vcodec_exit);
