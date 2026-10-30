/* of_device.h: what ath10k's snoc.c takes of it (of_dma_configure(): nothing to configure here) */
#ifndef _LX_LINUX_OF_DEVICE_H_
#define _LX_LINUX_OF_DEVICE_H_
#include <linux/of.h>
#include <linux/device.h>
static inline int of_dma_configure(struct device *dev, struct device_node *np, bool force_dma) { return 0; }
#endif
