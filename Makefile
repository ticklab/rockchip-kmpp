# SPDX-License-Identifier: (GPL-2.0+ OR MIT)

CROSS_COMPILE=/home/csy/3588_linux/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
#CROSS_COMPILE=/home/csy/3588_linux/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/
KERNEL_ROOT=/home/csy/3588_linux/kernel
KERNEL_VERSION=5.10
CPU_TYPE=arm64
OSTYPE=linux

TOP := $(PWD)

ifeq ($(CPU_TYPE), arm64)
	export PREB_KO := ./prebuild/ko_64
else
	export PREB_KO := ./prebuild/ko_32
endif



VCODEC_GIT_REVISION := \
	$(shell git log -1 --no-decorate --date=short \
	--pretty=format:"%h author: %<|(30)%an %cd %s" -- $(src) || \
	echo -n "unknown mpp version for missing VCS info")

VCODEC_REVISION_0 := $(subst \,\\\,$(VCODEC_GIT_REVISION))
VCODEC_REVISION   := $(subst ",\\\",$(VCODEC_REVISION_0))

include $(TOP)/mpp/Makefile
include $(TOP)/vcodec/Makefile

DIRS := $(shell find . -maxdepth 5 -type d)

FILES = $(foreach dir,$(DIRS),$(wildcard $(dir)/*.c))

OBJS = $(patsubst %.c,%.o, $(FILES))
CMDS = $(foreach dir,$(DIRS),$(wildcard $(dir)/.*.o.cmd))

.PHONY: all clean
all: $(OSTYPE)_build
clean: $(OSTYPE)_clean
install: $(OSTYPE)_install

linux_build:
	@echo -e "\e[0;32;1m--Compiling '$(VMPI)'... Configs as follow:\e[0;36;1m"
	@echo ---- CROSS=$(CROSS_COMPILE)
	@echo ---- CPU_TYPE=$(CPU_TYPE)
	@$(MAKE) CROSS_COMPILE=${CROSS_COMPILE} ARCH=$(CPU_TYPE) -C $(KERNEL_ROOT) M=$(PWD) modules
	@mkdir -p $(PREB_KO)
	cp mpp_vcodec.ko $(PREB_KO)
	cp rk_vcodec.ko  $(PREB_KO)

linux_clean:
	@rm -f *.ko *.mod.c *.o
	@rm -f *.symvers *.order
	@rm -rf .*.ko.cmd .*.o.cmd .tmp_versions
	@rm -rf $(PREB_KO)
	$(RM) $(OBJS) 
	$(RM) $(CMDS)
