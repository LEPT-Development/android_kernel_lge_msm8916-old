/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/clk/msm-clk.h>
#include <linux/clk/msm-clock-generic.h>
#include <linux/clk/msm-clk-provider.h>
#include <dt-bindings/clock/msm-clocks-8916.h>

#include "mdss-pll.h"
#include "mdss-dsi-pll.h"

#define VCO_DELAY_USEC			1000

static struct clk_div_ops fixed_2div_ops;
static struct clk_ops byte_mux_clk_ops;
static struct clk_ops pixel_clk_src_ops;
static struct clk_ops byte_clk_src_ops;
static struct clk_ops analog_postdiv_clk_ops;
static struct lpfr_cfg lpfr_lut_struct[] = {
	{479500000, 8},
	{480000000, 11},
	{575500000, 8},
	{576000000, 12},
	{610500000, 8},
	{659500000, 9},
	{671500000, 10},
	{672000000, 14},
	{708500000, 10},
	{750000000, 11},
};

static int vco_set_rate_lpm(struct clk *c, unsigned long rate)
{
	int rc;
	struct dsi_pll_vco_clk *vco = to_vco_clk(c);
	struct mdss_pll_resources *dsi_pll_res = vco->priv;

	rc = mdss_pll_resource_enable(dsi_pll_res, true);
	if (rc) {
		pr_err("Failed to enable mdss dsi pll resources\n");
		return rc;
	}

	/*
	 * DSI PLL software reset. Add HW recommended delays after toggling
	 * the software reset bit off and back on.
	 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x01);
	udelay(1000);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x00);
	udelay(1000);

	rc = vco_set_rate(vco, rate);

	mdss_pll_resource_enable(dsi_pll_res, false);
	return rc;
}

static int gf_2_dsi_pll_enable_seq_8916(struct mdss_pll_resources *dsi_pll_res)
{
	int pll_locked = 0;
	/*
	 * DSI PLL software reset. Add HW recommended delays after toggling
	 * the software reset bit off and back on.
	 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x01);
	ndelay(500);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x00);

	/*
	 * PLL power up sequence.
	 * Add necessary delays recommended by hardware.
	 */

	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG1, 0x04);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x01);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x05);
	udelay(3);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x0f);
	udelay(500);

	/* DSI PLL toggle lock detect setting */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x04);
	ndelay(500);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x05);
	udelay(512);

	pll_locked = dsi_pll_lock_status(dsi_pll_res);

	if (pll_locked)
		pr_debug("PLL Locked\n");
	else
		pr_err("PLL failed to lock\n");

	return pll_locked ? 0 : -EINVAL;
}

static int gf_1_dsi_pll_enable_seq_8916(struct mdss_pll_resources *dsi_pll_res)
{
	int pll_locked = 0;
	/*
	 * DSI PLL software reset. Add HW recommended delays after toggling
	 * the software reset bit off and back on.
	 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x01);
	ndelay(500);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x00);

	/*
	 * PLL power up sequence.
	 * Add necessary delays recommended by hardware.
	 */

	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG1, 0x14);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x01);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x05);
	udelay(3);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x0f);
	udelay(500);

	/* DSI PLL toggle lock detect setting */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x04);
	ndelay(500);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x05);
	udelay(512);

	pll_locked = dsi_pll_lock_status(dsi_pll_res);

	if (pll_locked)
		pr_debug("PLL Locked\n");
	else
		pr_err("PLL failed to lock\n");

	return pll_locked ? 0 : -EINVAL;
}

static int tsmc_dsi_pll_enable_seq_8916(struct mdss_pll_resources *dsi_pll_res)
{
	int pll_locked = 0;
	/*
	 * DSI PLL software reset. Add HW recommended delays after toggling
	 * the software reset bit off and back on.
	 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x01);
	ndelay(500);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x00);

	/*
	 * PLL power up sequence.
	 * Add necessary delays recommended by hardware.
	 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG1, 0x34);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x01);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x05);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x0f);
	udelay(500);

	/* DSI PLL toggle lock detect setting */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x04);
	ndelay(500);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x05);
	udelay(512);

	pll_locked = dsi_pll_lock_status(dsi_pll_res);

	if (pll_locked)
		pr_debug("PLL Locked\n");
	else
		pr_err("PLL failed to lock\n");

	return pll_locked ? 0 : -EINVAL;
}

/* Op structures */

static struct clk_ops clk_ops_dsi_vco = {
	.set_rate = vco_set_rate_lpm,
	.round_rate = vco_round_rate,
	.handoff = vco_handoff,
	.prepare = vco_prepare,
	.unprepare = vco_unprepare,
};


static struct clk_div_ops fixed_4div_ops = {
	.set_div = fixed_4div_set_div,
	.get_div = fixed_4div_get_div,
};

static struct clk_div_ops analog_postdiv_ops = {
	.set_div = analog_set_div,
	.get_div = analog_get_div,
};

static struct clk_div_ops digital_postdiv_ops = {
	.set_div = digital_set_div,
	.get_div = digital_get_div,
};

static struct clk_mux_ops byte_mux_ops = {
	.set_mux_sel = set_byte_mux_sel,
	.get_mux_sel = get_byte_mux_sel,
};

static struct dsi_pll_vco_clk dsi_vco_clk_8916 = {
	.ref_clk_rate = 19200000,
	.min_rate = 350000000,
	.max_rate = 750000000,
	.pll_en_seq_cnt = 9,
	.pll_enable_seqs[0] = tsmc_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[1] = tsmc_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[2] = tsmc_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[3] = gf_1_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[4] = gf_1_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[5] = gf_1_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[6] = gf_2_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[7] = gf_2_dsi_pll_enable_seq_8916,
	.pll_enable_seqs[8] = gf_2_dsi_pll_enable_seq_8916,
	.lpfr_lut_size = 10,
	.lpfr_lut = lpfr_lut_struct,
	.c = {
		.dbg_name = "dsi_vco_clk_8916",
		.ops = &clk_ops_dsi_vco,
		CLK_INIT(dsi_vco_clk_8916.c),
	},
};

static struct div_clk analog_postdiv_clk_8916 = {
	.data = {
		.max_div = 255,
		.min_div = 1,
	},
	.ops = &analog_postdiv_ops,
	.c = {
		.parent = &dsi_vco_clk_8916.c,
		.dbg_name = "analog_postdiv_clk",
		.ops = &analog_postdiv_clk_ops,
		.flags = CLKFLAG_NO_RATE_CACHE,
		CLK_INIT(analog_postdiv_clk_8916.c),
	},
};

static struct div_clk indirect_path_div2_clk_8916 = {
	.ops = &fixed_2div_ops,
	.data = {
		.div = 2,
		.min_div = 2,
		.max_div = 2,
	},
	.c = {
		.parent = &analog_postdiv_clk_8916.c,
		.dbg_name = "indirect_path_div2_clk",
		.ops = &clk_ops_div,
		.flags = CLKFLAG_NO_RATE_CACHE,
		CLK_INIT(indirect_path_div2_clk_8916.c),
	},
};

static struct div_clk pixel_clk_src = {
	.data = {
		.max_div = 255,
		.min_div = 1,
	},
	.ops = &digital_postdiv_ops,
	.c = {
		.parent = &dsi_vco_clk_8916.c,
		.dbg_name = "pixel_clk_src_8916",
		.ops = &pixel_clk_src_ops,
		.flags = CLKFLAG_NO_RATE_CACHE,
		CLK_INIT(pixel_clk_src.c),
	},
};

static struct mux_clk byte_mux_8916 = {
	.num_parents = 2,
	.parents = (struct clk_src[]){
		{&dsi_vco_clk_8916.c, 0},
		{&indirect_path_div2_clk_8916.c, 1},
	},
	.ops = &byte_mux_ops,
	.c = {
		.parent = &dsi_vco_clk_8916.c,
		.dbg_name = "byte_mux_8916",
		.ops = &byte_mux_clk_ops,
		CLK_INIT(byte_mux_8916.c),
	},
};

static struct div_clk byte_clk_src = {
	.ops = &fixed_4div_ops,
	.data = {
		.min_div = 4,
		.max_div = 4,
	},
	.c = {
		.parent = &byte_mux_8916.c,
		.dbg_name = "byte_clk_src_8916",
		.ops = &byte_clk_src_ops,
		CLK_INIT(byte_clk_src.c),
	},
};

static struct clk_lookup mdss_dsi_pllcc_8916[] = {
	CLK_LIST(pixel_clk_src),
	CLK_LIST(byte_clk_src),
};

int dsi_pll_clock_register_lpm(struct platform_device *pdev,
				struct mdss_pll_resources *pll_res)
{
	int rc;

	if (!pdev || !pdev->dev.of_node) {
		pr_err("Invalid input parameters\n");
		return -EINVAL;
	}

	if (!pll_res || !pll_res->pll_base) {
		pr_err("Invalid PLL resources\n");
		return -EPROBE_DEFER;
	}

	/* Set client data to mux, div and vco clocks */
	byte_clk_src.priv = pll_res;
	pixel_clk_src.priv = pll_res;
	byte_mux_8916.priv = pll_res;
	indirect_path_div2_clk_8916.priv = pll_res;
	analog_postdiv_clk_8916.priv = pll_res;
	dsi_vco_clk_8916.priv = pll_res;
	pll_res->vco_delay = VCO_DELAY_USEC;

	/* Set clock source operations */
	pixel_clk_src_ops = clk_ops_slave_div;
	pixel_clk_src_ops.prepare = dsi_pll_div_prepare;

	analog_postdiv_clk_ops = clk_ops_div;
	analog_postdiv_clk_ops.prepare = dsi_pll_div_prepare;

	byte_clk_src_ops = clk_ops_div;
	byte_clk_src_ops.prepare = dsi_pll_div_prepare;

	byte_mux_clk_ops = clk_ops_gen_mux;
	byte_mux_clk_ops.prepare = dsi_pll_mux_prepare;

	if (pll_res->target_id == MDSS_PLL_TARGET_8916 ||
		pll_res->target_id == MDSS_PLL_TARGET_8939 ||
		pll_res->target_id == MDSS_PLL_TARGET_8909) {
		rc = of_msm_clock_register(pdev->dev.of_node,
			mdss_dsi_pllcc_8916, ARRAY_SIZE(mdss_dsi_pllcc_8916));
		if (rc) {
			pr_err("Clock register failed\n");
			rc = -EPROBE_DEFER;
		}
	} else {
		pr_err("Invalid target ID\n");
		rc = -EINVAL;
	}

	if (!rc)
		pr_info("Registered DSI PLL clocks successfully\n");

	return rc;
}
