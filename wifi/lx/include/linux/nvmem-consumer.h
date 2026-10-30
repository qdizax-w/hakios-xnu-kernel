/*
 * Calibration data in non-volatile memory cells, which ath10k's core.c looks for ("calibration", "pre-calibration")
 * before it goes on without: there are no such cells here (LinuxKPI's own header is empty).
 */
#ifndef _LX_LINUX_NVMEM_CONSUMER_H_
#define _LX_LINUX_NVMEM_CONSUMER_H_

#include <linux/device.h>
#include <linux/err.h>

struct nvmem_cell;

static inline struct nvmem_cell *
devm_nvmem_cell_get(struct device *dev, const char *id)
{
	return (ERR_PTR(-ENOENT));
}

static inline void *
nvmem_cell_read(struct nvmem_cell *cell, size_t *length)
{
	return (ERR_PTR(-ENOENT));
}

#endif
