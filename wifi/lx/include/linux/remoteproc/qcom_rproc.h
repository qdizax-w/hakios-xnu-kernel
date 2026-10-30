/*
 * The modem's restarts, which snoc.c listens for (qcom_register_ssr_notifier("mpss")): the modem is the kernel's own
 * here (drivers/modem/hakios.mpss.c) and is not restarted under the driver; nothing is ever notified.
 */
#ifndef _LX_QCOM_RPROC_H_
#define _LX_QCOM_RPROC_H_
#include <linux/notifier.h>
#define QCOM_SSR_BEFORE_POWERUP 0
#define QCOM_SSR_AFTER_POWERUP  1
#define QCOM_SSR_BEFORE_SHUTDOWN 2
#define QCOM_SSR_AFTER_SHUTDOWN 3
struct qcom_ssr_notify_data {
	const char *name;
	bool crashed;
};
static inline void *qcom_register_ssr_notifier(const char *name, struct notifier_block *nb) { return (void *)1; }
static inline int qcom_unregister_ssr_notifier(void *notify, struct notifier_block *nb) { return 0; }
#endif
