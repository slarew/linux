// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Params subdevice
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/bitfield.h>
#include <linux/math.h>
#include <linux/string.h>

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP params */

#include "rkisp1-common.h"

#define RKISP1_PARAMS_DEV_NAME	RKISP1_DRIVER_NAME "_params"

#define RKISP1_ISP_PARAMS_REQ_BUFS_MIN	2
#define RKISP1_ISP_PARAMS_REQ_BUFS_MAX	8

#define RKISP1_ISP_DPCC_METHODS_SET(n) \
			(RKISP1_CIF_ISP_DPCC_METHODS_SET_1 + 0x4 * (n))
#define RKISP1_ISP_DPCC_LINE_THRESH(n) \
			(RKISP1_CIF_ISP_DPCC_LINE_THRESH_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_LINE_MAD_FAC(n) \
			(RKISP1_CIF_ISP_DPCC_LINE_MAD_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_PG_FAC(n) \
			(RKISP1_CIF_ISP_DPCC_PG_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_RND_THRESH(n) \
			(RKISP1_CIF_ISP_DPCC_RND_THRESH_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_RG_FAC(n) \
			(RKISP1_CIF_ISP_DPCC_RG_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_CC_COEFF(n) \
			(RKISP1_CIF_ISP_CC_COEFF_0 + (n) * 4)

#define RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS	BIT(0)
#define RKISP1_EXT_PARAMS_BLOCK_GROUP_LSC	BIT(1)

union rkisp1_ext_params_config {
	struct rkisp1_ext_params_block_header header;
	struct rkisp1_ext_params_bls_config bls;
	struct rkisp1_ext_params_dpcc_config dpcc;
	struct rkisp1_ext_params_sdg_config sdg;
	struct rkisp1_ext_params_lsc_config lsc;
	struct rkisp1_ext_params_awb_gain_config awbg;
	struct rkisp1_ext_params_flt_config flt;
	struct rkisp1_ext_params_bdm_config bdm;
	struct rkisp1_ext_params_ctk_config ctk;
	struct rkisp1_ext_params_goc_config goc;
	struct rkisp1_ext_params_dpf_config dpf;
	struct rkisp1_ext_params_dpf_strength_config dpfs;
	struct rkisp1_ext_params_cproc_config cproc;
	struct rkisp1_ext_params_ie_config ie;
	struct rkisp1_ext_params_awb_meas_config awbm;
	struct rkisp1_ext_params_hst_config hst;
	struct rkisp1_ext_params_aec_config aec;
	struct rkisp1_ext_params_afc_config afc;
	struct rkisp1_ext_params_compand_bls_config compand_bls;
	struct rkisp1_ext_params_compand_curve_config compand_curve;
	struct rkisp1_ext_params_wdr_config wdr;
};

enum rkisp1_params_formats {
	RKISP1_PARAMS_FIXED,
	RKISP1_PARAMS_EXTENSIBLE,
};

static const struct v4l2_meta_format rkisp1_params_formats[] = {
	[RKISP1_PARAMS_FIXED] = {
		.dataformat = V4L2_META_FMT_RK_ISP1_PARAMS,
		.buffersize = sizeof(struct rkisp1_params_cfg),
	},
	[RKISP1_PARAMS_EXTENSIBLE] = {
		.dataformat = V4L2_META_FMT_RK_ISP1_EXT_PARAMS,
		.buffersize = sizeof(struct rkisp1_ext_params_cfg),
	},
};

static const struct v4l2_meta_format *
rkisp1_params_get_format_info(u32 dataformat)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(rkisp1_params_formats); i++) {
		if (rkisp1_params_formats[i].dataformat == dataformat)
			return &rkisp1_params_formats[i];
	}

	return &rkisp1_params_formats[RKISP1_PARAMS_FIXED];
}

static inline void
rkisp1_param_set_bits(struct rkisp1_params *params, u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp1_read(params->rkisp1, reg);
	rkisp1_write(params->rkisp1, reg, val | bit_mask);
}

static inline void
rkisp1_param_clear_bits(struct rkisp1_params *params, u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp1_read(params->rkisp1, reg);
	rkisp1_write(params->rkisp1, reg, val & ~bit_mask);
}

/* ISP BP interface function */
static void rkisp1_dpcc_config(struct rkisp1_params *params,
			       const struct rkisp1_cif_isp_dpcc_config *arg)
{
	unsigned int i;
	u32 mode;

	/*
	 * The enable bit is controlled in rkisp1_isp_isr_other_config() and
	 * must be preserved. The grayscale mode should be configured
	 * automatically based on the media bus code on the ISP sink pad, so
	 * only the STAGE1_ENABLE bit can be set by userspace.
	 */
	mode = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_DPCC_MODE);
	mode &= RKISP1_CIF_ISP_DPCC_MODE_DPCC_ENABLE;
	mode |= arg->mode & RKISP1_CIF_ISP_DPCC_MODE_STAGE1_ENABLE;
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPCC_MODE, mode);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPCC_OUTPUT_MODE,
		     arg->output_mode & RKISP1_CIF_ISP_DPCC_OUTPUT_MODE_MASK);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPCC_SET_USE,
		     arg->set_use & RKISP1_CIF_ISP_DPCC_SET_USE_MASK);

	for (i = 0; i < RKISP1_CIF_ISP_DPCC_METHODS_MAX; i++) {
		rkisp1_write(params->rkisp1, RKISP1_ISP_DPCC_METHODS_SET(i),
			     arg->methods[i].method &
			     RKISP1_CIF_ISP_DPCC_METHODS_SET_MASK);
		rkisp1_write(params->rkisp1, RKISP1_ISP_DPCC_LINE_THRESH(i),
			     arg->methods[i].line_thresh &
			     RKISP1_CIF_ISP_DPCC_LINE_THRESH_MASK);
		rkisp1_write(params->rkisp1, RKISP1_ISP_DPCC_LINE_MAD_FAC(i),
			     arg->methods[i].line_mad_fac &
			     RKISP1_CIF_ISP_DPCC_LINE_MAD_FAC_MASK);
		rkisp1_write(params->rkisp1, RKISP1_ISP_DPCC_PG_FAC(i),
			     arg->methods[i].pg_fac &
			     RKISP1_CIF_ISP_DPCC_PG_FAC_MASK);
		rkisp1_write(params->rkisp1, RKISP1_ISP_DPCC_RND_THRESH(i),
			     arg->methods[i].rnd_thresh &
			     RKISP1_CIF_ISP_DPCC_RND_THRESH_MASK);
		rkisp1_write(params->rkisp1, RKISP1_ISP_DPCC_RG_FAC(i),
			     arg->methods[i].rg_fac &
			     RKISP1_CIF_ISP_DPCC_RG_FAC_MASK);
	}

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPCC_RND_OFFS,
		     arg->rnd_offs & RKISP1_CIF_ISP_DPCC_RND_OFFS_MASK);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPCC_RO_LIMITS,
		     arg->ro_limits & RKISP1_CIF_ISP_DPCC_RO_LIMIT_MASK);
}

/* ISP black level subtraction interface function */
static void rkisp1_bls_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_bls_config *arg)
{
	/* avoid to override the old enable value */
	u32 new_control;

	new_control = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_BLS_CTRL);
	new_control &= RKISP1_CIF_ISP_BLS_ENA;
	/* fixed subtraction values */
	if (!arg->enable_auto) {
		static const u32 regs[] = {
			RKISP1_CIF_ISP_BLS_A_FIXED,
			RKISP1_CIF_ISP_BLS_B_FIXED,
			RKISP1_CIF_ISP_BLS_C_FIXED,
			RKISP1_CIF_ISP_BLS_D_FIXED,
		};
		u32 swapped[4];

		rkisp1_bls_swap_regs(params->raw_type, regs, swapped);

		rkisp1_write(params->rkisp1, swapped[0], arg->fixed_val.r);
		rkisp1_write(params->rkisp1, swapped[1], arg->fixed_val.gr);
		rkisp1_write(params->rkisp1, swapped[2], arg->fixed_val.gb);
		rkisp1_write(params->rkisp1, swapped[3], arg->fixed_val.b);
	} else {
		if (arg->en_windows & BIT(1)) {
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_H2_START,
				     arg->bls_window2.h_offs);
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_H2_STOP,
				     arg->bls_window2.h_size);
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_V2_START,
				     arg->bls_window2.v_offs);
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_V2_STOP,
				     arg->bls_window2.v_size);
			new_control |= RKISP1_CIF_ISP_BLS_WINDOW_2;
		}

		if (arg->en_windows & BIT(0)) {
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_H1_START,
				     arg->bls_window1.h_offs);
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_H1_STOP,
				     arg->bls_window1.h_size);
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_V1_START,
				     arg->bls_window1.v_offs);
			rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_V1_STOP,
				     arg->bls_window1.v_size);
			new_control |= RKISP1_CIF_ISP_BLS_WINDOW_1;
		}

		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_SAMPLES,
			     arg->bls_samples);

		new_control |= RKISP1_CIF_ISP_BLS_MODE_MEASURED;
	}
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_BLS_CTRL, new_control);
}

/* ISP LS correction interface function */
static void
rkisp1_lsc_matrix_config_v10(struct rkisp1_params *params,
			     const struct rkisp1_cif_isp_lsc_config *pconfig)
{
	struct rkisp1_device *rkisp1 = params->rkisp1;
	u32 lsc_status, sram_addr, lsc_table_sel;
	unsigned int i, j;

	lsc_status = rkisp1_read(rkisp1, RKISP1_CIF_ISP_LSC_STATUS);

	/* RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_153 = ( 17 * 18 ) >> 1 */
	sram_addr = lsc_status & RKISP1_CIF_ISP_LSC_ACTIVE_TABLE ?
		    RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_0 :
		    RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_153;
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_R_TABLE_ADDR, sram_addr);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GR_TABLE_ADDR, sram_addr);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GB_TABLE_ADDR, sram_addr);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_B_TABLE_ADDR, sram_addr);

	/* program data tables (table size is 9 * 17 = 153) */
	for (i = 0; i < RKISP1_CIF_ISP_LSC_SAMPLES_MAX; i++) {
		const __u16 *r_tbl = pconfig->r_data_tbl[i];
		const __u16 *gr_tbl = pconfig->gr_data_tbl[i];
		const __u16 *gb_tbl = pconfig->gb_data_tbl[i];
		const __u16 *b_tbl = pconfig->b_data_tbl[i];

		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (2nd value of last DWORD unused)
		 */
		for (j = 0; j < RKISP1_CIF_ISP_LSC_SAMPLES_MAX - 1; j += 2) {
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_R_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(
					r_tbl[j], r_tbl[j + 1]));
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GR_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(
					gr_tbl[j], gr_tbl[j + 1]));
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GB_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(
					gb_tbl[j], gb_tbl[j + 1]));
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_B_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(
					b_tbl[j], b_tbl[j + 1]));
		}

		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_R_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(r_tbl[j], 0));
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GR_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(gr_tbl[j], 0));
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GB_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(gb_tbl[j], 0));
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_B_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V10(b_tbl[j], 0));
	}

	lsc_table_sel = lsc_status & RKISP1_CIF_ISP_LSC_ACTIVE_TABLE ?
			RKISP1_CIF_ISP_LSC_TABLE_0 : RKISP1_CIF_ISP_LSC_TABLE_1;
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_TABLE_SEL, lsc_table_sel);
}

static void
rkisp1_lsc_matrix_config_v12(struct rkisp1_params *params,
			     const struct rkisp1_cif_isp_lsc_config *pconfig)
{
	struct rkisp1_device *rkisp1 = params->rkisp1;
	u32 lsc_status, sram_addr, lsc_table_sel;
	unsigned int i, j;

	lsc_status = rkisp1_read(rkisp1, RKISP1_CIF_ISP_LSC_STATUS);

	/* RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_153 = ( 17 * 18 ) >> 1 */
	sram_addr = lsc_status & RKISP1_CIF_ISP_LSC_ACTIVE_TABLE ?
		    RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_0 :
		    RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_153;
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_R_TABLE_ADDR, sram_addr);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GR_TABLE_ADDR, sram_addr);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GB_TABLE_ADDR, sram_addr);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_B_TABLE_ADDR, sram_addr);

	/* program data tables (table size is 9 * 17 = 153) */
	for (i = 0; i < RKISP1_CIF_ISP_LSC_SAMPLES_MAX; i++) {
		const __u16 *r_tbl = pconfig->r_data_tbl[i];
		const __u16 *gr_tbl = pconfig->gr_data_tbl[i];
		const __u16 *gb_tbl = pconfig->gb_data_tbl[i];
		const __u16 *b_tbl = pconfig->b_data_tbl[i];

		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (2nd value of last DWORD unused)
		 */
		for (j = 0; j < RKISP1_CIF_ISP_LSC_SAMPLES_MAX - 1; j += 2) {
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_R_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(
					r_tbl[j], r_tbl[j + 1]));
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GR_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(
					gr_tbl[j], gr_tbl[j + 1]));
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GB_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(
					gb_tbl[j], gb_tbl[j + 1]));
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_B_TABLE_DATA,
				     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(
					b_tbl[j], b_tbl[j + 1]));
		}

		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_R_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(r_tbl[j], 0));
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GR_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(gr_tbl[j], 0));
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_GB_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(gb_tbl[j], 0));
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_B_TABLE_DATA,
			     RKISP1_CIF_ISP_LSC_TABLE_DATA_V12(b_tbl[j], 0));
	}

	lsc_table_sel = lsc_status & RKISP1_CIF_ISP_LSC_ACTIVE_TABLE ?
			RKISP1_CIF_ISP_LSC_TABLE_0 : RKISP1_CIF_ISP_LSC_TABLE_1;
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_TABLE_SEL, lsc_table_sel);
}

static void rkisp1_lsc_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_lsc_config *arg)
{
	struct rkisp1_device *rkisp1 = params->rkisp1;
	u32 lsc_ctrl, data;
	unsigned int i;

	/* To config must be off , store the current status firstly */
	lsc_ctrl = rkisp1_read(rkisp1, RKISP1_CIF_ISP_LSC_CTRL);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
				RKISP1_CIF_ISP_LSC_CTRL_ENA);
	params->ops->lsc_matrix_config(params, arg);

	for (i = 0; i < RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE / 2; i++) {
		/* program x size tables */
		data = RKISP1_CIF_ISP_LSC_SECT_SIZE(arg->x_size_tbl[i * 2],
						    arg->x_size_tbl[i * 2 + 1]);
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_XSIZE(i), data);

		/* program x grad tables */
		data = RKISP1_CIF_ISP_LSC_SECT_GRAD(arg->x_grad_tbl[i * 2],
						    arg->x_grad_tbl[i * 2 + 1]);
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_XGRAD(i), data);

		/* program y size tables */
		data = RKISP1_CIF_ISP_LSC_SECT_SIZE(arg->y_size_tbl[i * 2],
						    arg->y_size_tbl[i * 2 + 1]);
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_YSIZE(i), data);

		/* program y grad tables */
		data = RKISP1_CIF_ISP_LSC_SECT_GRAD(arg->y_grad_tbl[i * 2],
						    arg->y_grad_tbl[i * 2 + 1]);
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_LSC_YGRAD(i), data);
	}

	/* restore the lsc ctrl status */
	if (lsc_ctrl & RKISP1_CIF_ISP_LSC_CTRL_ENA)
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
				      RKISP1_CIF_ISP_LSC_CTRL_ENA);
	else
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
					RKISP1_CIF_ISP_LSC_CTRL_ENA);
}

/* ISP Filtering function */
static void rkisp1_flt_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_flt_config *arg)
{
	u32 filt_mode;

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_THRESH_BL0,
		     arg->thresh_bl0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_THRESH_BL1,
		     arg->thresh_bl1);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_THRESH_SH0,
		     arg->thresh_sh0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_THRESH_SH1,
		     arg->thresh_sh1);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_FAC_BL0,
		     arg->fac_bl0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_FAC_BL1,
		     arg->fac_bl1);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_FAC_MID,
		     arg->fac_mid);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_FAC_SH0,
		     arg->fac_sh0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_FAC_SH1,
		     arg->fac_sh1);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_LUM_WEIGHT,
		     arg->lum_weight);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_MODE,
		     (arg->mode ? RKISP1_CIF_ISP_FLT_MODE_DNR : 0) |
		     RKISP1_CIF_ISP_FLT_CHROMA_V_MODE(arg->chr_v_mode) |
		     RKISP1_CIF_ISP_FLT_CHROMA_H_MODE(arg->chr_h_mode) |
		     RKISP1_CIF_ISP_FLT_GREEN_STAGE1(arg->grn_stage1));

	/* avoid to override the old enable value */
	filt_mode = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_FILT_MODE);
	filt_mode &= RKISP1_CIF_ISP_FLT_ENA;
	if (arg->mode)
		filt_mode |= RKISP1_CIF_ISP_FLT_MODE_DNR;
	filt_mode |= RKISP1_CIF_ISP_FLT_CHROMA_V_MODE(arg->chr_v_mode) |
		     RKISP1_CIF_ISP_FLT_CHROMA_H_MODE(arg->chr_h_mode) |
		     RKISP1_CIF_ISP_FLT_GREEN_STAGE1(arg->grn_stage1);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_FILT_MODE, filt_mode);
}

/* ISP demosaic interface function */
static int rkisp1_bdm_config(struct rkisp1_params *params,
			     const struct rkisp1_cif_isp_bdm_config *arg)
{
	u32 bdm_th;

	/* avoid to override the old enable value */
	bdm_th = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_DEMOSAIC);
	bdm_th &= RKISP1_CIF_ISP_DEMOSAIC_BYPASS;
	bdm_th |= arg->demosaic_th & ~RKISP1_CIF_ISP_DEMOSAIC_BYPASS;
	/* set demosaic threshold */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DEMOSAIC, bdm_th);
	return 0;
}

/* ISP GAMMA correction interface function */
static void rkisp1_sdg_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_sdg_config *arg)
{
	unsigned int i;

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_GAMMA_DX_LO,
		     arg->xa_pnts.gamma_dx0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_GAMMA_DX_HI,
		     arg->xa_pnts.gamma_dx1);

	for (i = 0; i < RKISP1_CIF_ISP_DEGAMMA_CURVE_SIZE; i++) {
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_GAMMA_R_Y0 + i * 4,
			     arg->curve_r.gamma_y[i]);
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_GAMMA_G_Y0 + i * 4,
			     arg->curve_g.gamma_y[i]);
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_GAMMA_B_Y0 + i * 4,
			     arg->curve_b.gamma_y[i]);
	}
}

/* ISP GAMMA correction interface function */
static void rkisp1_goc_config_v10(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_goc_config *arg)
{
	unsigned int i;

	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_GAMMA_OUT_MODE_V10,
		     arg->mode);

	for (i = 0; i < RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V10; i++)
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_GAMMA_OUT_Y_0_V10 + i * 4,
			     arg->gamma_y[i]);
}

static void rkisp1_goc_config_v12(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_goc_config *arg)
{
	unsigned int i;
	u32 value;

	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_GAMMA_OUT_MODE_V12,
		     arg->mode);

	for (i = 0; i < RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V12 / 2; i++) {
		value = RKISP1_CIF_ISP_GAMMA_VALUE_V12(
			arg->gamma_y[2 * i + 1],
			arg->gamma_y[2 * i]);
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_GAMMA_OUT_Y_0_V12 + i * 4, value);
	}
}

/* ISP Cross Talk */
static void rkisp1_ctk_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_ctk_config *arg)
{
	unsigned int i, j, k = 0;

	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			rkisp1_write(params->rkisp1,
				     RKISP1_CIF_ISP_CT_COEFF_0 + 4 * k++,
				     arg->coeff[i][j]);
	for (i = 0; i < 3; i++)
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_CT_OFFSET_R + i * 4,
			     arg->ct_offset[i]);
}

static void rkisp1_ctk_enable(struct rkisp1_params *params, bool en)
{
	if (en)
		return;

	/* Write back the default values. */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_0, 0x80);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_1, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_2, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_3, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_4, 0x80);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_5, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_6, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_7, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_COEFF_8, 0x80);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_OFFSET_R, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_OFFSET_G, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CT_OFFSET_B, 0);
}

/* ISP White Balance Mode */
static void rkisp1_awb_meas_config_v10(struct rkisp1_params *params,
				       const struct rkisp1_cif_isp_awb_meas_config *arg)
{
	u32 reg_val = 0;
	/* based on the mode,configure the awb module */
	if (arg->awb_mode == RKISP1_CIF_ISP_AWB_MODE_YCBCR) {
		/* Reference Cb and Cr */
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_REF_V10,
			     RKISP1_CIF_ISP_AWB_REF_CR_SET(arg->awb_ref_cr) |
			     arg->awb_ref_cb);
		/* Yc Threshold */
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_THRESH_V10,
			     RKISP1_CIF_ISP_AWB_MAX_Y_SET(arg->max_y) |
			     RKISP1_CIF_ISP_AWB_MIN_Y_SET(arg->min_y) |
			     RKISP1_CIF_ISP_AWB_MAX_CS_SET(arg->max_csum) |
			     arg->min_c);
	}

	reg_val = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V10);
	if (arg->enable_ymax_cmp)
		reg_val |= RKISP1_CIF_ISP_AWB_YMAX_CMP_EN;
	else
		reg_val &= ~RKISP1_CIF_ISP_AWB_YMAX_CMP_EN;
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V10, reg_val);

	/* window offset */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_WND_V_OFFS_V10,
		     arg->awb_wnd.v_offs);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_WND_H_OFFS_V10,
		     arg->awb_wnd.h_offs);
	/* AWB window size */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_WND_V_SIZE_V10,
		     arg->awb_wnd.v_size);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_WND_H_SIZE_V10,
		     arg->awb_wnd.h_size);
	/* Number of frames */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_FRAMES_V10,
		     arg->frames);
}

static void rkisp1_awb_meas_config_v12(struct rkisp1_params *params,
				       const struct rkisp1_cif_isp_awb_meas_config *arg)
{
	u32 reg_val = 0;
	/* based on the mode,configure the awb module */
	if (arg->awb_mode == RKISP1_CIF_ISP_AWB_MODE_YCBCR) {
		/* Reference Cb and Cr */
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_REF_V12,
			     RKISP1_CIF_ISP_AWB_REF_CR_SET(arg->awb_ref_cr) |
			     arg->awb_ref_cb);
		/* Yc Threshold */
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_THRESH_V12,
			     RKISP1_CIF_ISP_AWB_MAX_Y_SET(arg->max_y) |
			     RKISP1_CIF_ISP_AWB_MIN_Y_SET(arg->min_y) |
			     RKISP1_CIF_ISP_AWB_MAX_CS_SET(arg->max_csum) |
			     arg->min_c);
	}

	reg_val = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V12);
	if (arg->enable_ymax_cmp)
		reg_val |= RKISP1_CIF_ISP_AWB_YMAX_CMP_EN;
	else
		reg_val &= ~RKISP1_CIF_ISP_AWB_YMAX_CMP_EN;
	reg_val &= ~RKISP1_CIF_ISP_AWB_SET_FRAMES_MASK_V12;
	reg_val |= RKISP1_CIF_ISP_AWB_SET_FRAMES_V12(arg->frames);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V12, reg_val);

	/* window offset */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_OFFS_V12,
		     arg->awb_wnd.v_offs << 16 | arg->awb_wnd.h_offs);
	/* AWB window size */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_SIZE_V12,
		     arg->awb_wnd.v_size << 16 | arg->awb_wnd.h_size);
}

static void
rkisp1_awb_meas_enable_v10(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_awb_meas_config *arg,
			   bool en)
{
	u32 reg_val = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V10);

	/* switch off */
	reg_val &= RKISP1_CIF_ISP_AWB_MODE_MASK_NONE;

	if (en) {
		if (arg->awb_mode == RKISP1_CIF_ISP_AWB_MODE_RGB)
			reg_val |= RKISP1_CIF_ISP_AWB_MODE_RGB_EN;
		else
			reg_val |= RKISP1_CIF_ISP_AWB_MODE_YCBCR_EN;

		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V10,
			     reg_val);

		/* Measurements require AWB block be active. */
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
				      RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	} else {
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V10,
			     reg_val);
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	}
}

static void
rkisp1_awb_meas_enable_v12(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_awb_meas_config *arg,
			   bool en)
{
	u32 reg_val = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V12);

	/* switch off */
	reg_val &= RKISP1_CIF_ISP_AWB_MODE_MASK_NONE;

	if (en) {
		if (arg->awb_mode == RKISP1_CIF_ISP_AWB_MODE_RGB)
			reg_val |= RKISP1_CIF_ISP_AWB_MODE_RGB_EN;
		else
			reg_val |= RKISP1_CIF_ISP_AWB_MODE_YCBCR_EN;

		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V12,
			     reg_val);

		/* Measurements require AWB block be active. */
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
				      RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	} else {
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP_V12,
			     reg_val);
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	}
}

static void
rkisp1_awb_gain_config_v10(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_awb_gain_config *arg)
{
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_GAIN_G_V10,
		     RKISP1_CIF_ISP_AWB_GAIN_R_SET(arg->gain_green_r) |
		     arg->gain_green_b);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_GAIN_RB_V10,
		     RKISP1_CIF_ISP_AWB_GAIN_R_SET(arg->gain_red) |
		     arg->gain_blue);
}

static void
rkisp1_awb_gain_config_v12(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_awb_gain_config *arg)
{
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_GAIN_G_V12,
		     RKISP1_CIF_ISP_AWB_GAIN_R_SET(arg->gain_green_r) |
		     arg->gain_green_b);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AWB_GAIN_RB_V12,
		     RKISP1_CIF_ISP_AWB_GAIN_R_SET(arg->gain_red) |
		     arg->gain_blue);
}

static void rkisp1_aec_config_v10(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_aec_config *arg)
{
	unsigned int block_hsize, block_vsize;
	u32 exp_ctrl;

	/* avoid to override the old enable value */
	exp_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_EXP_CTRL);
	exp_ctrl &= RKISP1_CIF_ISP_EXP_ENA;
	if (arg->autostop)
		exp_ctrl |= RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP;
	if (arg->mode == RKISP1_CIF_ISP_EXP_MEASURING_MODE_1)
		exp_ctrl |= RKISP1_CIF_ISP_EXP_CTRL_MEASMODE_1;
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_CTRL, exp_ctrl);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_H_OFFSET_V10,
		     arg->meas_window.h_offs);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_V_OFFSET_V10,
		     arg->meas_window.v_offs);

	block_hsize = arg->meas_window.h_size /
		      RKISP1_CIF_ISP_EXP_COLUMN_NUM_V10 - 1;
	block_vsize = arg->meas_window.v_size /
		      RKISP1_CIF_ISP_EXP_ROW_NUM_V10 - 1;

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_H_SIZE_V10,
		     RKISP1_CIF_ISP_EXP_H_SIZE_SET_V10(block_hsize));
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_V_SIZE_V10,
		     RKISP1_CIF_ISP_EXP_V_SIZE_SET_V10(block_vsize));
}

static void rkisp1_aec_config_v12(struct rkisp1_params *params,
			       const struct rkisp1_cif_isp_aec_config *arg)
{
	u32 exp_ctrl;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx = 1;
	static const u32 ae_wnd_num[] = { 5, 9, 15, 15 };

	/* avoid to override the old enable value */
	exp_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_EXP_CTRL);
	exp_ctrl &= RKISP1_CIF_ISP_EXP_ENA;
	if (arg->autostop)
		exp_ctrl |= RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP;
	if (arg->mode == RKISP1_CIF_ISP_EXP_MEASURING_MODE_1)
		exp_ctrl |= RKISP1_CIF_ISP_EXP_CTRL_MEASMODE_1;
	exp_ctrl |= RKISP1_CIF_ISP_EXP_CTRL_WNDNUM_SET_V12(wnd_num_idx);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_CTRL, exp_ctrl);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_OFFS_V12,
		     RKISP1_CIF_ISP_EXP_V_OFFSET_SET_V12(arg->meas_window.v_offs) |
		     RKISP1_CIF_ISP_EXP_H_OFFSET_SET_V12(arg->meas_window.h_offs));

	block_hsize = arg->meas_window.h_size / ae_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->meas_window.v_size / ae_wnd_num[wnd_num_idx] - 1;

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_EXP_SIZE_V12,
		     RKISP1_CIF_ISP_EXP_V_SIZE_SET_V12(block_vsize) |
		     RKISP1_CIF_ISP_EXP_H_SIZE_SET_V12(block_hsize));
}

static void rkisp1_cproc_config(struct rkisp1_params *params,
				const struct rkisp1_cif_isp_cproc_config *arg)
{
	struct rkisp1_cif_isp_isp_other_cfg *cur_other_cfg =
		container_of(arg, struct rkisp1_cif_isp_isp_other_cfg, cproc_config);
	struct rkisp1_cif_isp_ie_config *cur_ie_config =
						&cur_other_cfg->ie_config;
	u32 effect = cur_ie_config->effect;
	u32 quantization = params->quantization;

	rkisp1_write(params->rkisp1, RKISP1_CIF_C_PROC_CONTRAST,
		     arg->contrast);
	rkisp1_write(params->rkisp1, RKISP1_CIF_C_PROC_HUE, arg->hue);
	rkisp1_write(params->rkisp1, RKISP1_CIF_C_PROC_SATURATION, arg->sat);
	rkisp1_write(params->rkisp1, RKISP1_CIF_C_PROC_BRIGHTNESS,
		     arg->brightness);

	if (quantization != V4L2_QUANTIZATION_FULL_RANGE ||
	    effect != V4L2_COLORFX_NONE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_C_PROC_CTRL,
					RKISP1_CIF_C_PROC_YOUT_FULL |
					RKISP1_CIF_C_PROC_YIN_FULL |
					RKISP1_CIF_C_PROC_COUT_FULL);
	} else {
		rkisp1_param_set_bits(params, RKISP1_CIF_C_PROC_CTRL,
				      RKISP1_CIF_C_PROC_YOUT_FULL |
				      RKISP1_CIF_C_PROC_YIN_FULL |
				      RKISP1_CIF_C_PROC_COUT_FULL);
	}
}

static void rkisp1_hst_config_v10(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_hst_config *arg)
{
	unsigned int block_hsize, block_vsize;
	static const u32 hist_weight_regs[] = {
		RKISP1_CIF_ISP_HIST_WEIGHT_00TO30_V10,
		RKISP1_CIF_ISP_HIST_WEIGHT_40TO21_V10,
		RKISP1_CIF_ISP_HIST_WEIGHT_31TO12_V10,
		RKISP1_CIF_ISP_HIST_WEIGHT_22TO03_V10,
		RKISP1_CIF_ISP_HIST_WEIGHT_13TO43_V10,
		RKISP1_CIF_ISP_HIST_WEIGHT_04TO34_V10,
	};
	const u8 *weight;
	unsigned int i;
	u32 hist_prop;

	/* avoid to override the old enable value */
	hist_prop = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_HIST_PROP_V10);
	hist_prop &= RKISP1_CIF_ISP_HIST_PROP_MODE_MASK_V10;
	hist_prop |= RKISP1_CIF_ISP_HIST_PREDIV_SET_V10(arg->histogram_predivider);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_PROP_V10, hist_prop);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_H_OFFS_V10,
		     arg->meas_window.h_offs);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_V_OFFS_V10,
		     arg->meas_window.v_offs);

	block_hsize = arg->meas_window.h_size /
		      RKISP1_CIF_ISP_HIST_COLUMN_NUM_V10 - 1;
	block_vsize = arg->meas_window.v_size / RKISP1_CIF_ISP_HIST_ROW_NUM_V10 - 1;

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_H_SIZE_V10,
		     block_hsize);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_V_SIZE_V10,
		     block_vsize);

	weight = arg->hist_weight;
	for (i = 0; i < ARRAY_SIZE(hist_weight_regs); ++i, weight += 4)
		rkisp1_write(params->rkisp1, hist_weight_regs[i],
			     RKISP1_CIF_ISP_HIST_WEIGHT_SET_V10(weight[0], weight[1],
								weight[2], weight[3]));

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_WEIGHT_44_V10,
		     weight[0] & 0x1f);
}

static void rkisp1_hst_config_v12(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_hst_config *arg)
{
	unsigned int i, j;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx, hist_weight_num, hist_ctrl, value;
	u8 weight15x15[RKISP1_CIF_ISP_HIST_WEIGHT_REG_SIZE_V12];
	static const u32 hist_wnd_num[] = { 5, 9, 15, 15 };

	/* now we just support 9x9 window */
	wnd_num_idx = 1;
	memset(weight15x15, 0x00, sizeof(weight15x15));
	/* avoid to override the old enable value */
	hist_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_HIST_CTRL_V12);
	hist_ctrl &= RKISP1_CIF_ISP_HIST_CTRL_MODE_MASK_V12 |
		     RKISP1_CIF_ISP_HIST_CTRL_EN_MASK_V12;
	hist_ctrl = hist_ctrl |
		    RKISP1_CIF_ISP_HIST_CTRL_INTRSEL_SET_V12(1) |
		    RKISP1_CIF_ISP_HIST_CTRL_DATASEL_SET_V12(0) |
		    RKISP1_CIF_ISP_HIST_CTRL_WATERLINE_SET_V12(0) |
		    RKISP1_CIF_ISP_HIST_CTRL_AUTOSTOP_SET_V12(0) |
		    RKISP1_CIF_ISP_HIST_CTRL_WNDNUM_SET_V12(1) |
		    RKISP1_CIF_ISP_HIST_CTRL_STEPSIZE_SET_V12(arg->histogram_predivider);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_CTRL_V12, hist_ctrl);

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_OFFS_V12,
		     RKISP1_CIF_ISP_HIST_OFFS_SET_V12(arg->meas_window.h_offs,
						      arg->meas_window.v_offs));

	block_hsize = arg->meas_window.h_size / hist_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->meas_window.v_size / hist_wnd_num[wnd_num_idx] - 1;
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_SIZE_V12,
		     RKISP1_CIF_ISP_HIST_SIZE_SET_V12(block_hsize, block_vsize));

	for (i = 0; i < hist_wnd_num[wnd_num_idx]; i++) {
		for (j = 0; j < hist_wnd_num[wnd_num_idx]; j++) {
			weight15x15[i * RKISP1_CIF_ISP_HIST_ROW_NUM_V12 + j] =
				arg->hist_weight[i * hist_wnd_num[wnd_num_idx] + j];
		}
	}

	hist_weight_num = RKISP1_CIF_ISP_HIST_WEIGHT_REG_SIZE_V12;
	for (i = 0; i < (hist_weight_num / 4); i++) {
		value = RKISP1_CIF_ISP_HIST_WEIGHT_SET_V12(
				 weight15x15[4 * i + 0],
				 weight15x15[4 * i + 1],
				 weight15x15[4 * i + 2],
				 weight15x15[4 * i + 3]);
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_HIST_WEIGHT_V12 + 4 * i, value);
	}
	value = RKISP1_CIF_ISP_HIST_WEIGHT_SET_V12(weight15x15[4 * i + 0], 0, 0, 0);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_HIST_WEIGHT_V12 + 4 * i,
		     value);
}

static void
rkisp1_hst_enable_v10(struct rkisp1_params *params,
		      const struct rkisp1_cif_isp_hst_config *arg, bool en)
{
	if (en)	{
		u32 hist_prop = rkisp1_read(params->rkisp1,
					    RKISP1_CIF_ISP_HIST_PROP_V10);

		hist_prop &= ~RKISP1_CIF_ISP_HIST_PROP_MODE_MASK_V10;
		hist_prop |= arg->mode;
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_HIST_PROP_V10,
				      hist_prop);
	} else {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_HIST_PROP_V10,
					RKISP1_CIF_ISP_HIST_PROP_MODE_MASK_V10);
	}
}

static void
rkisp1_hst_enable_v12(struct rkisp1_params *params,
		      const struct rkisp1_cif_isp_hst_config *arg, bool en)
{
	if (en) {
		u32 hist_ctrl = rkisp1_read(params->rkisp1,
					    RKISP1_CIF_ISP_HIST_CTRL_V12);

		hist_ctrl &= ~RKISP1_CIF_ISP_HIST_CTRL_MODE_MASK_V12;
		hist_ctrl |= RKISP1_CIF_ISP_HIST_CTRL_MODE_SET_V12(arg->mode);
		hist_ctrl |= RKISP1_CIF_ISP_HIST_CTRL_EN_SET_V12(1);
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_HIST_CTRL_V12,
				      hist_ctrl);
	} else {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_HIST_CTRL_V12,
					RKISP1_CIF_ISP_HIST_CTRL_MODE_MASK_V12 |
					RKISP1_CIF_ISP_HIST_CTRL_EN_MASK_V12);
	}
}

static void rkisp1_afm_config_v10(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_afc_config *arg)
{
	size_t num_of_win = min_t(size_t, ARRAY_SIZE(arg->afm_win),
				  arg->num_afm_win);
	u32 afm_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AFM_CTRL);
	unsigned int i;

	/* Switch off to configure. */
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
				RKISP1_CIF_ISP_AFM_ENA);

	for (i = 0; i < num_of_win; i++) {
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_LT_A + i * 8,
			     RKISP1_CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_offs) |
			     RKISP1_CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_offs));
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_RB_A + i * 8,
			     RKISP1_CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_size +
							 arg->afm_win[i].h_offs) |
			     RKISP1_CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_size +
							 arg->afm_win[i].v_offs));
	}
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_THRES, arg->thres);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_VAR_SHIFT,
		     arg->var_shift);
	/* restore afm status */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_CTRL, afm_ctrl);
}

static void rkisp1_afm_config_v12(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_afc_config *arg)
{
	size_t num_of_win = min_t(size_t, ARRAY_SIZE(arg->afm_win),
				  arg->num_afm_win);
	u32 afm_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AFM_CTRL);
	u32 lum_var_shift, afm_var_shift;
	unsigned int i;

	/* Switch off to configure. */
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
				RKISP1_CIF_ISP_AFM_ENA);

	for (i = 0; i < num_of_win; i++) {
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_LT_A + i * 8,
			     RKISP1_CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_offs) |
			     RKISP1_CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_offs));
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_RB_A + i * 8,
			     RKISP1_CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_size +
							 arg->afm_win[i].h_offs) |
			     RKISP1_CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_size +
							 arg->afm_win[i].v_offs));
	}
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_THRES, arg->thres);

	lum_var_shift = RKISP1_CIF_ISP_AFM_GET_LUM_SHIFT_a_V12(arg->var_shift);
	afm_var_shift = RKISP1_CIF_ISP_AFM_GET_AFM_SHIFT_a_V12(arg->var_shift);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_VAR_SHIFT,
		     RKISP1_CIF_ISP_AFM_SET_SHIFT_a_V12(lum_var_shift, afm_var_shift) |
		     RKISP1_CIF_ISP_AFM_SET_SHIFT_b_V12(lum_var_shift, afm_var_shift) |
		     RKISP1_CIF_ISP_AFM_SET_SHIFT_c_V12(lum_var_shift, afm_var_shift));

	/* restore afm status */
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_AFM_CTRL, afm_ctrl);
}

static void rkisp1_ie_config(struct rkisp1_params *params,
			     const struct rkisp1_cif_isp_ie_config *arg)
{
	u32 eff_ctrl;

	eff_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_IMG_EFF_CTRL);
	eff_ctrl &= ~RKISP1_CIF_IMG_EFF_CTRL_MODE_MASK;

	if (params->quantization == V4L2_QUANTIZATION_FULL_RANGE)
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_YCBCR_FULL;

	switch (arg->effect) {
	case V4L2_COLORFX_SEPIA:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
	case V4L2_COLORFX_SET_CBCR:
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_TINT,
			     arg->eff_tint);
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
		/*
		 * Color selection is similar to water color(AQUA):
		 * grayscale + selected color w threshold
		 */
	case V4L2_COLORFX_AQUA:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_COLOR_SEL;
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_COLOR_SEL,
			     arg->color_sel);
		break;
	case V4L2_COLORFX_EMBOSS:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_EMBOSS;
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_MAT_1,
			     arg->eff_mat_1);
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_MAT_2,
			     arg->eff_mat_2);
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_MAT_3,
			     arg->eff_mat_3);
		break;
	case V4L2_COLORFX_SKETCH:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_SKETCH;
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_MAT_3,
			     arg->eff_mat_3);
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_MAT_4,
			     arg->eff_mat_4);
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_MAT_5,
			     arg->eff_mat_5);
		break;
	case V4L2_COLORFX_BW:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_BLACKWHITE;
		break;
	case V4L2_COLORFX_NEGATIVE:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_NEGATIVE;
		break;
	default:
		break;
	}

	rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_CTRL, eff_ctrl);
}

static void rkisp1_ie_enable(struct rkisp1_params *params, bool en)
{
	if (en) {
		rkisp1_param_set_bits(params, RKISP1_CIF_VI_ICCL,
				      RKISP1_CIF_VI_ICCL_IE_CLK);
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_CTRL,
			     RKISP1_CIF_IMG_EFF_CTRL_ENABLE);
		rkisp1_param_set_bits(params, RKISP1_CIF_IMG_EFF_CTRL,
				      RKISP1_CIF_IMG_EFF_CTRL_CFG_UPD);
	} else {
		rkisp1_param_clear_bits(params, RKISP1_CIF_IMG_EFF_CTRL,
					RKISP1_CIF_IMG_EFF_CTRL_ENABLE);
		rkisp1_param_clear_bits(params, RKISP1_CIF_VI_ICCL,
					RKISP1_CIF_VI_ICCL_IE_CLK);
	}
}

static void rkisp1_csm_config(struct rkisp1_params *params)
{
	struct csm_coeffs {
		u16 limited[9];
		u16 full[9];
	};
	static const struct csm_coeffs rec601_coeffs = {
		.limited = {
			0x0021, 0x0042, 0x000d,
			0x01ed, 0x01db, 0x0038,
			0x0038, 0x01d1, 0x01f7,
		},
		.full = {
			0x0026, 0x004b, 0x000f,
			0x01ea, 0x01d6, 0x0040,
			0x0040, 0x01ca, 0x01f6,
		},
	};
	static const struct csm_coeffs rec709_coeffs = {
		.limited = {
			0x0018, 0x0050, 0x0008,
			0x01f3, 0x01d5, 0x0038,
			0x0038, 0x01cd, 0x01fb,
		},
		.full = {
			0x001b, 0x005c, 0x0009,
			0x01f1, 0x01cf, 0x0040,
			0x0040, 0x01c6, 0x01fa,
		},
	};
	static const struct csm_coeffs rec2020_coeffs = {
		.limited = {
			0x001d, 0x004c, 0x0007,
			0x01f0, 0x01d8, 0x0038,
			0x0038, 0x01cd, 0x01fb,
		},
		.full = {
			0x0022, 0x0057, 0x0008,
			0x01ee, 0x01d2, 0x0040,
			0x0040, 0x01c5, 0x01fb,
		},
	};
	static const struct csm_coeffs smpte240m_coeffs = {
		.limited = {
			0x0018, 0x004f, 0x000a,
			0x01f3, 0x01d5, 0x0038,
			0x0038, 0x01ce, 0x01fa,
		},
		.full = {
			0x001b, 0x005a, 0x000b,
			0x01f1, 0x01cf, 0x0040,
			0x0040, 0x01c7, 0x01f9,
		},
	};

	const struct csm_coeffs *coeffs;
	const u16 *csm;
	unsigned int i;

	switch (params->ycbcr_encoding) {
	case V4L2_YCBCR_ENC_601:
	default:
		coeffs = &rec601_coeffs;
		break;
	case V4L2_YCBCR_ENC_709:
		coeffs = &rec709_coeffs;
		break;
	case V4L2_YCBCR_ENC_BT2020:
		coeffs = &rec2020_coeffs;
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		coeffs = &smpte240m_coeffs;
		break;
	}

	if (params->quantization == V4L2_QUANTIZATION_FULL_RANGE) {
		csm = coeffs->full;
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
				      RKISP1_CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
				      RKISP1_CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	} else {
		csm = coeffs->limited;
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
					RKISP1_CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	}

	for (i = 0; i < 9; i++)
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_CC_COEFF_0 + i * 4,
			     csm[i]);
}

/* ISP De-noise Pre-Filter(DPF) function */
static void rkisp1_dpf_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_dpf_config *arg)
{
	unsigned int isp_dpf_mode, spatial_coeff, i;

	switch (arg->gain.mode) {
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_USE_NF_GAIN |
			       RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_LSC_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_USE_NF_GAIN |
			       RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP |
			       RKISP1_CIF_ISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_LSC_GAIN_COMP |
			       RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED:
	default:
		isp_dpf_mode = 0;
		break;
	}

	if (arg->nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_NLL_SEGMENTATION;
	if (arg->rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_RB_FLTSIZE_9x9;
	if (!arg->rb_flt.r_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_R_FLT_DIS;
	if (!arg->rb_flt.b_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_B_FLT_DIS;
	if (!arg->g_flt.gb_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_GB_FLT_DIS;
	if (!arg->g_flt.gr_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_GR_FLT_DIS;

	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_DPF_MODE,
			      isp_dpf_mode);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_NF_GAIN_B,
		     arg->gain.nf_b_gain);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_NF_GAIN_R,
		     arg->gain.nf_r_gain);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_NF_GAIN_GB,
		     arg->gain.nf_gb_gain);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_NF_GAIN_GR,
		     arg->gain.nf_gr_gain);

	for (i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; i++) {
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_DPF_NULL_COEFF_0 + i * 4,
			     arg->nll.coeff[i]);
	}

	spatial_coeff = arg->g_flt.spatial_coeff[0] |
			(arg->g_flt.spatial_coeff[1] << 8) |
			(arg->g_flt.spatial_coeff[2] << 16) |
			(arg->g_flt.spatial_coeff[3] << 24);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_S_WEIGHT_G_1_4,
		     spatial_coeff);

	spatial_coeff = arg->g_flt.spatial_coeff[4] |
			(arg->g_flt.spatial_coeff[5] << 8);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_S_WEIGHT_G_5_6,
		     spatial_coeff);

	spatial_coeff = arg->rb_flt.spatial_coeff[0] |
			(arg->rb_flt.spatial_coeff[1] << 8) |
			(arg->rb_flt.spatial_coeff[2] << 16) |
			(arg->rb_flt.spatial_coeff[3] << 24);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_S_WEIGHT_RB_1_4,
		     spatial_coeff);

	spatial_coeff = arg->rb_flt.spatial_coeff[4] |
			(arg->rb_flt.spatial_coeff[5] << 8);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_S_WEIGHT_RB_5_6,
		     spatial_coeff);
}

static void
rkisp1_dpf_strength_config(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_dpf_strength_config *arg)
{
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_STRENGTH_B, arg->b);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_STRENGTH_G, arg->g);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_DPF_STRENGTH_R, arg->r);
}

static void rkisp1_compand_write_px_curve(struct rkisp1_params *params,
					  unsigned int addr, const u8 *curve)
{
	const unsigned int points_per_reg = 6;
	const unsigned int num_regs =
		DIV_ROUND_UP(RKISP1_CIF_ISP_COMPAND_NUM_POINTS,
			     points_per_reg);

	/*
	 * The compand curve is specified as a piecewise linear function with
	 * 64 points. X coordinates are stored as a log2 of the displacement
	 * from the previous point, in 5 bits, with 6 values per register. The
	 * last register stores 4 values.
	 */
	for (unsigned int reg = 0; reg < num_regs; ++reg) {
		unsigned int num_points =
			min(RKISP1_CIF_ISP_COMPAND_NUM_POINTS -
			    reg * points_per_reg, points_per_reg);
		u32 val = 0;

		for (unsigned int i = 0; i < num_points; i++)
			val |= (*curve++ & 0x1f) << (i * 5);

		rkisp1_write(params->rkisp1, addr, val);
		addr += 4;
	}
}

static void
rkisp1_compand_write_curve_mem(struct rkisp1_params *params,
			       unsigned int reg_addr, unsigned int reg_data,
			       const u32 curve[RKISP1_CIF_ISP_COMPAND_NUM_POINTS])
{
	for (unsigned int i = 0; i < RKISP1_CIF_ISP_COMPAND_NUM_POINTS; i++) {
		rkisp1_write(params->rkisp1, reg_addr, i);
		rkisp1_write(params->rkisp1, reg_data, curve[i]);
	}
}

static void
rkisp1_compand_bls_config(struct rkisp1_params *params,
			  const struct rkisp1_cif_isp_compand_bls_config *arg)
{
	static const u32 regs[] = {
		RKISP1_CIF_ISP_COMPAND_BLS_A_FIXED,
		RKISP1_CIF_ISP_COMPAND_BLS_B_FIXED,
		RKISP1_CIF_ISP_COMPAND_BLS_C_FIXED,
		RKISP1_CIF_ISP_COMPAND_BLS_D_FIXED,
	};
	u32 swapped[4];

	rkisp1_bls_swap_regs(params->raw_type, regs, swapped);

	rkisp1_write(params->rkisp1, swapped[0], arg->r);
	rkisp1_write(params->rkisp1, swapped[1], arg->gr);
	rkisp1_write(params->rkisp1, swapped[2], arg->gb);
	rkisp1_write(params->rkisp1, swapped[3], arg->b);
}

static void
rkisp1_compand_expand_config(struct rkisp1_params *params,
			     const struct rkisp1_cif_isp_compand_curve_config *arg)
{
	rkisp1_compand_write_px_curve(params, RKISP1_CIF_ISP_COMPAND_EXPAND_PX_N(0),
				      arg->px);
	rkisp1_compand_write_curve_mem(params, RKISP1_CIF_ISP_COMPAND_EXPAND_Y_ADDR,
				       RKISP1_CIF_ISP_COMPAND_EXPAND_Y_WRITE_DATA,
				       arg->y);
	rkisp1_compand_write_curve_mem(params, RKISP1_CIF_ISP_COMPAND_EXPAND_X_ADDR,
				       RKISP1_CIF_ISP_COMPAND_EXPAND_X_WRITE_DATA,
				       arg->x);
}

static void
rkisp1_compand_compress_config(struct rkisp1_params *params,
			       const struct rkisp1_cif_isp_compand_curve_config *arg)
{
	rkisp1_compand_write_px_curve(params, RKISP1_CIF_ISP_COMPAND_COMPRESS_PX_N(0),
				      arg->px);
	rkisp1_compand_write_curve_mem(params, RKISP1_CIF_ISP_COMPAND_COMPRESS_Y_ADDR,
				       RKISP1_CIF_ISP_COMPAND_COMPRESS_Y_WRITE_DATA,
				       arg->y);
	rkisp1_compand_write_curve_mem(params, RKISP1_CIF_ISP_COMPAND_COMPRESS_X_ADDR,
				       RKISP1_CIF_ISP_COMPAND_COMPRESS_X_WRITE_DATA,
				       arg->x);
}

static void rkisp1_wdr_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_wdr_config *arg)
{
	unsigned int i;
	u32 value;

	value = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_WDR_CTRL)
	      & ~(RKISP1_CIF_ISP_WDR_USE_IREF |
		  RKISP1_CIF_ISP_WDR_COLOR_SPACE_SELECT |
		  RKISP1_CIF_ISP_WDR_CR_MAPPING_DISABLE |
		  RKISP1_CIF_ISP_WDR_USE_Y9_8 |
		  RKISP1_CIF_ISP_WDR_USE_RGB7_8 |
		  RKISP1_CIF_ISP_WDR_DISABLE_TRANSIENT |
		  RKISP1_CIF_ISP_WDR_RGB_FACTOR_MASK);

	/* Colorspace and chrominance mapping */
	if (arg->use_rgb_colorspace)
		value |= RKISP1_CIF_ISP_WDR_COLOR_SPACE_SELECT;

	if (!arg->use_rgb_colorspace && arg->bypass_chroma_mapping)
		value |= RKISP1_CIF_ISP_WDR_CR_MAPPING_DISABLE;

	/* Illumination reference */
	if (arg->use_iref) {
		value |= RKISP1_CIF_ISP_WDR_USE_IREF;

		if (arg->iref_config.use_y9_8)
			value |= RKISP1_CIF_ISP_WDR_USE_Y9_8;

		if (arg->iref_config.use_rgb7_8)
			value |= RKISP1_CIF_ISP_WDR_USE_RGB7_8;

		if (arg->iref_config.disable_transient)
			value |= RKISP1_CIF_ISP_WDR_DISABLE_TRANSIENT;

		value |= FIELD_PREP(RKISP1_CIF_ISP_WDR_RGB_FACTOR_MASK,
				    min(arg->iref_config.rgb_factor,
					RKISP1_CIF_ISP_WDR_RGB_FACTOR_MAX));
	}

	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_WDR_CTRL, value);

	/* RGB and Luminance offsets */
	value = FIELD_PREP(RKISP1_CIF_ISP_WDR_RGB_OFFSET_MASK,
			   arg->rgb_offset)
	      | FIELD_PREP(RKISP1_CIF_ISP_WDR_LUM_OFFSET_MASK,
			   arg->luma_offset);
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_WDR_OFFSET, value);

	/* DeltaMin */
	value = FIELD_PREP(RKISP1_CIF_ISP_WDR_DMIN_THRESH_MASK,
			   arg->dmin_thresh)
	      | FIELD_PREP(RKISP1_CIF_ISP_WDR_DMIN_STRENGTH_MASK,
			   min(arg->dmin_strength,
			       RKISP1_CIF_ISP_WDR_DMIN_STRENGTH_MAX));
	rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_WDR_DELTAMIN, value);

	/* Tone curve */
	for (i = 0; i < RKISP1_CIF_ISP_WDR_CURVE_NUM_DY_REGS; i++)
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_WDR_TONECURVE(i),
			     arg->tone_curve.dY[i]);
	for (i = 0; i < RKISP1_CIF_ISP_WDR_CURVE_NUM_COEFF; i++)
		rkisp1_write(params->rkisp1, RKISP1_CIF_ISP_WDR_TONECURVE_YM(i),
			     arg->tone_curve.ym[i] &
				     RKISP1_CIF_ISP_WDR_TONE_CURVE_YM_MASK);
}

static void
rkisp1_isp_isr_other_config(struct rkisp1_params *params,
			    const struct rkisp1_params_cfg *new_params)
{
	unsigned int module_en_update, module_cfg_update, module_ens;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if (!rkisp1_has_feature(params->rkisp1, BLS)) {
		module_en_update &= ~RKISP1_CIF_ISP_MODULE_BLS;
		module_cfg_update &= ~RKISP1_CIF_ISP_MODULE_BLS;
		module_ens &= ~RKISP1_CIF_ISP_MODULE_BLS;
	}

	/* update dpc config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_DPCC)
		rkisp1_dpcc_config(params,
				   &new_params->others.dpcc_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_DPCC) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_DPCC)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_DPCC_MODE,
					      RKISP1_CIF_ISP_DPCC_MODE_DPCC_ENABLE);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_DPCC_MODE,
						RKISP1_CIF_ISP_DPCC_MODE_DPCC_ENABLE);
	}

	/* update bls config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_BLS)
		rkisp1_bls_config(params,
				  &new_params->others.bls_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_BLS) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_BLS)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_BLS_CTRL,
					      RKISP1_CIF_ISP_BLS_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_BLS_CTRL,
						RKISP1_CIF_ISP_BLS_ENA);
	}

	/* update sdg config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_SDG)
		rkisp1_sdg_config(params,
				  &new_params->others.sdg_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_SDG) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_SDG)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_CTRL,
					      RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_CTRL,
						RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
	}

	/* update awb gains */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_AWB_GAIN)
		params->ops->awb_gain_config(params, &new_params->others.awb_gain_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_AWB_GAIN) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_AWB_GAIN)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_CTRL,
					      RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_CTRL,
						RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	}

	/* update bdm config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_BDM)
		rkisp1_bdm_config(params,
				  &new_params->others.bdm_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_BDM) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_BDM)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_DEMOSAIC,
					      RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_DEMOSAIC,
						RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
	}

	/* update filter config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_FLT)
		rkisp1_flt_config(params,
				  &new_params->others.flt_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_FLT) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_FLT)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_FILT_MODE,
					      RKISP1_CIF_ISP_FLT_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_FILT_MODE,
						RKISP1_CIF_ISP_FLT_ENA);
	}

	/* update ctk config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_CTK)
		rkisp1_ctk_config(params,
				  &new_params->others.ctk_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_CTK)
		rkisp1_ctk_enable(params, !!(module_ens & RKISP1_CIF_ISP_MODULE_CTK));

	/* update goc config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_GOC)
		params->ops->goc_config(params, &new_params->others.goc_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_GOC) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_GOC)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_CTRL,
					      RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_CTRL,
						RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	}

	/* update cproc config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_CPROC)
		rkisp1_cproc_config(params,
				    &new_params->others.cproc_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_CPROC) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_CPROC)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_C_PROC_CTRL,
					      RKISP1_CIF_C_PROC_CTR_ENABLE);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_C_PROC_CTRL,
						RKISP1_CIF_C_PROC_CTR_ENABLE);
	}

	/* update ie config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_IE)
		rkisp1_ie_config(params, &new_params->others.ie_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_IE)
		rkisp1_ie_enable(params, !!(module_ens & RKISP1_CIF_ISP_MODULE_IE));

	/* update dpf config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_DPF)
		rkisp1_dpf_config(params, &new_params->others.dpf_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_DPF) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_DPF)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_DPF_MODE,
					      RKISP1_CIF_ISP_DPF_MODE_EN);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_DPF_MODE,
						RKISP1_CIF_ISP_DPF_MODE_EN);
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_DPF_STRENGTH) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_DPF_STRENGTH)) {
		/* update dpf strength config */
		rkisp1_dpf_strength_config(params,
					   &new_params->others.dpf_strength_config);
	}
}

static void
rkisp1_isp_isr_lsc_config(struct rkisp1_params *params,
			  const struct rkisp1_params_cfg *new_params)
{
	unsigned int module_en_update, module_cfg_update, module_ens;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	/* update lsc config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_LSC)
		rkisp1_lsc_config(params,
				  &new_params->others.lsc_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_LSC) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_LSC)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_LSC_CTRL,
					      RKISP1_CIF_ISP_LSC_CTRL_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_LSC_CTRL,
						RKISP1_CIF_ISP_LSC_CTRL_ENA);
	}
}

static void rkisp1_isp_isr_meas_config(struct rkisp1_params *params,
				       struct  rkisp1_params_cfg *new_params)
{
	unsigned int module_en_update, module_cfg_update, module_ens;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	/* update awb config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_AWB)
		params->ops->awb_meas_config(params, &new_params->meas.awb_meas_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_AWB)
		params->ops->awb_meas_enable(params,
					     &new_params->meas.awb_meas_config,
					     !!(module_ens & RKISP1_CIF_ISP_MODULE_AWB));

	/* update afc config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_AFC)
		params->ops->afm_config(params,
					&new_params->meas.afc_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_AFC) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_AFC)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_AFM_CTRL,
					      RKISP1_CIF_ISP_AFM_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_AFM_CTRL,
						RKISP1_CIF_ISP_AFM_ENA);
	}

	/* update hst config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_HST)
		params->ops->hst_config(params,
					&new_params->meas.hst_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_HST)
		params->ops->hst_enable(params,
					&new_params->meas.hst_config,
					!!(module_ens & RKISP1_CIF_ISP_MODULE_HST));

	/* update aec config */
	if (module_cfg_update & RKISP1_CIF_ISP_MODULE_AEC)
		params->ops->aec_config(params,
					&new_params->meas.aec_config);

	if (module_en_update & RKISP1_CIF_ISP_MODULE_AEC) {
		if (module_ens & RKISP1_CIF_ISP_MODULE_AEC)
			rkisp1_param_set_bits(params,
					      RKISP1_CIF_ISP_EXP_CTRL,
					      RKISP1_CIF_ISP_EXP_ENA);
		else
			rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_EXP_CTRL,
						RKISP1_CIF_ISP_EXP_ENA);
	}
}

/*------------------------------------------------------------------------------
 * Extensible parameters format handling
 */

static void
rkisp1_ext_params_bls(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_bls_config *bls = &block->bls;

	if (bls->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_BLS_CTRL,
					RKISP1_CIF_ISP_BLS_ENA);
		return;
	}

	rkisp1_bls_config(params, &bls->config);

	if ((bls->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(bls->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_BLS_CTRL,
				      RKISP1_CIF_ISP_BLS_ENA);
}

static void
rkisp1_ext_params_dpcc(struct rkisp1_params *params,
		       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_dpcc_config *dpcc = &block->dpcc;

	if (dpcc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DPCC_MODE,
					RKISP1_CIF_ISP_DPCC_MODE_DPCC_ENABLE);
		return;
	}

	rkisp1_dpcc_config(params, &dpcc->config);

	if ((dpcc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(dpcc->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_DPCC_MODE,
				      RKISP1_CIF_ISP_DPCC_MODE_DPCC_ENABLE);
}

static void
rkisp1_ext_params_sdg(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_sdg_config *sdg = &block->sdg;

	if (sdg->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
		return;
	}

	rkisp1_sdg_config(params, &sdg->config);

	if ((sdg->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(sdg->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
				      RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
}

static void
rkisp1_ext_params_lsc(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_lsc_config *lsc = &block->lsc;

	if (lsc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
					RKISP1_CIF_ISP_LSC_CTRL_ENA);
		return;
	}

	rkisp1_lsc_config(params, &lsc->config);

	if ((lsc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(lsc->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
				      RKISP1_CIF_ISP_LSC_CTRL_ENA);
}

static void
rkisp1_ext_params_awbg(struct rkisp1_params *params,
		       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_awb_gain_config *awbg = &block->awbg;

	if (awbg->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
		return;
	}

	params->ops->awb_gain_config(params, &awbg->config);

	if ((awbg->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(awbg->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
				      RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
}

static void
rkisp1_ext_params_flt(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_flt_config *flt = &block->flt;

	if (flt->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_FILT_MODE,
					RKISP1_CIF_ISP_FLT_ENA);
		return;
	}

	rkisp1_flt_config(params, &flt->config);

	if ((flt->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(flt->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_FILT_MODE,
				      RKISP1_CIF_ISP_FLT_ENA);
}

static void
rkisp1_ext_params_bdm(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_bdm_config *bdm = &block->bdm;

	if (bdm->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DEMOSAIC,
					RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
		return;
	}

	rkisp1_bdm_config(params, &bdm->config);

	if ((bdm->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(bdm->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_DEMOSAIC,
				      RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
}

static void
rkisp1_ext_params_ctk(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_ctk_config *ctk = &block->ctk;

	if (ctk->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_ctk_enable(params, false);
		return;
	}

	rkisp1_ctk_config(params, &ctk->config);

	if ((ctk->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(ctk->header.type)))
		rkisp1_ctk_enable(params, true);
}

static void
rkisp1_ext_params_goc(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_goc_config *goc = &block->goc;

	if (goc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
		return;
	}

	params->ops->goc_config(params, &goc->config);

	/*
	 * Unconditionally re-enable the GOC module which gets disabled by
	 * goc_config().
	 */
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
			      RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
}

static void
rkisp1_ext_params_dpf(struct rkisp1_params *params,
		      const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_dpf_config *dpf = &block->dpf;

	if (dpf->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DPF_MODE,
					RKISP1_CIF_ISP_DPF_MODE_EN);
		return;
	}

	rkisp1_dpf_config(params, &dpf->config);

	if ((dpf->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(dpf->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_DPF_MODE,
				      RKISP1_CIF_ISP_DPF_MODE_EN);
}

static void
rkisp1_ext_params_dpfs(struct rkisp1_params *params,
		       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_dpf_strength_config *dpfs = &block->dpfs;

	rkisp1_dpf_strength_config(params, &dpfs->config);
}

static void
rkisp1_ext_params_cproc(struct rkisp1_params *params,
			const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_cproc_config *cproc = &block->cproc;

	if (cproc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_C_PROC_CTRL,
					RKISP1_CIF_C_PROC_CTR_ENABLE);
		return;
	}

	rkisp1_cproc_config(params, &cproc->config);

	if ((cproc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(cproc->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_C_PROC_CTRL,
				      RKISP1_CIF_C_PROC_CTR_ENABLE);
}

static void
rkisp1_ext_params_ie(struct rkisp1_params *params,
		     const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_ie_config *ie = &block->ie;

	if (ie->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_ie_enable(params, false);
		return;
	}

	rkisp1_ie_config(params, &ie->config);

	if ((ie->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(ie->header.type)))
		rkisp1_ie_enable(params, true);
}

static void
rkisp1_ext_params_awbm(struct rkisp1_params *params,
		       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_awb_meas_config *awbm = &block->awbm;

	if (awbm->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		params->ops->awb_meas_enable(params, &awbm->config,
					     false);
		return;
	}

	params->ops->awb_meas_config(params, &awbm->config);

	if ((awbm->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(awbm->header.type)))
		params->ops->awb_meas_enable(params, &awbm->config,
					     true);
}

static void
rkisp1_ext_params_hstm(struct rkisp1_params *params,
		       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_hst_config *hst = &block->hst;

	if (hst->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		params->ops->hst_enable(params, &hst->config, false);
		return;
	}

	params->ops->hst_config(params, &hst->config);

	if ((hst->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(hst->header.type)))
		params->ops->hst_enable(params, &hst->config, true);
}

static void
rkisp1_ext_params_aecm(struct rkisp1_params *params,
		       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_aec_config *aec = &block->aec;

	if (aec->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_EXP_CTRL,
					RKISP1_CIF_ISP_EXP_ENA);
		return;
	}

	params->ops->aec_config(params, &aec->config);

	if ((aec->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(aec->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_EXP_CTRL,
				      RKISP1_CIF_ISP_EXP_ENA);
}

static void
rkisp1_ext_params_afcm(struct rkisp1_params *params,
		       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_afc_config *afc = &block->afc;

	if (afc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
					RKISP1_CIF_ISP_AFM_ENA);
		return;
	}

	params->ops->afm_config(params, &afc->config);

	if ((afc->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(afc->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
				      RKISP1_CIF_ISP_AFM_ENA);
}

static void rkisp1_ext_params_compand_bls(struct rkisp1_params *params,
					  const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_compand_bls_config *bls =
		&block->compand_bls;

	if (bls->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_COMPAND_CTRL,
					RKISP1_CIF_ISP_COMPAND_CTRL_BLS_ENABLE);
		return;
	}

	rkisp1_compand_bls_config(params, &bls->config);

	if ((bls->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(bls->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_COMPAND_CTRL,
				      RKISP1_CIF_ISP_COMPAND_CTRL_BLS_ENABLE);
}

static void rkisp1_ext_params_compand_expand(struct rkisp1_params *params,
					     const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_compand_curve_config *curve =
		&block->compand_curve;

	if (curve->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_COMPAND_CTRL,
					RKISP1_CIF_ISP_COMPAND_CTRL_EXPAND_ENABLE);
		return;
	}

	rkisp1_compand_expand_config(params, &curve->config);

	if ((curve->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(curve->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_COMPAND_CTRL,
				      RKISP1_CIF_ISP_COMPAND_CTRL_EXPAND_ENABLE);
}

static void rkisp1_ext_params_compand_compress(struct rkisp1_params *params,
					       const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_compand_curve_config *curve =
		&block->compand_curve;

	if (curve->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_COMPAND_CTRL,
					RKISP1_CIF_ISP_COMPAND_CTRL_COMPRESS_ENABLE);
		return;
	}

	rkisp1_compand_compress_config(params, &curve->config);

	if ((curve->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(curve->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_COMPAND_CTRL,
				      RKISP1_CIF_ISP_COMPAND_CTRL_COMPRESS_ENABLE);
}

static void rkisp1_ext_params_wdr(struct rkisp1_params *params,
				  const union rkisp1_ext_params_config *block)
{
	const struct rkisp1_ext_params_wdr_config *wdr = &block->wdr;

	if (wdr->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_WDR_CTRL,
					RKISP1_CIF_ISP_WDR_CTRL_ENABLE);
		return;
	}

	rkisp1_wdr_config(params, &wdr->config);

	if ((wdr->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE) &&
	    !(params->enabled_blocks & BIT(wdr->header.type)))
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_WDR_CTRL,
				      RKISP1_CIF_ISP_WDR_CTRL_ENABLE);
}

typedef void (*rkisp1_block_handler)(struct rkisp1_params *params,
			     const union rkisp1_ext_params_config *config);

static const struct rkisp1_ext_params_handler {
	size_t size;
	rkisp1_block_handler handler;
	unsigned int group;
	unsigned int features;
} rkisp1_ext_params_handlers[] = {
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_BLS] = {
		.size		= sizeof(struct rkisp1_ext_params_bls_config),
		.handler	= rkisp1_ext_params_bls,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
		.features       = RKISP1_FEATURE_BLS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_DPCC] = {
		.size		= sizeof(struct rkisp1_ext_params_dpcc_config),
		.handler	= rkisp1_ext_params_dpcc,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_SDG] = {
		.size		= sizeof(struct rkisp1_ext_params_sdg_config),
		.handler	= rkisp1_ext_params_sdg,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_AWB_GAIN] = {
		.size		= sizeof(struct rkisp1_ext_params_awb_gain_config),
		.handler	= rkisp1_ext_params_awbg,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_FLT] = {
		.size		= sizeof(struct rkisp1_ext_params_flt_config),
		.handler	= rkisp1_ext_params_flt,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_BDM] = {
		.size		= sizeof(struct rkisp1_ext_params_bdm_config),
		.handler	= rkisp1_ext_params_bdm,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_CTK] = {
		.size		= sizeof(struct rkisp1_ext_params_ctk_config),
		.handler	= rkisp1_ext_params_ctk,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_GOC] = {
		.size		= sizeof(struct rkisp1_ext_params_goc_config),
		.handler	= rkisp1_ext_params_goc,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_DPF] = {
		.size		= sizeof(struct rkisp1_ext_params_dpf_config),
		.handler	= rkisp1_ext_params_dpf,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_DPF_STRENGTH] = {
		.size		= sizeof(struct rkisp1_ext_params_dpf_strength_config),
		.handler	= rkisp1_ext_params_dpfs,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_CPROC] = {
		.size		= sizeof(struct rkisp1_ext_params_cproc_config),
		.handler	= rkisp1_ext_params_cproc,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_IE] = {
		.size		= sizeof(struct rkisp1_ext_params_ie_config),
		.handler	= rkisp1_ext_params_ie,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_LSC] = {
		.size		= sizeof(struct rkisp1_ext_params_lsc_config),
		.handler	= rkisp1_ext_params_lsc,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_LSC,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_AWB_MEAS] = {
		.size		= sizeof(struct rkisp1_ext_params_awb_meas_config),
		.handler	= rkisp1_ext_params_awbm,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_HST_MEAS] = {
		.size		= sizeof(struct rkisp1_ext_params_hst_config),
		.handler	= rkisp1_ext_params_hstm,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_AEC_MEAS] = {
		.size		= sizeof(struct rkisp1_ext_params_aec_config),
		.handler	= rkisp1_ext_params_aecm,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_AFC_MEAS] = {
		.size		= sizeof(struct rkisp1_ext_params_afc_config),
		.handler	= rkisp1_ext_params_afcm,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_COMPAND_BLS] = {
		.size		= sizeof(struct rkisp1_ext_params_compand_bls_config),
		.handler	= rkisp1_ext_params_compand_bls,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
		.features	= RKISP1_FEATURE_COMPAND,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_COMPAND_EXPAND] = {
		.size		= sizeof(struct rkisp1_ext_params_compand_curve_config),
		.handler	= rkisp1_ext_params_compand_expand,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
		.features	= RKISP1_FEATURE_COMPAND,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_COMPAND_COMPRESS] = {
		.size		= sizeof(struct rkisp1_ext_params_compand_curve_config),
		.handler	= rkisp1_ext_params_compand_compress,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
		.features	= RKISP1_FEATURE_COMPAND,
	},
	[RKISP1_EXT_PARAMS_BLOCK_TYPE_WDR] = {
		.size		= sizeof(struct rkisp1_ext_params_wdr_config),
		.handler	= rkisp1_ext_params_wdr,
		.group		= RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS,
	},
};

static void rkisp1_ext_params_config(struct rkisp1_params *params,
				     struct rkisp1_ext_params_cfg *cfg,
				     u32 block_group_mask)
{
	size_t block_offset = 0;

	if (WARN_ON(!cfg))
		return;

	/* Walk the list of parameter blocks and process them. */
	while (block_offset < cfg->data_size) {
		const struct rkisp1_ext_params_handler *block_handler;
		const union rkisp1_ext_params_config *block;

		block = (const union rkisp1_ext_params_config *)
			&cfg->data[block_offset];
		block_offset += block->header.size;

		/*
		 * Make sure the block is supported by the platform and in the
		 * list of groups to configure.
		 */
		block_handler = &rkisp1_ext_params_handlers[block->header.type];
		if (!(block_handler->group & block_group_mask))
			continue;

		if ((params->rkisp1->info->features & block_handler->features) !=
		    block_handler->features)
			continue;

		block_handler->handler(params, block);

		if (block->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE)
			params->enabled_blocks &= ~BIT(block->header.type);
		else if (block->header.flags & RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE)
			params->enabled_blocks |= BIT(block->header.type);
	}
}

static void rkisp1_params_complete_buffer(struct rkisp1_params *params,
					  struct rkisp1_params_buffer *buf,
					  unsigned int frame_sequence)
{
	list_del(&buf->queue);

	buf->vb.sequence = frame_sequence;
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

void rkisp1_params_isr(struct rkisp1_device *rkisp1)
{
	struct rkisp1_params *params = &rkisp1->params;
	struct rkisp1_params_buffer *cur_buf;

	spin_lock(&params->config_lock);

	cur_buf = list_first_entry_or_null(&params->params,
					   struct rkisp1_params_buffer, queue);
	if (!cur_buf)
		goto unlock;

	if (params->metafmt->dataformat == V4L2_META_FMT_RK_ISP1_PARAMS) {
		rkisp1_isp_isr_other_config(params, cur_buf->cfg);
		rkisp1_isp_isr_lsc_config(params, cur_buf->cfg);
		rkisp1_isp_isr_meas_config(params, cur_buf->cfg);
	} else {
		rkisp1_ext_params_config(params, cur_buf->cfg,
					 RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS |
					 RKISP1_EXT_PARAMS_BLOCK_GROUP_LSC);
	}

	/* update shadow register immediately */
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
			      RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD);

	/*
	 * This isr is called when the ISR finishes processing a frame
	 * (RKISP1_CIF_ISP_FRAME). Configurations performed here will be
	 * applied on the next frame. Since frame_sequence is updated on the
	 * vertical sync signal, we should use frame_sequence + 1 here to
	 * indicate to userspace on which frame these parameters are being
	 * applied.
	 */
	rkisp1_params_complete_buffer(params, cur_buf,
				      rkisp1->isp.frame_sequence + 1);

unlock:
	spin_unlock(&params->config_lock);
}

static const struct rkisp1_cif_isp_awb_meas_config rkisp1_awb_params_default_config = {
	{
		0, 0, RKISP1_DEFAULT_WIDTH, RKISP1_DEFAULT_HEIGHT
	},
	RKISP1_CIF_ISP_AWB_MODE_YCBCR, 200, 30, 20, 20, 0, 128, 128
};

static const struct rkisp1_cif_isp_aec_config rkisp1_aec_params_default_config = {
	RKISP1_CIF_ISP_EXP_MEASURING_MODE_0,
	RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_0,
	{
		RKISP1_DEFAULT_WIDTH >> 2, RKISP1_DEFAULT_HEIGHT >> 2,
		RKISP1_DEFAULT_WIDTH >> 1, RKISP1_DEFAULT_HEIGHT >> 1
	}
};

static const struct rkisp1_cif_isp_hst_config rkisp1_hst_params_default_config = {
	RKISP1_CIF_ISP_HISTOGRAM_MODE_RGB_COMBINED,
	3,
	{
		RKISP1_DEFAULT_WIDTH >> 2, RKISP1_DEFAULT_HEIGHT >> 2,
		RKISP1_DEFAULT_WIDTH >> 1, RKISP1_DEFAULT_HEIGHT >> 1
	},
	{
		0, /* To be filled in with 0x01 at runtime. */
	}
};

static const struct rkisp1_cif_isp_afc_config rkisp1_afc_params_default_config = {
	1,
	{
		{
			300, 225, 200, 150
		}
	},
	4,
	14
};

void rkisp1_params_pre_configure(struct rkisp1_params *params,
				 enum rkisp1_fmt_raw_pat_type bayer_pat,
				 enum v4l2_quantization quantization,
				 enum v4l2_ycbcr_encoding ycbcr_encoding)
{
	struct rkisp1_cif_isp_hst_config hst = rkisp1_hst_params_default_config;
	struct rkisp1_params_buffer *cur_buf;

	params->quantization = quantization;
	params->ycbcr_encoding = ycbcr_encoding;
	params->raw_type = bayer_pat;

	params->ops->awb_meas_config(params, &rkisp1_awb_params_default_config);
	params->ops->awb_meas_enable(params, &rkisp1_awb_params_default_config,
				     true);

	params->ops->aec_config(params, &rkisp1_aec_params_default_config);
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_EXP_CTRL,
			      RKISP1_CIF_ISP_EXP_ENA);

	params->ops->afm_config(params, &rkisp1_afc_params_default_config);
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
			      RKISP1_CIF_ISP_AFM_ENA);

	memset(hst.hist_weight, 0x01, sizeof(hst.hist_weight));
	params->ops->hst_config(params, &hst);
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_HIST_PROP_V10,
			      rkisp1_hst_params_default_config.mode);

	rkisp1_csm_config(params);

	spin_lock_irq(&params->config_lock);

	/* apply the first buffer if there is one already */

	cur_buf = list_first_entry_or_null(&params->params,
					   struct rkisp1_params_buffer, queue);
	if (!cur_buf)
		goto unlock;

	if (params->metafmt->dataformat == V4L2_META_FMT_RK_ISP1_PARAMS) {
		rkisp1_isp_isr_other_config(params, cur_buf->cfg);
		rkisp1_isp_isr_meas_config(params, cur_buf->cfg);
	} else {
		rkisp1_ext_params_config(params, cur_buf->cfg,
					 RKISP1_EXT_PARAMS_BLOCK_GROUP_OTHERS);
	}

	/* update shadow register immediately */
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
			      RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD);

unlock:
	spin_unlock_irq(&params->config_lock);
}

void rkisp1_params_post_configure(struct rkisp1_params *params)
{
	struct rkisp1_params_buffer *cur_buf;

	spin_lock_irq(&params->config_lock);

	/*
	 * Apply LSC parameters from the first buffer (if any is already
	 * available. This must be done after the ISP gets started in the
	 * ISP8000Nano v18.02 (found in the i.MX8MP) as access to the LSC RAM
	 * is gated by the ISP_CTRL.ISP_ENABLE bit. As this initialization
	 * ordering doesn't affect other ISP versions negatively, do so
	 * unconditionally.
	 */
	cur_buf = list_first_entry_or_null(&params->params,
					   struct rkisp1_params_buffer, queue);
	if (!cur_buf)
		goto unlock;

	if (params->metafmt->dataformat == V4L2_META_FMT_RK_ISP1_PARAMS)
		rkisp1_isp_isr_lsc_config(params, cur_buf->cfg);
	else
		rkisp1_ext_params_config(params, cur_buf->cfg,
					 RKISP1_EXT_PARAMS_BLOCK_GROUP_LSC);

	/* update shadow register immediately */
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
			      RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD);

	rkisp1_params_complete_buffer(params, cur_buf, 0);

unlock:
	spin_unlock_irq(&params->config_lock);
}

/*
 * Not called when the camera is active, therefore there is no need to acquire
 * a lock.
 */
void rkisp1_params_disable(struct rkisp1_params *params)
{
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DPCC_MODE,
				RKISP1_CIF_ISP_DPCC_MODE_DPCC_ENABLE);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
				RKISP1_CIF_ISP_LSC_CTRL_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_BLS_CTRL,
				RKISP1_CIF_ISP_BLS_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DEMOSAIC,
				RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_FILT_MODE,
				RKISP1_CIF_ISP_FLT_ENA);
	params->ops->awb_meas_enable(params, NULL, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_EXP_CTRL,
				RKISP1_CIF_ISP_EXP_ENA);
	rkisp1_ctk_enable(params, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_C_PROC_CTRL,
				RKISP1_CIF_C_PROC_CTR_ENABLE);
	params->ops->hst_enable(params, NULL, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
				RKISP1_CIF_ISP_AFM_ENA);
	rkisp1_ie_enable(params, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DPF_MODE,
				RKISP1_CIF_ISP_DPF_MODE_EN);
}

static const struct rkisp1_params_ops rkisp1_v10_params_ops = {
	.lsc_matrix_config = rkisp1_lsc_matrix_config_v10,
	.goc_config = rkisp1_goc_config_v10,
	.awb_meas_config = rkisp1_awb_meas_config_v10,
	.awb_meas_enable = rkisp1_awb_meas_enable_v10,
	.awb_gain_config = rkisp1_awb_gain_config_v10,
	.aec_config = rkisp1_aec_config_v10,
	.hst_config = rkisp1_hst_config_v10,
	.hst_enable = rkisp1_hst_enable_v10,
	.afm_config = rkisp1_afm_config_v10,
};

static const struct rkisp1_params_ops rkisp1_v12_params_ops = {
	.lsc_matrix_config = rkisp1_lsc_matrix_config_v12,
	.goc_config = rkisp1_goc_config_v12,
	.awb_meas_config = rkisp1_awb_meas_config_v12,
	.awb_meas_enable = rkisp1_awb_meas_enable_v12,
	.awb_gain_config = rkisp1_awb_gain_config_v12,
	.aec_config = rkisp1_aec_config_v12,
	.hst_config = rkisp1_hst_config_v12,
	.hst_enable = rkisp1_hst_enable_v12,
	.afm_config = rkisp1_afm_config_v12,
};

static int rkisp1_params_enum_fmt_meta_out(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);

	if (f->index >= ARRAY_SIZE(rkisp1_params_formats) ||
	    f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = rkisp1_params_formats[f->index].dataformat;

	return 0;
}

static int rkisp1_params_g_fmt_meta_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_params *params = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	*meta = *params->metafmt;

	return 0;
}

static int rkisp1_params_try_fmt_meta_out(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	*meta = *rkisp1_params_get_format_info(meta->dataformat);

	return 0;
}

static int rkisp1_params_s_fmt_meta_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_params *params = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	if (vb2_is_busy(video->queue))
		return -EBUSY;

	params->metafmt = rkisp1_params_get_format_info(meta->dataformat);
	*meta = *params->metafmt;

	return 0;
}

static int rkisp1_params_querycap(struct file *file,
				  void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	strscpy(cap->driver, RKISP1_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, vdev->name, sizeof(cap->card));
	strscpy(cap->bus_info, RKISP1_BUS_INFO, sizeof(cap->bus_info));

	return 0;
}

/* ISP params video device IOCTLs */
static const struct v4l2_ioctl_ops rkisp1_params_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_out = rkisp1_params_enum_fmt_meta_out,
	.vidioc_g_fmt_meta_out = rkisp1_params_g_fmt_meta_out,
	.vidioc_s_fmt_meta_out = rkisp1_params_s_fmt_meta_out,
	.vidioc_try_fmt_meta_out = rkisp1_params_try_fmt_meta_out,
	.vidioc_querycap = rkisp1_params_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int rkisp1_params_vb2_queue_setup(struct vb2_queue *vq,
					 unsigned int *num_buffers,
					 unsigned int *num_planes,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	struct rkisp1_params *params = vq->drv_priv;

	*num_buffers = clamp_t(u32, *num_buffers,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MIN,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MAX);

	*num_planes = 1;

	sizes[0] = params->metafmt->buffersize;

	return 0;
}

static int rkisp1_params_vb2_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_params_buffer *params_buf = to_rkisp1_params_buffer(vbuf);
	struct rkisp1_params *params = vb->vb2_queue->drv_priv;

	params_buf->cfg = kvmalloc(params->metafmt->buffersize,
				   GFP_KERNEL);
	if (!params_buf->cfg)
		return -ENOMEM;

	return 0;
}

static void rkisp1_params_vb2_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_params_buffer *params_buf = to_rkisp1_params_buffer(vbuf);

	kvfree(params_buf->cfg);
	params_buf->cfg = NULL;
}

static void rkisp1_params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_params_buffer *params_buf = to_rkisp1_params_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkisp1_params *params = vq->drv_priv;

	spin_lock_irq(&params->config_lock);
	list_add_tail(&params_buf->queue, &params->params);
	spin_unlock_irq(&params->config_lock);
}

static int rkisp1_params_prepare_ext_params(struct rkisp1_params *params,
					    struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_params_buffer *params_buf = to_rkisp1_params_buffer(vbuf);
	size_t header_size = offsetof(struct rkisp1_ext_params_cfg, data);
	struct rkisp1_ext_params_cfg *cfg = params_buf->cfg;
	size_t payload_size = vb2_get_plane_payload(vb, 0);
	struct rkisp1_ext_params_cfg *usr_cfg =
		vb2_plane_vaddr(&vbuf->vb2_buf, 0);
	size_t block_offset = 0;
	size_t cfg_size;

	/*
	 * Validate the buffer payload size before copying the parameters. The
	 * payload has to be smaller than the destination buffer size and larger
	 * than the header size.
	 */
	if (payload_size > params->metafmt->buffersize) {
		dev_dbg(params->rkisp1->dev,
			"Too large buffer payload size %zu\n", payload_size);
		return -EINVAL;
	}

	if (payload_size < header_size) {
		dev_dbg(params->rkisp1->dev,
			"Buffer payload %zu smaller than header size %zu\n",
			payload_size, header_size);
		return -EINVAL;
	}

	/*
	 * Copy the parameters buffer to the internal scratch buffer to avoid
	 * userspace modifying the buffer content while the driver processes it.
	 */
	memcpy(cfg, usr_cfg, payload_size);

	/* Only v1 is supported at the moment. */
	if (cfg->version != RKISP1_EXT_PARAM_BUFFER_V1) {
		dev_dbg(params->rkisp1->dev,
			"Unsupported extensible format version: %u\n",
			cfg->version);
		return -EINVAL;
	}

	/* Validate the size reported in the parameters buffer header. */
	cfg_size = header_size + cfg->data_size;
	if (cfg_size != payload_size) {
		dev_dbg(params->rkisp1->dev,
			"Data size %zu different than buffer payload size %zu\n",
			cfg_size, payload_size);
		return -EINVAL;
	}

	/* Walk the list of parameter blocks and validate them. */
	cfg_size = cfg->data_size;
	while (cfg_size >= sizeof(struct rkisp1_ext_params_block_header)) {
		const struct rkisp1_ext_params_block_header *block;
		const struct rkisp1_ext_params_handler *handler;

		block = (const struct rkisp1_ext_params_block_header *)
			&cfg->data[block_offset];

		if (block->type >= ARRAY_SIZE(rkisp1_ext_params_handlers)) {
			dev_dbg(params->rkisp1->dev,
				"Invalid parameters block type\n");
			return -EINVAL;
		}

		if (block->size > cfg_size) {
			dev_dbg(params->rkisp1->dev,
				"Premature end of parameters data\n");
			return -EINVAL;
		}

		if ((block->flags & (RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE |
				     RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE)) ==
		   (RKISP1_EXT_PARAMS_FL_BLOCK_ENABLE |
		    RKISP1_EXT_PARAMS_FL_BLOCK_DISABLE)) {
			dev_dbg(params->rkisp1->dev,
				"Invalid parameters block flags\n");
			return -EINVAL;
		}

		handler = &rkisp1_ext_params_handlers[block->type];
		if (block->size != handler->size) {
			dev_dbg(params->rkisp1->dev,
				"Invalid parameters block size\n");
			return -EINVAL;
		}

		block_offset += block->size;
		cfg_size -= block->size;
	}

	if (cfg_size) {
		dev_dbg(params->rkisp1->dev,
			"Unexpected data after the parameters buffer end\n");
		return -EINVAL;
	}

	return 0;
}

static int rkisp1_params_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct rkisp1_params *params = vb->vb2_queue->drv_priv;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_params_buffer *params_buf = to_rkisp1_params_buffer(vbuf);
	struct rkisp1_params_cfg *cfg = vb2_plane_vaddr(&vbuf->vb2_buf, 0);
	size_t payload = vb2_get_plane_payload(vb, 0);

	if (params->metafmt->dataformat == V4L2_META_FMT_RK_ISP1_EXT_PARAMS)
		return rkisp1_params_prepare_ext_params(params, vb);

	/*
	 * For the fixed parameters format the payload size must be exactly the
	 * size of the parameters structure.
	 */
	if (payload != sizeof(*cfg))
		return -EINVAL;

	/*
	 * Copy the parameters buffer to the internal scratch buffer to avoid
	 * userspace modifying the buffer content while the driver processes it.
	 */
	memcpy(params_buf->cfg, cfg, payload);

	return 0;
}

static void rkisp1_params_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp1_params *params = vq->drv_priv;
	struct rkisp1_params_buffer *buf;
	LIST_HEAD(tmp_list);

	/*
	 * we first move the buffers into a local list 'tmp_list'
	 * and then we can iterate it and call vb2_buffer_done
	 * without holding the lock
	 */
	spin_lock_irq(&params->config_lock);
	list_splice_init(&params->params, &tmp_list);
	spin_unlock_irq(&params->config_lock);

	list_for_each_entry(buf, &tmp_list, queue)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);

	params->enabled_blocks = 0;
}

static const struct vb2_ops rkisp1_params_vb2_ops = {
	.queue_setup = rkisp1_params_vb2_queue_setup,
	.buf_init = rkisp1_params_vb2_buf_init,
	.buf_cleanup = rkisp1_params_vb2_buf_cleanup,
	.buf_queue = rkisp1_params_vb2_buf_queue,
	.buf_prepare = rkisp1_params_vb2_buf_prepare,
	.stop_streaming = rkisp1_params_vb2_stop_streaming,
};

static const struct v4l2_file_operations rkisp1_params_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = v4l2_fh_open,
	.release = vb2_fop_release
};

static int rkisp1_params_init_vb2_queue(struct vb2_queue *q,
					struct rkisp1_params *params)
{
	struct rkisp1_vdev_node *node;

	node = container_of(q, struct rkisp1_vdev_node, buf_queue);

	q->type = V4L2_BUF_TYPE_META_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = params;
	q->ops = &rkisp1_params_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkisp1_params_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->vlock;

	return vb2_queue_init(q);
}

static int rkisp1_params_ctrl_init(struct rkisp1_params *params)
{
	struct v4l2_ctrl_config ctrl_config = {
		.id = RKISP1_CID_SUPPORTED_PARAMS_BLOCKS,
		.name = "Supported Params Blocks",
		.type = V4L2_CTRL_TYPE_BITMASK,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	};
	int ret;

	v4l2_ctrl_handler_init(&params->ctrls, 1);

	for (unsigned int i = 0; i < ARRAY_SIZE(rkisp1_ext_params_handlers); i++) {
		const struct rkisp1_ext_params_handler *block_handler;

		block_handler = &rkisp1_ext_params_handlers[i];
		ctrl_config.max |= BIT(i);

		if ((params->rkisp1->info->features & block_handler->features) !=
		    block_handler->features)
			continue;

		ctrl_config.def |= BIT(i);
	}

	v4l2_ctrl_new_custom(&params->ctrls, &ctrl_config, NULL);

	params->vnode.vdev.ctrl_handler = &params->ctrls;

	if (params->ctrls.error) {
		ret = params->ctrls.error;
		v4l2_ctrl_handler_free(&params->ctrls);
		return ret;
	}

	return 0;
}

int rkisp1_params_register(struct rkisp1_device *rkisp1)
{
	struct rkisp1_params *params = &rkisp1->params;
	struct rkisp1_vdev_node *node = &params->vnode;
	struct video_device *vdev = &node->vdev;
	int ret;

	params->rkisp1 = rkisp1;
	mutex_init(&node->vlock);
	INIT_LIST_HEAD(&params->params);
	spin_lock_init(&params->config_lock);

	strscpy(vdev->name, RKISP1_PARAMS_DEV_NAME, sizeof(vdev->name));

	video_set_drvdata(vdev, params);
	vdev->ioctl_ops = &rkisp1_params_ioctl;
	vdev->fops = &rkisp1_params_fops;
	vdev->release = video_device_release_empty;
	/*
	 * Provide a mutex to v4l2 core. It will be used
	 * to protect all fops and v4l2 ioctls.
	 */
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = &rkisp1->v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_META_OUTPUT;
	vdev->vfl_dir = VFL_DIR_TX;
	ret = rkisp1_params_init_vb2_queue(vdev->queue, params);
	if (ret)
		goto err_media;

	params->metafmt = &rkisp1_params_formats[RKISP1_PARAMS_FIXED];

	if (params->rkisp1->info->isp_ver == RKISP1_V12)
		params->ops = &rkisp1_v12_params_ops;
	else
		params->ops = &rkisp1_v10_params_ops;

	video_set_drvdata(vdev, params);

	node->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret)
		goto err_media;

	ret = rkisp1_params_ctrl_init(params);
	if (ret) {
		dev_err(rkisp1->dev, "Control initialization error %d\n", ret);
		goto err_media;
	}

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(rkisp1->dev,
			"failed to register %s, ret=%d\n", vdev->name, ret);
		goto err_ctrl;
	}

	return 0;

err_ctrl:
	v4l2_ctrl_handler_free(&params->ctrls);
err_media:
	media_entity_cleanup(&vdev->entity);
	mutex_destroy(&node->vlock);
	return ret;
}

void rkisp1_params_unregister(struct rkisp1_device *rkisp1)
{
	struct rkisp1_params *params = &rkisp1->params;
	struct rkisp1_vdev_node *node = &params->vnode;
	struct video_device *vdev = &node->vdev;

	if (!video_is_registered(vdev))
		return;

	vb2_video_unregister_device(vdev);
	v4l2_ctrl_handler_free(&params->ctrls);
	media_entity_cleanup(&vdev->entity);
	mutex_destroy(&node->vlock);
}
