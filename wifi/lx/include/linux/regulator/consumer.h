/*
 * Regulators, as ath10k's snoc.c asks for them: the WCN3990's rails, by their names in the device tree. On
 * miatoll the boot chain and the modem have them up -- the firmware is answering by the time the driver
 * runs (drivers/modem/hakios.mpss.c, WLFW) -- and the rail requests that freeze this phone are not made
 * (adsp_rails/modem_rails=0): every rail is there and on, and the driver's switching is a no-op.
 */
#ifndef _LINUXKPI_LINUX_REGULATOR_CONSUMER_H_
#define _LINUXKPI_LINUX_REGULATOR_CONSUMER_H_

#include <linux/err.h>
#include <linux/device.h>

struct regulator;
struct regulator_bulk_data {
	const char *supply;
	struct regulator *consumer;
	int init_load_uA;
	int ret;
};

static inline int
devm_regulator_bulk_get(struct device *dev, int n, struct regulator_bulk_data *consumers)
{
	for (int i = 0; i < n; i++)
		consumers[i].consumer = (struct regulator *)(uintptr_t)1;
	return (0);
}

static inline int regulator_bulk_enable(int n, struct regulator_bulk_data *consumers) { return (0); }
static inline int regulator_bulk_disable(int n, struct regulator_bulk_data *consumers) { return (0); }
static inline int regulator_set_load(struct regulator *regulator, int load) { return (0); }
static inline int regulator_set_voltage(struct regulator *regulator, int low, int high) { return (0); }

#endif
