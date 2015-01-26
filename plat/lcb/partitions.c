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

#include <debug.h>
#include <dw_mmc.h>
#include <errno.h>
#include <mmio.h>
#include <partitions.h>
#include <string.h>

#define EFI_ENTRIES		128
#define EFI_NAMELEN		36
#define EFI_ENTRY_SIZE		(sizeof(struct efi_entry))
#define EFI_MBR_SIZE		512
#define EFI_HEADER_SIZE		512
#define EFI_TOTAL_SIZE		(EFI_MBR_SIZE + EFI_HEADER_SIZE +	\
				EFI_ENTRY_SIZE * EFI_ENTRIES)

struct efi_entry {
	uint8_t		type_uuid[16];
	uint8_t		uniq_uuid[16];
	uint64_t	first_lba;
	uint64_t	last_lba;
	uint64_t	attr;
	uint16_t	name[EFI_NAMELEN];
};

struct ptentry {
	char		name[16];
	unsigned int	start;
	unsigned int	length;
	unsigned int	flags;
	unsigned int	loadaddr;
};

struct ptentry ptable[MAX_PARTITION_NUM] = {
	{ "fastboot1", 		0x00000000, 0x00040000,	1, 0x00000000 },
	{ "ptable", 		0x00000000, 0x00080000,	0, 0x00000000 },
	{ "mcuimage",		0x00100000, 0x00040000, 0, 0xf6000100 },
	{ "fastboot",		0x00200000, 0x00400000, 0, 0x00000000 },
	{ "securetystorage",	0x06800000, 0x02000000, 0, 0x00000000 },
	{ "teeos",		0x15e00000, 0x00400000, 0, 0xfff80000 },
	{ "sensorhub",		0x16400000, 0x01000000, 0, 0x00000000 },
	{ "boot",		0x18000000, 0x01800000, 0, 0x00000000 },
	{ "recovery",		0x19800000, 0x02000000, 0, 0x00000000 },
	{ "dtimage",		0x1b800000, 0x02000000, 0, 0x00000000 },
	{ "system",		0x37800000, 0x70000000, 0, 0x00000000 },
	{ "userdata",		0xd7800000, 0xe8800000, 0, 0x00000000 },
};

struct ptentry *get_partition_entry(const char *name)
{
	struct ptentry *p = NULL;
	int i = 0;

	for (i = 0; i < sizeof(ptable) / sizeof(struct ptentry); i++) {
		if (!strncmp(ptable[i].name, name, 16)) {
			p = &ptable[i];
			break;
		}
	}
	return p;
}

int get_partition(void)
{
	struct ptentry *p = NULL;
	int ret;

	p = get_partition_entry("ptable");
	if (!p) {
		NOTICE("failed to find \"ptable\" partition\n");
		return -EFAULT;
	}
	/* load 256KB into address 0x0 */
	ret = mmc0_read(0, 0x4400, 0, 0);
	return ret;
}
