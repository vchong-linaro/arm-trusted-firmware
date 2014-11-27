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

#include <console.h>
#include <debug.h>
#include <errno.h>
#include <mmio.h>
#include <sp804_timer.h>
#include <pll.h>
#include <hi6220.h>

int query_clk_freq(int clk_idx)
{
	unsigned int data, clk_sel, freq;
	//int i;

	if ((clk_idx < 0) || (clk_idx >= CLK_ERROR)) {
		NOTICE("wrong clock index is specified:%d\n", clk_idx);
		return 0;
	}

	/* disable fm select first */
	mmio_write_32(PERI_SC_PERIPH_CTRL14, 0);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_CTRL14);
	} while (data & PERIPH_CTRL14_FM_EN);

	/* select desired fm clock */
	clk_sel = clk_idx << PERIPH_CTRL14_FM_CLK_SEL_SHIFT;
	clk_sel |= PERIPH_CTRL14_FM_EN;
	mmio_write_32(PERI_SC_PERIPH_CTRL14, clk_sel);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_CTRL14);
	} while (data != clk_sel);

	mdelay(1);
	/* read frequency for desired clock */
	freq = mmio_read_32(PERI_SC_PERIPH_STAT1);
	if (freq / 1000000)
		NOTICE("get clk(%d) with %dMHz (%dHz)\n",
			clk_idx, freq / 1000000, freq);
	else if (freq / 1000)
		NOTICE("get clk(%d) with %dKHz (%dHz)\n",
			clk_idx, freq / 1000, freq);
	else
		NOTICE("get clk(%d) with %dHz\n", clk_idx, freq);
	return freq;
}

