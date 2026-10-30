/*
 * Power sequencing (the WCN6855's and others' power-management unit, which newer trees describe): the WCN3990 has
 * none -- its rails are regulators of their own (snoc.c's vreg list) -- and the driver asks for one optionally: "not yet"
 * (-EPROBE_DEFER) is what Linux says where there is none, and the only answer snoc.c goes on after.
 */
#ifndef _LX_LINUX_PWRSEQ_CONSUMER_H_
#define _LX_LINUX_PWRSEQ_CONSUMER_H_
#include <linux/device.h>
#include <linux/err.h>
struct pwrseq_desc;
static inline struct pwrseq_desc *devm_pwrseq_get(struct device *dev, const char *target) { return ERR_PTR(-EPROBE_DEFER); }
static inline int pwrseq_power_on(struct pwrseq_desc *desc) { return 0; }
static inline int pwrseq_power_off(struct pwrseq_desc *desc) { return 0; }
#endif
