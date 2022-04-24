# SPDX-License-Identifier: (GPL-2.0+ OR MIT)

$(VEPU_CORE)_ENABLE = y
$(VEPU_CORE)_PP_ENABLE = y
EXTRA_CFLAGS += -DMPP_VERSION="\"$(VCODEC_REVISION)\""
EXTRA_CFLAGS += -D$(VEPU_CORE)_ENABLE
EXTRA_CFLAGS += -D$(VEPU_CORE)_PP_ENABLE
EXTRA_CFLAGS += -I$(KERNEL_ROOT)/drivers/devfreq

rk_vcodec-objs := mpp/mpp_service.o mpp/mpp_common.o mpp/mpp_iommu.o
rk_vcodec-$(RKVEPU580_ENABLE) += mpp/mpp_rkvenc2.o
rk_vcodec-$(RKVEPU540C_ENABLE) += mpp/mpp_rkvenc_540c.o
rk_vcodec-$(RKVEPU540C_PP_ENABLE) += mpp/mpp_rkvenc_pp.o

ifneq ($(BUILD_ONE_KO), y)
	obj-m += rk_vcodec.o
endif
