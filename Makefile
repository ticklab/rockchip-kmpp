# SPDX-License-Identifier: (GPL-2.0+ OR MIT)
KERNEL_DIR ?= /home/csy/3588_linux/kernel
ARCH ?= arm64
CROSS_COMPILE ?= /home/csy/3588_linux/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
#CROSS_COMPILE=/home/csy/3588_linux/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/
KERNEL_ROOT=$(KERNEL_DIR)
KERNEL_VERSION=5.10
CPU_TYPE=$(ARCH)
OSTYPE=linux

ifeq ($(RK_ENABLE_FASTBOOT), y)
EXTRA_CFLAGS += -fno-verbose-asm
TOP := $(src)
else
TOP := $(PWD)
endif

VEPU_CORE := RKVEPU540C
BUILD_ONE_KO=y

ifeq ($(CPU_TYPE), arm64)
	export PREB_KO := ./prebuild/ko_64
	export REL_KO  := ./prebuild/ko_64_rel
else
	export PREB_KO := ./prebuild/ko_32
	export REL_KO  := ./prebuild/ko_32_rel
endif

VCODEC_GIT_REVISION := \
	$(shell cd $(TOP) && git log -1 --no-decorate --date=short \
	--pretty=format:"%h author: %<|(30)%an %cd %s" -- . || \
	echo -n "unknown mpp version for missing VCS info")

VCODEC_REVISION_0 := $(subst \,\\\,$(VCODEC_GIT_REVISION))
VCODEC_REVISION   := $(subst ",\\\",$(VCODEC_REVISION_0))

# add git hooks
$(shell [ -d "$(TOP)/.git/" ] && [ -d "$(TOP)/tools/hooks" ] && cp -rf $(TOP)/tools/hooks $(TOP)/.git/)

ifeq ($(BUILD_ONE_KO), y)
	EXTRA_CFLAGS += -DBUILD_ONE_KO
endif
include $(TOP)/mpp/Makefile
include $(TOP)/vcodec/Makefile
include $(TOP)/vproc/Makefile

ifeq ($(BUILD_ONE_KO), y)

ifeq ($(RK_ENABLE_FASTBOOT), y)
obj-y += mpp_vcodec.o
lib-m += $(mpp_vcodec-objs:%.o=%.s) $(rk_vcodec-objs:%.o=%.s) $(vepu_pp-objs:%.o=%.s) $(rk_vcodec-y:%.o=%.s)
else
obj-m += mpp_vcodec.o
endif

mpp_vcodec-objs += rk_vcodec.o
mpp_vcodec-objs += vepu_pp.o
endif

DIRS := $(shell find . -maxdepth 5 -type d)

FILES = $(foreach dir,$(DIRS),$(wildcard $(dir)/*.c))

OBJS = $(patsubst %.c,%.o, $(FILES))
CMDS = $(foreach dir,$(DIRS),$(wildcard $(dir)/.*.o.cmd))

.PHONY: all clean
all: $(OSTYPE)_build
clean: $(OSTYPE)_clean
install: $(OSTYPE)_install

linux_build: linux_clean
	@echo -e "\e[0;32;1m--Compiling '$(VMPI)'... Configs as follow:\e[0;36;1m"
	@echo ---- CROSS=$(CROSS_COMPILE)
	@echo ---- CPU_TYPE=$(CPU_TYPE)
	@$(MAKE) CROSS_COMPILE=${CROSS_COMPILE} ARCH=$(CPU_TYPE) -C $(KERNEL_ROOT) M=$(PWD) modules
	@mkdir -p $(PREB_KO)
	@cp mpp_vcodec.ko $(PREB_KO)
ifneq ($(BUILD_ONE_KO), y)
	@cp rk_vcodec.ko  $(PREB_KO)
	@cp vepu_pp.ko  $(PREB_KO)
endif
	@mkdir -p $(REL_KO)
	@cp mpp_vcodec.ko $(REL_KO)
ifneq ($(BUILD_ONE_KO), y)
	@cp rk_vcodec.ko  $(REL_KO)
	@cp vepu_pp.ko  $(REL_KO)
endif	
	@find $(REL_KO) -name "*.ko" | xargs ${CROSS_COMPILE}strip --strip-debug --remove-section=.comment --remove-section=.note --preserve-dates

linux_clean:
	@rm -f *.ko *.mod.c *.o
	@rm -f *.symvers *.order
	@find ./ -name "*.cmd" -o -name "*.s" -o -name "*.o" -o -name "*.mod" | xargs rm -rf
	@rm -rf .tmp_versions
	@rm -f built-in.a lib.a
	@rm -rf $(PREB_KO)
	@rm -rf $(REL_KO)
	@rm -rf $(OBJS)
	@rm -rf $(CMDS)

linux_release:
	find $(REL_KO) -name "*.ko" | xargs ${CROSS_COMPILE}strip --strip-debug --remove-section=.comment --remove-section=.note --preserve-dates
