// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#ifndef __MPP_LOG_H__
#define __MPP_LOG_H__
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include "rk_type.h"

/*
 * mpp runtime log system usage:
 * mpp_err is for error status message, it will print for sure.
 * mpp_log is for important message like open/close/reset/flush, it will print too.
 * mpp_dbg is for all optional message. it can be controlled by debug and flag.
 */
extern RK_U32 mpp_debug;

#ifndef pr_fmt
#define pr_fmt(fmt) MODULE_TAG ": %s:%d" fmt, __func__, __LINE__
#endif

#define _mpp_dbg(debug, flag, fmt, args...)             \
             do {                       \
                    if (unlikely(debug & flag))         \
                         pr_info("%s:%d: " fmt, __func__, __LINE__, ##args);  \
             } while (0)

#define mpp_dbg(flag, fmt, ...) _mpp_dbg(mpp_debug, flag, fmt, ## __VA_ARGS__)
#define mpp_err(fmt, ...)  pr_err("%d: " fmt, __LINE__, ## __VA_ARGS__)

/*
 * _f function will add function name to the log
 */
#define mpp_log(fmt, ...)  pr_info("%d: " fmt, __LINE__, ## __VA_ARGS__)

#define mpp_log_f(fmt, ...)  pr_info("%s:%d: " fmt, __func__, __LINE__, ## __VA_ARGS__)

#define mpp_err_f(fmt, args...)  pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

#define _mpp_dbg_f(debug, flag, fmt, ...)           \
            do {                        \
                    if (unlikely(debug & flag))         \
                         pr_info("%s:%d: " fmt, __func__, __LINE__, ## __VA_ARGS__);  \
            } while (0)

#define mpp_dbg_f(flag, fmt, ...) _mpp_dbg_f(mpp_debug, flag, fmt, ## __VA_ARGS__)

#define MPP_STRINGS(x)      MPP_TO_STRING(x)
#define MPP_TO_STRING(x)    #x

#define mpp_abort()                 \
    do {                            \
            WARN_ON(!(0));          \
    } while (0)

#define mpp_assert(cond)                        \
    do {                                                    \
        if (!(cond)) {                      \
            mpp_err("Assertion %s failed at %s:%d\n",   \
            MPP_STRINGS(cond), __FUNCTION__, __LINE__); \
            WARN_ON(!(cond));               \
        }                           \
    } while (0)

#endif /*__MPP_LOG_H__*/
