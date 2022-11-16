// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-iommu.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_ARM_DMA_USE_IOMMU
#include <asm/dma-iommu.h>
#endif
#include <soc/rockchip/rockchip_iommu.h>

#include "mpp_debug.h"
#include "mpp_iommu.h"


struct mpp_dma_buffer *
mpp_dma_alloc(struct device *dev, size_t size)
{
	size_t align_size;
	dma_addr_t iova;
	struct  mpp_dma_buffer *buffer;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return NULL;

	align_size = PAGE_ALIGN(size);
	buffer->vaddr = dma_alloc_coherent(dev, align_size, &iova, GFP_KERNEL);
	if (!buffer->vaddr)
		goto fail_dma_alloc;

	buffer->size = align_size;
	buffer->iova = iova;
	buffer->dev = dev;

	return buffer;
fail_dma_alloc:
	kfree(buffer);
	return NULL;
}

int mpp_dma_free(struct mpp_dma_buffer *buffer)
{
	dma_free_coherent(buffer->dev, buffer->size,
			  buffer->vaddr, buffer->iova);
	buffer->vaddr = NULL;
	buffer->iova = 0;
	buffer->size = 0;
	buffer->dev = NULL;
	kfree(buffer);

	return 0;
}

int mpp_iommu_remove(struct mpp_iommu_info *info)
{
	if (!info)
		return 0;

	iommu_group_put(info->group);
	platform_device_put(info->pdev);

	return 0;
}

int mpp_iommu_refresh(struct mpp_iommu_info *info, struct device *dev)
{
	int ret;

	if (!info || info->skip_refresh)
		return 0;

	/* disable iommu */
	ret = rockchip_iommu_disable(dev);
	if (ret)
		return ret;
	/* re-enable iommu */
	return rockchip_iommu_enable(dev);
}

int mpp_iommu_flush_tlb(struct mpp_iommu_info *info)
{
	if (!info)
		return 0;

	if (info->domain && info->domain->ops)
		iommu_flush_iotlb_all(info->domain);

	return 0;
}

int mpp_dma_get_iova(struct dma_buf *dmabuf, struct device *dev)
{
	struct dma_buf_attachment *attach;
	int ret = 0;
	struct sg_table *sgt;

	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(attach)) {
		mpp_err("dma_buf_attach dmabuf %p failed\n", dmabuf);
		return -1;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);

	if (IS_ERR(sgt)) {
		mpp_err("dma_buf_map_attachment dmabuf %p failed ret %d\n", dmabuf, ret);
		goto fail_map;
	}

	ret = (u32)sg_dma_address(sgt->sgl);

	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);

	dma_buf_detach(dmabuf, attach);

	return ret;
fail_map:
	dma_buf_detach(dmabuf, attach);
	return -1;
}
