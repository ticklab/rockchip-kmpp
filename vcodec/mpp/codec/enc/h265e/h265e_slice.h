// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *
 *
 */
#ifndef __H265E_SLICE_H__
#define __H265E_SLICE_H__

#include "rk_type.h"
#include "mpp_err.h"
#include "h265e_dpb.h"
#include "h265_syntax.h"
#include "h265e_enctropy.h"
#include "h265e_context_table.h"
#include "rk_venc_ref.h"
#include "mpp_rc_defs.h"

#define MIN_PU_SIZE             4
#define MIN_TU_SIZE             4
#define MAX_NUM_SPU_W           (64 / MIN_PU_SIZE)	// maximum number of SPU in horizontal line

/*
 * For H.265 encoder slice header process.
 * Remove some syntax that encoder not supported.
 * Field, mbaff, B slice are not supported yet.
 */
typedef struct h265_dpb_frm_e h265_dpb_frm;

typedef struct DataCu_t {
	RK_U8 cu_size[256];
	RK_U8 cu_depth[256];
	RK_U32 pixel_x;
	RK_U32 pixel_y;
	RK_U32 mb_w;
	RK_U32 mb_h;
	RK_U32 cur_addr;
} DataCu;

typedef struct h265_reference_picture_set_e {
	RK_S32 delta_ridx_minus1;
	RK_S32 delta_rps;
	RK_S32 num_ref_idc;
	RK_S32 ref_idc[MAX_REFS + 1];

	// Parameters for long term references
	RK_U32 check_lt_msb[MAX_REFS];
	RK_S32 poc_lsblt[MAX_REFS];
	RK_S32 delta_poc_msb_cycle_lt[MAX_REFS];
	RK_U32 delta_poc_msb_present_flag[MAX_REFS];

	RK_S32 num_of_pictures;
	RK_S32 num_negative_pic;
	RK_S32 num_positive_pic;
	RK_S32 delta_poc[MAX_REFS];
	RK_U32 used[MAX_REFS];
	RK_U32 refed[MAX_REFS];
	RK_S32 poc[MAX_REFS];
	RK_S32 real_poc[MAX_REFS];

	RK_U32 inter_rps_prediction;
	RK_S32 num_long_term_pic;	// Zero when disabled
} h265_reference_picture_set;

typedef struct h265_sps_rps_list_e {
	RK_S32 num_short_term_ref_pic_sets;
	h265_reference_picture_set *m_referencePictureSets;
} h265_sps_rps_list;

typedef struct h265_rpl_modification_e {
	RK_U32 rpl_modification_flag_l0;
	RK_U32 rpl_modification_flag_l1;
	RK_U32 ref_pic_set_idx_l0[REF_PIC_LIST_NUM_IDX];
	RK_U32 ref_pic_set_idx_l1[REF_PIC_LIST_NUM_IDX];
} h265_rpl_modification;

typedef struct ProfileTierLevel_e {
	RK_U8 tier_flag;
	RK_U8 profile_compatibility_flag[32];
	RK_U8 general_progressive_source_flag;
	RK_U8 general_interlaced_source_flag;
	RK_U8 general_non_packed_constraint_flag;
	RK_U8 general_frame_only_constraint_flag;

	RK_S32 profile_idc;
	RK_S32 profile_space;
	RK_S32 general_level_idc;
} profile_tier_level;

typedef struct h265_profile_tier_level_e {
	profile_tier_level general_PTL;
	profile_tier_level sub_layer_PTL[6];	// max. value of max_sub_layers_minus1 is 6
	RK_U8 sub_layer_profile_present_flag[6];
	RK_U8 sub_layer_level_present_flag[6];
} h265_profile_tier_level;

typedef struct TimeingInfo_e {
	RK_U32 m_timingInfoPresentFlag;
	RK_U32 m_numUnitsInTick;
	RK_U32 m_timeScale;
	RK_U32 m_pocProportionalToTimingFlag;
	RK_U32 m_numTicksPocDiffOneMinus1;
} TimingInfo;

typedef struct h265_hrd_sublayer_set_e {
	RK_U8 fixed_pic_rate_general_flag;
	RK_U8 fixed_pic_rate_within_cvs_flag;
	RK_U8 low_delay_hrd_flag;
	RK_U32 elemental_duration_in_tc_minus1;
	RK_U32 cpb_cnt_minus1;
	RK_U32 bit_rate_value_minus1[MAX_CPB_CNT][2];
	RK_U32 bit_rate_du_value_minus1[MAX_CPB_CNT][2];
	RK_U32 cpb_size_value_minus1[MAX_CPB_CNT][2];
	RK_U32 cpb_size_du_value_minus1[MAX_CPB_CNT][2];
	RK_U32 cbr_flag[MAX_CPB_CNT][2];
} h265_hrd_sublayer_set;

typedef struct h265_hrd_param_set_e {
	RK_U8 nal_hrd_parameters_present_flag;
	RK_U8 vcl_hrd_parameters_present_flag;
	RK_U8 sub_pic_hrd_params_present_flag;
	RK_U8 sub_pic_cpb_params_in_pic_timing_sei_flag;
	RK_U32 tick_divisor_minus2;
	RK_U32 du_cpb_removal_delay_increment_length_minus1;
	RK_U32 dpb_output_delay_du_length_minus1;
	RK_U32 bit_rate_scale;
	RK_U32 cpb_size_scale;
	RK_U32 cpb_size_du_scale;
	RK_U32 initial_cpb_removal_delay_length_minus1;
	RK_U32 au_cpb_removal_delay_length_minus1;
	RK_U32 dpb_output_delay_length_minus1;
	h265_hrd_sublayer_set hrd_sublayer[MAX_SUB_LAYERS];
} h265_hrd_param_set;

typedef struct H265eVps_e {
	RK_S32 vps_video_parameter_set_id;
	RK_U32 vps_max_sub_layers_minus1;
	RK_U32 vps_temporal_id_nesting_flag;

	RK_U32 vps_num_reorder_pics[MAX_SUB_LAYERS];
	RK_U32 vps_max_dec_pic_buffering_minus1[MAX_SUB_LAYERS];
	RK_U32 vps_max_latency_increase_plus1[MAX_SUB_LAYERS];	// Really max latency increase plus 1 (value 0 expresses no limit)

	RK_U32 vps_num_hrd_parameters;
	RK_U32 vps_max_nuh_reserved_zero_layer_id;
	RK_U32 vps_max_op_sets_minus1;
	RK_U32
	    layer_id_included_flag[MAX_VPS_OP_SETS_PLUS1]
	    [MAX_VPS_NUH_RESERVED_ZERO_LAYER_ID_PLUS1];
	RK_U8 vps_timing_info_present_flag;
	RK_U32 vps_num_units_in_tick;
	RK_U32 vps_time_scale;
	RK_U8 vps_poc_proportional_to_timing_flag;
	RK_S32 vps_num_ticks_poc_diff_one_minus1;

	h265_hrd_param_set *hrd_parameters;
	h265_profile_tier_level profile_tier_level;
} h265_vps;

typedef struct h265_vui_param_set_e {
	RK_U8 aspect_ratio_info_present_flag;
	RK_U8 overscan_info_present_flag;
	RK_U8 overscan_appropriate_flag;
	RK_U8 video_signal_type_present_flag;
	RK_U8 video_full_range_flag;
	RK_U8 colour_description_present_flag;
	RK_U8 chroma_loc_info_present_flag;
	RK_U8 neutral_chroma_indication_flag;
	RK_U8 field_seq_flag;
	RK_U8 default_display_window_flag;
	RK_U8 vui_timing_info_present_flag;
	RK_U8 vui_poc_proportional_to_timing_flag;
	RK_U8 frame_field_info_present_flag;
	RK_U8 hrd_parameters_present_flag;
	RK_U8 bitstream_restriction_flag;
	RK_U8 tiles_fixed_structure_flag;
	RK_U8 motion_vectors_over_pic_boundaries_flag;
	RK_U8 restricted_ref_pic_lists_flag;
	RK_S32 aspect_ratio_idc;
	RK_S32 sar_width;
	RK_S32 sar_height;
	RK_S32 video_format;
	RK_S32 colour_primaries;
	RK_S32 transfer_characteristics;
	RK_S32 matrix_coefficients;
	RK_S32 chroma_sample_loc_type_top_field;
	RK_S32 chroma_sample_loc_type_bottom_field;
	RK_U32 def_disp_win_left_offset;
	RK_U32 def_disp_win_right_offset;
	RK_U32 def_disp_win_top_offset;
	RK_U32 def_disp_win_bottom_offset;
	RK_U32 vui_num_units_in_tick;
	RK_U32 vui_time_scale;
	RK_U32 vui_num_ticks_poc_diff_one_minus1;

	RK_S32 min_spatial_segmentation_idc;
	RK_S32 max_bytes_per_pic_denom;
	RK_S32 max_bits_per_mincu_denom;
	RK_S32 log2_max_mv_length_horizontal;
	RK_S32 log2_max_mv_length_vertical;
	h265_hrd_param_set hrd_param;
} h265_vui_param_set;

typedef struct H265eSps_e {
	RK_U8 separate_colour_plane_flag;
	RK_U8 long_term_ref_pics_present_flag;
	RK_U8 sps_temporal_mvp_enable_flag;
	RK_U8 amp_enabled_flag;
	RK_U8 pcm_enabled_flag;
	RK_U8 pcm_loop_filter_disable_flag;
	RK_U8 vps_temporal_id_nesting_flag;	// temporal_id_nesting_flag
	RK_U8 sps_strong_intra_smoothing_enable_flag;
	RK_U8 vui_parameters_present_flag;
	RK_U8 sample_adaptive_offset_enabled_flag;
	RK_U8 scaling_list_enabled_flag;
	RK_U8 sps_scaling_list_data_present_flag;
	RK_U8 used_by_curr_pic_lt_sps_flag[MAX_LSB_NUM];

	RK_S32 sps_seq_parameter_set_id;
	RK_S32 vps_video_parameter_set_id;
	RK_S32 chroma_format_idc;
	RK_U32 vps_max_sub_layers_minus1;	// maximum number of temporal layers

	// Structure
	RK_U32 pic_width_in_luma_samples;
	RK_U32 pic_height_in_luma_samples;

	RK_S32 log2_min_coding_block_size_minus3;
	RK_S32 log2_diff_max_min_coding_block_size;
	RK_U32 max_cu_size;
	RK_U32 max_cu_depth;
	RK_U32 add_cu_depth;

	RK_S32 vps_num_reorder_pics[MAX_SUB_LAYERS];
	// Tool list
	RK_U32 log2_diff_max_min_transform_block_size;
	RK_U32 log2_min_transform_block_size_minus2;
	RK_U32 max_transform_hierarchy_depth_inter;
	RK_U32 max_transform_hierarchy_depth_intra;
	RK_U32 pcm_log2_max_size;
	RK_U32 pcm_log2_min_size;

	// Parameter
	RK_S32 bit_depth_luma_minus8;
	RK_S32 bit_depth_chroma_minus8;
	RK_U32 pcm_bit_depth_luma;
	RK_U32 pcm_bit_depth_chroma;

	RK_U32 bits_for_poc;
	RK_U32 num_long_term_ref_pic_sps;
	RK_U32 lt_ref_pic_poc_lsb_sps[MAX_LSB_NUM];

	RK_U32 vps_max_dec_pic_buffering_minus1[MAX_SUB_LAYERS];
	RK_U32 vps_max_latency_increase_plus1[MAX_SUB_LAYERS];	// Really max latency increase plus 1 (value 0 expresses no limit)

	RK_U32 zscan2raster[MAX_NUM_SPU_W * MAX_NUM_SPU_W];
	RK_U32 raster2zscan[MAX_NUM_SPU_W * MAX_NUM_SPU_W];
	RK_U32 raster2pelx[MAX_NUM_SPU_W * MAX_NUM_SPU_W];
	RK_U32 raster2pely[MAX_NUM_SPU_W * MAX_NUM_SPU_W];

	h265_sps_rps_list rps_list;
	h265_vui_param_set vui;
	h265_profile_tier_level *profile_tier_level;
} h265_sps;

typedef struct H265ePps_e {

	RK_U8 cu_qp_delta_enabled_flag;
	RK_U8 constrained_intra_pred_flag;	// constrained_intra_pred_flag
	RK_U8 pps_slice_chroma_qp_offsets_present_flag;	// slicelevel_chroma_qp_flag
	RK_U8 slice_segment_header_extension_present_flag;
	RK_U8 deblocking_filter_control_present_flag;
	RK_U8 loop_filter_across_slices_enabled_flag;
	RK_U8 deblocking_filter_override_enabled_flag;
	RK_U8 pps_disable_deblocking_filter_flag;
	RK_U8 weighted_pred_flag;	// Use of Weighting Prediction (P_SLICE)
	RK_U8 weighted_bipred_flag;	// Use of Weighting Bi-Prediction (B_SLICE)
	RK_U8 output_flag_present_flag;	// Indicates the presence of output_flag in slice header
	RK_U8 loop_filter_across_tiles_enabled_flag;
	RK_U8 cabac_init_present_flag;
	RK_U8 sign_data_hiding_flag;
	RK_U8 tiles_enabled_flag;
	RK_U8 uniform_spacing_flag;
	RK_U8 sps_scaling_list_data_present_flag;
	RK_U8 lists_modification_present_flag;
	RK_U8 transquant_bypass_enable_flag;	// Indicates presence of cu_transquant_bypass_flag in CUs.
	RK_U8 transform_skip_enabled_flag;
	RK_U8 entropy_coding_sync_enabled_flag;	//!< Indicates the presence of wavefronts

	RK_S32 pps_pic_parameter_set_id;	// pic_parameter_set_id
	RK_S32 pps_seq_parameter_set_id;	// seq_parameter_set_id
	RK_S32 init_qp_minus26;

	// access channel
	RK_U32 diff_cu_qp_delta_depth;
	RK_S32 pps_cb_qp_offset;
	RK_S32 pps_cr_qp_offset;
	RK_U32 num_ref_idx_l0_default_active_minus1;
	RK_U32 num_ref_idx_l1_default_active_minus1;
	RK_S32 num_tile_columns_minus1;
	RK_S32 tile_column_width_array[33];
	RK_S32 num_tile_rows_minus1;
	RK_S32 tile_row_height_array[128];

	RK_S32 pps_beta_offset_div2;	//< beta offset for deblocking filter
	RK_S32 pps_tc_offset_div2;	//< tc offset for deblocking filter
	RK_U32 log2_parallel_merge_level_minus2;
	RK_S32 num_extra_slice_header_bits;
	h265_sps *sps;
} h265_pps;

typedef struct h265_slice_e {

	RK_U8 sao_enable_flag;
	RK_U8 sao_enable_flag_chroma;	///< SAO Cb&Cr enabled flag
	RK_U8 pic_output_flag;	///< pic_output_flag
	RK_U8 dependent_slice_segment_flag;
	RK_U8 pps_pic_parameter_set_id;	///< picture parameter set ID
	RK_U8 deblocking_filter_override_flag;	//< offsets for deblocking filter inherit from PPS
	RK_U8 deblocking_filter_disable;
	RK_U8 temporal_layer_non_ref_flag;
	RK_U8 loop_filter_across_slices_enabled_flag;
	RK_U8 tmprl_mvp_en;
	RK_U8 slice_reserved_flag;
	RK_U8 no_output_of_prior_pics_flag;
	RK_U8 col_from_l0_flag;	// collocated picture from List0 flag
	RK_U8 cabac_init_flag;
	RK_U8 ref_pic_list_modification_flag_l0;

	RK_S32 poc;
	RK_S32 gop_idx;
	RK_S32 last_idr;
	RK_S32 slice_qp;
	RK_S32 pps_beta_offset_div2;	//< beta offset for deblocking filter
	RK_S32 pps_tc_offset_div2;	//< tc offset for deblocking filter
	RK_S32 num_ref_idx[2];	//  for multiple reference of current slice

	//  Data
	RK_S32 slice_qp_delta_cb;
	RK_S32 slice_qp_delta_cr;
	RK_S32 ref_poc_list[2][MAX_REFS + 1];
	RK_U32 is_used_long_term[2][MAX_REFS + 1];

	// referenced slice?
	RK_U32 is_referenced;

	// access channel
	RK_U32 col_ref_idx;
	RK_U32 max_num_merge_cand;

//   TComScalingList* m_scalingList; //!< pointer of quantization matrix
	RK_U32 lmvd_l1_zero;

	RK_U32 slice_header_extension_length;
	RK_U32 lst_entry_l0;
	RK_U32 tot_poc_num;
	RK_U32 num_long_term_sps;
	RK_U32 num_long_term_pics;

	enum NALUnitType nal_unit_type;	///< Nal unit type for the slice
	SliceType slice_type;
	h265_sps *sps;
	h265_pps *pps;
	h265_vps *vps;
	h265_dpb_frm *ref_pic_list[2][MAX_REFS + 1];
	h265_reference_picture_set *rps;
	h265_reference_picture_set local_rps;
	h265_rpl_modification rpl_modification;
	h265_contex_model contex_models[MAX_OFF_CTX_MOD];
	h265_cabac_ctx cabac_ctx;
} h265_slice;

#ifdef  __cplusplus
extern "C" {
#endif

	void h265e_slice_set_ref_list(h265_dpb_frm * frame_list,
				      h265_slice * slice);
	void h265e_slice_set_ref_poc_list(h265_slice * slice);
	void h265e_slice_init(void *ctx, EncFrmStatus curr);
	RK_S32 h265e_code_slice_skip_frame(void *ctx, h265_slice * slice,
					   RK_U8 * buf, RK_S32 len);

#ifdef __cplusplus
}
#endif
#endif				/* __H265E_SLICE_H__ */
