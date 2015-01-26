/*
 * Copyright (c) 2014, Linaro Ltd. All rights reserved.
 * Copyright (c) 2014, Hisilicon Ltd. All rights reserved.
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
#include <bl_common.h>
#include <debug.h>
#include <dw_mmc.h>
#include <errno.h>
#include <io_driver.h>
#include <io_storage.h>
#include <platform.h>
#include <platform_def.h>
#include <stdint.h>
#include <string.h>
#include <mmio.h>

/* As we need to be able to keep state for seek, only one file can be open
 * at a time. Make this a structure and point to the entity->info. When we
 * can malloc memory we can change this to support more open files.
 */
typedef struct {
	/* Use the 'in_use' flag as any value for base and file_pos could be
	 * valid.
	 */
	int		in_use;
	uintptr_t	base;
	size_t		file_pos;
	int		boot_partition;
} file_state_t;

struct dw_mmc_info {
	int		init;
	int		boot_partition;
};

static file_state_t current_file = {0};

static struct dw_mmc_info dw_mmc_info = {0, 0};

static int dw_mmc_dev_open(const uintptr_t dev_spec, io_dev_info_t **dev_info);
static int dw_mmc_block_open(io_dev_info_t *dev_info, const uintptr_t spec,
			     io_entity_t *entity);
static int dw_mmc_block_seek(io_entity_t *entity, int mode, ssize_t offset);
static int dw_mmc_block_read(io_entity_t *entity, uintptr_t buffer,
			     size_t length, size_t *length_read);
static int dw_mmc_block_write(io_entity_t *entity, uintptr_t buffer,
			      size_t length, size_t *length_written);
static int dw_mmc_block_close(io_entity_t *entity);
static int dw_mmc_dev_init(io_dev_info_t *dev_info,
			   const uintptr_t init_params);
static int dw_mmc_dev_close(io_dev_info_t *dev_info);

/* Identify the device type as memmap */
io_type_t device_type_dw_mmc(void)
{
	return IO_TYPE_MMC;
}

static const io_dev_connector_t dw_mmc_dev_connector = {
	.dev_open = dw_mmc_dev_open
};

static const io_dev_funcs_t dw_mmc_dev_funcs = {
	.type = device_type_dw_mmc,
	.open = dw_mmc_block_open,
	.seek = dw_mmc_block_seek,
	.size = NULL,
	.read = dw_mmc_block_read,
	.write = dw_mmc_block_write,
	.close = dw_mmc_block_close,
	.dev_init = dw_mmc_dev_init,
	.dev_close = dw_mmc_dev_close,
};


/* No state associated with this device so structure can be const */
static const io_dev_info_t dw_mmc_dev_info = {
	.funcs = &dw_mmc_dev_funcs,
	.info = (uintptr_t)&dw_mmc_info,
};

/* Open a connection to the DW MMC device */
static int dw_mmc_dev_open(const uintptr_t dev_spec __attribute__((unused)),
			   io_dev_info_t **dev_info)
{
	assert(dev_info != NULL);
	*dev_info = (io_dev_info_t *)&dw_mmc_dev_info; /* cast away const */

	return IO_SUCCESS;
}

/* Close a connection to the memmap device */
static int dw_mmc_dev_close(io_dev_info_t *dev_info)
{
	/* NOP */
	/* TODO: Consider tracking open files and cleaning them up here */
	return IO_SUCCESS;
}


/* Open a file on the DW MMC device */
/* TODO: Can we do any sensible limit checks on requested memory */
static int dw_mmc_block_open(io_dev_info_t *dev_info, const uintptr_t spec,
			     io_entity_t *entity)
{
	int result = IO_FAIL;
	const io_block_spec_t *block_spec = (io_block_spec_t *)spec;
	struct dw_mmc_info *info = (struct dw_mmc_info *)(dev_info->info);

	/* Since we need to track open state for seek() we only allow one open
	 * spec at a time. When we have dynamic memory we can malloc and set
	 * entity->info.
	 */
	if (current_file.in_use == 0) {
		assert(block_spec != NULL);
		assert(entity != NULL);

		current_file.in_use = 1;
		current_file.base = block_spec->offset;
		/* File cursor offset for seek and incremental reads etc. */
		current_file.file_pos = 0;
		current_file.boot_partition = info->boot_partition;
		entity->info = (uintptr_t)&current_file;
		result = IO_SUCCESS;
	} else {
		WARN("A DW MMC device is already active. Close first.\n");
		result = IO_RESOURCES_EXHAUSTED;
	}

	return result;
}

/* Seek to a particular file offset on the DW MMC device */
static int dw_mmc_block_seek(io_entity_t *entity, int mode, ssize_t offset)
{
	int result = IO_FAIL;

	/* We only support IO_SEEK_SET for the moment. */
	if (mode == IO_SEEK_SET) {
		assert(entity != NULL);

		/* TODO: can we do some basic limit checks on seek? */
		((file_state_t *)entity->info)->file_pos = offset;
		result = IO_SUCCESS;
	} else {
		result = IO_FAIL;
	}

	return result;
}


/* Read data from a file on the DW MMC device */
static int dw_mmc_block_read(io_entity_t *entity, uintptr_t buffer,
			     size_t length, size_t *length_read)
{
	file_state_t *fp;
	int result, i;

	assert(entity != NULL);
	assert(buffer != (uintptr_t)NULL);
	assert(length_read != NULL);

	fp = (file_state_t *)entity->info;

	NOTICE("###%s, %d, boot_partition:%d\n", __func__, __LINE__, fp->boot_partition);
	/* dst_start param isn't used yet, data will be loaded into MMC_DATA_BASE */
	result = mmc0_read(fp->base + fp->file_pos, length, buffer, fp->boot_partition);
	if (result) {
		NOTICE("failed to ready mmc offset 0x%x\n", fp->base + fp->file_pos);
		return result;
	}

	*length_read = length;
	/* advance the file 'cursor' for incremental reads */
	fp->file_pos += length;

	for (i = 0x0; i < 0x10; i += 4) {
		NOTICE("###[0x%x]:0x%x  ", buffer + i, mmio_read_32(buffer + i));
	}

	return IO_SUCCESS;
}

static int dw_mmc_block_write(io_entity_t *entity, uintptr_t buffer,
			      size_t length, size_t *length_written)
{
	file_state_t *fp;
	int result, i;

	assert(entity != NULL);
	assert(buffer != (uintptr_t)NULL);
	assert(length_written != NULL);

	fp = (file_state_t *)entity->info;

	NOTICE("###%s, %d, boot_partition:%d\n", __func__, __LINE__, fp->boot_partition);
	result = mmc0_write(fp->base + fp->file_pos, length, buffer, fp->boot_partition);
	if (result) {
		NOTICE("failed to ready mmc offset 0x%x\n", fp->base + fp->file_pos);
		return result;
	}

	*length_written = length;
	/* advance the file 'cursor' for incremental reads */
	fp->file_pos += length;

	for (i = 0x0; i < 0x10; i += 4) {
		NOTICE("###[0x%x]:0x%x  ", buffer + i, mmio_read_32(buffer + i));
	}
	return IO_SUCCESS;
}

/* Close a file on the DW_MMC device */
static int dw_mmc_block_close(io_entity_t *entity)
{
	assert(entity != NULL);

	entity->info = 0;

	/* This would be a mem free() if we had malloc.*/
	memset((void *)&current_file, 0, sizeof(current_file));

	return IO_SUCCESS;
}

static int dw_mmc_dev_init(io_dev_info_t *dev_info, const uintptr_t init_params)
{
	struct dw_mmc_info *info = (struct dw_mmc_info *)(dev_info->info);

	NOTICE("###init dwmmc, init:%d, boot partition:%d\n", info->init, init_params);
	if (!info->init) {
		init_mmc();
		info->init = 1;
	}
	info->boot_partition = init_params;
	return IO_SUCCESS;
}

/* Exported functions */

/* Register the memmap driver with the IO abstraction */
int register_io_dev_dw_mmc(const io_dev_connector_t **dev_con)
{
	int result = IO_FAIL;
	assert(dev_con != NULL);

	result = io_register_device(&dw_mmc_dev_info);
	if (result == IO_SUCCESS)
		*dev_con = &dw_mmc_dev_connector;

	return result;
}
