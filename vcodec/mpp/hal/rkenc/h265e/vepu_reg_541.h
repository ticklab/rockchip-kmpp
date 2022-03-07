#ifndef     _VEPU_REG_H_
#define     _VEPU_REG_H_
//#include "macro.h"
#define CHRM_LAMBADA_REG_NUM         (24)

/* OSD position */ 
    typedef struct {
	unsigned int lt_pos_x:8;	/* left-top */
	unsigned int lt_pos_y:8;
	unsigned int rb_pos_x:8;	/* right-bottom */
	unsigned int rb_pos_y:8;
} OsdPos;
 
/* OSD palette */ 
    typedef struct {
	unsigned int y:8;
	unsigned int u:8;
	unsigned int v:8;
	unsigned int alpha:8;
} OsdPlt;
 typedef struct {
	
	    /* 0x0 - VERSION, swreg01 */ 
	    struct {
		unsigned int rkvenc_ver:32;	//default : 0x0000_0001
	} version;
	
	    /* 0x4 - swreg02, ENC_STRT */ 
	    struct {
		unsigned int lkt_num:8;
		unsigned int rkvenc_cmd:2;
		unsigned int reserve:6;
		unsigned int enc_cke:1;
		unsigned int Reserve:15;
	} enc_strt;
	
	    /* 0x8 - ENC_CLR */ 
	    struct {
		unsigned int safe_clr:1;
		unsigned int force_clr:1;
		unsigned int reserve:30;
	} enc_clr;
	
	    /* 0xc - swreg04, LKT_ADDR */ 
	    struct {
		unsigned int lkt_addr:32;
	} lkt_addr;
	
	    /* 0x10 - swreg05, INT_EN */ 
	    struct {
		unsigned int ofe_fnsh:1;
		unsigned int lkt_fnsh:1;
		unsigned int clr_fnsh:1;
		unsigned int ose_fnsh:1;
		unsigned int bs_ovflr:1;
		unsigned int brsp_ful:1;
		unsigned int brsp_err:1;
		unsigned int rrsp_err:1;
		unsigned int tmt_err:1;
		unsigned int reserve:23;
	} int_en;
	struct {
		unsigned int reserve:32;
	} int_msk;		//swreg06, INT_MSK
	struct {
		unsigned int clr_ofe_fnsh:1;
		unsigned int clr_lkt_fnsh:1;
		unsigned int clr_clr_fnsh:1;
		unsigned int clr_ose_fnsh:1;
		unsigned int clr_bs_ovflr:1;
		unsigned int clr_brsp_ful:1;
		unsigned int clr_brsp_err:1;
		unsigned int clr_rrsp_err:1;
		unsigned int clr_tmt_err:1;
		unsigned int reserved:23;
	} int_clr;		//swreg07, INT_CLR
	
	    /* 0x1C - swreg08, INT_STA */ 
	    struct {
		unsigned int reserve:32;
	} int_stus;
	unsigned int reserved_0x20_0x2c[4];
	
	    /* 0x30 - swreg09, ENC_RSL */ 
	    struct {
		unsigned int pic_wd8_m1:9;
		unsigned int reserve0:1;
		unsigned int pic_wfill:6;
		unsigned int pic_hd8_m1:9;
		unsigned int reserve1:1;
		unsigned int pic_hfill:6;
	} enc_rsl;
	
	    /* 0x34 - ENC_PIC */ 
	    struct {
		unsigned int enc_stnd:1;
		unsigned int roi_enc:1;
		unsigned int cur_frm_ref:1;
		unsigned int mei_stor:1;
		unsigned int bs_scp:1;
		unsigned int lamb_mod_sel:1;
		unsigned int reserved:2;
		unsigned int pic_qp:6;
		unsigned int tot_poc_num:5;
		unsigned int log2_ctu_num:4;
		unsigned int reserve1:7;
		unsigned int slen_fifo:1;
		unsigned int node_int:1;
	} enc_pic;
	struct {
		unsigned int ppln_enc_lmt:4;
		unsigned int reserve:4;
		unsigned int rfp_load_thrd:8;
		unsigned int reserve1:16;
	} enc_wdg;		//swreg11, ENC_WDG
	
	    /* 0x3c - DTRNS_MAP */ 
	    struct {
		unsigned int src_bus_ordr:1;	/* reserved bit */
		unsigned int cmvw_bus_ordr:1;
		unsigned int dspw_bus_ordr:1;
		unsigned int rfpw_bus_ordr:1;
		unsigned int src_bus_edin:4;
		unsigned int meiw_bus_edin:4;
		unsigned int bsw_bus_edin:3;
		unsigned int lktr_bus_edin:4;
		unsigned int ctur_bus_edin:4;
		unsigned int lktw_bus_edin:4;
		unsigned int pp_burst_size:1;
		unsigned int reserved:4;
	} dtrns_map;
	struct {
		unsigned int axi_brsp_cke:7;
		unsigned int cime_dspw_orsd:1;
		unsigned int reserve:24;
	} dtrns_cfg;		// swreg13, DTRNS_CFG
	
	    /* 0x44 - SRC_FMT */ 
	    struct {
		unsigned int src_aswap:1;
		unsigned int src_cswap:1;
		unsigned int src_cfmt:4;
		unsigned int range:1;
		unsigned int reserve:25;
	} src_fmt;
	
	    /* 0x48 - SRC_UDFY */ 
	    struct {
		int wght_b2y:9;
		int wght_g2y:9;
		int wght_r2y:9;
		int reserved:5;
	} src_udfy;
	
	    /* 0x4c - SRC_UDFU */ 
	    struct {
		int wght_b2u:9;
		int wght_g2u:9;
		int wght_r2u:9;
		int reserved:5;
	} src_udfu;
	
	    /* 0x50 - SRC_UDFV */ 
	    struct {
		int wght_b2v:9;
		int wght_g2v:9;
		int wght_r2v:9;
		int reserved:5;
	} src_udfv;
	
	    /* 0x54 - SRC_UDFO */ 
	    struct {
		unsigned int ofst_rgb2v:8;
		unsigned int ofst_rgb2u:8;
		unsigned int ofst_rgb2y:5;
		unsigned int reserve:11;
	} src_udfo;
	
	    /* 0x58 - SRC_PROC */ 
	    struct {
		unsigned int reserved0:26;
		unsigned int src_mirr:1;
		unsigned int src_rot:2;
		unsigned int txa_en:1;
		unsigned int fbd_en:1;
		unsigned int reserved1:1;
	} src_proc;
	
	    /* 0x5c - MMU0_DTE_ADDR */ 
	unsigned int mmu0_dte_addr;
	
	    /* 0x60 - MMU1_DTE_ADDR */ 
	unsigned int mmu1_dte_addr;
	
	    /* 0x64 - KLUT_OFST */ 
	    struct {
		unsigned int chrm_kult_ofst:3;
		unsigned int reserved:29;
	} klut_ofst;
	
	    /* 0x68-0xc4 - KLUT_WGT */ 
	    struct {
		unsigned int chrm_klut_wgt0:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt1:9;
	} klut_wgt0;
	struct {
		unsigned int chrm_klut_wgt1:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt2:18;
	} klut_wgt1;
	struct {
		unsigned int chrm_klut_wgt3:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt4:9;
	} klut_wgt2;
	struct {
		unsigned int chrm_klut_wgt4:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt5:18;
	} klut_wgt3;
	struct {
		unsigned int chrm_klut_wgt6:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt7:9;
	} klut_wgt4;
	struct {
		unsigned int chrm_klut_wgt7:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt8:18;
	} klut_wgt5;
	struct {
		unsigned int chrm_klut_wgt9:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt10:9;
	} klut_wgt6;
	struct {
		unsigned int chrm_klut_wgt10:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt11:18;
	} klut_wgt7;
	struct {
		unsigned int chrm_klut_wgt12:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt13:9;
	} klut_wgt8;
	struct {
		unsigned int chrm_klut_wgt13:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt14:18;
	} klut_wgt9;
	struct {
		unsigned int chrm_klut_wgt15:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt16:9;
	} klut_wgt10;
	struct {
		unsigned int chrm_klut_wgt16:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt17:18;
	} klut_wgt11;
	struct {
		unsigned int chrm_klut_wgt18:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt19:9;
	} klut_wgt12;
	struct {
		unsigned int chrm_klut_wgt19:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt20:18;
	} klut_wgt13;
	struct {
		unsigned int chrm_klut_wgt21:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt22:9;
	} klut_wgt14;
	struct {
		unsigned int chrm_klut_wgt22:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt23:18;
	} klut_wgt15;
	struct {
		unsigned int chrm_klut_wgt24:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt25:9;
	} klut_wgt16;
	struct {
		unsigned int chrm_klut_wgt25:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt26:18;
	} klut_wgt17;
	struct {
		unsigned int chrm_klut_wgt27:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt28:9;
	} klut_wgt18;
	struct {
		unsigned int chrm_klut_wgt28:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt29:18;
	} klut_wgt19;
	struct {
		unsigned int chrm_klut_wgt30:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt31:9;
	} klut_wgt20;
	struct {
		unsigned int chrm_klut_wgt31:9;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt32:18;
	} klut_wgt21;
	struct {
		unsigned int chrm_klut_wgt33:18;
		unsigned int reserved:5;
		unsigned int chrm_klut_wgt34:9;
	} klut_wgt22;
	
	    /* 0xc4 - klut_wgt23 */ 
	    struct {
		unsigned int chrm_klut_wgt34:9;
		unsigned int reserved:23;
	} klut_wgt23;
	
	    /* 0xc8 - RC_CFG */ 
	    struct {
		unsigned int rc_en:1;
		unsigned int aqmode_en:1;
		unsigned int qp_mode:1;
		unsigned int reserved:13;
		unsigned int rc_ctu_num:16;
	} rc_cfg;
	
	    /* 0xcc - RC_QP */ 
	    struct {
		unsigned int reserved:16;
		unsigned int rc_qp_range:4;
		unsigned int rc_max_qp:6;
		unsigned int rc_min_qp:6;
	} rc_qp;
	
	    /* 0xd0 - swreg55, RC_TGT */ 
	    struct {
		unsigned int ctu_ebits:20;
		unsigned int reserve:12;
	} rc_tgt;
	
	    /* 0xd4 - RC_ADJ0 */ 
	    struct {
		int qp_adjust0:5;
		int qp_adjust1:5;
		int qp_adjust2:5;
		int qp_adjust3:5;
		int qp_adjust4:5;
		int reserved:7;
	} rc_adj0;
	
	    /* 0xd8 - RCADJ1 */ 
	    struct {
		int qp_adjust5:5;
		int qp_adjust6:5;
		int qp_adjust7:5;
		int qp_adjust8:5;
		int reserved:12;
	} rc_adj1;
	
	    /* 0xdc-0xfc swreg47, */ 
	    struct {
		int bits_error0;
	} rc_erp0;		//RC_ERP0
	struct {
		int bits_error1;
	} rc_erp1;		//swreg48, RC_ERP1
	struct {
		int bits_error2;
	} rc_erp2;		//swreg49, RC_ERP2
	struct {
		int bits_error3;
	} rc_erp3;		//swreg50, RC_ERP3
	struct {
		int bits_error4;
	} rc_erp4;		//swreg51, RC_ERP4
	
	    /* 0xec-0xf8 */ 
	    struct {
		int bits_error5;
	} rc_erp5;
	struct {
		int bits_error6;
	} rc_erp6;
	struct {
		int bits_error7;
	} rc_erp7;
	
	    /* 0xfc - RC_ERP8 */ 
	    struct {
		int bits_error8;
	} rc_erp8;
	
	    /* 0x100 - QPMAP0 */ 
	    struct {
		unsigned int qpmin_area0:6;
		unsigned int qpmax_area0:6;
		unsigned int qpmin_area1:6;
		unsigned int qpmax_area1:6;
		unsigned int qpmin_area2:6;
		unsigned int reserved:2;
	} qpmap0;
	
	    /* 0x104 - QPMAP1 */ 
	    struct {
		unsigned int qpmax_area2:6;
		unsigned int qpmin_area3:6;
		unsigned int qpmax_area3:6;
		unsigned int qpmin_area4:6;
		unsigned int qpmax_area4:6;
		unsigned int reserved:2;
	} qpmap1;
	
	    /* 0x108 - QPMAP2 */ 
	    struct {
		unsigned int qpmin_area5:6;
		unsigned int qpmax_area5:6;
		unsigned int qpmin_area6:6;
		unsigned int qpmax_area6:6;
		unsigned int qpmin_area7:6;
		unsigned int reserved:2;
	} qpmap2;
	
	    /* 0x10c - QPMAP3 */ 
	    struct {
		unsigned int qpmax_area7:6;
		unsigned int reserved:24;
		unsigned int qpmap_mode:2;
	} qpmap3;
	
	    /* 0x110 - PIC_OFST */ 
	    struct {
		unsigned int pic_ofst_y:13;
		unsigned int reserved0:3;
		unsigned int pic_ofst_x:13;
		unsigned int reserved1:3;
	} pic_ofst;
	
	    /* 0x114 - swreg23, SRC_STRID */ 
	    struct {
		unsigned int src_ystrid:16;
		unsigned int src_cstrid:16;
	} src_strid;
	unsigned int adr_srcy_hevc;	/* 0x118 */
	unsigned int adr_srcu_hevc;
	unsigned int adr_srcv_hevc;
	unsigned int ctuc_addr_hevc;
	unsigned int rfpw_h_addr_hevc;
	unsigned int rfpw_b_addr_hevc;
	unsigned int rfpr_h_addr_hevc;
	unsigned int rfpr_b_addr_hevc;
	unsigned int cmvw_addr_hevc;
	unsigned int cmvr_addr_hevc;
	unsigned int dspw_addr_hevc;
	unsigned int dspr_addr_hevc;
	unsigned int meiw_addr_hevc;
	unsigned int bsbt_addr_hevc;
	unsigned int bsbb_addr_hevc;
	unsigned int bsbr_addr_hevc;
	unsigned int bsbw_addr_hevc;	/* 0x158 */
	
	    /* 0x15c - swreg41, SLI_SPL */ 
	    struct {
		unsigned int sli_cut:1;
		unsigned int sli_cut_mode:1;
		unsigned int sli_cut_bmod:1;
		unsigned int sli_max_num_m1:10;
		unsigned int sli_out_mode:1;
		unsigned int reserve:2;
		unsigned int sli_cut_cnum_m1:16;
	} sli_spl;
	
	    /* 0x160 - swreg42, SLI_SPL_BYTE */ 
	    struct {
		unsigned int sli_cut_byte:18;
		unsigned int reserve:14;
	} sli_spl_byte;
	
	    /* 0x164 - swreg43, ME_RNGE */ 
	    struct {
		unsigned int cime_srch_h:4;
		unsigned int cime_srch_v:4;
		unsigned int rime_srch_h:3;
		unsigned int rime_srch_v:3;
		unsigned int reserved:2;
		unsigned int dlt_frm_num:16;
	} me_rnge;
	
	    /* 0x168 - swreg44, ME_CNST */ 
	    struct {
		unsigned int pmv_mdst_h:8;
		unsigned int pmv_mdst_v:8;
		unsigned int mv_limit:2;
		unsigned int mv_num:2;
		unsigned int colmv_store:1;
		unsigned int colmv_load:1;
		unsigned int rime_dis_en:5;	/* used for rtl debug */
		unsigned int fme_dis_en:5;	/* used for rtl debug */
	} me_cnst;
	
	    /* 0x16c - ME_RAM */ 
	    struct {
		unsigned int cime_rama_max:11;
		unsigned int cime_rama_h:5;
		unsigned int cach_l2_tag:2;
		unsigned int reserved:14;
	} me_ram;
	
	    /*0x170 - SYNT_REF_MARK4 */ 
	    struct {
		unsigned int poc_lsb_lt1:16;
		unsigned int poc_lsb_lt2:16;
	} synt_ref_mark4;
	
	    /*0x174 SYNT_REF_MARK5 */ 
	    struct {
		unsigned int dlt_poc_msb_cycl1:16;
		unsigned int dlt_poc_msb_cycl2:16;
	} synt_ref_mark5;
	unsigned int reserved_0x178_0x190[7];
	
	    /* 0x194 - REG_THD, reserved */ 
	unsigned int reg_thd;
	
	    /* 0x198 - swreg56, RDO_CFG */ 
	    struct {
		unsigned int ltm_col:1;
		unsigned int ltm_idx0l0:1;
		unsigned int chrm_special:1;
		unsigned int rdoq_en:1;	/* may be used in the future */
		unsigned int reserved0:2;
		unsigned int cu_inter_en:4;
		unsigned int reserved1:9;
		unsigned int cu_intra_en:4;
		unsigned int chrm_klut_en:1;
		unsigned int seq_scaling_matrix_present_flg:1;
		unsigned int atf_p_en:1;
		unsigned int atf_i_en:1;
		unsigned int reserved2:5;
	} rdo_cfg;
	
	    /* 0x19c - swreg57, SYNT_NAL */ 
	    struct {
		unsigned int nal_unit_type:6;
		unsigned int reserve:26;
	} synt_nal;
	
	    /* 0x1a0 - swreg58, SYNT_SPS */ 
	    struct {
		unsigned int smpl_adpt_ofst_en:1;
		unsigned int num_st_ref_pic:7;
		unsigned int lt_ref_pic_prsnt:1;
		unsigned int num_lt_ref_pic:6;
		unsigned int tmpl_mvp_en:1;
		unsigned int log2_max_poc_lsb:4;
		unsigned int strg_intra_smth:1;
		unsigned int reserved:11;
	} synt_sps;
	
	    /* 0x1a4 - swreg59, SYNT_PPS */ 
	    struct {
		unsigned int dpdnt_sli_seg_en:1;
		unsigned int out_flg_prsnt_flg:1;
		unsigned int num_extr_sli_hdr:3;
		unsigned int sgn_dat_hid_en:1;
		unsigned int cbc_init_prsnt_flg:1;
		unsigned int pic_init_qp:6;
		unsigned int cu_qp_dlt_en:1;
		unsigned int chrm_qp_ofst_prsn:1;
		unsigned int lp_fltr_acrs_sli:1;
		unsigned int dblk_fltr_ovrd_en:1;
		unsigned int lst_mdfy_prsnt_flg:1;
		unsigned int sli_seg_hdr_extn:1;
		unsigned int cu_qp_dlt_depth:2;
		unsigned int reserved:11;
	} synt_pps;
	
	    /* 0x1a8 - swreg60, SYNT_SLI0 */ 
	    struct {
		unsigned int cbc_init_flg:1;
		unsigned int mvd_l1_zero_flg:1;
		unsigned int merge_up_flag:1;
		unsigned int merge_left_flag:1;
		unsigned int reserved:1;
		unsigned int ref_pic_lst_mdf_l0:1;
		unsigned int num_refidx_l1_act:2;
		unsigned int num_refidx_l0_act:2;
		unsigned int num_refidx_act_ovrd:1;
		unsigned int sli_sao_chrm_flg:1;
		unsigned int sli_sao_luma_flg:1;
		unsigned int sli_tmprl_mvp_en:1;
		unsigned int pic_out_flg:1;
		unsigned int sli_type:2;
		unsigned int sli_rsrv_flg:7;
		unsigned int dpdnt_sli_seg_flg:1;
		unsigned int sli_pps_id:6;
		unsigned int no_out_pri_pic:1;
	} synt_sli0;
	
	    /* 0x1ac - swreg61, SYNT_SLI1 */ 
	    struct {
		int sli_tc_ofst_div2:4;
		int sli_beta_ofst_div2:4;
		unsigned int sli_lp_fltr_acrs_sli:1;
		unsigned int sli_dblk_fltr_dis:1;
		unsigned int dblk_fltr_ovrd_flg:1;
		int sli_cb_qp_ofst:5;
		unsigned int sli_qp:6;
		unsigned int fivm_max_mrg_cnd:3;
		unsigned int col_ref_idx:1;
		unsigned int col_frm_l0_flg:1;
		unsigned int lst_entry_l0:4;
		unsigned int reserved:1;
	} synt_sli1;
	
	    /* 0x1b0 - swreg62, SYNT_SLI2_RODR */ 
	    struct {
		unsigned int sli_poc_lsb:16;
		unsigned int sli_hdr_ext_len:9;
		unsigned int reserve:7;
	} synt_sli2_rodr;
	
	    /* 0x1b4 - swreg63, SYNT_REF_MARK0 */ 
	    struct {
		unsigned int st_ref_pic_flg:1;
		unsigned int poc_lsb_lt0:16;
		unsigned int lt_idx_sps:5;
		unsigned int num_lt_pic:2;
		unsigned int st_ref_pic_idx:6;
		unsigned int num_lt_sps:2;
	} synt_ref_mark0;
	
	    /* 0x1b8 - swreg64, SYNT_REF_MARK1 */ 
	    struct {
		unsigned int used_by_s0_flg:4;
		unsigned int num_pos_pic:1;
		unsigned int num_neg_pic:5;
		unsigned int dlt_poc_msb_cycl0:16;
		unsigned int dlt_poc_msb_prsnt0:1;
		unsigned int dlt_poc_msb_prsnt1:1;
		unsigned int dlt_poc_msb_prsnt2:1;
		unsigned int used_by_lt_flg0:1;
		unsigned int used_by_lt_flg1:1;
		unsigned int used_by_lt_flg2:1;
	} synt_ref_mark1;
	unsigned int reserved_0x1bc;	/* not used for a long time */
	
	    /* 0x1c0 - OSD_CFG */ 
	    struct {
		unsigned int osd_en:8;
		unsigned int osd_inv:8;
		unsigned int osd_plt_cks:1;
		unsigned int osd_plt_type:1;
		unsigned int reserved:14;
	} osd_cfg;
	
	    /* 0x1c4 - OSD_INV */ 
	    struct {
		unsigned int osd_inv_r0:4;
		unsigned int osd_inv_r1:4;
		unsigned int osd_inv_r2:4;
		unsigned int osd_inv_r3:4;
		unsigned int osd_inv_r4:4;
		unsigned int osd_inv_r5:4;
		unsigned int osd_inv_r6:4;
		unsigned int osd_inv_r7:4;
	} osd_inv;
	
	    /* 0x1c8 - SYNT_REF_MARK2 */ 
	    struct {
		unsigned int dlt_poc_s0_m10:16;
		unsigned int dlt_poc_s0_m11:16;
	} synt_ref_mark2;
	
	    /* 0x1cc - SYNT_REF_MARK3 */ 
	    struct {
		unsigned int dlt_poc_s0_m12:16;
		unsigned int dlt_poc_s0_m13:16;
	} synt_ref_mark3;
	
	    /* 0x1d0-0x1ec - OSD_POS0-OSD_POS7 */ 
	 OsdPos osd_pos[8];
	
	    /* 0x1f0-0x20c - OSD_ADDR0-OSD_ADDR7 */ 
	unsigned int osd_addr[8];
	
	    /* 0x210 - ST_BSL */ 
	    struct {
		unsigned int bs_lgth:32;
	} st_bsl;
	
	    /* 0x214 - ST_SSE_L32 */ 
	    struct {
		unsigned int sse_l32:32;
	} st_sse_l32;
	
	    /* 0x218 - ST_SSE_QP */ 
	    struct {
		unsigned int qp_sum:24;	/* sum of valid CU8x8s' QP */
		unsigned int sse_h8:8;
	} st_sse_qp;
	
	    /* 0x21c - ST_SAO */ 
	    struct {
		unsigned int slice_scnum:12;
		unsigned int slice_slnum:12;
		unsigned int reserve:8;
	} st_sao;
	
	    /* 0x220 - MMU0_STA, used by hardware?? */ 
	unsigned int mmu0_sta;
	unsigned int mmu1_sta;
	
	    /* 0x228 - ST_ENC */ 
	    struct {
		unsigned int st_enc:2;
		unsigned int axiw_cln:2;
		unsigned int axir_cln:2;
		unsigned int reserve:26;
	} st_enc;
	
	    /* 0x22c - ST_LKT */ 
	    struct {
		unsigned int fnum_enc:8;
		unsigned int fnum_cfg:8;
		unsigned int fnum_int:8;
		unsigned int reserve:8;
	} st_lkt;
	
	    /* 0x230 - ST_NOD */ 
	    struct {
		unsigned int node_addr:32;
	} st_nod;
	
	    /* 0x234 - ST_BSB */ 
	    struct {
		unsigned int Bsbw_ovfl:1;
		unsigned int reserve:2;
		unsigned int bsbw_addr:29;
	} st_bsb;
	
	    /* 0x238 - ST_DTRNS */ 
	    struct {
		unsigned int axib_idl:7;
		unsigned int axib_ful:7;
		unsigned int axib_err:7;
		unsigned int axir_err:6;
		unsigned int reserve:5;
	} st_dtrns;
	
	    /* 0x23c - ST_SNUM */ 
	    struct {
		unsigned int slice_num:6;
		unsigned int reserve:26;
	} st_snum;
	
	    /* 0x240 - ST_SLEN */ 
	    struct {
		unsigned int slice_len:23;
		unsigned int reserve:9;
	} st_slen;
	
	    /* 0x244-0x340 - debug registers
	     * ST_LVL64_INTER_NUM etc.
	     */ 
	unsigned int st_lvl64_inter_num;
	unsigned int st_lvl32_inter_num;
	unsigned int st_lvl16_inter_num;
	unsigned int st_lvl8_inter_num;
	unsigned int st_lvl32_intra_num;
	unsigned int st_lvl16_intra_num;
	unsigned int st_lvl8_intra_num;
	unsigned int st_lvl4_intra_num;
	unsigned int st_cu_num_qp[52];
	unsigned int st_madp;
	unsigned int st_ctu_num;	/* used for MADP calculation */
	unsigned int st_madi;
	unsigned int st_mb_num;	/* used for MADI calculation */
	unsigned int reserved_0x340_0x34c[4];
	
	    /* 0x350-0x38c - MMU registers
	     * MMU0_CMD etc.
	     */ 
	unsigned int reserved_0x350_0x38c[16];
	
	    /* 0x3f0-0x3f8 - L2_ADDR, L2_WDATA, L2_RDATA */ 
	unsigned int l2_addr;
	unsigned int l2_wdata;
	unsigned int l2_rdata;
	unsigned int reserved_0x3fc;
	
	    /* 0x400-0x7fc */ 
	 OsdPlt osd_plt[256];
	
	    /* 0x800 - AXIP0_CMD */ 
	    struct {
		unsigned int axip0_work:1;
		unsigned int axip0_clr:1;
		unsigned int axip0_frm:1;
		unsigned int reserve:29;
	} swreg81;
	
	    /* 0x804 - AXIP0_LTCY */ 
	    struct {
		unsigned int axip0_ltcy_id:4;
		unsigned int axip0_ltcy_thr:12;
		unsigned int reserve:16;
	} swreg82;
	
	    /* 0x808 - AXIP0_CNT */ 
	    struct {
		unsigned int axip0_cnt_type:1;
		unsigned int axip0_cnt_ddr:2;
		unsigned int axip0_cnt_rid:5;
		unsigned int axip0_cnt_wid:5;
		unsigned int reserve:19;
	} swreg83;
	unsigned int reserved_0x80c;
	
	    /* 0x810 - AXIP1_CMD */ 
	    struct {
		unsigned int axip1_work:1;
		unsigned int axip1_clr:1;
		unsigned int reserve:30;
	} swreg84;
	
	    /* 0x814 - AXIP1_LTCY */ 
	    struct {
		unsigned int axip1_ltcy_id:4;
		unsigned int axip1_ltcy_thr:12;
		unsigned int reserve:16;
	} swreg85;
	
	    /* 0x818 - AXIP1_CNT */ 
	    struct {
		unsigned int axip1_cnt_type:1;
		unsigned int axip1_cnt_ddr:2;
		unsigned int axip1_cnt_rid:5;
		unsigned int axip1_cnt_wid:5;
		unsigned int reserve:19;
	} swreg86;
	unsigned int reserved_0x81c;
	
	    /* 0x820 - ST_AXIP0_MAXL */ 
	    struct {
		unsigned int axip0_cnt_type:16;
		unsigned int reserve:16;
	} swreg87;
	
	    /* 0x824 - ST_AXIP0_NUML */ 
	    struct {
		unsigned int axip0_num_ltcy:32;
	} swreg88;
	
	    /* 0x828 - ST_AXIP0_SUML */ 
	    struct {
		unsigned int axip0_sum_ltcy:32;
	} swreg89;
	
	    /* 0x82c - ST_AXIP0_RDB */ 
	    struct {
		unsigned int axip0_byte_rd:32;
	} swreg90;
	
	    /* 0x830 - ST_AXIP0_WRB */ 
	    struct {
		unsigned int axip0_byte_wr:32;
	} swreg91;
	
	    /* 0x834 - ST_AXIP0_CYCL */ 
	    struct {
		unsigned int axip0_wrk_cyc:32;
	} swreg92;
	unsigned int reserved_0x838_0x83c[2];
	
	    /* 0x840 - ST_AXIP1_MAXL */ 
	    struct {
		unsigned int axip1_cnt_type:16;
		unsigned int reserve:16;
	} swreg93;
	
	    /* 0x844 - ST_AXIP1_NUML */ 
	    struct {
		unsigned int axip1_num_ltcy:32;
	} swreg94;
	
	    /* 0x848 - ST_AXIP1_SUML */ 
	    struct {
		unsigned int axip1_sum_ltcy:32;
	} swreg95;
	
	    /* 0x84c - ST_AXIP1_RDB */ 
	    struct {
		unsigned int axip1_byte_rd:32;
	} swreg96;
	
	    /* 0x850 - ST_AXIP1_WRB */ 
	    struct {
		unsigned int axip1_byte_wr:32;
	} swreg97;
	
	    /* 0x854 - ST_AXIP1_CYCL */ 
	    struct {
		unsigned int axip1_wrk_cyc:32;
	} swreg98;
	
	    /* 0x858 - 0xc20 */ 
	
	    /* L2 Register: 0x4 */ 
	    struct {
		unsigned int lvl32_intra_cst_thd0:12;
		unsigned int reserved0:4;
		unsigned int lvl32_intra_cst_thd1:12;
		unsigned int reserved1:4;
	} lvl32_intra_CST_THD0;
	struct {
		unsigned int lvl32_intra_cst_thd2:12;
		unsigned int reserved0:4;
		unsigned int lvl32_intra_cst_thd3:12;
		unsigned int reserved1:4;
	} lvl32_intra_CST_THD1;
	struct {
		unsigned int lvl16_intra_cst_thd0:12;
		unsigned int reserved0:4;
		unsigned int lvl16_intra_cst_thd1:12;
		unsigned int reserved1:4;
	} lvl16_intra_CST_THD0;
	struct {
		unsigned int lvl16_intra_cst_thd2:12;
		unsigned int reserved0:4;
		unsigned int lvl16_intra_cst_thd3:12;
		unsigned int reserved1:4;
	} lvl16_intra_CST_THD1;
	
	    /* 0x14-0x1c - reserved */ 
	unsigned int lvl8_intra_CST_THD0;
	unsigned int lvl8_intra_CST_THD1;
	unsigned int lvl16_intra_UL_CST_THD;
	struct {
		unsigned int lvl32_intra_cst_wgt0:8;
		unsigned int lvl32_intra_cst_wgt1:8;
		unsigned int lvl32_intra_cst_wgt2:8;
		unsigned int lvl32_intra_cst_wgt3:8;
	} lvl32_intra_CST_WGT0;
	struct {
		unsigned int lvl32_intra_cst_wgt4:8;
		unsigned int lvl32_intra_cst_wgt5:8;
		unsigned int lvl32_intra_cst_wgt6:8;
		unsigned int reserved2:8;
	} lvl32_intra_CST_WGT1;
	struct {
		unsigned int lvl16_intra_cst_wgt0:8;
		unsigned int lvl16_intra_cst_wgt1:8;
		unsigned int lvl16_intra_cst_wgt2:8;
		unsigned int lvl16_intra_cst_wgt3:8;
	} lvl16_intra_CST_WGT0;
	struct {
		unsigned int lvl16_intra_cst_wgt4:8;
		unsigned int lvl16_intra_cst_wgt5:8;
		unsigned int lvl16_intra_cst_wgt6:8;
		unsigned int reserved2:8;
	} lvl16_intra_CST_WGT1;
	
	    /* 0x30 - RDO_QUANT */ 
	    struct {
		unsigned int quant_f_bias_I:10;
		unsigned int quant_f_bias_P:10;
		unsigned int reserved:12;
	} rdo_quant;
	
	    /* 0x34 - ATR_THD0, reserved */ 
	    struct {
		unsigned int atr_thd0:12;
		unsigned int reserved0:4;
		unsigned int atr_thd1:12;
		unsigned int reserved1:4;
	} atr_thd0;
	
	    /* 0x38 - ATR_THD1, reserved */ 
	    struct {
		unsigned int atr_thd2:12;
		unsigned int reserved0:4;
		unsigned int atr_thdqp:6;
		unsigned int reserved1:10;
	} atr_thd1;
	
	    /* 0x3c - Lvl16_ATR_WGT, reserved */ 
	    struct {
		unsigned int lvl16_atr_wgt0:8;
		unsigned int lvl16_atr_wgt1:8;
		unsigned int lvl16_atr_wgt2:8;
		unsigned int reserved:8;
	} lvl16_atr_wgt;
	
	    /* 0x40 - Lvl8_ATR_WGT, reserved */ 
	    struct {
		unsigned int lvl8_atr_wgt0:8;
		unsigned int lvl8_atr_wgt1:8;
		unsigned int lvl8_atr_wgt2:8;
		unsigned int reserved:8;
	} lvl8_atr_wgt;
	
	    /* 0x44 - Lvl4_ATR_WGT, reserved */ 
	    struct {
		unsigned int lvl4_atr_wgt0:8;
		unsigned int lvl4_atr_wgt1:8;
		unsigned int lvl4_atr_wgt2:8;
		unsigned int reserved:8;
	} lvl4_atr_wgt;
	
	    /* 0x48 - ATF_THD0 */ 
	    struct {
		unsigned int atf_thd0_i32:6;
		unsigned int reserved0:10;
		unsigned int atf_thd1_i32:6;
		unsigned int reserved1:10;
	} atf_thd0;
	
	    /* 0x4c - ATF_THD1 */ 
	    struct {
		unsigned int atf_thd0_i16:6;
		unsigned int reserved0:10;
		unsigned int atf_thd1_i16:6;
		unsigned int reserved1:10;
	} atf_thd1;
	
	    /* 0x50 - ATF_SAD_THD0 */ 
	    struct {
		unsigned int atf_thd0_p64:6;
		unsigned int reserved0:10;
		unsigned int atf_thd1_p64:6;
		unsigned int reserved1:10;
	} atf_sad_thd0;
	
	    /* 0x54 - ATF_SAD_THD1 */ 
	    struct {
		unsigned int atf_thd0_p32:6;
		unsigned int reserved0:10;
		unsigned int atf_thd1_p32:6;
		unsigned int reserved1:10;
	} atf_sad_thd1;
	
	    /* 0x58 - ATF_SAD_WGT0 */ 
	    struct {
		unsigned int atf_thd0_p16:6;
		unsigned int reserved0:10;
		unsigned int atf_thd1_p16:6;
		unsigned int reserved1:10;
	} atf_sad_wgt0;
	
	    /* 0x5c - ATF_SAD_WGT1 */ 
	    struct {
		unsigned int atf_wgt_i16:6;
		unsigned int reserved0:10;
		unsigned int atf_wgt_i32:6;
		unsigned int reserved1:10;
	} atf_sad_wgt1;
	
	    /* 0x60 - ATF_SAD_WGT2 */ 
	    struct {
		unsigned int atf_wgt_p32:6;
		unsigned int reserved0:10;
		unsigned int atf_wgt_p64:6;
		unsigned int reserved1:10;
	} atf_sad_wgt2;
	
	    /* 0x64 - ATF_SAD_OFST0 */ 
	    struct {
		unsigned int atf_wgt_p16:6;
		unsigned int reserved:26;
	} atf_sad_ofst0;
	
	    /* 0x68 - ATF_SAD_OFST1, reserved */ 
	    struct {
		unsigned int atf_sad_ofst12:14;
		unsigned int reserved0:2;
		unsigned int atf_sad_ofst20:14;
		unsigned int reserved1:2;
	} atf_sad_ofst1;
	
	    /* 0x6c - ATF_SAD_OFST2, reserved */ 
	    struct {
		unsigned int atf_sad_ofst21:14;
		unsigned int reserved0:2;
		unsigned int atf_sad_ofst30:14;
		unsigned int reserved1:2;
	} atf_sad_ofst2;
	
	    /* 0x70-0x13c - LAMD_SATD_qp */ 
	unsigned int lamd_satd_qp[52];
	
	    /* 0x140-0x20c - LAMD_MOD_qp, combo for I and P */ 
	unsigned int lamd_moda_qp[52];
	
	    /* 0x210-0x2dc */ 
	unsigned int lamd_modb_qp[52];
	
	    /* 0x2e0 - MADI_CFG */ 
	    struct {
		unsigned int reserved:32;
	} madi_cfg;
	
	    /* 0x2e4 - AQ_THD0 */ 
	    struct {
		unsigned int aq_thld0:8;
		unsigned int aq_thld1:8;
		unsigned int aq_thld2:8;
		unsigned int aq_thld3:8;
	} aq_thd0;
	
	    /* 0x2e8 - AQ_THD1 */ 
	    struct {
		unsigned int aq_thld4:8;
		unsigned int aq_thld5:8;
		unsigned int aq_thld6:8;
		unsigned int aq_thld7:8;
	} aq_thd1;
	
	    /* 0x2ec - AQ_THD2 */ 
	    struct {
		unsigned int aq_thld8:8;
		unsigned int aq_thld9:8;
		unsigned int aq_thld10:8;
		unsigned int aq_thld11:8;
	} aq_thd2;
	
	    /* 0x2f0 - AQ_THD3 */ 
	    struct {
		unsigned int aq_thld12:8;
		unsigned int aq_thld13:8;
		unsigned int aq_thld14:8;
		unsigned int aq_thld15:8;
	} aq_thd3;
	
	    /* 0x2f4 - AQ_QP_DLT0 */ 
	    struct {
		signed int qp_delta0:6;
		signed int reserved0:2;
		signed int qp_delta1:6;
		signed int reserved1:2;
		signed int qp_delta2:6;
		signed int reserved2:2;
		signed int qp_delta3:6;
		signed int reserved3:2;
	} aq_qp_dlt0;
	
	    /* 0x2f8 - AQ_QP_DLT1 */ 
	    struct {
		signed int qp_delta4:6;
		signed int reserved0:2;
		signed int qp_delta5:6;
		signed int reserved1:2;
		signed int qp_delta6:6;
		signed int reserved2:2;
		signed int qp_delta7:6;
		signed int reserved3:2;
	} aq_qp_dlt1;
	
	    /* 0x2fc - AQ_QP_DLT2 */ 
	    struct {
		signed int qp_delta8:6;
		signed int reserved0:2;
		signed int qp_delta9:6;
		signed int reserved1:2;
		signed int qp_delta10:6;
		signed int reserved2:2;
		signed int qp_delta11:6;
		signed int reserved3:2;
	} aq_qp_dlt2;
	
	    /* 0x300 - AQ_QP_DLT3 */ 
	    struct {
		signed int qp_delta12:6;
		signed int reserved0:2;
		signed int qp_delta13:6;
		signed int reserved1:2;
		signed int qp_delta14:6;
		signed int reserved2:2;
		signed int qp_delta15:6;
		signed int reserved3:2;
	} aq_qp_dlt3;
	
	    /* 0x304 - reserved */ 
	unsigned int rime_reserved;
	
	    /* --------------Registers End---------------- */ 
	
	    /* KLUT_WGT */ 
	unsigned int klut_wgt[CHRM_LAMBADA_REG_NUM];
	void *adr_srcy;
	void *adr_srcu;
	void *adr_srcv;
	void *ctuc_addr;
	void *rfpw_h_addr;
	void *rfpw_b_addr;
	void *rfpr_h_addr;
	void *rfpr_b_addr;
	void *cmvw_addr;
	void *cmvr_addr;
	void *dspw_addr;
	void *dspr_addr;
	void *meiw_addr;
	void *bsbt_addr;
	void *bsbb_addr;
	void *bsbr_addr;
	void *bsbw_addr;
	
	    /* save lambda */ 
	unsigned int lamd_mod_qp_I[52];	/* Table-A */
	unsigned int lamd_mod_qp_P[52];	/* Table-B */
	
	    /* save lamb_mod_sel */ 
	unsigned int lamb_mod_sel_i_slice;
	unsigned int lamb_mod_sel_p_slice;
	unsigned int real_stride_y;
	unsigned int real_stride_c;
	unsigned int direct_flag;
} VEPU_REGS;
 
/* Note: Compared with relative qp, absolute qp owns higher priority,
 * that is, if set_qp_y is set to 1, ROI region's qp is qp_y, ignoring
 * qp_delta.
 */ 
    
#if 0
    typedef struct ROIParam {
	unsigned short qp_y:6;	/* absolute qp */
	unsigned short set_qp_y:1;	/* set absolute qp flag */
	unsigned short forbid_inter:1;	/* intra flag for CTU level */
	unsigned short qp_area_idx:3;	/* qp range index */
	unsigned short qp_delta:5;	/* relative qp */
} ROIParam;

#endif /*  */
    typedef struct ROIParam {
	unsigned short forbid_inter:1;	/* intra flag for cu 16x16 */
	unsigned short reserved:3;	/* reserved */
	unsigned short qp_area_idx:3;	/* roi range index */
	unsigned short area_map:1;	/* roi en */
	short qp_y:7;		/*qp_value ,  absolute qp or relative qp */
	unsigned short set_qp_y:1;	/* qp_adj_mode,    set absolute qp flag */
} ROIParam;
  typedef struct ROIParamPrintf {
	ROIParam roi_info_ctu[16];
} ROIParamPrintf;
    typedef struct ROICTU {
	unsigned char qp_en[16];	// level 16
	unsigned short set_qp_y[16];
	short qp_y[16];
	unsigned short forbid_inter_ctu;
	unsigned short forbid_inter[85];	//raster in level ;  level 8x8: 0~63  ;  level 16x16: 64~79  ;  level 32x32: 80~83  ;  level 64x64: 84  ;
	unsigned short area_map[85];
	unsigned short qp_area_idx[85];
} ROICTU;
  
#endif /*  */
