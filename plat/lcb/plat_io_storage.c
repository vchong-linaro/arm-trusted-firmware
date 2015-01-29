/*
 * Copyright (c) 2014, ARM Limited and Contributors. All rights reserved.
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

#include <assert.h>
#include <debug.h>
#include <io_driver.h>
#include <io_fip.h>
#include <io_dw_mmc.h>
#include <io_memmap.h>
#include <io_storage.h>
#include <mmio.h>
#include <platform_def.h>
#include <semihosting.h>	/* For FOPEN_MODE_... */
#include <string.h>

static const io_dev_connector_t *bl1_mem_dev_con;
static uintptr_t bl1_mem_dev_spec;
static uintptr_t loader_mem_dev_handle;
static uintptr_t bl1_mem_init_params;
static const io_dev_connector_t *fip_dev_con;
static uintptr_t fip_dev_spec;
static uintptr_t fip_dev_handle;
static const io_dev_connector_t *dw_mmc_dev_con;
static uintptr_t dw_mmc_dev_spec;
static uintptr_t dw_mmc_dev_handle;

static const io_block_spec_t loader_mem_spec = {
	/* l-loader.bin that contains bl1.bin */
	.offset = LOADER_RAM_BASE,
	.length = BL1_RO_LIMIT - LOADER_RAM_BASE,
};

static const io_block_spec_t boot_emmc_spec = {
	.offset = MMC_LOADER_BASE,
	.length = BL1_RO_LIMIT - LOADER_RAM_BASE,
};

static const io_block_spec_t normal_emmc_spec = {
	.offset = MMC_BASE,
	.length = MMC_SIZE,
};

static const io_block_spec_t fip_block_spec = {
	.offset = MMC_BL2_BASE,
	.length = MMC_SIZE - (MMC_BL2_BASE - MMC_BASE)
};

static const io_file_spec_t bl2_file_spec = {
	.path = BL2_IMAGE_NAME,
	.mode = FOPEN_MODE_RB
};

static const io_file_spec_t bl31_file_spec = {
	.path = BL31_IMAGE_NAME,
	.mode = FOPEN_MODE_RB
};

static const io_file_spec_t bl33_file_spec = {
	.path = BL33_IMAGE_NAME,
	.mode = FOPEN_MODE_RB
};

static int open_loader_mem(const uintptr_t spec);
static int open_fip(const uintptr_t spec);
static int open_dw_mmc(const uintptr_t spec);
static int open_dw_mmc_boot(const uintptr_t spec);

struct plat_io_policy {
	const char *image_name;
	uintptr_t *dev_handle;
	uintptr_t image_spec;
	int (*check)(const uintptr_t spec);
};

static const struct plat_io_policy policies[] = {
	{
		LOADER_MEM_NAME,
		&loader_mem_dev_handle,
		(uintptr_t)&loader_mem_spec,
		open_loader_mem
	}, {
		BOOT_EMMC_NAME,
		&dw_mmc_dev_handle,
		(uintptr_t)&boot_emmc_spec,
		open_dw_mmc_boot
	}, {
		NORMAL_EMMC_NAME,
		&dw_mmc_dev_handle,
		(uintptr_t)&normal_emmc_spec,
		open_dw_mmc
	}, {
		FIP_IMAGE_NAME,
		&dw_mmc_dev_handle,
		(uintptr_t)&fip_block_spec,
		open_dw_mmc
	}, {
		BL2_IMAGE_NAME,
		&fip_dev_handle,
		(uintptr_t)&bl2_file_spec,
		open_fip
	}, {
		BL31_IMAGE_NAME,
		&fip_dev_handle,
		(uintptr_t)&bl31_file_spec,
		open_fip
	}, {
		BL33_IMAGE_NAME,
		&fip_dev_handle,
		(uintptr_t)&bl33_file_spec,
		open_fip
	}, {
		0, 0, 0
	}
};

static int open_loader_mem(const uintptr_t spec)
{
	int result = IO_FAIL;
	uintptr_t image_handle;

	result = io_dev_init(loader_mem_dev_handle, bl1_mem_init_params);
	if (result == IO_SUCCESS) {
		result = io_open(loader_mem_dev_handle, spec, &image_handle);
		if (result == IO_SUCCESS) {
			io_close(image_handle);
		}
	}
	return result;
}

static int open_fip(const uintptr_t spec)
{
	int result = IO_FAIL;

	/* See if a Firmware Image Package is available */
	result = io_dev_init(fip_dev_handle, (uintptr_t)FIP_IMAGE_NAME);
	if (result == IO_SUCCESS) {
		INFO("Using FIP\n");
		/*TODO: Check image defined in spec is present in FIP. */
	}
	return result;
}


static int open_dw_mmc(const uintptr_t spec)
{
	int result = IO_FAIL;
	uintptr_t image_handle;

	/* indicate to select normal partition in eMMC */
	result = io_dev_init(dw_mmc_dev_handle, 0);
	if (result == IO_SUCCESS) {
		result = io_open(dw_mmc_dev_handle, spec, &image_handle);
		if (result == IO_SUCCESS) {
			/* INFO("Using DW MMC IO\n"); */
			io_close(image_handle);
		}
	}
	return result;
}

static int open_dw_mmc_boot(const uintptr_t spec)
{
	int result = IO_FAIL;
	uintptr_t image_handle;

	/* indicate to select boot partition in eMMC */
	result = io_dev_init(dw_mmc_dev_handle, 1);
	if (result == IO_SUCCESS) {
		result = io_open(dw_mmc_dev_handle, spec, &image_handle);
		if (result == IO_SUCCESS) {
			/* INFO("Using DW MMC IO\n"); */
			io_close(image_handle);
		}
	}
	return result;
}

void io_setup(void)
{
	int io_result = IO_FAIL;

	/* Register the IO devices on this platform */
	io_result = register_io_dev_fip(&fip_dev_con);
	assert(io_result == IO_SUCCESS);

	io_result = register_io_dev_dw_mmc(&dw_mmc_dev_con);
	assert(io_result == IO_SUCCESS);

	io_result = register_io_dev_memmap(&bl1_mem_dev_con);
	assert(io_result == IO_SUCCESS);

	/* Open connections to devices and cache the handles */
	io_result = io_dev_open(fip_dev_con, fip_dev_spec, &fip_dev_handle);
	assert(io_result == IO_SUCCESS);

	io_result = io_dev_open(dw_mmc_dev_con, dw_mmc_dev_spec,
				&dw_mmc_dev_handle);
	assert(io_result == IO_SUCCESS);

	io_result = io_dev_open(bl1_mem_dev_con, bl1_mem_dev_spec,
				&loader_mem_dev_handle);
	assert(io_result == IO_SUCCESS);

	/* Ignore improbable errors in release builds */
	(void)io_result;
}

/* Return an IO device handle and specification which can be used to access
 * an image. Use this to enforce platform load policy */
int plat_get_image_source(const char *image_name, uintptr_t *dev_handle,
			  uintptr_t *image_spec)
{
	int result = IO_FAIL;
	const struct plat_io_policy *policy;

	if ((image_name != NULL) && (dev_handle != NULL) &&
	    (image_spec != NULL)) {
		policy = policies;
		while (policy->image_name != NULL) {
			if (strcmp(policy->image_name, image_name) == 0) {
				result = policy->check(policy->image_spec);
				if (result == IO_SUCCESS) {
					*image_spec = policy->image_spec;
					*dev_handle = *(policy->dev_handle);
					break;
				}
			}
			policy++;
		}
	} else {
		result = IO_FAIL;
	}
	return result;
}

#define FLUSH_BASE		(DDR_BASE + 0x100000)
#define TEST_BASE		(DDR_BASE + 0x300000)

struct entry_head {
	unsigned char	magic[8];
	unsigned char	name[8];
	unsigned int	start;	/* lba */
	unsigned int	count;	/* lba */
	unsigned int	flag;
};

static int parse_loader(void *buf, int num, struct entry_head *hd)
{
	if (hd == NULL)
		return IO_FAIL;
	memcpy((void *)hd, (void *)(FLUSH_BASE + 28), 140);
	return IO_SUCCESS;
}

static int flush_loader(void)
{
	uintptr_t img_handle, spec;
	int result = IO_FAIL;
	size_t bytes_read, length;
	struct entry_head entries[5];
	ssize_t offset;
	int i, fp;

	result = parse_loader((void *)FLUSH_BASE, 5, entries);
	if (result) {
		WARN("failed to parse entries in loader image\n");
		return result;
	}

	spec = 0;
	for (i = 0, fp = 0; i < 5; i++) {
		if (entries[i].flag) {
			result = plat_get_image_source(BOOT_EMMC_NAME, &dw_mmc_dev_handle, &spec);
			if (result) {
				WARN("failed to open emmc boot partition\n");
				return result;
			}
			if (i == 0)
				offset = MMC_LOADER_BASE + 0x800;
			else
				offset = MMC_LOADER_BASE + 0x800 + entries[i].start * 512;

			result = io_open(dw_mmc_dev_handle, (uintptr_t)&boot_emmc_spec, &img_handle);
			if (result != IO_SUCCESS) {
				WARN("Failed to open memmap device\n");
				return result;
			}
		} else {
			result = plat_get_image_source(NORMAL_EMMC_NAME, &dw_mmc_dev_handle, &spec);
			if (result) {
				WARN("failed to open emmc normal partition\n");
				return result;
			}
			if (i == 2)
				offset = MMC_BL2_BASE;
			else
				offset = MMC_LOADER_BASE + entries[i].start * 512;

			result = io_open(dw_mmc_dev_handle, (uintptr_t)&normal_emmc_spec, &img_handle);
			if (result != IO_SUCCESS) {
				WARN("Failed to open memmap device\n");
				return result;
			}
		}
		length = entries[i].count * 512;

		result = io_seek(img_handle, IO_SEEK_SET, offset);
		if (result)
			goto exit;

		if (i < 3) {
			result = io_write(img_handle, FLUSH_BASE + fp, length, &bytes_read);
			if ((result != IO_SUCCESS) || (bytes_read < length)) {
				WARN("Failed to write '%s' file (%i)\n", LOADER_MEM_NAME, result);
				goto exit;
			}
		}
		fp += entries[i].count * 512;

		io_close(img_handle);
	}
	return result;
exit:
	io_close(img_handle);
	return result;
}

int flush_image(void)
{
	uintptr_t bl1_image_spec;
	int result = IO_FAIL;
	size_t bytes_read, length;
	uintptr_t img_handle;
	int i;

	result = plat_get_image_source(LOADER_MEM_NAME, &loader_mem_dev_handle,
				       &bl1_image_spec);

	result = io_open(loader_mem_dev_handle, bl1_image_spec, &img_handle);
	if (result != IO_SUCCESS) {
		WARN("Failed to open memmap device\n");
		goto exit;
	}
	length = loader_mem_spec.length;
	result = io_read(img_handle, FLUSH_BASE, length, &bytes_read);
	if ((result != IO_SUCCESS) || (bytes_read < length)) {
		WARN("Failed to load '%s' file (%i)\n", LOADER_MEM_NAME, result);
		goto exit;
	}
	/* clear flags */
	for (i = 0; i < 0x10; i += 4)
		mmio_write_32(FLUSH_BASE + 0x800 + i, 0);
	io_close(img_handle);

	result = flush_loader();
	if (result != IO_SUCCESS) {
		io_dev_close(loader_mem_dev_handle);
		return result;
	}
exit:
	io_close(img_handle);
	io_dev_close(loader_mem_dev_handle);
	return result;
}
