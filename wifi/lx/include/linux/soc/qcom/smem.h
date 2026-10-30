/*
 * The shared memory, as ath10k's qmi.c uses it: the Wi-Fi firmware's version written into the image version table
 * (ath10k_qmi_add_wlan_ver_smem()), which only Android's tools read. The kernel's own shared memory is the modem
 * driver's (drivers/modem/hakios.mpss.c); here every item is "not there" and the driver goes on without it
 * (wifi/lx/src10/lx_snoc.c).
 */
#ifndef _LX_QCOM_SMEM_H_
#define _LX_QCOM_SMEM_H_

#include <linux/types.h>

#define QCOM_SMEM_HOST_ANY      -1

int qcom_smem_alloc(unsigned host, unsigned item, size_t size);
void *qcom_smem_get(unsigned host, unsigned item, size_t *size);

#endif
