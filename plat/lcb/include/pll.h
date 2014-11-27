/*
 * Copyright (c) 2014, Linaro Ltd.
 * Copyright (c) 2014, Hisilicon Ltd.
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

#ifndef __PLL_H__
#define	__PLL_H__

enum {
	CLK_SYSPLL_DIV2_SRC = 0,
	CLK_300M_SRC,
	CLK_SYSPLL_DIV_SRC,
	CLK_BBPPLL0_DIV5_SRC,
	CLK_SYSPLL_DIV8_SRC,
	CLK_ACPU_DIV_SRC,
	CLK_DDRPHY_REF_SRC,
	CLK_MMC0_SRC,
	CLK_MMC1_SRC = 8,
	CLK_MMC2_SRC,
	CLK_PICOPHY_TEST_SRC,
	CLK_HIFI_SRC,
	CLK_CS_TPIU_SRC,
	CLK_TSENSOR_SRC,
	CLK_150M_SRC,
	CLK_BUS_SRC,
	CLK_DDRC_AXI1_SRC = 16,
	CLK_DDRC_AXI3_SRC,
	CLK_DDRC_AXI_SRC,
	CLK_SYS_NOC_150M_SRC,
	CLK_HARQ_SRC,
	CLK_CS_DAPB_SRC,
	CLK_SYS_SEL_SRC,
	CLK_DDR_SEL_SRC,
	CLK_SLOW_OFF_SRC = 24,
	CLK_CCPU_SEL_SRC,
	CLK_UART1_SRC,
	CLK_UART2_SRC,
	CLK_UART3_SRC,
	CLK_UART4_SRC,
	CLK_ACPUPLL_SRC,
	CLK_GPUPLL_SRC,
	CLK_MEDPLL_SRC = 32,
	CLK_SYSPLL_SRC,
	CLK_DDRPLL_SRC,
	CLK_DDRPLL1_SRC,
	CLK_BBPPLL0_SRC,
	CLK_SYS_ON,
	CLK_TCXO,
	JTAG_TCK,
	CLK_ERROR,
};

extern int query_clk_freq(int);

#endif	/* __PLL_H__ */
