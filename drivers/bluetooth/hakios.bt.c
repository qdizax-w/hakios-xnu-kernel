/*
 * Bluetooth of the POCO X5 Pro (redwood): STEP ONE, a look. Only with boot-arg bt=1, and it only READS.
 *
 * The phone's radio chip is a Qualcomm WCN6750, one chip for Wi-Fi and Bluetooth (the phone's device tree: the node
 * bt_wcn6750, compatible qcom,wcn6750-bt; the Wi-Fi half is drivers/wifi/hakios.wifi.c). What the device tree says
 * about the Bluetooth half, and what this file takes from it (numbers, not text):
 *
 *   qcom,bt-reset-gpio     GPIO 85    the Bluetooth half's reset / enable line
 *   qcom,bt-sw-ctrl-gpio   GPIO 86    the line that tells the radio's switch which half has the antenna
 *   qcom,wl-reset-gpio     GPIO 84    the Wi-Fi half's reset line, listed here too: the chip comes up as a whole
 *   the supplies (qcom,bt-vdd-*-supply, each with a -config of minimum and maximum microvolts; read out of the
 *   device tree by script, not by eye), the same for both halves:
 *       io 1800000..2000000    aon, dig, rfacmn, rfa-0p8 824000..1120000    rfa1 1872000..2040000
 *       rfa2 1256000..1500000  ipa-2p2 2200000..2210000                     asd 2800000..3544000
 *     (which regulator each supply is, is a phandle in the tree; nothing here writes any of them)
 *
 * What this does. At boot-arg bt=1 it reads the three pins' configuration and level out of the TLMM (the same
 * registers the haptic driver reads for its pins) and prints them, once at the start and once bt_again seconds later
 * (default 30), to see whether the Wi-Fi bring-up (wifi=1) moved the reset lines in between. It writes NOTHING: no
 * pin, no supply, no clock, no serial engine. Reading a pin's configuration is a plain load from the TLMM, which the
 * haptic and thermal drivers do on this phone without trouble.
 *
 * What is NOT here, and what it needs. Bluetooth on this chip, as Android and Linux (hci_qca, btqca) do it:
 *   1. the supplies on (they are shared with Wi-Fi: wifi=1 already raises what the Wi-Fi half needs),
 *   2. the reset line high, and the chip given a moment,
 *   3. the UART of the Bluetooth half opened at 115200 baud (a Qualcomm serial engine, "hsuart0" in the device tree:
 *      its clocks and its pins have to be set up first, and an access to an engine whose clock is off hangs the bus),
 *   4. the chip's own firmware loaded over that UART with Qualcomm's vendor commands (a rampatch and an NVM file out of
 *      /vendor/firmware on the phone: NOT in the repository, and not pulled by devices/redwood/pull-firmware.sh yet; Linux
 *      names them qca/msbtfw*.mbn and qca/msnv*.bin for the WCN6750, as I remember it, which is NOT verified here),
 *   5. the baud rate raised, then H4 packets (HCI) in both directions.
 * None of 1 to 5 is written, because none of it could be checked: the phone was not on the desk when this was written, and
 * the register numbers and command bytes for a chip are not the kind of thing to write from memory and ship. Each step comes
 * with a boot-arg level the way haptic=N and wifi_step=N did: the next one is bt=2 (supplies and reset line, seen on the
 * pins), after this one has been run and its output compared with a phone that is running Android.
 *
 * Status: COMPILED ONLY. Not run in QEMU (it has no such TLMM, and bt=1 must not be given there: a load from an address it
 * does not map faults) and not run on the phone.
 *
 * Boot-args:
 *   bt=1           the look
 *   bt_again=<s>   the second look this many seconds later (0: no second look; 30)
 */
#include <stdint.h>
#include <kern/clock.h>
#include <kern/startup.h>
#include <kern/thread_call.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void loader_log_putc(char c);

static int say(const char *format, ...) __printflike(1, 2);

static int
say(const char *format, ...)
{
	char line[200];
	va_list list;

	va_start(list, format);
	vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	for (const char *c = line; *c != 0; c++) {
		loader_log_putc(*c);
	}
	return printf("%s", line);
}

#define TLMM_PHYS               0x0f100000UL
#define TLMM_SIZE               0x100000UL
#define GPIO_CFG(n)             (tlmm + 0x1000UL * (n))         /* [1:0] pull, [5:2] function, [8:6] drive, [9] output */
#define GPIO_IO(n)              (tlmm + 0x1000UL * (n) + 4)     /* bit 0 in, bit 1 out */
#define CFG_OUTPUT              (1u << 9)

static vm_offset_t tlmm;
static uint32_t again_seconds = 30;
static thread_call_t again_call;

/* The three pins of the device tree's bt_wcn6750 node. */
static const struct {
	unsigned pin;
	const char *what;
} pins[] = {
	{ 84, "qcom,wl-reset-gpio: the Wi-Fi half's reset" },
	{ 85, "qcom,bt-reset-gpio: the Bluetooth half's reset" },
	{ 86, "qcom,bt-sw-ctrl-gpio: the antenna switch" },
};

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

/* One look at the pins: what the TLMM says each is set to, and the level it reads. Nothing is written. */
static void
look(const char *when)
{
	for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		uint32_t cfg = rd(GPIO_CFG(pins[i].pin)), io = rd(GPIO_IO(pins[i].pin));

		say("bt: %s: gpio%u (%s): function %u, pull %u, %u mA, %s, reads %u\n", when, pins[i].pin, pins[i].what, (cfg >> 2) & 15,
		    cfg & 3, (((cfg >> 6) & 7) + 1) * 2, (cfg & CFG_OUTPUT) ? (io & 2 ? "drives high" : "drives low") : "input", io & 1);
	}
}

/*
 * ---- miatoll (Redmi Note 9S, SM7125): the WCN3990's Bluetooth, STEP ONE, a look (bt=1), reading only -----------------
 *
 * The Bluetooth half of this phone's WCN3990 is on a UART: serial engine 3 of QUP 0 (the vendor's tree: qcom,qup_uart@88c000,
 * "qcom,msm-geni-serial-hs"; mainline sc7180.dtsi: uart3, serial@88c000, SPI 604 level), pins GPIO 38 CTS, 39 RTS, 40 TX,
 * 41 RX in the TLMM's south tile, function 1 ("qup03": pinctrl-sc7180.c). Its clocks are the GCC's (gcc-sc7180.c), all
 * switched by votes in 0x52008: QUP 0's m-ahb (bit 6, status 0x17004), s-ahb (7, 0x17008), core (8, 0x1700c), core 2x
 * (9, 0x17014) and the engine's own (13, 0x173c0; its rate's RCG at 0x173c4). Its supplies (the tree's bt_wcn3990):
 * vdd-io L10A 1.8 V, vdd-core L2C 1.3 V, vdd-pa L10C 3.0-3.3 V, vdd-xtal L1C 1.8 V -- the last three already voted for by
 * the Wi-Fi (hakios.wifi.c, miatoll_rails_on()).
 *
 * What this does: the GCC's votes and each clock's status bit, the RCG's settings, the four pins, and -- only if both of
 * QUP 0's AHB clocks are running, since a read of a serial engine whose bus clock is off hangs the bus -- the engine's
 * firmware revision (which protocol the boot chain loaded into it: 2 is UART), status, I/O and clock settings, and the
 * wrapper's version. Nothing is written. Once at start, once bt_again seconds later (after the Wi-Fi's votes).
 */
#define MIATOLL_SOC_BASE        0x00100000ul    /* where the loader says miatoll's peripherals begin */
#define M_GCC_PHYS              0x00100000UL
#define M_GCC_SIZE              0x60000UL
#define M_GCC_VOTE              0x52008
#define M_CLK_OFF               (1u << 31)      /* a branch's status register: the clock is not running */
#define M_SE3_RCG               0x173c4         /* CMD; CFG +4, M +8, N +0xc, D +0x10 */
#define M_TLMM_SOUTH_PHYS       0x03d00000UL
#define M_PIN_FIRST             38
#define M_PIN_LAST              41
#define M_SE3_PHYS              0x0088c000UL
#define M_SE_SIZE               0x4000UL
#define M_QUP0_PHYS             0x008c0000UL
#define M_QUP0_SIZE             0x1000UL

extern vm_offset_t pe_arm_get_soc_base_phys(void);

static vm_offset_t m_gcc, m_pins, m_se3, m_qup0;

static const struct {
	uint32_t status;
	unsigned vote;
	const char *name;
} m_clocks[] = {
	{ 0x17004, 6, "QUP 0 m-ahb" }, { 0x17008, 7, "QUP 0 s-ahb" }, { 0x1700c, 8, "QUP 0 core" }, { 0x17014, 9, "QUP 0 core 2x" },
	{ 0x173c0, 13, "QUP 0 serial engine 3" },
};
static const char *const m_pin_names[] = { "CTS", "RTS", "TX", "RX" };

static void
look_miatoll(const char *when)
{
	uint32_t vote = rd(m_gcc + M_GCC_VOTE);
	bool ahb = true;

	say("bt: %s: GCC votes (0x52008) %#x\n", when, vote);
	for (unsigned i = 0; i < sizeof(m_clocks) / sizeof(m_clocks[0]); i++) {
		uint32_t status = rd(m_gcc + m_clocks[i].status);

		say("bt: %s: %s: voted %u, status %#x (%s)\n", when, m_clocks[i].name, (vote >> m_clocks[i].vote) & 1, status,
		    (status & M_CLK_OFF) ? "OFF" : "running");
		if (i < 2 && (status & M_CLK_OFF)) {
			ahb = false;
		}
	}
	say("bt: %s: its rate (RCG 0x173c4): cmd %#x, cfg %#x, m %#x, n %#x, d %#x\n", when, rd(m_gcc + M_SE3_RCG),
	    rd(m_gcc + M_SE3_RCG + 4), rd(m_gcc + M_SE3_RCG + 8), rd(m_gcc + M_SE3_RCG + 0xc), rd(m_gcc + M_SE3_RCG + 0x10));
	for (unsigned pin = M_PIN_FIRST; pin <= M_PIN_LAST; pin++) {
		vm_offset_t regs = m_pins + 0x1000UL * (pin - M_PIN_FIRST);
		uint32_t cfg = rd(regs), io = rd(regs + 4);

		say("bt: %s: gpio%u (%s): function %u (1: the UART's), pull %u, %u mA, %s, reads %u\n", when, pin,
		    m_pin_names[pin - M_PIN_FIRST], (cfg >> 2) & 15, cfg & 3, (((cfg >> 6) & 7) + 1) * 2,
		    (cfg & CFG_OUTPUT) ? (io & 2 ? "drives high" : "drives low") : "input", io & 1);
	}
	if (!ahb) {
		say("bt: %s: the serial engine's registers are NOT read: QUP 0's bus clocks are off (a read would hang the bus)\n",
		    when);
		return;
	}
	if (m_se3 == 0) {
		m_se3 = ml_io_map(M_SE3_PHYS, M_SE_SIZE);
		m_qup0 = ml_io_map(M_QUP0_PHYS, M_QUP0_SIZE);
	}
	if (m_se3 == 0 || m_qup0 == 0) {
		say("bt: %s: could not map the serial engine\n", when);
		return;
	}
	{
		uint32_t fw = rd(m_se3 + 0x68);

		say("bt: %s: QUP 0 version %#x; engine 3: firmware %#x (protocol %u: 0 none, 1 SPI, 2 UART, 3 I2C), status %#x, "
		    "I/O disabled %#x, clock %#x, hardware %#x\n", when, rd(m_qup0 + 0x4), fw, (fw >> 8) & 0xff, rd(m_se3 + 0x40),
		    rd(m_se3 + 0x64), rd(m_se3 + 0x48), rd(m_se3 + 0xe24));
	}
}

static void
look_again(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	if (m_gcc != 0) {
		look_miatoll("again");
		return;
	}
	look("again");
}

__startup_func
static void
bt_init(void)
{
	uint32_t enabled = 0, value;
	uint64_t deadline;

	if (!PE_parse_boot_argn("bt", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	if (PE_parse_boot_argn("bt_again", &value, sizeof(value)) && value <= 3600) {
		again_seconds = value;
	}
	if (pe_arm_get_soc_base_phys() == MIATOLL_SOC_BASE) {
		m_gcc = ml_io_map(M_GCC_PHYS, M_GCC_SIZE);
		m_pins = ml_io_map(M_TLMM_SOUTH_PHYS + 0x1000UL * M_PIN_FIRST, 0x1000UL * (M_PIN_LAST - M_PIN_FIRST + 1));
		if (m_gcc == 0 || m_pins == 0) {
			say("bt: could not map the GCC or the TLMM's south tile; off\n");
			m_gcc = 0;
			return;
		}
		say("bt: level 1: a look, reading only (miatoll's WCN3990: its UART, QUP 0 engine 3; nothing is written)\n");
		look_miatoll("at start");
		if (again_seconds != 0) {
			again_call = thread_call_allocate(look_again, NULL);
			clock_interval_to_deadline(again_seconds, NSEC_PER_SEC, &deadline);
			thread_call_enter_delayed(again_call, deadline);
		}
		return;
	}
	tlmm = ml_io_map(TLMM_PHYS, TLMM_SIZE);
	if (tlmm == 0) {
		say("bt: could not map the TLMM; off\n");
		return;
	}
	say("bt: level 1: a look, reading only (WCN6750; nothing is written)\n");
	look("at start");
	if (again_seconds != 0) {
		again_call = thread_call_allocate(look_again, NULL);
		clock_interval_to_deadline(again_seconds, NSEC_PER_SEC, &deadline);
		thread_call_enter_delayed(again_call, deadline);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, bt_init);
