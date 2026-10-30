/*
 * What the drivers share, kept apart from any one of them, so that each can be left out of the kernel on its own
 * (drivers/<subsystem>/conf, CONFIG_HAKIOS_*: docs/drivers-config.md).
 *
 *   the locks      of the hardware several drivers take turns at: the PMIC arbiter (the buttons, the flashlight, the
 *                  camera's regulators, the PMIC survey), the always-on processor's mailbox, the shared memory's
 *                  allocation and RPMh's sequencer (the Wi-Fi, audio DSP and modem bring-ups, the touch screen's
 *                  supply), and the SMMU's free match registers (Wi-Fi, sound, camera, modem). They were in
 *                  hakios.buttons.c and hakios.wifi.c, which the others then needed in the kernel to link.
 *   kern.hakios    the sysctl node the drivers hang theirs under (it was hakios.battery.c's)
 *   the list       of the drivers this kernel was built with: sysctl kern.hakios.drivers, and a line in the boot log,
 *                  so that a run says what was in it (hakios.drivers.h is made by tools/hakios-config.py kernel).
 */
#include <kern/locks.h>
#include <kern/startup.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include "hakios.drivers.h"

/* kern.hakios: the node every driver of this port hangs its sysctls under (it was hakios.battery.c's) */
SYSCTL_NODE(_kern, OID_AUTO, hakios, CTLFLAG_RW | CTLFLAG_LOCKED, 0, "redwood phone bits");

static LCK_GRP_DECLARE(hakios_platform_lock_group, "hakios-platform");

/*
 * One transfer at a time through the PMIC arbiter for this system, whoever asks: with two at once the arbiter drops
 * one ("the transfer was dropped"), as Linux never lets happen (one lock for the bus). A spin lock: it is taken
 * around a transfer of some tens of microseconds.
 */
static LCK_SPIN_DECLARE(spmi_lock, &hakios_platform_lock_group);

void
hakios_spmi_lock(void)
{
	lck_spin_lock(&spmi_lock);
}

void
hakios_spmi_unlock(void)
{
	lck_spin_unlock(&spmi_lock);
}

/*
 * What the DSPs' starts use while another may be at work beside them: the mailbox to the always-on processor, one
 * message at a time, and the allocation of items in the shared memory (whose hardware lock both take under the same
 * number, so it does not keep the two apart); RPMh's sequencer (TCS 0 of the RSC); a free match register and context
 * of the SMMU, found and taken.
 */
LCK_MTX_DECLARE(hakios_aop_mailbox_lock, &hakios_platform_lock_group);
LCK_MTX_DECLARE(hakios_smem_alloc_lock, &hakios_platform_lock_group);
LCK_MTX_DECLARE(hakios_rsc_lock, &hakios_platform_lock_group);
LCK_MTX_DECLARE(hakios_smmu_lock, &hakios_platform_lock_group);
/* One call into TrustZone at a time, from the DSPs' starts (Linux's qcom_scm takes a lock for every call as well) */
LCK_MTX_DECLARE(hakios_scm_lock, &hakios_platform_lock_group);

/*
 * A number on the screen, big, for a start that may stop the phone dead: the last number shown stays there when nothing
 * runs any more, and whoever holds the phone can say it -- when no log can be read (the network not up yet, or stopped
 * with everything else). Two digits drawn as a seven-segment display in the top half of the panel, row 0 cyan (the audio
 * DSP, the sound), row 1 yellow (the modem), row 2 white; a black box under them first. Only with boot-arg marks=1.
 */
extern bool loader_screen(const uint8_t **base, uint32_t *width, uint32_t *height, uint32_t *stride);

static void
mark_box(uint8_t *base, uint32_t stride, uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
    uint32_t color)
{
	for (uint32_t row = y; row < y + h && row < height; row++) {
		uint32_t *line = (uint32_t *)(void *)(base + (uint64_t)row * stride);

		for (uint32_t col = x; col < x + w && col < width; col++) {
			line[col] = color;
		}
	}
}

void
hakios_mark(unsigned row, unsigned value)
{
	/* the segments of each digit: a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle */
	static const uint8_t digits[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
	static const uint32_t colors[3] = { 0x0000ffff, 0x00ffff00, 0x00ffffff };   /* BGRX: cyan, yellow, white */
	static int enabled = -1;
	const uint8_t *screen;
	uint8_t *base;
	uint32_t width, height, stride, x0, y0, color;
	const uint32_t w = 120, h = 220, t = 26;        /* a digit's width, height, and its segments' thickness */

	if (enabled < 0) {
		uint32_t want = 0;              /* (only for a test: boot-arg marks=1) */

		PE_parse_boot_argn("marks", &want, sizeof(want));
		enabled = want != 0;
	}
	if (!enabled || row > 2 || !loader_screen(&screen, &width, &height, &stride) || width < 600 || height < 1200) {
		return;
	}
	base = (uint8_t *)(uintptr_t)screen;
	value %= 100;
	color = colors[row];
	x0 = width / 2 - w - 30;
	y0 = 260 + row * (h + 80);
	mark_box(base, stride, width, height, x0 - 30, y0 - 30, 2 * w + 120, h + 60, 0);
	for (unsigned d = 0; d < 2; d++) {
		uint8_t seg = digits[d == 0 ? value / 10 : value % 10];
		uint32_t x = x0 + d * (w + 60);

		if (seg & 0x01) mark_box(base, stride, width, height, x, y0, w, t, color);
		if (seg & 0x02) mark_box(base, stride, width, height, x + w - t, y0, t, h / 2, color);
		if (seg & 0x04) mark_box(base, stride, width, height, x + w - t, y0 + h / 2, t, h / 2, color);
		if (seg & 0x08) mark_box(base, stride, width, height, x, y0 + h - t, w, t, color);
		if (seg & 0x10) mark_box(base, stride, width, height, x, y0 + h / 2, t, h / 2, color);
		if (seg & 0x20) mark_box(base, stride, width, height, x, y0, t, h / 2, color);
		if (seg & 0x40) mark_box(base, stride, width, height, x, y0 + h / 2 - t / 2, w, t, color);
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static char hakios_drivers_string[] = HAKIOS_DRIVERS_LIST;

SYSCTL_STRING(_kern_hakios, OID_AUTO, drivers, CTLFLAG_RD | CTLFLAG_LOCKED, hakios_drivers_string, 0,
    "the drivers this kernel was built with (CONFIG_HAKIOS_* of its configuration, lowercase)");

static void
hakios_platform_init(void)
{
	printf("hakios-kernel: %d drivers built in (%s): %s\n", HAKIOS_DRIVERS_COUNT, HAKIOS_DRIVERS_CONFIG, hakios_drivers_string);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, hakios_platform_init);
