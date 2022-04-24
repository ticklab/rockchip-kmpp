# SPDX-License-Identifier: (GPL-2.0+ OR MIT)

OSAL_DIR := $(TOP)/vcodec/osal

VCODEC_TEST_ENABLE = n

include $(TOP)/vcodec/mpp/base/base_make.cfg
include $(TOP)/vcodec/mpp/codec/codec_make.cfg
include $(TOP)/vcodec/mpp/hal/hal_make.cfg
include $(TOP)/vcodec/mpp/codec/rc/rc_make.cfg


mpp_vcodec-objs := vcodec/mpp_vcodec_base.o \
		   vcodec/mpp_vcodec_chan.o \
		   vcodec/mpp_vcodec_event.o \
		   vcodec/mpp_vcodec_flow.o \
		   vcodec/mpp_vcodec_thread.o \
		   vcodec/mpp_vcodec_driver.o

mpp_vcodec-$(VCODEC_TEST_ENABLE) += vcodec/mpp/base/mpp_trie.o \
				    vcodec/mpp/base/mpp_enc_cfg.o \
				    vcodec/mpp/base/mpp_cfg.o \
				    vcodec/mpp_vcodec_test.o


mpp_vcodec-objs += vcodec/osal/mpp_log.o \
		   vcodec/osal/mpp_mem.o \
		   vcodec/osal/mpp_maths.o \
		   vcodec/osal/mpp_device.o \
		   vcodec/osal/mpp_service.o \
		   vcodec/osal/mpp_time.o


mpp_vcodec-objs += $(BASE_CFILES)
mpp_vcodec-objs += $(CODEC_CFILES)
mpp_vcodec-objs += $(HAL_CFILES)
mpp_vcodec-objs += $(RC_CFILES)
EXTRA_CFLAGS += -I$(TOP)/vcodec/osal/inc
EXTRA_CFLAGS += -I$(TOP)/vcodec/inc
EXTRA_CFLAGS += -I$(TOP)/vcodec/mpp/common
EXTRA_CFLAGS += -I$(TOP)/vcodec/mpp/inc
EXTRA_CFLAGS += -I$(TOP)/vcodec/mpp/hal/inc
EXTRA_CFLAGS += $(BASE_CFLAGS)
EXTRA_CFLAGS += $(CODEC_CFLAGS)
EXTRA_CFLAGS += $(HAL_CFLAGS)
EXTRA_CFLAGS += $(RC_CFLAGS)
EXTRA_CFLAGS += -DSUPPORT_ENC
EXTRA_CFLAGS += -DUSE_RING_BUF
EXTRA_CFLAGS += -DVCODEC_VERSION="\"$(VCODEC_REVISION)\""

ifneq ($(BUILD_ONE_KO), y)
	obj-m += mpp_vcodec.o
endif
