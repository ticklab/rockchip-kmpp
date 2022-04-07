// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */

#define MODULE_TAG "h265e_header_gen"

#include <linux/string.h>

#include "mpp_maths.h"
#include "mpp_mem.h"
#include "h265e_ps.h"
#include "h265e_header_gen.h"
#include "mpp_packet.h"

static void h265e_nals_init(H265eExtraInfo * out)
{
	out->nal_buf = mpp_calloc(RK_U8, H265E_EXTRA_INFO_BUF_SIZE);
	out->nal_num = 0;
}

static void h265e_nals_deinit(H265eExtraInfo * out)
{
	MPP_FREE(out->nal_buf);

	out->nal_num = 0;
}

static RK_U8 *h265e_nal_escape_c(RK_U8 * dst, RK_U8 * src, RK_U8 * end)
{
	if (src < end)
		*dst++ = *src++;
	if (src < end)
		*dst++ = *src++;
	while (src < end) {
		// if (src[0] <= 0x03 && !dst[-2] && !dst[-1])
		// *dst++ = 0x03;
		*dst++ = *src++;
	}
	return dst;
}

static void h265e_nal_encode(RK_U8 * dst, H265eNal * nal)
{
	RK_S32 b_annexb = 1;
	RK_S32 size = 0;
	RK_U8 *src = nal->p_payload;
	RK_U8 *end = nal->p_payload + nal->i_payload;
	RK_U8 *orig_dst = dst;
	MppWriteCtx s;

	if (b_annexb) {
		*dst++ = 0x00;
		*dst++ = 0x00;
		*dst++ = 0x00;
		*dst++ = 0x01;
	} else			/* save room for size later */
		dst += 4;

	/* nal header */
	mpp_writer_init(&s, dst, 10);
	mpp_writer_put_bits(&s, 0, 1);	//forbidden_zero_bit
	mpp_writer_put_bits(&s, nal->i_type, 6);	//nal_unit_type
	mpp_writer_put_bits(&s, 0, 6);	//nuh_reserved_zero_6bits
	mpp_writer_put_bits(&s, 1, 3);	//nuh_temporal_id_plus1
	dst += 2;
	dst = h265e_nal_escape_c(dst, src, end);
	size = (RK_S32) ((dst - orig_dst) - 4);

	/* Write the size header for mp4/etc */
	if (!b_annexb) {
		/* Size doesn't include the size of the header we're writing now. */
		orig_dst[0] = size >> 24;
		orig_dst[1] = size >> 16;
		orig_dst[2] = size >> 8;
		orig_dst[3] = size >> 0;
	}

	nal->i_payload = size + 4;
	nal->p_payload = orig_dst;
}

static MPP_RET h265e_encapsulate_nals(H265eExtraInfo * out)
{
	RK_S32 i = 0;
	RK_S32 i_avcintra_class = 0;
	RK_S32 nal_size = 0;
	RK_S32 necessary_size = 0;
	RK_U8 *nal_buffer = out->nal_buf;
	RK_S32 nal_num = out->nal_num;
	H265eNal *nal = out->nal;

	h265e_dbg_func("enter\n");
	for (i = 0; i < nal_num; i++)
		nal_size += nal[i].i_payload;

	/* Worst-case NAL unit escaping: reallocate the buffer if it's too small. */
	necessary_size = nal_size * 3 / 2 + nal_num * 4 + 4 + 64;
	for (i = 0; i < nal_num; i++)
		necessary_size += nal[i].i_padding;

	for (i = 0; i < nal_num; i++) {
		nal[i].b_long_startcode = !i ||
					  nal[i].i_type == NAL_VPS ||
					  nal[i].i_type == NAL_SPS ||
					  nal[i].i_type == NAL_PPS || i_avcintra_class;
		h265e_nal_encode(nal_buffer, &nal[i]);
		nal_buffer += nal[i].i_payload;
	}

	h265e_dbg(H265E_DBG_HEADER, "nals total size: %d bytes",
		  (RK_U32) (nal_buffer - out->nal_buf));

	h265e_dbg_func("leave\n");
	return MPP_OK;
}

static MPP_RET h265e_sei_write(H265eStream * s, RK_U8 uuid[16],
			       const RK_U8 * payload, RK_S32 payload_size,
			       RK_S32 payload_type)
{
	RK_S32 i = 0;
	RK_S32 uuid_len = H265E_UUID_LENGTH;
	RK_S32 data_len = payload_size;

	h265e_dbg_func("enter\n");

	h265e_stream_realign(s);

	payload_size += uuid_len;

	for (i = 0; i <= payload_type - 255; i += 255)
		h265e_stream_write_with_log(s, 0xff, 8,
					    "sei_payload_type_ff_byte");

	h265e_stream_write_with_log(s, payload_type - i, 8,
				    "sei_last_payload_type_byte");

	for (i = 0; i <= payload_size - 255; i += 255)
		h265e_stream_write_with_log(s, 0xff, 8,
					    "sei_payload_size_ff_byte");

	h265e_stream_write_with_log(s, payload_size - i, 8,
				    "sei_last_payload_size_byte");

	for (i = 0; i < uuid_len; i++)
		h265e_stream_write_with_log(s, uuid[i], 8, "sei_uuid_byte");

	for (i = 0; i < data_len; i++)
		h265e_stream_write_with_log(s, (RK_U32) payload[i], 8,
					    "sei_payload_data");

	h265e_stream_rbsp_trailing(s);

	h265e_dbg_func("leave\n");

	return MPP_OK;
}

void code_profile_tier(H265eStream * s, profile_tier_level * ptl)
{
	RK_S32 j;
	h265e_stream_write_with_log(s, ptl->profile_space, 2,
				    "profile_space[]");
	h265e_stream_write1_with_log(s, ptl->tier_flag, "tier_flag[]");
	h265e_stream_write_with_log(s, ptl->profile_idc, 5, "profile_idc[]");
	for (j = 0; j < 32; j++) {
		h265e_stream_write1_with_log(s,
					     ptl->profile_compatibility_flag[j],
					     "profile_compatibility_flag[][j]");
	}

	h265e_stream_write1_with_log(s, ptl->general_progressive_source_flag,
				     "general_progressive_source_flag");
	h265e_stream_write1_with_log(s, ptl->general_interlaced_source_flag,
				     "general_interlaced_source_flag");
	h265e_stream_write1_with_log(s, ptl->general_non_packed_constraint_flag,
				     "general_non_packed_constraint_flag");
	h265e_stream_write1_with_log(s, ptl->general_frame_only_constraint_flag,
				     "general_frame_only_constraint_flag");

	h265e_stream_write_with_log(s, 0, 16, "reserved_zero_44bits[0..15]");
	h265e_stream_write_with_log(s, 0, 16, "reserved_zero_44bits[16..31]");
	h265e_stream_write_with_log(s, 0, 12, "eserved_zero_44bits[32..43]");
}

static void code_profile_tier_level(H265eStream * s,
				    h265_profile_tier_level * ptl,
				    RK_U32 profile_present_flag,
				    int max_sub_layers_minus1)
{
	RK_S32 i;
	if (profile_present_flag)
		code_profile_tier(s, &ptl->general_PTL);
	h265e_stream_write_with_log(s, ptl->general_PTL.general_level_idc, 8,
				    "general_level_idc");

	for (i = 0; i < max_sub_layers_minus1; i++) {
		if (profile_present_flag) {
			h265e_stream_write1_with_log(s,
						     ptl->
						     sub_layer_profile_present_flag
						     [i],
						     "sub_layer_profile_present_flag[i]");
		}

		h265e_stream_write1_with_log(s,
					     ptl->
					     sub_layer_level_present_flag[i],
					     "sub_layer_level_present_flag[i]");
	}

	if (max_sub_layers_minus1 > 0) {
		for (i = max_sub_layers_minus1; i < 8; i++) {
			h265e_stream_write_with_log(s, 0, 2,
						    "reserved_zero_2bits");
		}
	}

	for (i = 0; i < max_sub_layers_minus1; i++) {
		if (profile_present_flag
		    && ptl->sub_layer_profile_present_flag[i]) {
			code_profile_tier(s, &ptl->sub_layer_PTL[i]);	// sub_layer_...
		}
		if (ptl->sub_layer_level_present_flag[i]) {
			h265e_stream_write_with_log(s,
						    ptl->sub_layer_PTL[i].
						    general_level_idc, 8,
						    "sub_layer_level_idc[i]");
		}
	}
}

static MPP_RET h265e_vps_write(h265_vps * vps, H265eStream * s)
{
	RK_S32 vps_byte_start = 0;
	RK_U32 i, opsIdx;

	h265e_dbg_func("enter\n");
	h265e_stream_realign(s);
	vps_byte_start = s->enc_stream.byte_cnt;
	h265e_stream_write_with_log(s, vps->vps_video_parameter_set_id, 4,
				    "vps_video_parameter_set_id");
	h265e_stream_write_with_log(s, 3, 2, "vps_reserved_three_2bits");
	h265e_stream_write_with_log(s, 0, 6, "vps_reserved_zero_6bits");
	h265e_stream_write_with_log(s, vps->vps_max_sub_layers_minus1, 3,
				    "vps_max_sub_layers_minus1");
	h265e_stream_write1_with_log(s, vps->vps_temporal_id_nesting_flag,
				     "vps_temporal_id_nesting_flag");

	h265e_stream_write_with_log(s, 0xffff, 16, "vps_reserved_ffff_16bits");

	code_profile_tier_level(s, &vps->profile_tier_level, 1,
				vps->vps_max_sub_layers_minus1);

	h265e_stream_write1_with_log(s, 1,
				     "vps_sub_layer_ordering_info_present_flag");
	for (i = 0; i <= vps->vps_max_sub_layers_minus1; i++) {
		h265e_stream_write_ue_with_log(s,
					       vps->
					       vps_max_dec_pic_buffering_minus1
					       [i],
					       "vps_max_dec_pic_buffering_minus1[i]");
		h265e_stream_write_ue_with_log(s, vps->vps_num_reorder_pics[i],
					       "vps_num_reorder_pics[i]");
		h265e_stream_write_ue_with_log(s,
					       vps->
					       vps_max_latency_increase_plus1
					       [i],
					       "vps_max_latency_increase_plus1[i]");
	}

	mpp_assert(vps->vps_num_hrd_parameters <= MAX_VPS_NUM_HRD_PARAMETERS);
	mpp_assert(vps->vps_max_nuh_reserved_zero_layer_id <
		   MAX_VPS_NUH_RESERVED_ZERO_LAYER_ID_PLUS1);
	h265e_stream_write_with_log(s, vps->vps_max_nuh_reserved_zero_layer_id,
				    6, "vps_max_nuh_reserved_zero_layer_id");
	vps->vps_max_op_sets_minus1 = 0;
	h265e_stream_write_ue_with_log(s, vps->vps_max_op_sets_minus1,
				       "vps_max_op_sets_minus1");
	for (opsIdx = 1; opsIdx <= (vps->vps_max_op_sets_minus1); opsIdx++) {
		// Operation point set
		for (i = 0; i <= vps->vps_max_nuh_reserved_zero_layer_id; i++) {
			// Only applicable for version 1
			vps->layer_id_included_flag[opsIdx][i] = 1;
			h265e_stream_write1_with_log(s,
						     vps->
						     layer_id_included_flag
						     [opsIdx][i] ? 1 : 0,
						     "layer_id_included_flag[opsIdx][i]");
		}
	}

	h265e_stream_write1_with_log(s, vps->vps_timing_info_present_flag,
				     "vps_timing_info_present_flag");
	if (vps->vps_timing_info_present_flag) {
		h265e_stream_write_with_log(s, vps->vps_num_units_in_tick, 32,
					    "vps_num_units_in_tick");
		h265e_stream_write_with_log(s, vps->vps_time_scale, 32,
					    "vps_time_scale");
		h265e_stream_write1_with_log(s,
					     vps->
					     vps_poc_proportional_to_timing_flag,
					     "vps_poc_proportional_to_timing_flag");
		if (vps->vps_poc_proportional_to_timing_flag) {
			h265e_stream_write_ue_with_log(s,
						       vps->
						       vps_num_ticks_poc_diff_one_minus1,
						       "vps_num_ticks_poc_diff_one_minus1");
		}
		vps->vps_num_hrd_parameters = 0;
		h265e_stream_write_ue_with_log(s, vps->vps_num_hrd_parameters,
					       "vps_num_hrd_parameters");
#if 0
		if (vps->m_numHrdParameters > 0)
			vps->createHrdParamBuffer();
		for (uint32_t i = 0; i < vps->getNumHrdParameters(); i++) {
			// Only applicable for version 1
			vps->setHrdOpSetIdx(0, i);
			h265e_stream_write_ue_with_log(s,
						       vps->getHrdOpSetIdx(i),
						       "hrd_op_set_idx");
			if (i > 0) {
				h265e_stream_write1_with_log(s,
							     vps->
							     getCprmsPresentFlag
							     (i) ? 1 : 0,
							     "cprms_present_flag[i]");
			}
			codeHrdParameters(vps->getHrdParameters(i),
					  vps->getCprmsPresentFlag(i),
					  vps->getMaxTLayers() - 1);
		}
#endif
	}
	h265e_stream_write1_with_log(s, 0, "vps_extension_flag");
	h265e_stream_rbsp_trailing(s);
	h265e_stream_flush(s);
	h265e_dbg(H265E_DBG_HEADER, "write pure vps head size: %d bits",
		  (s->enc_stream.byte_cnt - vps_byte_start) * 8);
	//future extensions here..
	h265e_dbg_func("leave\n");
	return MPP_OK;
}

void codeVUI(H265eStream * s, h265_vui_param_set * vui)
{
	h265e_stream_write1_with_log(s, vui->aspect_ratio_info_present_flag,
				     "aspect_ratio_info_present_flag");
	if (vui->aspect_ratio_info_present_flag) {
		h265e_stream_write_with_log(s, vui->aspect_ratio_idc, 8,
					    "aspect_ratio_idc");
		if (vui->aspect_ratio_idc == 255) {
			h265e_stream_write_with_log(s, vui->sar_width, 16,
						    "sar_width");
			h265e_stream_write_with_log(s, vui->sar_height, 16,
						    "sar_height");
		}
	}
	h265e_stream_write1_with_log(s, vui->overscan_info_present_flag,
				     "overscan_info_present_flag");
	if (vui->overscan_info_present_flag) {
		h265e_stream_write1_with_log(s, vui->overscan_appropriate_flag,
					     "overscan_appropriate_flag");
	}
	h265e_stream_write1_with_log(s, vui->video_signal_type_present_flag,
				     "video_signal_type_present_flag");
	if (vui->video_signal_type_present_flag) {
		h265e_stream_write_with_log(s, vui->video_format, 3,
					    "video_format");
		h265e_stream_write1_with_log(s, vui->video_full_range_flag,
					     "video_full_range_flag");
		h265e_stream_write1_with_log(s,
					     vui->
					     colour_description_present_flag,
					     "colour_description_present_flag");
		if (vui->colour_description_present_flag) {
			h265e_stream_write_with_log(s, vui->colour_primaries, 8,
						    "colour_primaries");
			h265e_stream_write_with_log(s,
						    vui->
						    transfer_characteristics, 8,
						    "transfer_characteristics");
			h265e_stream_write_with_log(s, vui->matrix_coefficients,
						    8, "matrix_coefficients");
		}
	}

	h265e_stream_write1_with_log(s, vui->chroma_loc_info_present_flag,
				     "chroma_loc_info_present_flag");
	if (vui->chroma_loc_info_present_flag) {
		h265e_stream_write_ue_with_log(s,
					       vui->
					       chroma_sample_loc_type_top_field,
					       "chroma_sample_loc_type_top_field");
		h265e_stream_write_ue_with_log(s,
					       vui->
					       chroma_sample_loc_type_bottom_field,
					       "chroma_sample_loc_type_bottom_field");
	}

	h265e_stream_write1_with_log(s, vui->neutral_chroma_indication_flag,
				     "neutral_chroma_indication_flag");
	h265e_stream_write1_with_log(s, vui->field_seq_flag, "field_seq_flag");
	h265e_stream_write1_with_log(s, vui->frame_field_info_present_flag,
				     "frame_field_info_present_flag");

	h265e_stream_write1_with_log(s, vui->default_display_window_flag,
				     "default_display_window_flag");
	if (vui->default_display_window_flag) {
		h265e_stream_write_ue_with_log(s, vui->def_disp_win_left_offset,
					       "def_disp_win_left_offset");
		h265e_stream_write_ue_with_log(s,
					       vui->def_disp_win_right_offset,
					       "def_disp_win_right_offset");
		h265e_stream_write_ue_with_log(s, vui->def_disp_win_top_offset,
					       "def_disp_win_top_offset");
		h265e_stream_write_ue_with_log(s,
					       vui->def_disp_win_bottom_offset,
					       "def_disp_win_bottom_offset");
	}

	h265e_stream_write1_with_log(s, vui->vui_timing_info_present_flag,
				     "vui_timing_info_present_flag");
	if (vui->vui_timing_info_present_flag) {
		h265e_stream_write32(s, vui->vui_num_units_in_tick,
				     "vui_num_units_in_tick");
		h265e_stream_write32(s, vui->vui_time_scale, "vui_time_scale");
		h265e_stream_write1_with_log(s,
					     vui->
					     vui_poc_proportional_to_timing_flag,
					     "vui_poc_proportional_to_timing_flag");
		if (vui->vui_poc_proportional_to_timing_flag) {
			h265e_stream_write_ue_with_log(s,
						       vui->
						       vui_num_ticks_poc_diff_one_minus1,
						       "vui_num_ticks_poc_diff_one_minus1");
		}
		h265e_stream_write1_with_log(s,
					     vui->hrd_parameters_present_flag,
					     "hrd_parameters_present_flag");
		if (vui->hrd_parameters_present_flag) {
			// codeHrdParameters(vui->getHrdParameters(), 1, sps->getMaxTLayers() - 1); //todo
		}
	}
	h265e_stream_write1_with_log(s, vui->bitstream_restriction_flag,
				     "bitstream_restriction_flag");
	if (vui->bitstream_restriction_flag) {
		h265e_stream_write1_with_log(s, vui->tiles_fixed_structure_flag,
					     "tiles_fixed_structure_flag");
		h265e_stream_write1_with_log(s,
					     vui->
					     motion_vectors_over_pic_boundaries_flag,
					     "motion_vectors_over_pic_boundaries_flag");
		h265e_stream_write1_with_log(s,
					     vui->restricted_ref_pic_lists_flag,
					     "restricted_ref_pic_lists_flag");
		h265e_stream_write_ue_with_log(s,
					       vui->
					       min_spatial_segmentation_idc,
					       "min_spatial_segmentation_idc");
		h265e_stream_write_ue_with_log(s, vui->max_bytes_per_pic_denom,
					       "max_bytes_per_pic_denom");
		h265e_stream_write_ue_with_log(s, vui->max_bits_per_mincu_denom,
					       "max_bits_per_mincu_denom");
		h265e_stream_write_ue_with_log(s,
					       vui->
					       log2_max_mv_length_horizontal,
					       "log2_max_mv_length_horizontal");
		h265e_stream_write_ue_with_log(s,
					       vui->log2_max_mv_length_vertical,
					       "log2_max_mv_length_vertical");
	}
}

static MPP_RET h265e_sps_write(h265_sps * sps, H265eStream * s)
{
	RK_S32 sps_byte_start = 0;
	RK_U32 i, k;
	const RK_S32 winUnitX[] = { 1, 2, 2, 1 };
	const RK_S32 winUnitY[] = { 1, 2, 1, 1 };
	h265_sps_rps_list *rps_list = &sps->rps_list;

	h265e_dbg_func("enter\n");
	h265e_stream_realign(s);
	sps_byte_start = s->enc_stream.byte_cnt;

	h265e_stream_write_with_log(s, sps->vps_video_parameter_set_id, 4,
				    "sps_video_parameter_set_id");
	h265e_stream_write_with_log(s, sps->vps_max_sub_layers_minus1, 3,
				    "sps_max_sub_layers_minus1");
	h265e_stream_write1_with_log(s,
				     sps->vps_temporal_id_nesting_flag ? 1 : 0,
				     "sps_temporal_id_nesting_flag");
	code_profile_tier_level(s, sps->profile_tier_level, 1,
				sps->vps_max_sub_layers_minus1);
	h265e_stream_write_ue_with_log(s, sps->sps_seq_parameter_set_id,
				       "sps_seq_parameter_set_id");
	h265e_stream_write_ue_with_log(s, sps->chroma_format_idc,
				       "chroma_format_idc");

	if (sps->chroma_format_idc == 4) {
		h265e_stream_write1_with_log(s, sps->separate_colour_plane_flag,
					     "separate_colour_plane_flag");
	}

	h265e_stream_write_ue_with_log(s, sps->pic_width_in_luma_samples,
				       "pic_width_in_luma_samples");
	h265e_stream_write_ue_with_log(s, sps->pic_height_in_luma_samples,
				       "pic_height_in_luma_samples");
	h265e_stream_write1_with_log(s, sps->vui.default_display_window_flag,
				     "conformance_window_flag");
	if (sps->vui.default_display_window_flag) {
		h265e_stream_write_ue_with_log(s,
					       sps->vui.
					       def_disp_win_left_offset /
					       winUnitX[sps->chroma_format_idc],
					       "conf_win_left_offset");
		h265e_stream_write_ue_with_log(s,
					       sps->vui.
					       def_disp_win_right_offset /
					       winUnitX[sps->chroma_format_idc],
					       "conf_win_right_offset");
		h265e_stream_write_ue_with_log(s,
					       sps->vui.
					       def_disp_win_top_offset /
					       winUnitY[sps->chroma_format_idc],
					       "conf_win_top_offset");
		h265e_stream_write_ue_with_log(s,
					       sps->vui.
					       def_disp_win_bottom_offset /
					       winUnitY[sps->chroma_format_idc],
					       "conf_win_bottom_offset");
	}

	h265e_stream_write_ue_with_log(s, sps->bit_depth_luma_minus8,
				       "bit_depth_luma_minus8");
	h265e_stream_write_ue_with_log(s, sps->bit_depth_chroma_minus8,
				       "bit_depth_chroma_minus8");

	h265e_stream_write_ue_with_log(s, sps->bits_for_poc - 4,
				       "log2_max_pic_order_cnt_lsb_minus4");

	h265e_stream_write1_with_log(s, 1,
				     "sps_sub_layer_ordering_info_present_flag");
	for (i = 0; i <= sps->vps_max_sub_layers_minus1; i++) {
		h265e_stream_write_ue_with_log(s,
					       sps->
					       vps_max_dec_pic_buffering_minus1
					       [i],
					       "sps_max_dec_pic_buffering_minus1[i]");
		h265e_stream_write_ue_with_log(s, sps->vps_num_reorder_pics[i],
					       "sps_num_reorder_pics[i]");
		h265e_stream_write_ue_with_log(s,
					       sps->
					       vps_max_latency_increase_plus1
					       [i],
					       "sps_max_latency_increase_plus1[i]");
	}

	h265e_stream_write_ue_with_log(s,
				       sps->log2_min_coding_block_size_minus3,
				       "log2_min_coding_block_size_minus3");
	h265e_stream_write_ue_with_log(s,
				       sps->log2_diff_max_min_coding_block_size,
				       "log2_diff_max_min_coding_block_size");
	h265e_stream_write_ue_with_log(s,
				       sps->
				       log2_min_transform_block_size_minus2,
				       "log2_min_transform_block_size_minus2");
	h265e_stream_write_ue_with_log(s,
				       sps->
				       log2_diff_max_min_transform_block_size,
				       "log2_diff_max_min_transform_block_size");
	h265e_stream_write_ue_with_log(s,
				       sps->
				       max_transform_hierarchy_depth_inter - 1,
				       "max_transform_hierarchy_depth_inter");
	h265e_stream_write_ue_with_log(s,
				       sps->
				       max_transform_hierarchy_depth_intra - 1,
				       "max_transform_hierarchy_depth_intra");
	h265e_stream_write1_with_log(s, sps->scaling_list_enabled_flag ? 1 : 0,
				     "scaling_list_enabled_flag");
	if (sps->scaling_list_enabled_flag) {
		h265e_stream_write1_with_log(s,
					     sps->
					     sps_scaling_list_data_present_flag
					     ? 1 : 0,
					     "sps_scaling_list_data_present_flag");
		if (sps->sps_scaling_list_data_present_flag) {
			mpp_log("to do sps_scaling_list_data_present_flag");
			;	//codeScalingList(m_slice->getScalingList()); //todo only support default
		}
	}
	h265e_stream_write1_with_log(s, sps->amp_enabled_flag ? 1 : 0,
				     "amp_enabled_flag");
	h265e_stream_write1_with_log(s,
				     sps->
				     sample_adaptive_offset_enabled_flag ? 1 :
				     0, "sample_adaptive_offset_enabled_flag");

	h265e_stream_write1_with_log(s, sps->pcm_enabled_flag ? 1 : 0,
				     "pcm_enabled_flag");
	if (sps->pcm_enabled_flag) {
		h265e_stream_write_with_log(s, sps->pcm_bit_depth_luma - 1, 4,
					    "pcm_sample_bit_depth_luma_minus1");
		h265e_stream_write_with_log(s, sps->pcm_bit_depth_chroma - 1, 4,
					    "pcm_sample_bit_depth_chroma_minus1");
		h265e_stream_write_ue_with_log(s, sps->pcm_log2_min_size - 3,
					       "log2_min_pcm_luma_coding_block_size_minus3");
		h265e_stream_write_ue_with_log(s,
					       sps->pcm_log2_max_size -
					       sps->pcm_log2_min_size,
					       "log2_diff_max_min_pcm_luma_coding_block_size");
		h265e_stream_write1_with_log(s,
					     sps->
					     pcm_loop_filter_disable_flag ? 1 :
					     0, "pcm_loop_filter_disable_flag");
	}

	mpp_assert(sps->vps_max_sub_layers_minus1 + 1 > 0);

	//  H265eReferencePictureSet* rps;

	h265e_stream_write_ue_with_log(s, rps_list->num_short_term_ref_pic_sets,
				       "num_short_term_ref_pic_sets");
	for (i = 0; i < (RK_U32) rps_list->num_short_term_ref_pic_sets; i++) {
		mpp_log("todo num_short_term_ref_pic_sets");
		//rps = &rpsList->m_referencePictureSets[i];
		// codeShortTermRefPicSet(rps, false, i); //todo no support
	}
	h265e_stream_write1_with_log(s,
				     sps->
				     long_term_ref_pics_present_flag ? 1 : 0,
				     "long_term_ref_pics_present_flag");
	if (sps->long_term_ref_pics_present_flag) {
		h265e_stream_write_ue_with_log(s,
					       sps->num_long_term_ref_pic_sps,
					       "num_long_term_ref_pic_sps");
		for (k = 0; k < sps->num_long_term_ref_pic_sps; k++) {
			h265e_stream_write_with_log(s,
						    sps->
						    lt_ref_pic_poc_lsb_sps[k],
						    sps->bits_for_poc,
						    "lt_ref_pic_poc_lsb_sps");
			h265e_stream_write1_with_log(s,
						     sps->
						     used_by_curr_pic_lt_sps_flag
						     [k],
						     "used_by_curr_pic_lt_sps_flag");
		}
	}

	h265e_stream_write1_with_log(s,
				     sps->sps_temporal_mvp_enable_flag ? 1 : 0,
				     "sps_temporal_mvp_enable_flag");
	h265e_stream_write1_with_log(s,
				     sps->
				     sps_strong_intra_smoothing_enable_flag,
				     "sps_strong_intra_smoothing_enable_flag");

	h265e_stream_write1_with_log(s, sps->vui_parameters_present_flag,
				     "vui_parameters_present_flag");
	if (sps->vui_parameters_present_flag)
		codeVUI(s, &sps->vui);
	h265e_stream_write1_with_log(s, 0, "sps_extension_flag");
	h265e_stream_rbsp_trailing(s);
	h265e_stream_flush(s);
	h265e_dbg(H265E_DBG_HEADER, "write pure sps head size: %d bits",
		  (s->enc_stream.byte_cnt - sps_byte_start) * 8);

	h265e_dbg_func("leave\n");
	return MPP_OK;
}

#if 0
static void h265e_rkv_scaling_list_write(H265eRkvStream * s,
					 H265ePps * pps, RK_S32 idx)
{

}
#endif

static MPP_RET h265e_pps_write(h265_pps * pps, h265_sps * sps, H265eStream * s)
{
	RK_S32 pps_byte_start = 0;
	RK_S32 i;
	h265e_stream_realign(s);
	pps_byte_start = s->enc_stream.byte_cnt;
	(void)sps;

	h265e_dbg_func("enter\n");
	h265e_stream_write_ue_with_log(s, pps->pps_pic_parameter_set_id,
				       "pps_pic_parameter_set_id");
	h265e_stream_write_ue_with_log(s, pps->pps_seq_parameter_set_id,
				       "pps_seq_parameter_set_id");
	h265e_stream_write1_with_log(s, 0,
				     "dependent_slice_segments_enabled_flag");
	h265e_stream_write1_with_log(s, pps->output_flag_present_flag ? 1 : 0,
				     "output_flag_present_flag");
	h265e_stream_write_with_log(s, pps->num_extra_slice_header_bits, 3,
				    "num_extra_slice_header_bits");
	h265e_stream_write1_with_log(s, pps->sign_data_hiding_flag,
				     "sign_data_hiding_flag");
	h265e_stream_write1_with_log(s, pps->cabac_init_present_flag ? 1 : 0,
				     "cabac_init_present_flag");
	h265e_stream_write_ue_with_log(s,
				       pps->
				       num_ref_idx_l0_default_active_minus1 - 1,
				       "num_ref_idx_l0_default_active_minus1");
	h265e_stream_write_ue_with_log(s,
				       pps->
				       num_ref_idx_l1_default_active_minus1 - 1,
				       "num_ref_idx_l1_default_active_minus1");

	h265e_stream_write_se_with_log(s, pps->init_qp_minus26,
				       "init_qp_minus26");
	h265e_stream_write1_with_log(s,
				     pps->constrained_intra_pred_flag ? 1 : 0,
				     "constrained_intra_pred_flag");
	h265e_stream_write1_with_log(s,
				     pps->transform_skip_enabled_flag ? 1 : 0,
				     "transform_skip_enabled_flag");
	h265e_stream_write1_with_log(s, pps->cu_qp_delta_enabled_flag ? 1 : 0,
				     "cu_qp_delta_enabled_flag");
	if (pps->cu_qp_delta_enabled_flag) {
		h265e_stream_write_ue_with_log(s, pps->diff_cu_qp_delta_depth,
					       "diff_cu_qp_delta_depth");
	}
	h265e_stream_write_se_with_log(s, pps->pps_cb_qp_offset,
				       "pps_cb_qp_offset");
	h265e_stream_write_se_with_log(s, pps->pps_cr_qp_offset,
				       "pps_cr_qp_offset");
	h265e_stream_write1_with_log(s,
				     pps->
				     pps_slice_chroma_qp_offsets_present_flag ?
				     1 : 0,
				     "pps_slice_chroma_qp_offsets_present_flag");

	h265e_stream_write1_with_log(s, pps->weighted_pred_flag ? 1 : 0,
				     "weighted_pred_flag");	// Use of Weighting Prediction (P_SLICE)
	h265e_stream_write1_with_log(s, pps->weighted_bipred_flag ? 1 : 0,
				     "weighted_bipred_flag");	// Use of Weighting Bi-Prediction (B_SLICE)
	h265e_stream_write1_with_log(s,
				     pps->transquant_bypass_enable_flag ? 1 : 0,
				     "transquant_bypass_enable_flag");
	h265e_stream_write1_with_log(s, pps->tiles_enabled_flag,
				     "tiles_enabled_flag");
	h265e_stream_write1_with_log(s,
				     pps->
				     entropy_coding_sync_enabled_flag ? 1 : 0,
				     "entropy_coding_sync_enabled_flag");
	if (pps->tiles_enabled_flag) {
		h265e_stream_write_ue_with_log(s, pps->num_tile_columns_minus1,
					       "num_tile_columns_minus1");
		h265e_stream_write_ue_with_log(s, pps->num_tile_rows_minus1,
					       "num_tile_rows_minus1");
		h265e_stream_write1_with_log(s, pps->uniform_spacing_flag,
					     "uniform_spacing_flag");
		if (!pps->uniform_spacing_flag) {
			for (i = 0; i < pps->num_tile_columns_minus1; i++) {
				h265e_stream_write_ue_with_log(s,
							       pps->
							       tile_column_width_array
							       [i + 1] -
							       pps->
							       tile_column_width_array
							       [i] - 1,
							       "column_width_minus1");
			}
			for (i = 0; i < pps->num_tile_rows_minus1; i++) {
				h265e_stream_write_ue_with_log(s,
							       pps->
							       tile_row_height_array
							       [i + 1] -
							       pps->
							       tile_row_height_array
							       [i - 1],
							       "row_height_minus1");
			}
		}
		mpp_assert((pps->num_tile_columns_minus1 +
			    pps->num_tile_rows_minus1) != 0);
		h265e_stream_write1_with_log(s,
					     pps->
					     loop_filter_across_tiles_enabled_flag
					     ? 1 : 0,
					     "loop_filter_across_tiles_enabled_flag");
	}
	h265e_stream_write1_with_log(s,
				     pps->
				     loop_filter_across_slices_enabled_flag ? 1
				     : 0,
				     "loop_filter_across_slices_enabled_flag");

	// TODO: Here have some time sequence problem, we set below field in initEncSlice(), but use them in getStreamHeaders() early
	h265e_stream_write1_with_log(s,
				     pps->
				     deblocking_filter_control_present_flag ? 1
				     : 0,
				     "deblocking_filter_control_present_flag");
	if (pps->deblocking_filter_control_present_flag) {
		h265e_stream_write1_with_log(s,
					     pps->
					     deblocking_filter_override_enabled_flag
					     ? 1 : 0,
					     "deblocking_filter_override_enabled_flag");
		h265e_stream_write1_with_log(s,
					     pps->
					     pps_disable_deblocking_filter_flag
					     ? 1 : 0,
					     "pps_disable_deblocking_filter_flag");
		if (!pps->pps_disable_deblocking_filter_flag) {
			h265e_stream_write_se_with_log(s,
						       pps->
						       pps_beta_offset_div2,
						       "pps_beta_offset_div2");
			h265e_stream_write_se_with_log(s,
						       pps->pps_tc_offset_div2,
						       "pps_tc_offset_div2");
		}
	}
	h265e_stream_write1_with_log(s,
				     pps->
				     sps_scaling_list_data_present_flag ? 1 : 0,
				     "pps_scaling_list_data_present_flag");
	if (pps->sps_scaling_list_data_present_flag) {
		;		//codeScalingList(m_slice->getScalingList()); //todo
	}
	h265e_stream_write1_with_log(s, pps->lists_modification_present_flag,
				     "lists_modification_present_flag");
	h265e_stream_write_ue_with_log(s, pps->log2_parallel_merge_level_minus2,
				       "log2_parallel_merge_level_minus2");
	h265e_stream_write1_with_log(s,
				     pps->
				     slice_segment_header_extension_present_flag
				     ? 1 : 0,
				     "slice_segment_header_extension_present_flag");
	h265e_stream_write1_with_log(s, 0, "pps_extension_flag");

	h265e_stream_rbsp_trailing(s);
	h265e_stream_flush(s);

	h265e_dbg(H265E_DBG_HEADER, "write pure pps size: %d bits",
		  (s->enc_stream.byte_cnt - pps_byte_start) * 8);
	h265e_dbg_func("leave\n");

	return MPP_OK;
}

void h265e_nal_start(H265eExtraInfo * out, RK_S32 i_type, RK_S32 i_ref_idc)
{
	H265eStream *s = &out->stream;
	H265eNal *nal = &out->nal[out->nal_num];

	nal->i_ref_idc = i_ref_idc;
	nal->i_type = i_type;
	nal->b_long_startcode = 1;

	nal->i_payload = 0;
	/* NOTE: consistent with stream_init */
	nal->p_payload = &s->buf[s->enc_stream.byte_cnt];
	nal->i_padding = 0;
}

void h265e_nal_end(H265eExtraInfo * out)
{
	H265eNal *nal = &(out->nal[out->nal_num]);
	H265eStream *s = &out->stream;
	/* NOTE: consistent with stream_init */
	RK_U8 *end = &s->buf[s->enc_stream.byte_cnt];
	nal->i_payload = (RK_S32) (end - nal->p_payload);
	/*
	 * Assembly implementation of nal_escape reads past the end of the input.
	 * While undefined padding wouldn't actually affect the output,
	 * it makes valgrind unhappy.
	 */
	memset(end, 0xff, 64);
	out->nal_num++;
}

MPP_RET h265e_init_extra_info(void *extra_info)
{

	H265eExtraInfo *info = (H265eExtraInfo *) extra_info;
	// random ID number generated according to ISO-11578
	// NOTE: any element of h264e_sei_uuid should NOT be 0x00,
	// otherwise the string length of sei_buf will always be the distance between the
	// element 0x00 address and the sei_buf start address.
	static const RK_U8 h265e_sei_uuid[H265E_UUID_LENGTH] = {
		0x67, 0xfc, 0x6a, 0x3c, 0xd8, 0x5c, 0x44, 0x1e,
		0x87, 0xfb, 0x3f, 0xab, 0xec, 0xb3, 0x36, 0x77
	};

	h265e_nals_init(info);
	h265e_stream_init(&info->stream);

	info->sei_buf = mpp_calloc_size(RK_U8, H265E_SEI_BUF_SIZE);
	memcpy(info->sei_buf, h265e_sei_uuid, H265E_UUID_LENGTH);

	return MPP_OK;
}

MPP_RET h265e_deinit_extra_info(void *extra_info)
{
	H265eExtraInfo *info = (H265eExtraInfo *) extra_info;
	h265e_stream_deinit(&info->stream);
	h265e_nals_deinit(info);

	MPP_FREE(info->sei_buf);

	return MPP_OK;
}

MPP_RET h265e_set_extra_info(H265eCtx * ctx)
{
	H265eExtraInfo *info = (H265eExtraInfo *) ctx->extra_info;
	h265_sps *sps = &ctx->sps;
	h265_pps *pps = &ctx->pps;
	h265_vps *vps = &ctx->vps;

	h265e_dbg_func("enter\n");
	info->nal_num = 0;
	h265e_stream_reset(&info->stream);

	h265e_nal_start(info, NAL_VPS, H265_NAL_PRIORITY_HIGHEST);
	h265e_set_vps(ctx, vps);
	h265e_vps_write(vps, &info->stream);
	h265e_nal_end(info);

	h265e_nal_start(info, NAL_SPS, H265_NAL_PRIORITY_HIGHEST);
	h265e_set_sps(ctx, sps, vps);
	h265e_sps_write(sps, &info->stream);
	h265e_nal_end(info);

	h265e_nal_start(info, NAL_PPS, H265_NAL_PRIORITY_HIGHEST);
	h265e_set_pps(ctx, pps, sps);
	h265e_pps_write(pps, sps, &info->stream);
	h265e_nal_end(info);

	h265e_encapsulate_nals(info);

	h265e_dbg_func("leave\n");
	return MPP_OK;
}

RK_U32 h265e_data_to_sei(void *dst, RK_U8 uuid[16], const void *payload,
			 RK_S32 size)
{
	H265eNal sei_nal;
	H265eStream stream;
	RK_U8 *end = 0;

	h265e_dbg_func("enter\n");

	h265e_stream_init(&stream);
	memset(&sei_nal, 0, sizeof(H265eNal));

	sei_nal.i_type = NAL_SEI_PREFIX;
	sei_nal.p_payload = &stream.buf[stream.enc_stream.byte_cnt];

	h265e_sei_write(&stream, uuid, payload, size,
			H265_SEI_USER_DATA_UNREGISTERED);

	end = &stream.buf[stream.enc_stream.byte_cnt];
	sei_nal.i_payload = (RK_S32) (end - sei_nal.p_payload);

	h265e_nal_encode(dst, &sei_nal);

	h265e_stream_deinit(&stream);

	h265e_dbg_func("leave\n");
	return sei_nal.i_payload;
}

MPP_RET h265e_get_extra_info(H265eCtx * ctx, MppPacket pkt_out)
{
	RK_S32 k = 0;
	size_t offset = 0;
	H265eExtraInfo *src = NULL;

	if (pkt_out == NULL)
		return MPP_NOK;

	h265e_dbg_func("enter\n");

	src = (H265eExtraInfo *) ctx->extra_info;

	for (k = 0; k < src->nal_num; k++) {
		h265e_dbg(H265E_DBG_HEADER,
			  "get extra info nal type %d, size %d bytes",
			  src->nal[k].i_type, src->nal[k].i_payload);
		mpp_packet_write(pkt_out, offset, src->nal[k].p_payload,
				 src->nal[k].i_payload);
		offset += src->nal[k].i_payload;
	}
	mpp_packet_set_length(pkt_out, offset);

	h265e_dbg_func("leave\n");
	return MPP_OK;
}
