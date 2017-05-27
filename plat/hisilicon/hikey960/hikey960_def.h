/*
 * Copyright (c) 2017, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __HIKEY960_DEF_H__
#define __HIKEY960_DEF_H__

#include <common_def.h>
#include <tbbr_img_def.h>

#define DDR_BASE			0x0
#define DDR_SIZE			0xC0000000

#define DEVICE_BASE			0xE0000000
#define DEVICE_SIZE			0x20000000

/* Memory location options for TSP */
#define HIKEY960_DRAM_ID	0

/*
 * DDR (1 GB) at 0x0000_0000 is divided in several regions:
 *   - Secure DDR (default is the top 16MB) used by OP-TEE
 *   - Non-secure DDR used by OP-TEE (shared memory and padding) (4MB)
 *   - Secure DDR (4MB aligned on 4MB) for OP-TEE's "Secure Data Path" feature
 *   - Non-secure DDR (8MB) reserved for OP-TEE's future use
 *   - Non-Secure DDR (remaining DDR starting at DDR_BASE)
 */
#define DDR_SEC_SIZE			0x01000000
#define DDR_SEC_BASE			(DDR_BASE + DDR_SIZE - DDR_SEC_SIZE)

#define DDR_SDP_SIZE			0x00400000
#define DDR_SDP_BASE			(DDR_SEC_BASE - 0x400000 /* align */ - \
					DDR_SDP_SIZE)

#define DDR_NS_BASE			DDR_BASE
#define DDR_NS_SIZE			(DDR_SIZE - DDR_SEC_SIZE)

#define SRAM_BASE			0xFFF80000
#define SRAM_SIZE			0x00012000

/*
 * PL011 related constants
 */
#define PL011_UART5_BASE		0xFDF05000
#define PL011_UART6_BASE		0xFFF32000
#define PL011_BAUDRATE			115200
#define PL011_UART_CLK_IN_HZ		19200000

#define UFS_BASE			0
/* FIP partition */
#define HIKEY960_FIP_BASE		(UFS_BASE + 0x1400000)
#define HIKEY960_FIP_MAX_SIZE		(12 << 20)

#define HIKEY960_UFS_DESC_BASE		0x20000000
#define HIKEY960_UFS_DESC_SIZE		0x00200000	/* 2MB */
#define HIKEY960_UFS_DATA_BASE		0x10000000
#define HIKEY960_UFS_DATA_SIZE		0x0A000000	/* 160MB */

#endif /* __HIKEY960_DEF_H__ */
