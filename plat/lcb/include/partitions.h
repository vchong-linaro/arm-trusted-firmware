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

#ifndef __PARTITIONS_H__
#define __PARTITIONS_H__

#define MAX_PARTITION_NUM		128
#define EFI_NAMELEN		36

struct ptentry {
	unsigned int	start;
	unsigned int	length;
	unsigned int	flags;
	unsigned int	loadaddr;
	unsigned int	loadsize;      /*real data size of this partition, must smaller than "length"*/
	int		id;    
	char		name[EFI_NAMELEN];
};

typedef enum _vrl_id
{
	ID_NONE         = -3,
	ID_FASTBOOT1    = -2,
	ID_FASTBOOT2_BT = -1,  /*modify by y65256 for DTS2013081302132*/
	ID_PTABLE       = 0,   /* 1~8 reserved for ptables with different emmc size */
	ID_MCU_EXE      = 9,
	ID_CP_OS        = 10,
	ID_CP_NV        = 11,
	ID_DSP_BIN      = 12,
	ID_HIFI_BIN     = 13,
	ID_TEE_OS       = 14,
	ID_RECOVERY_IMG = 15,
	ID_BOOT_IMG     = 16,
	ID_FASTBOOT2    = 17, /*modify by y65256 for DTS2013072000181*/
	ID_DTS_IMG      = 18, /*modify by l00130025 */
	ID_BUTTOM
} vrl_id;


extern int get_partition(void);
extern struct ptentry *find_ptn(const char *str);
extern int update_fip_spec(void);

#endif /* __PARTITIONS_H__ */

