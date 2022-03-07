// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */


#ifndef __MPP_TRIE_H__
#define __MPP_TRIE_H__

#include "rk_type.h"
#include "mpp_err.h"

typedef void *MppTrie;

#ifdef __cplusplus
extern "C" {
#endif

	MPP_RET mpp_trie_init(MppTrie * trie, RK_S32 node_count,
			      RK_S32 info_count);
	MPP_RET mpp_trie_deinit(MppTrie trie);

	MPP_RET mpp_trie_add_info(MppTrie trie, const char **info);
	const char **mpp_trie_get_info(MppTrie trie, const char *name);

	RK_S32 mpp_trie_get_node_count(MppTrie trie);
	RK_S32 mpp_trie_get_info_count(MppTrie trie);

#ifdef __cplusplus
}
#endif
#endif /*__MPP_TRIE_H__*/
