/*
 * The platform bus, as ath10k's snoc.c uses it: LinuxKPI's own header has only a name and a device, because
 * FreeBSD has no such bus. This one stands in its place (the same guard, so that the two are never both read)
 * and has what snoc.c takes from the device it is probed on -- its registers by name, its interrupts by
 * number, its driver data -- given by the bus that stands in for the device tree (wifi/lx/src10/lx_snoc.c).
 */
#ifndef _LINUXKPI_LINUX_PLATFORM_DEVICE_H
#define _LINUXKPI_LINUX_PLATFORM_DEVICE_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/string.h>

struct platform_device {
	const char			*name;
	int				id;
	bool				id_auto;
	struct device			dev;
	/* (ours: what the device tree would have said of it) */
	const struct resource		*resource;
	unsigned int			num_resources;
	const int			*irqs;
	unsigned int			num_irqs;
};

struct platform_device_info {
	struct device			*parent;
	struct fwnode_handle		*fwnode;
	const char			*name;
	int				id;
	u64				dma_mask;
};

struct platform_driver {
	int				(*probe)(struct platform_device *);
	void				(*remove)(struct platform_device *);
	void				(*shutdown)(struct platform_device *);
	struct device_driver		driver;
};

#define	dev_is_platform(dev)	(false)
#define	to_platform_device(d)	container_of((d), struct platform_device, dev)

static inline void *
platform_get_drvdata(const struct platform_device *pdev)
{
	return (pdev->dev.driver_data);
}

static inline void
platform_set_drvdata(struct platform_device *pdev, void *data)
{
	pdev->dev.driver_data = data;
}

static inline struct resource *
platform_get_resource_byname(struct platform_device *pdev, unsigned int type, const char *name)
{
	for (unsigned int i = 0; i < pdev->num_resources; i++) {
		const struct resource *r = &pdev->resource[i];

		if ((r->flags & type) != 0 && r->name != NULL && strcmp(r->name, name) == 0)
			return ((struct resource *)(uintptr_t)r);
	}
	return (NULL);
}

static inline int
platform_get_irq(struct platform_device *pdev, unsigned int n)
{
	return (n < pdev->num_irqs ? pdev->irqs[n] : -ENXIO);
}

static inline void *
dev_get_platdata(struct device *dev)
{
	return (NULL);
}

/* (a device of its own for the radio's firmware, which only a tree with "wifi-firmware" in it asks for: there is none) */
static inline struct platform_device *
platform_device_register_full(const struct platform_device_info *info)
{
	return (ERR_PTR(-ENODEV));
}

static inline void
platform_device_unregister(struct platform_device *pdev)
{
}

static inline int
platform_driver_register(struct platform_driver *pdrv)
{
	return (-ENXIO);
}

static inline void
platform_driver_unregister(struct platform_driver *pdrv)
{
}

/* the driver is not registered with a bus: the one there is calls it by name (lx_snoc.c) */
#define	module_platform_driver(d)	static struct platform_driver *const lx_platform_driver __unused = &(d)

#endif
