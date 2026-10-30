/*
 * Clocks, as ath10k's snoc.c asks for them (devm_clk_bulk_get_optional(): the radio's "cxo_ref_clk_pin", optional):
 * the boot loader leaves the radio's reference clock running on miatoll, and there is none to get here -- the
 * driver goes on without it, as it does where the tree names none.
 */
#ifndef _LX_LINUX_CLK_H_
#define _LX_LINUX_CLK_H_

#include <linux/device.h>

struct clk;
struct clk_bulk_data {
	const char *id;
	struct clk *clk;
};

static inline int devm_clk_bulk_get_optional(struct device *dev, int n, struct clk_bulk_data *clks) { return 0; }
static inline int clk_bulk_prepare_enable(int n, const struct clk_bulk_data *clks) { return 0; }
static inline void clk_bulk_disable_unprepare(int n, const struct clk_bulk_data *clks) { }

#endif
