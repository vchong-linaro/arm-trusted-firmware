/*
 * Copyright (c) 2014, Hisilicon Ltd.
 * Copyright (c) 2014, Linaro Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch_helpers.h>
#include <assert.h>
#include <bl_common.h>
#include <cci400.h>
#include <console.h>
#include <debug.h>
#include <errno.h>
#include <mmio.h>
#include <platform.h>
#include <platform_def.h>
#include <string.h>
#include <dw_mmc.h>
#include <sp804_timer.h>
#include <gpio.h>
#include <pll.h>
#include <partitions.h>
#include <hi6220.h>
#include <hi6553.h>
#include "../../bl1/bl1_private.h"
#include "lcb_def.h"
#include "lcb_private.h"

#define DDR

/*******************************************************************************
 * Declarations of linker defined symbols which will help us find the layout
 * of trusted RAM
 ******************************************************************************/
extern unsigned long __COHERENT_RAM_START__;
extern unsigned long __COHERENT_RAM_END__;

/*
 * The next 2 constants identify the extents of the coherent memory region.
 * These addresses are used by the MMU setup code and therefore they must be
 * page-aligned.  It is the responsibility of the linker script to ensure that
 * __COHERENT_RAM_START__ and __COHERENT_RAM_END__ linker symbols refer to
 * page-aligned addresses.
 */
#define BL1_COHERENT_RAM_BASE (unsigned long)(&__COHERENT_RAM_START__)
#define BL1_COHERENT_RAM_LIMIT (unsigned long)(&__COHERENT_RAM_END__)

/* Data structure which holds the extents of the trusted RAM for BL1 */
static meminfo_t bl1_tzram_layout;

meminfo_t *bl1_plat_sec_mem_layout(void)
{
	return &bl1_tzram_layout;
}

/* PMU SSI is the device that could map external PMU register to IO */
static void init_pmussi(void)
{
	uint32_t data;

	/*
	 * After reset, PMUSSI stays in reset mode.
	 * Now make it out of reset.
	 */ 
	mmio_write_32(AO_SC_PERIPH_RSTDIS4, AO_SC_PERIPH_CLKEN4_PMUSSI);
	do {
		data = mmio_read_32(AO_SC_PERIPH_RSTSTAT4);
	} while (data & AO_SC_PERIPH_CLKEN4_PMUSSI);

	/* set PMU SSI clock latency for read operation */
	data = mmio_read_32(AO_SC_MCU_SUBSYS_CTRL3);
	data &= ~AO_SC_MCU_SUBSYS_CTRL3_RCLK_MASK;
	data |= AO_SC_MCU_SUBSYS_CTRL3_RCLK_3;
	mmio_write_32(AO_SC_MCU_SUBSYS_CTRL3, data);

	/* enable PMUSSI clock */
	data = AO_SC_PERIPH_CLKEN5_PMUSSI_CCPU | AO_SC_PERIPH_CLKEN5_PMUSSI_MCU;
	mmio_write_32(AO_SC_PERIPH_CLKEN5, data);
	data = AO_SC_PERIPH_CLKEN4_PMUSSI;
	mmio_write_32(AO_SC_PERIPH_CLKEN4, data);

	/* output high on gpio0 */
	gpio_direction_output(0);
	gpio_set_value(0, 1);
}

static void init_hi6553(void)
{
	int data;

	hi6553_write_8(PERI_EN_MARK, 0x1e);
	hi6553_write_8(NP_REG_ADJ1, 0);
	data = DISABLE6_XO_CLK_CONN | DISABLE6_XO_CLK_NFC |
		DISABLE6_XO_CLK_RF1 | DISABLE6_XO_CLK_RF2;
	hi6553_write_8(DISABLE6_XO_CLK, data);

	/* configure BUCK0 & BUCK1 */
	hi6553_write_8(BUCK01_CTRL2, 0x5e);
	hi6553_write_8(BUCK0_CTRL7, 0x10);
	hi6553_write_8(BUCK1_CTRL7, 0x10);
	hi6553_write_8(BUCK0_CTRL5, 0x1e);
	hi6553_write_8(BUCK1_CTRL5, 0x1e);
	hi6553_write_8(BUCK0_CTRL1, 0xfc);
	hi6553_write_8(BUCK1_CTRL1, 0xfc);

	/* configure BUCK2 */
	hi6553_write_8(BUCK2_REG1, 0x4f);
	hi6553_write_8(BUCK2_REG5, 0x99);
	hi6553_write_8(BUCK2_REG6, 0x45);

	/* configure BUCK3 */
	hi6553_write_8(BUCK3_REG3, 0x02);
	hi6553_write_8(BUCK3_REG5, 0x99);
	hi6553_write_8(BUCK3_REG6, 0x41);

	/* configure BUCK4 */
	hi6553_write_8(BUCK4_REG2, 0x9a);
	hi6553_write_8(BUCK4_REG5, 0x99);
	hi6553_write_8(BUCK4_REG6, 0x45);

	/* configure LDO20 */
	hi6553_write_8(LDO20_REG_ADJ, 0x50);

	hi6553_write_8(NP_REG_CHG, 0x0f);
	hi6553_write_8(CLK_TOP0, 0x06);
	hi6553_write_8(CLK_TOP3, 0xc0);
	hi6553_write_8(CLK_TOP4, 0x00);

	/* select 32.764KHz */
	hi6553_write_8(CLK19M2_600_586_EN, 0x01);
}

static void init_pll(void)
{
	unsigned int data;

	/* enable PLL */
	data = mmio_read_32(PMCTRL_ACPUPLLCTRL);
	data |= 0x1;
	mmio_write_32(PMCTRL_ACPUPLLCTRL, data);
	mdelay(10);
	do {
		data = mmio_read_32(PMCTRL_ACPUPLLCTRL);
	} while (!(data & (1 << 28)));

	/* switch from slow mode to normal mode */
	data = mmio_read_32(AO_SC_SYS_CTRL0);
	data &= ~AO_SC_SYS_CTRL0_MODE_MASK;
	data |= AO_SC_SYS_CTRL0_MODE_NORMAL;
	mmio_write_32(AO_SC_SYS_CTRL0, data);
	do {
		data = mmio_read_32(AO_SC_SYS_STAT1);
		data &= AO_SC_SYS_CTRL0_MODE_MASK;
	} while (data != AO_SC_SYS_CTRL0_MODE_NORMAL);
}

static void init_freq(void)
{
	unsigned int data, tmp;
	unsigned int cpuext_cfg, ddr_cfg;

	mmio_write_32(PMCTRL_ACPUDFTVOL, 0x4a);
	mmio_write_32(PMCTRL_ACPUVOLPMUADDR, 0xda);
	mmio_write_32(PMCTRL_ACPUVOLUPSTEP, 0x01);
	mmio_write_32(PMCTRL_ACPUVOLDNSTEP, 0x01);
	mmio_write_32(PMCTRL_ACPUPMUVOLUPTIME, 0x60);
	mmio_write_32(PMCTRL_ACPUPMUVOLDNTIME, 0x60);
	/* enable ACPU DVFS */
	mmio_write_32(PMCTRL_ACPUCLKOFFCFG, 0x1000);

	data = mmio_read_32(PMCTRL_ACPUSYSPLLCFG);
	data |= PMCTRL_ACPUSYSPLL_CLKDIV_SW;
	mmio_write_32(PMCTRL_ACPUSYSPLLCFG, data);

	data = mmio_read_32(PMCTRL_ACPUSYSPLLCFG);
	data |= PMCTRL_ACPUSYSPLL_CLKEN_CFG;
	mmio_write_32(PMCTRL_ACPUSYSPLLCFG, data);

	/* current frequency is 208MHz */
	data = mmio_read_32(PMCTRL_ACPUSYSPLLCFG);
	data &= ~PMCTRL_ACPUSYSPLL_CLKDIV_CFG_MASK;
	data |= 0x5;
	mmio_write_32(PMCTRL_ACPUSYSPLLCFG, data);
	NOTICE("#%s, %d\n", __func__, __LINE__);
	//mdelay(1000);

	do {
		data = mmio_read_32(ACPU_SC_CPU_STAT);
		data &= ACPU_SC_CPU_STAT_CLKDIV_VD_MASK;
	} while (data != ACPU_SC_CPU_STAT_CLKDIV_VD_MASK);
	NOTICE("#%s, %d\n", __func__, __LINE__);
	//mdelay(1000);

	data = mmio_read_32(ACPU_SC_VD_CTRL);
	data &= ~(ACPU_SC_VD_CTRL_TUNE_EN_DIF | ACPU_SC_VD_CTRL_TUNE_EN_INT);
	mmio_write_32(ACPU_SC_VD_CTRL, data);

	data = mmio_read_32(PMCTRL_ACPUCLKDIV);
	data &= ~PMCTRL_ACPUCLKDIV_DDR_CFG_MASK;
	data |= (1 << 8);
	mmio_write_32(PMCTRL_ACPUCLKDIV, data);

	data = mmio_read_32(PMCTRL_ACPUPLLSEL);
	data |= PMCTRL_ACPUPLLSEL_ACPUPLL_CFG;
	mmio_write_32(PMCTRL_ACPUPLLSEL, data);

	do {
		data = mmio_read_32(PMCTRL_ACPUPLLSEL);
		data &= PMCTRL_ACPUPLLSEL_SYSPLL_STAT;
	} while (data != PMCTRL_ACPUPLLSEL_SYSPLL_STAT);
	udelay(100);

	data = mmio_read_32(ACPU_SC_VD_HPM_CTRL);
	data &= ~ACPU_SC_VD_HPM_CTRL_OSC_DIV_MASK;
	data |= 0x56;
	mmio_write_32(ACPU_SC_VD_HPM_CTRL, data);

	data = mmio_read_32(ACPU_SC_VD_HPM_CTRL);
	data &= ~ACPU_SC_VD_HPM_CTRL_DLY_EXP_MASK;
	data |= 0xc7a << 8;
	mmio_write_32(ACPU_SC_VD_HPM_CTRL, data);

	data = mmio_read_32(ACPU_SC_VD_MASK_PATTERN_CTRL);
	data &= ACPU_SC_VD_MASK_PATTERN;
	data |= 0xccb;
	mmio_write_32(ACPU_SC_VD_MASK_PATTERN_CTRL, data);

	mmio_write_32(ACPU_SC_VD_DLY_TABLE0_CTRL, 0x1fff);
	mmio_write_32(ACPU_SC_VD_DLY_TABLE1_CTRL, 0x1ffffff);
	mmio_write_32(ACPU_SC_VD_DLY_TABLE2_CTRL, 0x7fffffff);
	mmio_write_32(ACPU_SC_VD_DLY_FIXED_CTRL, 0x1);

	data = mmio_read_32(ACPU_SC_VD_CTRL);
	data &= ~ACPU_SC_VD_CTRL_SHIFT_TABLE0_MASK;
	data |= 1 << 12;
	mmio_write_32(ACPU_SC_VD_CTRL, data);

	/* disable ACPUPLL */
	data = mmio_read_32(PMCTRL_ACPUPLLCTRL);
	data &= ~PMCTRL_ACPUPLLCTRL_EN_CFG;
	mmio_write_32(PMCTRL_ACPUPLLCTRL, data);

	mmio_write_32(PMCTRL_ACPUPLLFREQ, 0x5110207d);
	mmio_write_32(PMCTRL_ACPUPLLFRAC, 0x10000005);
	data = mmio_read_32(PMCTRL_ACPUPLLFRAC);

	/* enable ACPUPLL */
	data = mmio_read_32(PMCTRL_ACPUPLLCTRL);
	data |= PMCTRL_ACPUPLLCTRL_EN_CFG;
	mmio_write_32(PMCTRL_ACPUPLLCTRL, data);

	mmio_write_32(PMCTRL_ACPUVOLPMUADDR, 0x100da);
	data = mmio_read_32(PMCTRL_ACPUDESTVOL);
	data &= ~((1 << 7) - 1);
	data |= 0x6b;
	mmio_write_32(PMCTRL_ACPUDESTVOL, data);
	do {
		data = mmio_read_32(PMCTRL_ACPUDESTVOL);
		tmp = data & PMCTRL_ACPUDESTVOL_DEST_VOL_MASK;
		data = (data & PMCTRL_ACPUDESTVOL_CURR_VOL_MASK) >> 8;
		if (data != tmp)
			continue;
		data = mmio_read_32(PMCTRL_ACPUVOLTTIMEOUT);
	} while (!(data & 1));

	data = mmio_read_32(PMCTRL_ACPUCLKDIV);
	data &= ~(PMCTRL_ACPUCLKDIV_CPUEXT_CFG_MASK |
		PMCTRL_ACPUCLKDIV_DDR_CFG_MASK);
	cpuext_cfg = 1;
	ddr_cfg = 1;
	data |= cpuext_cfg | (ddr_cfg << 8);
	mmio_write_32(PMCTRL_ACPUCLKDIV, data);

	do {
		data = mmio_read_32(PMCTRL_ACPUCLKDIV);
		tmp = (data & PMCTRL_ACPUCLKDIV_CPUEXT_STAT_MASK) >> 16;
		if (cpuext_cfg != tmp)
			continue;
		tmp = (data & PMCTRL_ACPUCLKDIV_DDR_STAT_MASK) >> 24;
		if (ddr_cfg != tmp)
			continue;
		data = mmio_read_32(PMCTRL_ACPUPLLCTRL);
		data &= 1 << 28;
	} while (!data);

	data = mmio_read_32(PMCTRL_ACPUPLLSEL);
	data &= ~PMCTRL_ACPUPLLSEL_ACPUPLL_CFG;
	mmio_write_32(PMCTRL_ACPUPLLSEL, data);
	do {
		data = mmio_read_32(PMCTRL_ACPUPLLSEL);
		data &= PMCTRL_ACPUPLLSEL_ACPUPLL_STAT;
	} while (data != PMCTRL_ACPUPLLSEL_ACPUPLL_STAT);
	mdelay(1000);

	data = mmio_read_32(ACPU_SC_VD_CTRL);
	data &= ~ACPU_SC_VD_CTRL_FORCE_CLK_EN;
	mmio_write_32(ACPU_SC_VD_CTRL, data);

	data = mmio_read_32(PMCTRL_ACPUSYSPLLCFG);
	data &= ~(PMCTRL_ACPUSYSPLLCFG_SYSPLL_CLKEN |
		PMCTRL_ACPUSYSPLLCFG_CLKDIV_MASK);
	mmio_write_32(PMCTRL_ACPUSYSPLLCFG, data);
}

#ifdef DDR
static int cat_533mhz_800mhz(void)
{
	unsigned int data, i;
	unsigned int bdl[5];

	/* enable open eye */
	data = mmio_read_32(MDDRC_PACK_DDRC_CATCONFIG);
	data &= ~0x0f0f;
	data |= 0x100f01;
	mmio_write_32(MDDRC_PACK_DDRC_CATCONFIG, data);

	for (i = 0; i < 0x20; i++) {
		mmio_write_32(MDDRC_PACK_DDRC_ADDRPHBOUND, 0xc0000);
		data = (i << 0x10) + i;
		mmio_write_32(MDDRC_PACK_DDRC_ACADDRBDL0, data);
		mmio_write_32(MDDRC_PACK_DDRC_ACADDRBDL1, data);
		mmio_write_32(MDDRC_PACK_DDRC_ACADDRBDL2, data);
		mmio_write_32(MDDRC_PACK_DDRC_ACADDRBDL3, data);
		mmio_write_32(MDDRC_PACK_DDRC_ACADDRBDL4, data);

		/* operate on bit[19], cfg_dlyupd */
		data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
		data |= 0x80000;
		mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
		data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
		data &= ~0x80000;
		mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
		
		/* operate on bit[15], phyconn_rst */
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x8000);
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x0);
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x801);
		do {
			data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
		} while (data & 1);

		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
		if (!(data & 0x400)) {
			INFO("lpddr3 cat pass\n");
			return 0;
		}
		NOTICE("lpddr3 cat fail\n");
		data = mmio_read_32(MDDRC_PACK_DDRC_ADDRPHBOUND);
		if ((data & 0x1f00) && (!(data & 0x1f))) {
			bdl[0] = mmio_read_32(MDDRC_PACK_DDRC_ACADDRBDL0);
			bdl[1] = mmio_read_32(MDDRC_PACK_DDRC_ACADDRBDL1);
			bdl[2] = mmio_read_32(MDDRC_PACK_DDRC_ACADDRBDL2);
			bdl[3] = mmio_read_32(MDDRC_PACK_DDRC_ACADDRBDL3);
			bdl[4] = mmio_read_32(MDDRC_PACK_DDRC_ACADDRBDL4);
			if ((!(bdl[0] & 0x1f001f)) || (!(bdl[1] & 0x1f001f)) ||
			    (!(bdl[2] & 0x1f001f)) || (!(bdl[3] & 0x1f001f)) ||
			    (!(bdl[4] & 0x1f001f))) {
				NOTICE("lpddr3 cat deskew error\n");
				if (i == 0x1f) {
					NOTICE("addrnbdl is max\n");
					return -EINVAL;
				}
				mmio_write_32(MDDRC_PACK_DDRC_PHYINITSTATUS,
						0x400);
			} else {
				NOTICE("lpddr3 cat other error1\n");
				return -EINVAL;
			}
		} else {
			NOTICE("lpddr3 cat other error2\n");
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static void ddrx_rdet(void)
{
	unsigned int data, rdet, bdl[4];

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
	data &= ~0x7ff0000;
	data |= 0x8a0000;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL3);
	data &= ~0xf;
	data |= 0x4;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL3, data);

	/* bit[19], cfg_dlyupd */
	data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
	data |= 0x80000;
	mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
	data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
	data &= ~0x80000;
	mmio_write_32(MDDRC_PACK_DDRC_MISC, data);

	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x8000);
	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0);

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
	data &= ~0xf0000000;
	data |= 0x80000000;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);

	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x101);
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
	} while (!(data & 1));
	data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
	if (data & 0x100)
		INFO("rdet lbs fail\n");
	else
		INFO("rdet lbs pass\n");

	bdl[0] = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY0) & 0x7f;
	bdl[1] = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY1) & 0x7f;
	bdl[2] = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY2) & 0x7f;
	bdl[3] = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY3) & 0x7f;
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY0);
		data &= ~0x7f;
		data |= bdl[0];
		mmio_write_32(MDDRC_PACK_DDRC_DXNRDQSDLY0, data);
		data = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY1);
		data &= ~0x7f;
		data |= bdl[1];
		mmio_write_32(MDDRC_PACK_DDRC_DXNRDQSDLY1, data);
		data = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY2);
		data &= ~0x7f;
		data |= bdl[2];
		mmio_write_32(MDDRC_PACK_DDRC_DXNRDQSDLY2, data);
		data = mmio_read_32(MDDRC_PACK_DDRC_DXNRDQSDLY3);
		data &= ~0x7f;
		data |= bdl[3];
		mmio_write_32(MDDRC_PACK_DDRC_DXNRDQSDLY3, data);

		/* bit[19], cfg_dlyupd */
		data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
		data |= 0x80000;
		mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
		data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
		data &= ~0x80000;
		mmio_write_32(MDDRC_PACK_DDRC_MISC, data);

		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x8000);
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0);

		data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
		data &= ~0xf0000000;
		data |= 0x40000000;
		mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x101);
		do {
			data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
		} while (data & 1);

		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
		rdet = data & 0x100;
		if (rdet) {
			INFO("rdet ds fail\n");
			mmio_write_32(MDDRC_PACK_DDRC_PHYINITSTATUS, 0x100);
		} else
			INFO("rdet ds pass\n");
		bdl[0]++;
		bdl[1]++;
		bdl[2]++;
		bdl[3]++;
	} while (rdet);

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
	data &= ~0xf0000000;
	data |= 0x30000000;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);

	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x101);
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
	} while (data & 1);
	data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
	if (data & 0x100)
		INFO("rdet rbs av fail\n");
	else
		INFO("rdet rbs av pass\n");
}

static void ddrx_wdet(void)
{
	unsigned int data, wdet, zero_bdl, dq[4];
	int i;

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
	data &= ~0xf;
	data |= 0xa;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);

	data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
	data |= 0x80000;
	mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
	data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
	data &= ~0x80000;
	mmio_write_32(MDDRC_PACK_DDRC_MISC, data);

	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x8000);
	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0);
	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
	data &= ~0xf000;
	data |= 0x8000;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);
	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x201);
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
	} while (data & 1);
	data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
	if (data & 0x200)
		INFO("wdet lbs fail\n");
	else
		INFO("wdet lbs pass\n");

	dq[0] = mmio_read_32(MDDRC_PACK_DDRC_DXNWDQDLY0) & 0x1f00;
	dq[1] = mmio_read_32(MDDRC_PACK_DDRC_DXNWDQDLY1) & 0x1f00;
	dq[2] = mmio_read_32(MDDRC_PACK_DDRC_DXNWDQDLY2) & 0x1f00;
	dq[3] = mmio_read_32(MDDRC_PACK_DDRC_DXNWDQDLY3) & 0x1f00;

	do {
		mmio_write_32(MDDRC_PACK_DDRC_DXNWDQDLY0, dq[0]);
		mmio_write_32(MDDRC_PACK_DDRC_DXNWDQDLY1, dq[1]);
		mmio_write_32(MDDRC_PACK_DDRC_DXNWDQDLY2, dq[2]);
		mmio_write_32(MDDRC_PACK_DDRC_DXNWDQDLY3, dq[3]);

		data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
		data |= 0x80000;
		mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
		data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
		data &= ~0x80000;
		mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x8000);
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0);

		data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
		data &= 0xf000;
		data |= 0x4000;
		mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);
		mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x201);
		do {
			data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
		} while (data & 1);

		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
		wdet = data & 0x200;
		if (wdet) {
			INFO("wdet ds fail\n");
			mmio_write_32(MDDRC_PACK_DDRC_PHYINITSTATUS, 0x200);
		} else
			INFO("wdet ds pass\n");

		for (i = 0; i < 4; i++) {
			data = mmio_read_32(MDDRC_PACK_DDRC_DXNWDQNBDL0(i));
			if ((!(data & 0x1f)) || (!(data & 0x1f00)) ||
			    (!(data & 0x1f0000)) || (!(data & 0x1f000000)))
				zero_bdl = 1;
			data = mmio_read_32(MDDRC_PACK_DDRC_DXNWDQNBDL1(i));
			if ((!(data & 0x1f)) || (!(data & 0x1f00)) ||
			    (!(data & 0x1f0000)) || (!(data & 0x1f000000)))
				zero_bdl = 1;
			data = mmio_read_32(MDDRC_PACK_DDRC_DXNWDQNBDL2(i));
			if (!(data & 0x1f))
				zero_bdl = 1;
			if (zero_bdl) {
				if (i == 0)
					dq[0] = dq[0] - 0x100;
				if (i == 1)
					dq[1] = dq[1] - 0x100;
				if (i == 2)
					dq[2] = dq[2] - 0x100;
				if (i == 3)
					dq[3] = dq[3] - 0x100;
			}
		}
	} while (wdet);

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL1);
	data &= ~0xf000;
	data |= 0x3000;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL1, data);
	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x201);
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
	} while (data & 1);
	data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
	if (data & 0x200)
		INFO("wdet rbs av fail\n");
	else
		INFO("wdet rbs av pass\n");
}

static void set_ddrc_533mhz(void)
{
	unsigned int data;

	mmio_write_32(PMCTRL_DDRCLKSEL, 0x3);
	mmio_write_32(PMCTRL_DDRCLKDIVCFG, 0x11111);
	data = mmio_read_32(PMCTRL_ACPUCLKDIV);
	data |= 0x100;
	mmio_write_32(PMCTRL_ACPUCLKDIV, data);

	mmio_write_32(PERI_SC_DDR_CTRL0, 0x30);
	mmio_write_32(PERI_SC_PERIPH_CLKEN8, 0x5ffff);
	mmio_write_32(PERI_SC_PERIPH_RSTDIS8, 0xf5ff);
	mmio_write_32(MDDRC_PACK_DDRC_PHYCLKGATED, 0x400);
	udelay(100);
	mmio_write_32(MDDRC_PACK_DDRC_PLLCTRL, 0x7);
	mmio_write_32(MDDRC_PACK_DDRC_PHYCTRL1, 0x6400000);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYCTRL0, 0x640);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYCTRL1, 0x640);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYCTRL2, 0x640);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYCTRL3, 0x640);
	mmio_write_32(MDDRC_PACK_DDRC_PLLCTRL, 0x0);
	mmio_write_32(MDDRC_PACK_DDRC_IOCTL1, 0xf00000f);
	mmio_write_32(MDDRC_PACK_DDRC_IOCTL2, 0xf);
	mmio_write_32(MDDRC_PACK_DDRC_IOCTL, 0x3fff801);
	mmio_write_32(MDDRC_PACK_DDRC_MISC, 0x8940000);

	data = mmio_read_32(MDDRC_PACK_DDRC_PHYCTRL0);
	data |= 4;
	mmio_write_32(MDDRC_PACK_DDRC_PHYCTRL0, data);
	mmio_write_32(MDDRC_PACK_DDRC_PLLTIMER, 0x8000080);
	data = mmio_read_32(MDDRC_PACK_DDRC_DLYMEASCTRL);
	data &= 0xfffffffe;
	mmio_write_32(MDDRC_PACK_DDRC_DLYMEASCTRL, data);
	mmio_write_32(MDDRC_PACK_DDRC_ADDRPHBOUND, 0xc0000);
	mmio_write_32(MDDRC_PACK_DDRC_PHYTIMER0, 0x500000f);
	mmio_write_32(MDDRC_PACK_DDRC_PHYTIMER1, 0x10);
	data = mmio_read_32(MDDRC_PACK_DDRC_LPCTRL);
	data &= 0xffffff00;
	mmio_write_32(MDDRC_PACK_DDRC_LPCTRL, data);
	mmio_write_32(MDDRC_PACK_DDRC_DRAMTIMER0, 0x9dd87855);
	mmio_write_32(MDDRC_PACK_DDRC_DRAMTIMER1, 0xa7138bb);
	mmio_write_32(MDDRC_PACK_DDRC_DRAMTIMER2, 0x20091477);
	mmio_write_32(MDDRC_PACK_DDRC_DRAMTIMER3, 0x84534e16);
	mmio_write_32(MDDRC_PACK_DDRC_DRAMTIMER4, 0x3008817);
	mmio_write_32(MDDRC_PACK_DDRC_MODEREG01, 0x106c3);
	mmio_write_32(MDDRC_PACK_DDRC_MODEREG23, 0xff0a0000);
	data = mmio_read_32(MDDRC_PACK_DDRC_MISC);
	data &= 0xffff0000;
	data |= 0x305;
	mmio_write_32(MDDRC_PACK_DDRC_MISC, data);
	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL0);
	data |= 0x40000000;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL0, data);
	data = mmio_read_32(MDDRC_PACK_DDRC_DLYMEASCTRL);
	data &= ~0x10;
	mmio_write_32(MDDRC_PACK_DDRC_DLYMEASCTRL, data);
	data = mmio_read_32(MDDRC_PACK_DDRC_RETCTRL0);
	data &= ~0x2000;
	mmio_write_32(MDDRC_PACK_DDRC_RETCTRL0, data);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYRSVD0, 0x3);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYRSVD1, 0x3);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYRSVD2, 0x3);
	mmio_write_32(MDDRC_PACK_DDRC_DXPHYRSVD3, 0x3);
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL0, 0xd0420900);

	mmio_write_32(MDDRC_DMC_CFG_WORKHOME, 0x0);
	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x140f);
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
	} while (data & 1);
	data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
	if (data & 0x7fe) {
		NOTICE("failed to init lpddr3 rank0 dram phy\n");
		return;
	}
	NOTICE("succeed to init lpddr3 rank0 dram phy\n");
}

static void ddrc_common_init(void)
{
	unsigned int data;

	mmio_write_32(MDDRC_AXI_ACTION, 0x1);
	mmio_write_32(MDDRC_AXI_REGION_MAP0, 0x1700);
	mmio_write_32(MDDRC_AXI_REGION_ATTRIB0, 0x71040004);
	mmio_write_32(MDDRC_SECURITY1_ATTRIB0, 0xf);
	mmio_write_32(MDDRC_SECURITY2_ATTRIB0, 0xf);
	mmio_write_32(MDDRC_SECURITY3_ATTRIB0, 0xf);
	mmio_write_32(MDDRC_SECURITY4_ATTRIB0, 0xf);
	mmio_write_32(MDDRC_DMC_CFG_AREF, 0x6);
	mmio_write_32(MDDRC_DMC_CFG_SREF, 0x30003);
	mmio_write_32(MDDRC_DMC_CFG_PD, 0x310201);
	mmio_write_32(MDDRC_PACK_DDRC_LPCTRL, 0xfe007600);
	mmio_write_32(MDDRC_DMC_CFG_LP, 0xaf001);

	/* mask int bit[7] */
	data = mmio_read_32(MDDRC_DMC_INTMASK);
	data |= 1 << 7;
	mmio_write_32(MDDRC_DMC_INTMASK, data);
	mmio_write_32(MDDRC_DMC_CFG_TMON_RANK, 0x3);

	/* By default, the ddr freq is 400000 */
	mmio_write_32(MDDRC_DMC_CFG_TMON, 167 * 400000 / 1024);

	data = mmio_read_32(MDDRC_PACK_DDRC_RETCTRL0);
	data &= 0xffff;
	data |= 0x4002000;
	mmio_write_32(MDDRC_PACK_DDRC_RETCTRL0, data);
	mmio_write_32(MDDRC_DMC_CTRL_SREF, 0x0);
	do {
		data = mmio_read_32(MDDRC_DMC_CURR_FUNC);
	} while (data & 1);
	mmio_write_32(MDDRC_DMC_CTRL_SREF, 0x2);
}

/* detect ddr capacity */
static int dienum_det_and_rowcol_cfg(void)
{
	unsigned int data;

	mmio_write_32(MDDRC_DMC_CFG_SFC, 0x87);
	mmio_write_32(MDDRC_DMC_CFG_SFC_ADDR1, 0x10000);
	mmio_write_32(MDDRC_DMC_CTRL_SFC, 0x1);
	do {
		data = mmio_read_32(MDDRC_DMC_CTRL_SFC);
	} while (data & 1);
	data = mmio_read_32(MDDRC_DMC_HIS_SFC_RDATA0) & 0xfc;
	switch (data) {
	case 0x18:
		mmio_write_32(MDDRC_DMC_CFG_RNKVOL0, 0x132);
		mmio_write_32(MDDRC_DMC_CFG_RNKVOL1, 0x132);
		mmio_write_32(MDDRC_AXI_REGION_MAP0, 0x1600);
		mmio_write_32(MDDRC_AXI_REGION_ATTRIB0, 0x71040004);
		break;
	case 0x1c:
		mmio_write_32(MDDRC_DMC_CFG_RNKVOL0, 0x142);
		mmio_write_32(MDDRC_DMC_CFG_RNKVOL1, 0x142);
		mmio_write_32(MDDRC_AXI_REGION_MAP0, 0x1700);
		mmio_write_32(MDDRC_AXI_REGION_ATTRIB0, 0x71040004);
		break;
	case 0x58:
		mmio_write_32(MDDRC_DMC_CFG_RNKVOL0, 0x133);
		mmio_write_32(MDDRC_DMC_CFG_RNKVOL1, 0x133);
		mmio_write_32(MDDRC_AXI_REGION_MAP0, 0x1700);
		mmio_write_32(MDDRC_AXI_REGION_ATTRIB0, 0x71040004);
		break;
	default:
		break;
	}
	if (!data)
		return -EINVAL;
	return 0;
}

static int detect_ddr_chip_info(void)
{
	unsigned int data, mr5, mr6, mr7;

	mmio_write_32(MDDRC_DMC_CFG_SFC, 0x57);
	mmio_write_32(MDDRC_DMC_CFG_SFC_ADDR1, 0x10000);
	mmio_write_32(MDDRC_DMC_CTRL_SFC, 0x1);

	do {
		data = mmio_read_32(MDDRC_DMC_CTRL_SFC);
	} while (data & 1);

	data = mmio_read_32(MDDRC_DMC_HIS_SFC_RDATA0);
	mr5 = data & 0xff;
	switch (mr5) {
	case 1:
		NOTICE("Samsung DDR\n");
		break;
	case 6:
		NOTICE("Hynix DDR\n");
		break;
	case 3:
		NOTICE("Elpida DDR\n");
		break;
	default:
		NOTICE("DDR from other vendors\n");
		break;
	}

	mmio_write_32(MDDRC_DMC_CFG_SFC, 0x67);
	mmio_write_32(MDDRC_DMC_CFG_SFC_ADDR1, 0x10000);
	mmio_write_32(MDDRC_DMC_CTRL_SFC, 0x1);
	do {
		data = mmio_read_32(MDDRC_DMC_CTRL_SFC);
	} while (data & 1);
	data = mmio_read_32(MDDRC_DMC_HIS_SFC_RDATA0);
	mr6 = data & 0xff;
	mmio_write_32(MDDRC_DMC_CFG_SFC, 0x77);
	mmio_write_32(MDDRC_DMC_CFG_SFC_ADDR1, 0x10000);
	mmio_write_32(MDDRC_DMC_CTRL_SFC, 0x1);
	do {
		data = mmio_read_32(MDDRC_DMC_CTRL_SFC);
	} while (data & 1);
	data = mmio_read_32(MDDRC_DMC_HIS_SFC_RDATA0);
	mr7 = data & 0xff;
	data = mr5 + (mr6 << 8) + (mr7 << 16);
	return data;
}

static int lpddr3_freq_init(void)
{
	unsigned int data;

	set_ddrc_533mhz();
	NOTICE("#%s, %d set ddrc 533mhz\n", __func__, __LINE__);

	data = cat_533mhz_800mhz();
	if (data)
		NOTICE("fail to set eye diagram\n");

	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0xf1);
	mmio_write_32(MDDRC_DMC_CFG_DDRMODE, 0x100123);
	mmio_write_32(MDDRC_DMC_CFG_RNKVOL0, 0x133);
	mmio_write_32(MDDRC_DMC_CFG_RNKVOL1, 0x133);
	mmio_write_32(MDDRC_DMC_CFG_TIMING0, 0xb77b6718);
	mmio_write_32(MDDRC_DMC_CFG_TIMING1, 0x1e82a071);
	mmio_write_32(MDDRC_DMC_CFG_TIMING2, 0x9501c07e);
	mmio_write_32(MDDRC_DMC_CFG_TIMING3, 0xaf50c255);
	mmio_write_32(MDDRC_DMC_CFG_TIMING4, 0x10b00000);
	mmio_write_32(MDDRC_DMC_CFG_TIMING5, 0x13181908);
	mmio_write_32(MDDRC_DMC_CFG_TIMING6, 0x44);
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
	} while (data & 1);

	data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
	if (data & 0x7fe) {
		NOTICE("fail to init ddr3 rank0 in 533MHz\n");
		return -EFAULT;
	}
	INFO("init ddr3 rank0 in 533MHz\n");
	ddrx_rdet();
	ddrx_wdet();

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL0);
	data |= 1;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL0, data);
	mmio_write_32(MDDRC_PACK_DDRC_PHYINITCTRL, 0x21);
	do {
		data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITCTRL);
	} while (data & 1);

	data = mmio_read_32(MDDRC_PACK_DDRC_PHYINITSTATUS);
	if (data & 0x7fe)
		NOTICE("ddr3 rank1 init faile in 533MHz\n");
	else
		INFO("ddr3 rank1 init pass in 533MHz\n");

	data = mmio_read_32(MDDRC_PACK_DDRC_TRAINCTRL0);
	data &= ~0xf;
	mmio_write_32(MDDRC_PACK_DDRC_TRAINCTRL0, data);
	return 0;
}

static void init_ddr(void)
{
	unsigned int data;
	int ret;

	/* BUCK2: 1.104V, BUCK3: 1.3V */
	hi6553_write_8(VSET_BUCK2_ADJ, 0x26);
	hi6553_write_8(VSET_BUCK3_ADJ, 0x04);

	/* enable DDR PLL */
	data = mmio_read_32(PMCTRL_DDRPLL0CTRL);
	data |= 1;
	mmio_write_32(PMCTRL_DDRPLL0CTRL, data);
	data = mmio_read_32(PMCTRL_DDRPLL1CTRL);
	data |= 1;
	mmio_write_32(PMCTRL_DDRPLL1CTRL, data);

	udelay(300);

	do {
		data = mmio_read_32(PMCTRL_DDRPLL0CTRL);
		data &= 3 << 28;
	} while (data != (3 << 28));
	do {
		data = mmio_read_32(PMCTRL_DDRPLL1CTRL);
		data &= 3 << 28;
	} while (data != (3 << 28));

	NOTICE("#%s, %d\n", __func__, __LINE__);
	ret = lpddr3_freq_init();
	if (ret)
		return;

	ddrc_common_init();
	dienum_det_and_rowcol_cfg();
	detect_ddr_chip_info();

	/* pll for 533MHz */
	data = mmio_read_32(PMCTRL_DDRPLL0CTRL);
	data &= ~0x1;
	mmio_write_32(PMCTRL_DDRPLL0CTRL, data);
	data = mmio_read_32(PMCTRL_DDRPLL0CTRL);
}

static void init_ddrc_qos(void)
{
	unsigned int port, data;

	mmio_write_32(MDDRC_QOSB_BUF_BYP, 1);

	/* CCPU */
	port = 0;
	mmio_write_32(MDDRC_AXI_QOS_MAP_PORT(port), 0x1210);
	mmio_write_32(MDDRC_AXI_QOS_WRPRI_PORT(port), 0x11111111);
	mmio_write_32(MDDRC_AXI_QOS_RDPRI_PORT(port), 0x11111111);
	mmio_write_32(MDDRC_AXI_OSTD_GROUP(0), 0x001d0007);

	/* ACPU: port 3, G3D: port4 */
	for (port = 3; port <= 4; port++) {
		mmio_write_32(MDDRC_AXI_QOS_MAP_PORT(port), 0x1210);
		mmio_write_32(MDDRC_AXI_QOS_WRPRI_PORT(port), 0x77777777);
		mmio_write_32(MDDRC_AXI_QOS_RDPRI_PORT(port), 0x77777777);
	}

	/* Media: port 1 */
	port = 1;
	mmio_write_32(MDDRC_AXI_QOS_MAP_PORT(port), 0x30000);
	mmio_write_32(MDDRC_AXI_QOS_WRPRI_PORT(port), 0x1234567);
	mmio_write_32(MDDRC_AXI_QOS_RDPRI_PORT(port), 0x1234567);

	/* Sys: port2 */
	mmio_write_32(MDDRC_QOSB_TIMEOUT_MODE, 0);
	mmio_write_32(MDDRC_QOSB_WRTOUT_MAP, 0x3020100);
	mmio_write_32(MDDRC_QOSB_RDTOUT_MAP, 0x3020100);
	mmio_write_32(MDDRC_QOSB_WBUF_PRI_CTRL, 0x01000100);
	mmio_write_32(MDDRC_QOSB_WBUF_CTRL_CHAN(0), 0xd0670402);
	mmio_write_32(MDDRC_QOSB_BANK_CTRL_CHAN(0), 0x31);
	mmio_write_32(MDDRC_QOSB_PUSH_CTRL, 0x7);

	data = mmio_read_32(MDDRC_QOSB_WRTOUT0);
	data &= ~0xff0000;
	data |= 0x400000;
	mmio_write_32(MDDRC_QOSB_WRTOUT0, data);
	data = mmio_read_32(MDDRC_QOSB_RDTOUT0);
	data &= ~0xff0000;
	data |= 0x400000;
	mmio_write_32(MDDRC_QOSB_RDTOUT0, data);
	port = 2;
	mmio_write_32(MDDRC_AXI_QOS_MAP_PORT(port), 0x30000);
	mmio_write_32(MDDRC_AXI_QOS_WRPRI_PORT(port), 0x1234567);
	mmio_write_32(MDDRC_AXI_QOS_RDPRI_PORT(port), 0x1234567);

	/* timeout */
	mmio_write_32(MDDRC_QOSB_WRTOUT0, 0xff7fff);
	mmio_write_32(MDDRC_QOSB_WRTOUT1, 0xff);
	mmio_write_32(MDDRC_QOSB_RDTOUT0, 0xff7fff);
	mmio_write_32(MDDRC_QOSB_RDTOUT1, 0xff);
	mmio_write_32(MDDRC_QOSB_WRTOUT_MAP, 0x3020100);
	mmio_write_32(MDDRC_QOSB_RDTOUT_MAP, 0x3020100);
}
#endif

static void init_mmc_pll(void)
{
	unsigned int data;

	data = hi6553_read_8(LDO19_REG_ADJ);
	data |= 0x7;		/* 3.0V */
	hi6553_write_8(LDO19_REG_ADJ, data);
	/* select syspll as mmc clock */
	mmio_write_32(PERI_SC_CLK_SEL0, 1 << 5 | 1 << 21);
	/* enable mmc0 clock */
	mmio_write_32(PERI_SC_PERIPH_CLKEN0, PERI_CLK_MMC0);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_CLKSTAT0);
	} while (!(data & PERI_CLK_MMC0));
	/* enable source clock to mmc0 */
	data = mmio_read_32(PERI_SC_PERIPH_CLKEN12);
	data |= 1 << 1;
	mmio_write_32(PERI_SC_PERIPH_CLKEN12, data);
	/* scale mmc frequency to 100MHz (divider as 12 since PLL is 1.2GHz */
	mmio_write_32(PERI_SC_CLKCFG8BIT1, (1 << 7) | 0xb);
}

static void reset_mmc0_clk(void)
{
	unsigned int data;

	/* disable mmc0 bus clock */
	mmio_write_32(PERI_SC_PERIPH_CLKDIS0, PERI_CLK_MMC0);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_CLKSTAT0);
	} while (data & PERI_CLK_MMC0);
	/* enable mmc0 bus clock */
	mmio_write_32(PERI_SC_PERIPH_CLKEN0, PERI_CLK_MMC0);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_CLKSTAT0);
	} while (!(data & PERI_CLK_MMC0));
	/* reset mmc0 clock domain */
	mmio_write_32(PERI_SC_PERIPH_RSTEN0, PERI_CLK_MMC0);

	/* bypass mmc0 clock phase */
	data = mmio_read_32(PERI_SC_PERIPH_CTRL2);
	data |= 3;
	mmio_write_32(PERI_SC_PERIPH_CTRL2, data);

	/* disable low power */
	data = mmio_read_32(PERI_SC_PERIPH_CTRL13);
	data |= 1 << 3;
	mmio_write_32(PERI_SC_PERIPH_CTRL13, data);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_RSTSTAT0);
	} while (!(data & (1 << 0)));

	/* unreset mmc0 clock domain */
	mmio_write_32(PERI_SC_PERIPH_RSTDIS0, 1 << 0);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_RSTSTAT0);
	} while (data & (1 << 0));
}

/* Get the boot mode (normal boot/usb download/uart download) */
int query_boot_mode(void)
{
	int boot_mode;

	boot_mode = mmio_read_32(ONCHIPROM_PARAM_BASE);
	if ((boot_mode < 0) || (boot_mode > 2)) {
		NOTICE("Invalid boot mode is found:%d\n", boot_mode);
		panic();
	}
	return boot_mode;
}

/*******************************************************************************
 * Perform any BL1 specific platform actions.
 ******************************************************************************/
void bl1_early_platform_setup(void)
{
	const size_t bl1_size = BL1_RAM_LIMIT - BL1_RAM_BASE;

	/* Initialize the console to provide early debug support */
	console_init(PL011_UART0_BASE, PL011_UART0_CLK_IN_HZ, PL011_BAUDRATE);

	/* Allow BL1 to see the whole Trusted RAM */
	bl1_tzram_layout.total_base = BL1_RW_BASE;
	bl1_tzram_layout.total_size = BL1_RW_SIZE;

	/* Calculate how much RAM BL1 is using and how much remains free */
	bl1_tzram_layout.free_base = BL1_RW_BASE;
	bl1_tzram_layout.free_size = BL1_RW_SIZE;
	reserve_mem(&bl1_tzram_layout.free_base,
		    &bl1_tzram_layout.free_size,
		    BL1_RAM_BASE,
		    bl1_size);

	INFO("BL1: 0x%lx - 0x%lx [size = %u]\n", BL1_RAM_BASE, BL1_RAM_LIMIT,
	     bl1_size);
}

/*******************************************************************************
 * Perform the very early platform specific architecture setup here. At the
 * moment this only does basic initialization. Later architectural setup
 * (bl1_arch_setup()) does not do anything platform specific.
 ******************************************************************************/
void bl1_plat_arch_setup(void)
{
	NOTICE("#%s, %d, bl1 base:0x%x, bl1 size:0x%x\n", __func__, __LINE__,
		bl1_tzram_layout.total_base, bl1_tzram_layout.total_size);
	configure_mmu_el3(bl1_tzram_layout.total_base,
			  bl1_tzram_layout.total_size,
			  BL1_RO_BASE,
			  BL1_RO_LIMIT,
			  BL1_COHERENT_RAM_BASE,
			  BL1_COHERENT_RAM_LIMIT);
}

/*******************************************************************************
 * Function which will perform any remaining platform-specific setup that can
 * occur after the MMU and data cache have been enabled.
 ******************************************************************************/
void bl1_platform_setup(void)
{
	init_timer();
	init_pmussi();
	init_hi6553();
	init_pll();
	init_freq();
#ifdef DDR
	init_ddr();
	init_ddrc_qos();

	mmio_write_32(0x0, 0xa5a55a5a);
	NOTICE("ddr test value:0x%x\n", mmio_read_32(0x0));
#endif
	init_mmc_pll();
	reset_mmc0_clk();
	io_setup();
#if 0
	if (query_boot_mode()) {
		flush_image();
	}
#endif
	//init_mmc();
	query_clk_freq(CLK_MMC0_SRC);
	//get_partition();
}

/*******************************************************************************
 * Before calling this function BL2 is loaded in memory and its entrypoint
 * is set by load_image. This is a placeholder for the platform to change
 * the entrypoint of BL2 and set SPSR and security state.
 * On Juno we are only setting the security state, entrypoint
 ******************************************************************************/
void bl1_plat_set_bl2_ep_info(image_info_t *bl2_image,
			      entry_point_info_t *bl2_ep)
{
	SET_SECURITY_STATE(bl2_ep->h.attr, SECURE);
	bl2_ep->spsr = SPSR_64(MODE_EL1, MODE_SP_ELX, DISABLE_ALL_EXCEPTIONS);
}
