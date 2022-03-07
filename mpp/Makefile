# SPDX-License-Identifier: (GPL-2.0+ OR MIT)


rk_vcodec-objs := mpp/mpp_service.o mpp/mpp_common.o mpp/mpp_iommu.o

EXTRA_CFLAGS += -DMPP_VERSION="\"$(VCODEC_REVISION)\""

rk_vcodec-objs += mpp/mpp_rkvenc.o 
rk_vcodec-objs += mpp/mpp_rkvenc2.o

EXTRA_CFLAGS += -I$(KERNEL_ROOT)/drivers/devfreq

# hack for workaround

obj-m += rk_vcodec.o
