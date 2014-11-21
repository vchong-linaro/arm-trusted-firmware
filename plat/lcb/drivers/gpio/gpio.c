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

#include <console.h>
#include <mmio.h>
#include <errno.h>
#include <gpio.h>
#include <hi6220.h>

#define BIT(nr)		(1UL << (nr))

/* return 0 for failure */
static unsigned int find_gc_base(unsigned int gpio)
{
	int gc;

	gc = gpio / 8;
	if ((gc < 0) || (gc > 19))
		return 0;
	if (gc >= 4)
		return ((gc - 4) * 0x1000 + GPIO4_BASE);
	return (gc * 0x1000 + GPIO0_BASE);
}

int gpio_direction_input(unsigned int gpio)
{
	unsigned int gc_base, offset, data;

	gc_base = find_gc_base(gpio);
	if (!gc_base)
		return -EINVAL;
	offset = gpio % 8;

	data = mmio_read_8(gc_base);
	data &= ~(1 << offset);
	mmio_write_8(gc_base, data);
	return 0;
}

int gpio_direction_output(unsigned int gpio)
{
	unsigned int gc_base, offset, data;

	gc_base = find_gc_base(gpio);
	if (!gc_base)
		return -EINVAL;
	offset = gpio % 8;

	data = mmio_read_8(gc_base);
	data |= 1 << offset;
	mmio_write_8(gc_base, data);
	return 0;
}

int gpio_get_value(unsigned int gpio)
{
	unsigned int gc_base, offset;

	gc_base = find_gc_base(gpio);
	if (!gc_base)
		return -EINVAL;
	offset = gpio % 8;

	return !!mmio_read_8(gc_base + (BIT(offset + 2)));
}

int gpio_set_value(unsigned int gpio, unsigned int value)
{
	unsigned int gc_base, offset;

	gc_base = find_gc_base(gpio);
	if (!gc_base)
		return -EINVAL;
	offset = gpio % 8;
	mmio_write_8(gc_base + (BIT(offset + 2)), !!value << offset); 
	return 0;
}
