/*
 * The radio's bus on miatoll: ath10k's own (snoc.c, compiled here as part of this file, unmodified) on a device that the
 * device tree would have described -- its registers, its twelve interrupts, its properties -- and the entries the kernel
 * calls (lx_start() and the rest of bsd/sys/hakios.lx.h). What the platform bus does under Linux is done here in its
 * order: the device made, the driver's probe called on it, its removal called on the way out.
 *
 * The radio is a WCN3990 (Redmi Note 9S, SM7125; Linux: sc7180.dtsi, wifi@18800000):
 *   membase   0x18800000, 8 MB: the copy engines' registers
 *   CE 0..11  SPIs 414..425: not taken; a thread of ours looks at the copy engines once a millisecond while the driver
 *             has them on (the driver's interrupts, enabled or not, are noted and nothing else), as the POCO's bus did
 *             first (wifi/lx/src/lx_bus.c)
 *   MSA       the firmware's memory, which the modem's driver has already given it (drivers/modem/hakios.mpss.c, WLFW):
 *             "qcom,msa-fixed-perm", and no reserved region, so that nothing here ever maps or assigns it
 * The firmware is the modem's, up and ready before lx_start() is called; the driver's QMI client (qmi.c) hears that in
 * the answer to its first request and goes straight on to the driver's registration (snoc.c: ath10k_snoc_fw_indication()).
 * The rails and the reference clock are the boot chain's and the modem's, and on: the driver's regulators and clocks
 * are empty (wifi/lx/include/linux/regulator/consumer.h, linux/clk.h).
 *
 * Frames: in "Ethernet" frame mode the firmware puts the kernel's frames into 802.11 itself (mac.c: TX encapsulation
 * offload); what it receives the driver hands up as 802.11, which lx_80211.c makes Ethernet of (to_ethernet()).
 */
#define LX_QMI_AS_IT_IS				/* (this file is where the driver's requests go past) */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* what snoc.c asks of its device that the device tree would have answered, and Linux 7.0 has and LinuxKPI not */
#define device_get_match_data(dev)	((const void *)&drv_priv)
#define netif_threaded_enable(dev)	((void)0)
int device_property_read_u32(struct device *dev, const char *name, u32 *value);
void *devm_memremap(struct device *dev, resource_size_t offset, size_t size, unsigned long flags);

#include "snoc.c"

#include <lx_os.h>
#include "lx.h"

#define LX_IRQ		400		/* the numbers the copy engines' interrupts are given (they are no GIC's) */

static struct {
	struct ath10k *ar;
	struct napi_struct *napi;
	volatile bool napi_on;		/* between the driver's napi_enable() and napi_disable() */
	volatile bool polling;		/* the thread is in the driver right now */
	volatile bool looking;		/* the thread is wanted ... */
	volatile bool looks;		/* ... and runs */
	unsigned long long polls, busy;	/* looks at the copy engines, and of them those that found work */
	struct {
		irq_handler_t handler;
		void *argument;
		bool enabled;
	} irq[CE_COUNT_MAX];
} bus;

/* ---- what the driver says ---------------------------------------------------------------------------- */

static void
driver_says(const char *kind, const char *format, va_list list)
{
	char line[320];
	size_t length;

	vsnprintf(line, sizeof(line), format, list);
	length = strlen(line);
	printf("ath10k%s: %s%s", kind, line, length != 0 && line[length - 1] == '\n' ? "" : "\n");
}

void
ath10k_info(struct ath10k *ar, const char *format, ...)
{
	va_list list;

	va_start(list, format);
	driver_says("", format, list);
	va_end(list);
}

/* (the driver's warnings and errors are said whatever happens to the rest) */
static void
driver_complains(const char *kind, const char *format, va_list list)
{
	char line[320];

	vsnprintf(line, sizeof(line), format, list);
	lx_tell("ath10k %s: %s%s", kind, line, line[0] != 0 && line[strlen(line) - 1] == '\n' ? "" : "\n");
}

void
ath10k_warn(struct ath10k *ar, const char *format, ...)
{
	va_list list;

	va_start(list, format);
	driver_complains("WARNS", format, list);
	va_end(list);
}

void
ath10k_err(struct ath10k *ar, const char *format, ...)
{
	va_list list;

	va_start(list, format);
	driver_complains("ERROR", format, list);
	va_end(list);
}

void
__ath10k_dbg(struct ath10k *ar, enum ath10k_debug_mask mask, const char *format, ...)
{
	va_list list;

	va_start(list, format);
	driver_says(" (dbg)", format, list);
	va_end(list);
}

void
ath10k_dbg_dump(struct ath10k *ar, enum ath10k_debug_mask mask, const char *message, const char *prefix, const void *buffer,
    size_t length)
{
	if ((ath10k_debug_mask & mask) && message != NULL) {
		printf("ath10k (dbg): %s (%zu bytes)\n", message, length);
	}
}

void
ath10k_print_driver_info(struct ath10k *ar)
{
	printf("ath10k: %s (%#x), firmware %s, api %d, htt-op %d, wmi-op %d; max %u stations; frame mode %u\n",
	    ar->hw_params.name, ar->target_version, ar->hw->wiphy->fw_version, ar->fw_api,
	    ar->normal_mode_fw.fw_file.htt_op_version, ar->normal_mode_fw.fw_file.wmi_op_version, ar->max_num_stations,
	    ath10k_frame_mode);
}

void
ath10k_debug_print_hwfw_info(struct ath10k *ar)
{
	printf("ath10k: %s target %#x chip %#x; firmware %s (api %d)\n", ar->hw_params.name, ar->target_version, ar->bus_param.chip_id,
	    ar->hw->wiphy->fw_version, ar->fw_api);
}

void
ath10k_debug_print_board_info(struct ath10k *ar)
{
}

void
ath10k_debug_print_boot_info(struct ath10k *ar)
{
	printf("ath10k: htt-ver %d.%d wmi-op %d htt-op %d; max stations %u, raw %d, hardware crypto %d\n", ar->htt.target_version_major,
	    ar->htt.target_version_minor, ar->normal_mode_fw.fw_file.wmi_op_version, ar->normal_mode_fw.fw_file.htt_op_version,
	    ar->max_num_stations, test_bit(ATH10K_FLAG_RAW_MODE, &ar->dev_flags), !test_bit(ATH10K_FLAG_HW_CRYPTO_DISABLED,
	    &ar->dev_flags));
}

/* ---- the device tree, of which there is none: what it says of this radio ---------------------------------- */

static const struct property radio_properties[] = {
	{ .name = "qcom,msa-fixed-perm", .length = 0, .value = "" },
};

const struct property *
of_find_property(const struct device_node *np, const char *name, int *lenp)
{
	for (int i = 0; np != NULL && i < np->nproperties; i++) {
		if (strcmp(np->properties[i].name, name) == 0) {
			if (lenp != NULL) {
				*lenp = np->properties[i].length;
			}
			return (&np->properties[i]);
		}
	}
	return (NULL);
}

const void *
of_get_property(const struct device_node *np, const char *name, int *lenp)
{
	const struct property *p = of_find_property(np, name, lenp);

	return (p != NULL ? p->value : NULL);
}

int
of_property_read_string(const struct device_node *np, const char *name, const char **out)
{
	return (-EINVAL);
}

int
of_property_read_u32_index(const struct device_node *np, const char *name, u32 index, u32 *out)
{
	return (-EINVAL);
}

bool
of_device_is_compatible(const struct device_node *np, const char *compatible)
{
	return (strcmp(compatible, "qcom,wcn3990-wifi") == 0);
}

bool
of_machine_is_compatible(const char *compatible)
{
	return (false);
}

struct device_node *
of_get_child_by_name(const struct device_node *np, const char *name)
{
	return (NULL);			/* (no "wifi-firmware": the modem loads the firmware) */
}

struct device_node *
of_find_node_by_phandle(phandle handle)
{
	return (NULL);
}

struct device_node *
of_parse_phandle(const struct device_node *np, const char *name, int index)
{
	return (NULL);
}

const struct of_device_id *
of_match_node(const struct of_device_id *matches, const struct device_node *np)
{
	return (NULL);
}

int
of_reserved_mem_region_to_resource(const struct device_node *np, unsigned int index, struct resource *res)
{
	return (-ENODEV);		/* (the firmware's memory is the modem driver's business: see the top) */
}

int
device_property_read_u32(struct device *dev, const char *name, u32 *value)
{
	return (-EINVAL);		/* (no "qcom,xo-cal-data") */
}

void *
devm_memremap(struct device *dev, resource_size_t offset, size_t size, unsigned long flags)
{
	return (ERR_PTR(-ENXIO));
}

/* The radio's own address, which the driver asks its device for before it takes the firmware's (core.c). */
int
linuxkpi_device_get_mac_address(struct device *dev, char *address)
{
	return (lx_os_radio_address((unsigned char *)address) ? 0 : -ENOENT);
}

/* Memory for the lifetime of the device: never given back here (nothing removes this driver but the end of the run). */
void *
linuxkpi_dmam_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t flag)
{
	return (linux_dma_alloc_coherent(dev, size, handle, flag));
}

void
linuxkpi_dmam_free_coherent(struct device *dev, size_t size, void *memory, dma_addr_t handle)
{
}

/* The firmware's memory is not assigned here (msa-fixed-perm), and the modem's image table is not read. */
int
qcom_scm_assign_mem(phys_addr_t address, size_t size, u64 *source, const struct qcom_scm_vmperm *destination,
    unsigned int count)
{
	lx_tell("lx: the driver asks TrustZone to assign the firmware's memory: not done here\n");
	return (-EOPNOTSUPP);
}

void *
qcom_smem_get(unsigned host, unsigned item, size_t *size)
{
	return (ERR_PTR(-ENOENT));
}

/*
 * A request of the driver's to the firmware, on its way out (linux/soc/qcom/qmi.h, ours). One matters here: the mode.
 * Outside mission mode the copy engines' registers stop answering, and a read of them then stops the processor; the
 * driver lets go of them before it asks for "off" when it goes down in order, but not always where its start fails
 * late. So they are let go of here, before such a request leaves.
 */
int
lx_qmi_send_request(struct qmi_handle *qmi, struct sockaddr_qrtr *sq, struct qmi_txn *txn, int msg_id, size_t largest,
    const struct qmi_elem_info *ei, const void *structure)
{
	if (msg_id == QMI_WLFW_WLAN_MODE_REQ_V01) {
		unsigned mode = ((const struct wlfw_wlan_mode_req_msg_v01 *)structure)->mode;

		if (mode != QMI_WLFW_MISSION_V01) {
			bus.napi_on = false;
			while (bus.polling) {
				lx_pause_ms(1);
			}
		}
		lx_tell("lx: the firmware is asked for mode %u (0: mission, 4: off)\n", mode);
	}
	return (qmi_send_request(qmi, sq, txn, msg_id, largest, ei, structure));
}

/* ---- the copy engines' interrupts: noted, and looked for instead ------------------------------------------ */

int
lkpi_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler, irq_handler_t thread_handler, unsigned long flags,
    const char *name, void *argument)
{
	unsigned int n = irq - LX_IRQ;

	if (n >= CE_COUNT_MAX) {
		return (-EINVAL);
	}
	bus.irq[n].handler = handler;
	bus.irq[n].argument = argument;
	bus.irq[n].enabled = !(flags & IRQF_NO_AUTOEN);
	return (0);
}

int
lkpi_enable_irq(unsigned int irq)
{
	if (irq - LX_IRQ < CE_COUNT_MAX) {
		bus.irq[irq - LX_IRQ].enabled = true;
	}
	return (0);
}

void
lkpi_disable_irq(unsigned int irq)
{
	if (irq - LX_IRQ < CE_COUNT_MAX) {
		bus.irq[irq - LX_IRQ].enabled = false;
	}
}

void
lkpi_free_irq(unsigned int irq, void *argument)
{
	if (irq - LX_IRQ < CE_COUNT_MAX) {
		bus.irq[irq - LX_IRQ].handler = NULL;
		bus.irq[irq - LX_IRQ].enabled = false;
	}
}

int
enable_irq_wake(unsigned int irq)
{
	return (0);
}

int
disable_irq_wake(unsigned int irq)
{
	return (0);
}

/* ---- NAPI: the driver's polling, which the thread below does ---------------------------------------- */

struct net_device *
linuxkpi_alloc_netdev(size_t private, const char *name, uint32_t flags, void (*setup)(struct net_device *))
{
	struct net_device *nd = kzalloc(sizeof(*nd) + private, GFP_KERNEL);

	if (nd != NULL && setup != NULL) {
		setup(nd);
	}
	return (nd);
}

void
linuxkpi_free_netdev(struct net_device *nd)
{
	kfree(nd);
}

void
linuxkpi_init_dummy_netdev(struct net_device *nd)
{
}

void
linuxkpi_netif_napi_add(struct net_device *nd, struct napi_struct *napi, int (*poll)(struct napi_struct *, int))
{
	napi->dev = nd;
	napi->poll = poll;
	bus.napi = napi;
}

void
linuxkpi_netif_napi_del(struct napi_struct *napi)
{
	if (bus.napi == napi) {
		bus.napi = NULL;
	}
}

bool
linuxkpi_napi_schedule_prep(struct napi_struct *napi)
{
	return (true);
}

void
linuxkpi___napi_schedule(struct napi_struct *napi)
{
}

bool
linuxkpi_napi_schedule(struct napi_struct *napi)
{
	return (true);			/* (the thread looks once a millisecond anyway) */
}

void
linuxkpi_napi_reschedule(struct napi_struct *napi)
{
}

bool
linuxkpi_napi_complete_done(struct napi_struct *napi, int done)
{
	return (true);
}

bool
linuxkpi_napi_complete(struct napi_struct *napi)
{
	return (true);
}

/* returns when the thread is not in the driver */
void
linuxkpi_napi_synchronize(struct napi_struct *napi)
{
	while (bus.polling) {
		lx_pause_ms(1);
	}
}

void
linuxkpi_napi_disable(struct napi_struct *napi)
{
	bus.napi_on = false;
	linuxkpi_napi_synchronize(napi);
}

void
linuxkpi_napi_enable(struct napi_struct *napi)
{
	bus.napi_on = true;
}

/*
 * The thread that does what the copy engines' interrupts and NAPI would: once a millisecond while the driver has its
 * polling on, every engine is looked at (snoc.c's poll looks only at those the engines' summary register names, which
 * an interrupt would have been raised for: here every engine is, which is also what the driver does for the engines
 * it polls itself), then the data path's completions, again at once if there was more than a pass's worth.
 */
static void
looker(void *argument)
{
	bus.looks = true;
	while (bus.looking) {
		struct ath10k *ar = bus.ar;
		int done = 0;

		bus.polling = true;
		__asm__ volatile ("dmb ish" ::: "memory");
		if (ar != NULL && bus.napi_on && !test_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags)) {
			for (int id = 0; id < CE_COUNT; id++) {
				ath10k_ce_per_engine_service(ar, id);
			}
			done = ath10k_htt_txrx_compl_task(ar, 64);
			bus.polls++;
			bus.busy += done != 0;
		}
		bus.polling = false;
		if (done < 64) {
			lx_pause_ms(1);
		}
	}
	bus.looks = false;
}

/* ---- the entries ---------------------------------------------------------------------------------- */

static struct platform_device radio_device;
static struct ath10k *volatile driver;	/* the driver, while it is up: between lx_start() and lx_stop() */

static const struct resource radio_registers[] = {
	{ .start = 0x18800000, .end = 0x18800000 + 0x800000 - 1, .name = "membase", .flags = IORESOURCE_MEM },
};
static const int radio_irqs[CE_COUNT_MAX] = {
	LX_IRQ + 0, LX_IRQ + 1, LX_IRQ + 2, LX_IRQ + 3, LX_IRQ + 4, LX_IRQ + 5, LX_IRQ + 6, LX_IRQ + 7, LX_IRQ + 8, LX_IRQ + 9,
	LX_IRQ + 10, LX_IRQ + 11,
};

/* What the firmware has said of the radio, once the driver is registered. */
static void
say_radio(struct ath10k *ar)
{
	printf("lx: THE FIRMWARE SAYS OF THE RADIO: %s, firmware %s; %u chains (tx %#x, rx %#x); HT %#x, VHT %#x; regulatory "
	    "domain %#x; its address %02x:%02x:%02x:%02x:%02x:%02x; %u peers, %u stations\n", ar->hw_params.name,
	    ar->hw->wiphy->fw_version, ar->num_rf_chains, ar->cfg_tx_chainmask, ar->cfg_rx_chainmask, ar->ht_cap_info,
	    ar->vht_cap_info, ar->ath_common.regulatory.current_rd, ar->mac_addr[0], ar->mac_addr[1], ar->mac_addr[2],
	    ar->mac_addr[3], ar->mac_addr[4], ar->mac_addr[5], ar->max_num_peers, ar->max_num_stations);
}

int
lx_start(void)
{
	static struct device_node node;
	struct ath10k *ar;
	unsigned long long began;
	bool up;
	int ret;

	if (lx_prepare() != 0) {
		lx_tell("lx: no thread, or no memory the radio can be given\n");
		return (ENOMEM);
	}
	/* what the driver has to say while it is being looked into; not in a session that just uses the radio */
	ath10k_debug_mask = lx_os_option("wifi_quiet") ? 0 : ATH10K_DBG_BOOT | ATH10K_DBG_QMI | ATH10K_DBG_SNOC | ATH10K_DBG_MAC;
	/* the driver's parameter "frame_mode": Ethernet, which the firmware puts into 802.11 and ciphers itself */
	ath10k_frame_mode = ATH10K_HW_TXRX_ETHERNET;

	node.name = node.full_name = "wifi";
	node.properties = radio_properties;
	node.nproperties = ARRAY_SIZE(radio_properties);
	radio_device.name = "18800000.wifi";
	radio_device.dev.of_node = &node;
	radio_device.dev.kobj.name = "wifi";
	radio_device.resource = radio_registers;
	radio_device.num_resources = ARRAY_SIZE(radio_registers);
	radio_device.irqs = radio_irqs;
	radio_device.num_irqs = ARRAY_SIZE(radio_irqs);

	bus.looking = true;
	if (lx_os_thread("wifi-ce", looker, NULL) != 0) {
		bus.looking = false;
		return (EAGAIN);
	}
	printf("lx: the driver (ath10k, snoc) is probed on the radio at 0x18800000; its copy engines are looked at once a "
	    "millisecond\n");
	began = lx_os_nanoseconds();
	if ((ret = ath10k_snoc_probe(&radio_device)) != 0) {
		lx_tell("lx: the driver's probe fails: %d\n", ret);
		bus.looking = false;
		return (-ret);
	}
	ar = platform_get_drvdata(&radio_device);
	bus.ar = ar;

	/*
	 * The rest happens on the driver's own threads: its QMI client hears that the firmware is ready, the bus registers
	 * the core, and core.c's registration starts the firmware's mode, brings HTC and WMI up over the copy engines and
	 * registers the radio with what stands in for mac80211 (lx_80211.c). It went well if the core says it is registered.
	 */
	while (!test_bit(ATH10K_FLAG_CORE_REGISTERED, &ar->dev_flags) && lx_os_nanoseconds() - began < 60000000000ull) {
		lx_pause_ms(10);
	}
	up = test_bit(ATH10K_FLAG_CORE_REGISTERED, &ar->dev_flags);
	lx_tell("lx: %llu ms after its probe the driver %s; its flags: %#lx, the bus's %#lx; the copy engines looked at %llu "
	    "times, %llu with work\n", (lx_os_nanoseconds() - began) / 1000000ull, up ? "IS UP: the radio is registered" :
	    "HAS NOT REGISTERED THE RADIO", ar->dev_flags, ath10k_snoc_priv(ar)->flags, bus.polls, bus.busy);
	if (!up) {
		ath10k_snoc_remove(&radio_device);
		bus.ar = NULL;
		bus.looking = false;
		return (ETIMEDOUT);
	}
	say_radio(ar);

	driver = ar;
	if (lx_os_option("wifi_station")) {
		/* whoever wants a network asks the kernel, and the kernel asks the station: the driver stays up until lx_stop() */
		if ((ret = lx_station_offer()) == 0) {
			return (0);
		}
	} else if (lx_os_option("wifi_join")) {
		if ((ret = lx_station_join()) == 0) {
			return (0);
		}
	} else {
		ret = lx_os_option("wifi_scan") ? lx_station_scan() : 0;
	}
	lx_stop();
	return (ret);
}

/* The thread that counts the ticks, and the memory the radio is shown: once. 0, or an errno. */
int
lx_prepare(void)
{
	return (lx_base_init() != 0 || lx_pool_init() != 0 ? ENOMEM : 0);
}

/* The network left, and the driver down as its own removal does it. */
void
lx_stop(void)
{
	struct ath10k *ar = driver;

	if (ar == NULL) {
		return;
	}
	driver = NULL;
	lx_station_withdraw();
	lx_station_leave();
	ath10k_snoc_remove(&radio_device);
	bus.ar = NULL;
	bus.looking = false;
	while (bus.looks) {
		lx_pause_ms(1);
	}
	lx_tell("lx: the driver is down again\n");
}

int
lx_transmit(const unsigned char *frame, unsigned long length)
{
	return (driver != NULL ? lx_station_send(frame, length) : ENETDOWN);
}
