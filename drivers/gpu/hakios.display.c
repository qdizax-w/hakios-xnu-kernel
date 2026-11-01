/*
 * Where the panel's scan is, on the POCO X5 Pro (redwood): for iokit/Kernel/MiniIOMobileFramebuffer.cpp, which
 * puts every frame into the memory the panel is sent from and, without this, can only guess when the panel
 * reads it -- which is why the picture tore and why frames came at a pace of their own, beside the panel's.
 *
 * The panel is a DSI video panel, sent the framebuffer over and over by the display controller's timing engine
 * at the rate of its timing registers, which nobody touches after the firmware: 2432 lines a frame of which
 * 2400 are shown, 119.93 frames a second (seen: hakios.adreno.c, display_probe). The interface the panel hangs
 * on (INTF 1, dt: mdss_dsi0's mdp_intf_base) counts the frames and, within a frame, the line being sent; this
 * reads those two counters. To read them at all two bus clocks have to run, which the firmware leaves off: the
 * one of the display's clock controller (in GCC) and, in that controller, the one of the display block's
 * registers. They are turned on here, as Linux keeps them on and as display_probe did on this phone; that is all
 * that is ever written. The timing itself is only read.
 *
 * Boot-arg fb_scan=1 (on the phone; QEMU has no such block): on. Then the framebuffer driver
 *   - starts copying a frame into the panel's memory only where the scan lets the copy land whole in one
 *     refresh (hakios_display_window(): see MiniIOMobileFramebuffer.cpp, show), so that no refresh shows
 *     part of one frame and part of the next -- no tearing;
 *   - runs at the panel's own rate (120 Hz) and tells CoreAnimation that rate, and its refresh notifications
 *     come when the panel's frames begin, not on a clock of their own that drifts against them.
 *
 * Registers: Linux drivers/gpu/drm/msm/disp/dpu1/dpu_hw_intf.c (INTF), drivers/clk/qcom/dispcc-sc7280.c.
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.display.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define GCC_PHYS                0x00100000UL
#define DISPCC_PHYS             0x0af00000UL    /* dt: clock-controller@af00000 */
/*
 * Which phone: redwood's (SC7280: gcc-sc7280.c, dispcc-sc7280.c, mdss_dsi0's mdp_intf_base) or
 * miatoll's (SM7125, "atoll": gcc-atoll.c gcc_disp_ahb_clk, dispcc-atoll.c disp_cc_mdss_ahb_clk,
 * dt: qcom,gdsc@af03000 mdss_core_gdsc, qcom,mdss_mdp@ae00000 with qcom,sde-intf-off <0x6b000
 * 0x6b800>: INTF 1, the DSI panel's, at 0x6b800). The loader says which by where the peripherals begin.
 */
#define MIATOLL_SOC_BASE        0x00100000ul
extern vm_offset_t pe_arm_get_soc_base_phys(void);
static uint32_t GCC_DISP_AHB_CBCR = 0x27004, DISPCC_MDSS_CORE_GDSCR = 0x1004, DISPCC_MDSS_AHB_CBCR = 0x1050;
static vm_offset_t INTF1_PHYS = 0x0ae36000UL;
#define CBCR_ENABLE             (1u << 0)
#define CBCR_OFF                (1u << 31)
#define GDSC_POWER_ON           (1u << 31)

#define INTF_TIMING_ENGINE_EN   0x000
#define INTF_HSYNC_CTL          0x008           /* clocks a line << 16 | of which the pulse */
#define INTF_VSYNC_PERIOD       0x00c           /* clocks a frame */
#define INTF_DISPLAY_V_START    0x01c
#define INTF_DISPLAY_V_END      0x024
#define INTF_FRAME_COUNT        0x0ac
#define INTF_LINE_COUNT         0x0b0

static vm_offset_t intf;
static struct hakios_display_scan scan;

static inline uint32_t
rd(vm_offset_t base, uint32_t offset)
{
	return *(volatile uint32_t *)(base + offset);
}

static inline void
wr(vm_offset_t base, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(base + offset) = value;
}

/* one bus clock on (and left on); 1 if it runs */
static int
clock_on(vm_offset_t base, uint32_t cbcr, const char *name)
{
	uint32_t v = rd(base, cbcr);

	if (!(v & CBCR_ENABLE)) {
		wr(base, cbcr, v | CBCR_ENABLE);
		delay_for_interval(10, NSEC_PER_MSEC);
		v = rd(base, cbcr);
	}
	if (!(v & CBCR_ENABLE)) {
		printf("display: the %s clock does not take (%#x)\n", name, v);
		return 0;
	}
	return 1;
}

uint32_t
hakios_display_line(uint32_t *frame)
{
	uint32_t f, line;

	if (intf == 0) {
		return 0;
	}
	/* the two counters together: a frame that begins between the two reads is read again */
	do {
		f = rd(intf, INTF_FRAME_COUNT);
		line = rd(intf, INTF_LINE_COUNT);
	} while (rd(intf, INTF_FRAME_COUNT) != f);
	if (frame != NULL) {
		*frame = f;
	}
	return line;
}

const struct hakios_display_scan *
hakios_display_scan(void)
{
	return scan.ok ? &scan : NULL;
}

/*
 * Wait until the scan is in [first, last) of a frame (lines, 0 = the first line shown), at most `limit_ns`;
 * the wait is a sleep for most of the way and a short spin for the end. 1 when it got there.
 */
int
hakios_display_wait_lines(uint32_t first, uint32_t last, uint64_t limit_ns)
{
	uint64_t start = mach_absolute_time(), now, waited;

	if (!scan.ok || first >= last || last > scan.lines) {
		return 0;
	}
	for (;;) {
		uint32_t line = hakios_display_line(NULL);
		uint64_t ahead_ns;

		if (line >= first && line < last) {
			return 1;
		}
		now = mach_absolute_time();
		absolutetime_to_nanoseconds(now - start, &waited);
		if (waited >= limit_ns) {
			return 0;
		}
		/* lines to go to `first`, as time: sleep all but the last 200 us of it */
		ahead_ns = (uint64_t)((first + scan.lines - line) % scan.lines) * scan.line_ns;
		if (ahead_ns > 400000) {
			uint64_t nap = ahead_ns - 200000;

			if (waited + nap > limit_ns) {
				nap = limit_ns - waited;
			}
			delay_for_interval((uint32_t)(nap / 1000), NSEC_PER_USEC);
		}
	}
}

/* nanoseconds until the next frame begins (its first line shown), from where the scan is now */
uint64_t
hakios_display_next_frame_ns(void)
{
	uint32_t line;

	if (!scan.ok) {
		return 0;
	}
	line = hakios_display_line(NULL);
	return (uint64_t)(scan.lines - line) * scan.line_ns;
}

/*
 * The display block's registers readable and writable: the bus clock of the display's clock controller and,
 * in it, that of the display block's registers (both left on), with the block powered by the firmware.
 * Once; 1 when they are.
 */
static int
display_bus_on(void)
{
	static int done, ok;
	vm_offset_t gcc, dispcc;

	if (done) {
		return ok;
	}
	done = 1;
	if (pe_arm_get_soc_base_phys() == MIATOLL_SOC_BASE) {
		GCC_DISP_AHB_CBCR = 0xb00c;
		DISPCC_MDSS_CORE_GDSCR = 0x3000;
		DISPCC_MDSS_AHB_CBCR = 0x2080;
		INTF1_PHYS = 0x0ae6b800UL;
	}
	gcc = ml_io_map(GCC_PHYS + (GCC_DISP_AHB_CBCR & ~0xfffu), 0x1000);
	dispcc = ml_io_map(DISPCC_PHYS, 0x4000);
	if (!clock_on(gcc, GCC_DISP_AHB_CBCR & 0xfffu, "display clock controller's bus")) {
		return 0;
	}
	if (!(rd(dispcc, DISPCC_MDSS_CORE_GDSCR) & GDSC_POWER_ON)) {
		printf("display: the display block has no power (the firmware did not leave the panel on?): its registers cannot be used\n");
		return 0;
	}
	if (!clock_on(dispcc, DISPCC_MDSS_AHB_CBCR, "display registers' bus")) {
		return 0;
	}
	ok = 1;
	return 1;
}

int
hakios_display_scan_start(void)
{
	uint32_t on = 0, hsync, period, v0, v1, f0, f1, l0, l1;
	uint64_t t0, t1, ns, gone;

	if (scan.ok) {
		return 1;
	}
	if (!PE_parse_boot_argn("fb_scan", &on, sizeof(on)) || on != 1) {
		return 0;
	}
	if (!display_bus_on()) {
		printf("display: no scan to go by\n");
		return 0;
	}
	intf = ml_io_map(INTF1_PHYS & ~(vm_offset_t)0xfff, 0x1000) + (INTF1_PHYS & 0xfff);   /* (miatoll's is at +0x800 of its page) */
	hsync = rd(intf, INTF_HSYNC_CTL) >> 16;
	period = rd(intf, INTF_VSYNC_PERIOD);
	v0 = rd(intf, INTF_DISPLAY_V_START);
	v1 = rd(intf, INTF_DISPLAY_V_END);
	if (hsync == 0 || period < hsync || v1 <= v0) {
		printf("display: the timing engine's registers make no sense (line %u, frame %u clocks): no scan to go by\n", hsync, period);
		intf = 0;
		return 0;
	}
	scan.lines = period / hsync;
	scan.shown = (v1 - v0 + 1) / hsync;
	/* the rate the panel is really sent at: the counters 100 ms apart */
	l0 = hakios_display_line(&f0);
	t0 = mach_absolute_time();
	delay_for_interval(100, NSEC_PER_MSEC);
	l1 = hakios_display_line(&f1);
	t1 = mach_absolute_time();
	absolutetime_to_nanoseconds(t1 - t0, &ns);
	gone = (uint64_t)(f1 - f0) * scan.lines + l1 - l0;
	if (f1 == f0 || gone == 0 || ns == 0 || scan.lines < scan.shown || scan.shown == 0) {
		printf("display: the scan counters do not move (frame %u -> %u, line %u -> %u): no scan to go by\n", f0, f1, l0, l1);
		intf = 0;
		return 0;
	}
	scan.line_ns = ns / gone;
	scan.frame_ns = scan.line_ns * scan.lines;
	scan.millihz = (uint32_t)(1000000000000ULL / scan.frame_ns);
	scan.ok = 1;
	printf("display: the panel's scan: %u lines a frame (%u shown), %u.%03u frames a second, %llu ns a line; "
	    "frames follow it (fb_scan=1)\n", scan.lines, scan.shown, scan.millihz / 1000, scan.millihz % 1000, scan.line_ns);
	return 1;
}

/*
 * Double buffering on miatoll (boot-arg fb_flip, 1 by default; 0: off): the framebuffer driver draws every
 * whole-screen frame into a buffer the panel is not being sent from, and then has the display controller
 * fetch from that buffer instead -- a page flip, which the hardware makes at the start of a frame. However
 * long the drawing takes, no refresh shows part of one frame and part of the next (without it the frames
 * were copied into the very memory being sent, racing the scan, and a slow one -- an app opening, a swipe
 * -- was cut where the scan caught it up).
 *
 * The firmware (XBL's splash, then UEFI's GOP) leaves the display controller (SDE, qcom,mdss_mdp@ae00000)
 * in video mode: one source pipe fetching the boot framebuffer at 0x9c000000, through a mixer, out of
 * INTF 1 to the DSI panel. That framebuffer is the start of cont_splash_region (0x9c000000, 0x1700000
 * bytes: "Display Reserved" in miatollPkg's memory map, never the kernel's RAM), and a second frame of
 * 1080 x 2400 fits in it right after the first. A flip writes the pipe's source address and that pipe's
 * bit into the CTL's flush register; the controller latches the address at the next frame start and
 * then clears the bit (what Linux's dpu driver waits for in video mode: dpu_encoder_phys_vid.c).
 *
 * Nothing is assumed beyond the device tree's layout: the pipe is the one whose address is the boot
 * framebuffer's, the CTL the one driving INTF 1, and the first flip is a test -- not taken by the
 * hardware within a few frames, everything is put back and frames go on as before (fb_scan).
 * Registers: Linux drivers/gpu/drm/msm/disp/dpu1/dpu_hw_sspp.c, dpu_hw_ctl.c; dt: qcom,sde-sspp-off
 * <0x5000 0x25000 0x27000 0x29000> (VIG0, DMA0..2), qcom,sde-ctl-off <0x2000 0x2200 0x2400>.
 */
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_vtophys(vm_offset_t vaddr);

#define MDP_PHYS                0x0ae00000UL
#define SPLASH_PHYS             0x9c000000UL    /* dt: cont_splash_region@9c000000 */
#define SPLASH_SIZE             0x01700000UL
#define SSPP_SRC_SIZE           0x000
#define SSPP_SRC0_ADDR          0x014
#define SSPP_SRC_YSTRIDE0       0x024
#define CTL_TOP                 0x014
#define CTL_FLUSH               0x018
#define CTL_INTF_ACTIVE         0x0f4
#define CTL_INTF_FLUSH          0x110
#define CTL_FLUSH_INTF          (1u << 31)
#define INTF_1_BIT              (1u << 1)

static const struct {
	uint32_t offset, flush;
	const char *name;
} miatoll_pipes[] = {
	{ 0x05000, 1u << 0, "VIG0" }, { 0x25000, 1u << 11, "DMA0" }, { 0x27000, 1u << 12, "DMA1" }, { 0x29000, 1u << 24, "DMA2" },
};
static const uint32_t miatoll_ctls[] = { 0x2000, 0x2200, 0x2400 };

static struct {
	int ok;
	vm_offset_t pipe, ctl;
	uint32_t flush;                 /* the pipe's bit in CTL_FLUSH */
	int with_intf;                  /* the hardware wanted the interface flushed along (CTL_INTF_FLUSH) */
	uint64_t pa[2];
	uint8_t *va[2];
	int front;                      /* the buffer the pipe fetches from, or will from the next frame on */
	uint32_t late;                  /* flips not taken in time */
} flip;

static int
flip_pending(void)
{
	return (rd(flip.ctl, CTL_FLUSH) & flip.flush) != 0;
}

/* until the last flip has been taken (its flush bit cleared), at most limit_ns; 1 when it has */
static int
flip_wait(uint64_t limit_ns)
{
	uint64_t start = mach_absolute_time(), waited;

	while (flip_pending()) {
		absolutetime_to_nanoseconds(mach_absolute_time() - start, &waited);
		if (waited >= limit_ns) {
			return 0;
		}
		delay_for_interval(250, NSEC_PER_USEC);
	}
	return 1;
}

/* the pipe to fetch from buffer `which` from the next frame on */
static void
flip_to(int which)
{
	__builtin_arm_dsb(0xf);         /* the frame's writes (write-combined) in memory before the controller reads it */
	wr(flip.pipe, SSPP_SRC0_ADDR, (uint32_t)flip.pa[which]);
	if (flip.with_intf) {
		wr(flip.ctl, CTL_INTF_FLUSH, INTF_1_BIT);
		wr(flip.ctl, CTL_FLUSH, flip.flush | CTL_FLUSH_INTF);
	} else {
		wr(flip.ctl, CTL_FLUSH, flip.flush);
	}
	__builtin_arm_dsb(0xf);
	flip.front = which;
}

int
hakios_display_flip_start(uint8_t *base, uint32_t row_bytes, uint32_t height, uint8_t **front)
{
	uint32_t on = 1, stride = 0, i;
	uint64_t pa, size;
	vm_offset_t ctl = 0;
	int ctls = 0;
	const char *pipe_name = NULL;

	if (flip.ok) {
		*front = flip.va[flip.front];
		return 1;
	}
	if (pe_arm_get_soc_base_phys() != MIATOLL_SOC_BASE) {
		return 0;
	}
	if (PE_parse_boot_argn("fb_flip", &on, sizeof(on)) && on == 0) {
		printf("display: no double buffering (fb_flip=0)\n");
		return 0;
	}
	if (!display_bus_on()) {
		printf("display: no double buffering: the display block's registers cannot be used\n");
		return 0;
	}
	pa = ml_vtophys((vm_offset_t)base);
	size = ((uint64_t)row_bytes * height + 0xfffu) & ~(uint64_t)0xfff;
	if (pa == 0 || (pa & 0xfffu) != 0 || pa < SPLASH_PHYS || pa + 2 * size > SPLASH_PHYS + SPLASH_SIZE) {
		printf("display: no double buffering: the framebuffer (%#llx, 2 x %llu bytes) is not inside the splash region "
		    "(%#lx, %#lx bytes)\n", pa, size, SPLASH_PHYS, SPLASH_SIZE);
		return 0;
	}
	/* the pipe fetching the boot framebuffer */
	for (i = 0; i < sizeof(miatoll_pipes) / sizeof(miatoll_pipes[0]); i++) {
		vm_offset_t p = ml_io_map(MDP_PHYS + miatoll_pipes[i].offset, 0x1000);
		uint32_t addr = rd(p, SSPP_SRC0_ADDR);

		if (addr == (uint32_t)pa && !flip.pipe) {
			flip.pipe = p;
			flip.flush = miatoll_pipes[i].flush;
			pipe_name = miatoll_pipes[i].name;
			stride = rd(p, SSPP_SRC_YSTRIDE0) & 0xffff;
		}
	}
	if (!flip.pipe) {
		printf("display: no double buffering: no pipe fetches the framebuffer at %#llx\n", pa);
		return 0;
	}
	if (stride != row_bytes) {
		printf("display: no double buffering: pipe %s's rows are %u bytes, the framebuffer's %u\n", pipe_name, stride, row_bytes);
		flip.pipe = 0;
		return 0;
	}
	/* the CTL driving the panel's interface (INTF 1); without that register, the only one in use */
	for (i = 0; i < sizeof(miatoll_ctls) / sizeof(miatoll_ctls[0]); i++) {
		vm_offset_t c = ml_io_map(MDP_PHYS + miatoll_ctls[i], 0x1000) + (miatoll_ctls[i] & 0xfffu);

		if (rd(c, CTL_INTF_ACTIVE) & INTF_1_BIT) {
			ctl = c;
			ctls = 1;
			break;
		}
		if (rd(c, CTL_TOP) != 0) {
			ctl = ctls == 0 ? c : ctl;
			ctls++;
		}
	}
	if (ctls != 1) {
		printf("display: no double buffering: %d CTLs look in use, not 1\n", ctls);
		flip.pipe = 0;
		return 0;
	}
	flip.ctl = ctl;
	flip.pa[0] = pa;
	flip.pa[1] = pa + size;
	flip.va[0] = (uint8_t *)ml_io_map_wcomb((vm_offset_t)pa, (vm_size_t)size);
	flip.va[1] = (uint8_t *)ml_io_map_wcomb((vm_offset_t)(pa + size), (vm_size_t)size);
	if (flip.va[0] == NULL || flip.va[1] == NULL) {
		printf("display: no double buffering: the two buffers cannot be mapped\n");
		flip.pipe = 0;
		return 0;
	}
	/*
	 * The test: the second buffer made a copy of what is shown, then flipped to. Taken within 100 ms (6
	 * frames at 60 Hz), it works; if not, once more with the interface flushed along; then given up.
	 */
	memcpy(flip.va[1], flip.va[0], (size_t)size);
	if (!flip_wait(100 * NSEC_PER_MSEC)) {
		printf("display: no double buffering: pipe %s has a flush pending that never ends (CTL_FLUSH %#x)\n", pipe_name,
		    rd(flip.ctl, CTL_FLUSH));
		flip.pipe = 0;
		return 0;
	}
	flip_to(1);
	if (!flip_wait(100 * NSEC_PER_MSEC)) {
		flip.with_intf = 1;
		flip_to(1);
		if (!flip_wait(100 * NSEC_PER_MSEC)) {
			printf("display: no double buffering: the controller does not take a new address for pipe %s "
			    "(CTL_FLUSH %#x); the framebuffer stays as it was\n", pipe_name, rd(flip.ctl, CTL_FLUSH));
			flip.with_intf = 0;
			wr(flip.pipe, SSPP_SRC0_ADDR, (uint32_t)pa);
			flip.pipe = 0;
			return 0;
		}
	}
	flip.ok = 1;
	printf("display: double buffering: pipe %s (flush bit %#x%s) flips between %#llx and %#llx at the frame start\n",
	    pipe_name, flip.flush, flip.with_intf ? ", with the interface's" : "", flip.pa[0], flip.pa[1]);
	*front = flip.va[1];
	return 1;
}

/*
 * The buffer to draw the next frame into: the one not being sent, once the last flip has been taken (at
 * most limit_ns: a panel that is off takes none, and its frames go nowhere anyway). NULL without flips.
 */
uint8_t *
hakios_display_flip_back(uint64_t limit_ns)
{
	if (!flip.ok) {
		return NULL;
	}
	if (!flip_wait(limit_ns) && flip.late++ % 64 == 0) {
		printf("display: a flip was not taken within %llu us (%u so far)\n", limit_ns / 1000, flip.late);
	}
	return flip.va[flip.front ^ 1];
}

/* the buffer being sent (or about to be): where a frame drawn in place goes. NULL without flips. */
uint8_t *
hakios_display_flip_front(void)
{
	return flip.ok ? flip.va[flip.front] : NULL;
}

/* the frame drawn into hakios_display_flip_back()'s buffer: sent from the next frame start on */
void
hakios_display_flip_show(void)
{
	if (flip.ok) {
		flip_to(flip.front ^ 1);
	}
}
