/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#ifndef __ROCKCHIP_MPP_IOMMU_H__
#define __ROCKCHIP_MPP_IOMMU_H__

#include <linux/iommu.h>
#include <linux/dma-mapping.h>

struct mpp_dma_buffer {
	/* link to dma session buffer list */
	struct list_head link;

	/* dma session belong */
	struct mpp_dma_session *dma;
	/* DMABUF information */
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct sg_table *copy_sgt;
	enum dma_data_direction dir;

	dma_addr_t iova;
	unsigned long size;
	void *vaddr;

	struct kref ref;
	ktime_t last_used;
	/* alloc by device */
	struct device *dev;
};

#define MPP_SESSION_MAX_BUFFERS		60

struct mpp_dma_session {
	/* the buffer used in session */
	struct list_head unused_list;
	struct list_head used_list;
	struct list_head new_import_list;
	struct mpp_dma_buffer dma_bufs[MPP_SESSION_MAX_BUFFERS];
	/* the mutex for the above buffer list */
	struct mutex list_mutex;
	/* the max buffer num for the buffer list */
	u32 max_buffers;
	/* the count for the buffer list */
	int buffer_count;

	struct device *dev;
};

struct mpp_rk_iommu {
	struct list_head link;
	u32 grf_val;
	int mmu_num;
	u32 base_addr[2];
	void __iomem *bases[2];
	u32 dte_addr;
	u32 is_paged;
};

struct mpp_iommu_info {
	struct rw_semaphore rw_sem;

	struct device *dev;
	struct platform_device *pdev;
	struct iommu_domain *domain;
	struct iommu_group *group;
	struct mpp_rk_iommu *iommu;
	iommu_fault_handler_t hdl;
	u32 skip_refresh;
};

struct mpp_dma_buffer *
mpp_dma_alloc(struct device *dev, size_t size);
int mpp_dma_free(struct mpp_dma_buffer *buffer);

int mpp_dma_get_iova(struct dma_buf *dmabuf, struct device *dev);

int mpp_iommu_remove(struct mpp_iommu_info *info);
int mpp_iommu_refresh(struct mpp_iommu_info *info, struct device *dev);
int mpp_iommu_flush_tlb(struct mpp_iommu_info *info);

static inline int mpp_iommu_down_read(struct mpp_iommu_info *info)
{
	if (info)
		down_read(&info->rw_sem);

	return 0;
}

static inline int mpp_iommu_up_read(struct mpp_iommu_info *info)
{
	if (info)
		up_read(&info->rw_sem);

	return 0;
}

static inline int mpp_iommu_down_write(struct mpp_iommu_info *info)
{
	if (info)
		down_write(&info->rw_sem);

	return 0;
}

static inline int mpp_iommu_up_write(struct mpp_iommu_info *info)
{
	if (info)
		up_write(&info->rw_sem);

	return 0;
}

#endif
