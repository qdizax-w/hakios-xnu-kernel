/*
 * What ath10k's snoc.c adds to LinuxKPI's iommu.h: a domain of its own for the radio's firmware, which it makes
 * only where the tree has a "wifi-firmware" node (the firmware loaded by Linux, not by the modem). Here the modem
 * loads it: none of this is ever reached, and it fails if it is. The radio's own view of memory is the kernel's
 * (lx_os_radio_memory()).
 */
#ifndef _LX_LINUX_IOMMU_H_
#define _LX_LINUX_IOMMU_H_

#include_next <linux/iommu.h>
#include <linux/err.h>

#define	IOMMU_READ	(1 << 0)
#define	IOMMU_WRITE	(1 << 1)

struct device;
static inline struct iommu_domain *iommu_paging_domain_alloc(struct device *dev) { return (ERR_PTR(-ENODEV)); }
static inline int iommu_attach_device(struct iommu_domain *domain, struct device *dev) { return (-ENODEV); }
static inline void iommu_detach_device(struct iommu_domain *domain, struct device *dev) { }
static inline void iommu_domain_free(struct iommu_domain *domain) { }
static inline int iommu_map(struct iommu_domain *domain, unsigned long iova, phys_addr_t paddr, size_t size, int prot,
    gfp_t gfp) { return (-ENODEV); }
static inline size_t iommu_unmap(struct iommu_domain *domain, unsigned long iova, size_t size) { return (0); }

#endif
