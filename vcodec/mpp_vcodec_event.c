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

#include "mpp_vcodec_event.h"

int mpp_vcodec_dec_event_notify(struct vcodec_event_msg_head *msg)
{
	return 0;
}

int mpp_vcodec_enc_event_notify(struct vcodec_event_msg_head *msg)
{
	return 0;
}
