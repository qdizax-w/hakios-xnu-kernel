/*
 * The brightness iOS asks for, to the panel's backlight: /dev/hakios-backlight (miatoll:
 * bsd/dev/hakios.miatoll_backlight.c, boot-arg backlight=2), thousandths as a 32-bit number. Included by
 * inject_sb.c (SpringBoard: Control Center's slider, the brightness keys) and inject_tweaks.c (every
 * program the tweak loader is in, Preferences among them: Settings > Display & Brightness).
 *
 * There is no backlight service here for CoreBrightness to drive, so backboardd says brightness control is
 * not available and drops what is asked of it: the level BackBoardServices keeps then never changes, and
 * following it (BKSDisplayBrightnessGetCurrent) shows nothing. So the asking itself is caught, in the
 * programs that ask:
 *
 *   -[BrightnessSystemClient setProperty:forKey:] with the key DisplayBrightness   (CoreBrightness: Control
 *        Center's module and Settings' slider; the value an NSNumber, or a dictionary with one under "Brightness")
 *   -[SBDisplayBrightnessController setBrightnessLevel:] and setBrightnessLevel:animated:   (SpringBoard)
 *
 * Whether those take a float or a double is read from the method's own type encoding, not assumed; a method
 * whose encoding is neither is left alone, and says so. The original is always called after.
 * The classes are looked for once a second for a minute (CoreBrightness's may be loaded late), and the hooks
 * are put in once per process however many of our libraries are in it (an environment variable says so).
 */
#ifndef HAKIOS_BRIGHTNESS_HOOK_H
#define HAKIOS_BRIGHTNESS_HOOK_H

#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* each program's library says it its own way: the file that includes this defines it */
static void bh_say(const char *format, ...) __printflike(1, 2);

static int bh_fd = -1;
static unsigned bh_told;

static void
bh_send(const char *source, double value)
{
	uint32_t permille = value <= 0 ? 0 : value >= 1 ? 1000 : (uint32_t)(value * 1000 + 0.5);

	if (bh_fd < 0) {
		bh_fd = open("/dev/hakios-backlight", O_WRONLY);
	}
	if (bh_fd >= 0 && write(bh_fd, &permille, sizeof(permille)) == sizeof(permille) && bh_told < 12) {
		bh_told++;
		bh_say("hakios: backlight: %s asks for %u of 1000\n", source, permille);
	}
}

/* ---- -[BrightnessSystemClient setProperty:forKey:] ------------------------------------------------- */

static void (*bh_orig_set_property)(id, SEL, id, id);

static void
bh_set_property(id self, SEL selector, id value, id key)
{
	if (key != nil && value != nil && CFGetTypeID((CFTypeRef)key) == CFStringGetTypeID() &&
	    CFEqual((CFStringRef)key, CFSTR("DisplayBrightness"))) {
		CFTypeRef number = (CFTypeRef)value;
		double level;

		if (CFGetTypeID(number) == CFDictionaryGetTypeID()) {
			number = CFDictionaryGetValue((CFDictionaryRef)value, CFSTR("Brightness"));
		}
		if (number != NULL && CFGetTypeID(number) == CFNumberGetTypeID() &&
		    CFNumberGetValue((CFNumberRef)number, kCFNumberDoubleType, &level)) {
			bh_send("BrightnessSystemClient", level);
		}
	}
	bh_orig_set_property(self, selector, value, key);
}

/* ---- -[SBDisplayBrightnessController setBrightnessLevel:(animated:)] -------------------------------- */

static void (*bh_orig_level_f)(id, SEL, float);
static void (*bh_orig_level_d)(id, SEL, double);
static void (*bh_orig_level_anim_f)(id, SEL, float, BOOL);
static void (*bh_orig_level_anim_d)(id, SEL, double, BOOL);

static void bh_level_f(id s, SEL c, float v) { bh_send("SBDisplayBrightnessController", v); bh_orig_level_f(s, c, v); }
static void bh_level_d(id s, SEL c, double v) { bh_send("SBDisplayBrightnessController", v); bh_orig_level_d(s, c, v); }
static void bh_level_anim_f(id s, SEL c, float v, BOOL a) { bh_send("SBDisplayBrightnessController", v); bh_orig_level_anim_f(s, c, v, a); }
static void bh_level_anim_d(id s, SEL c, double v, BOOL a) { bh_send("SBDisplayBrightnessController", v); bh_orig_level_anim_d(s, c, v, a); }

/* the type of the first argument after self and _cmd: 'f', 'd', or 0 */
static char
bh_arg_type(Method m)
{
	char type[16] = "";

	method_getArgumentType(m, 2, type, sizeof(type));
	return (type[0] == 'f' || type[0] == 'd') ? type[0] : 0;
}

static bool bh_client_done, bh_sb_done;

static void
bh_try(void)
{
	Class client = objc_getClass("BrightnessSystemClient");
	Class sb = objc_getClass("SBDisplayBrightnessController");
	Method m;

	if (!bh_client_done && client != Nil &&
	    (m = class_getInstanceMethod(client, sel_registerName("setProperty:forKey:"))) != NULL) {
		bh_orig_set_property = (void (*)(id, SEL, id, id))method_setImplementation(m, (IMP)bh_set_property);
		bh_client_done = true;
		bh_say("hakios: backlight: -[BrightnessSystemClient setProperty:forKey:] caught\n");
	}
	if (!bh_sb_done && sb != Nil) {
		bh_sb_done = true;
		if ((m = class_getInstanceMethod(sb, sel_registerName("setBrightnessLevel:"))) != NULL) {
			char t = bh_arg_type(m);

			if (t == 'f') {
				bh_orig_level_f = (void (*)(id, SEL, float))method_setImplementation(m, (IMP)bh_level_f);
			} else if (t == 'd') {
				bh_orig_level_d = (void (*)(id, SEL, double))method_setImplementation(m, (IMP)bh_level_d);
			}
			bh_say("hakios: backlight: -[SBDisplayBrightnessController setBrightnessLevel:] (%s) %s\n",
			    method_getTypeEncoding(m), t ? "caught" : "left alone: not a float or a double");
		}
		if ((m = class_getInstanceMethod(sb, sel_registerName("setBrightnessLevel:animated:"))) != NULL) {
			char t = bh_arg_type(m);

			if (t == 'f') {
				bh_orig_level_anim_f = (void (*)(id, SEL, float, BOOL))method_setImplementation(m, (IMP)bh_level_anim_f);
			} else if (t == 'd') {
				bh_orig_level_anim_d = (void (*)(id, SEL, double, BOOL))method_setImplementation(m, (IMP)bh_level_anim_d);
			}
			bh_say("hakios: backlight: -[SBDisplayBrightnessController setBrightnessLevel:animated:] (%s) %s\n",
			    method_getTypeEncoding(m), t ? "caught" : "left alone: not a float or a double");
		}
	}
}

/* Once per process, and only where the kernel has the device. */
static void
bh_install(bool springboard)
{
	__block int tries = 0;
	dispatch_source_t timer;

	if (getenv("HAKIOS_BRIGHTNESS_HOOKED") != NULL || access("/dev/hakios-backlight", W_OK) != 0) {
		return;
	}
	setenv("HAKIOS_BRIGHTNESS_HOOKED", "1", 1);
	timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
	dispatch_source_set_timer(timer, DISPATCH_TIME_NOW, NSEC_PER_SEC, NSEC_PER_SEC / 10);
	dispatch_source_set_event_handler(timer, ^{
		bh_try();
		if ((bh_client_done && (bh_sb_done || !springboard)) || ++tries >= 60) {
			if (!bh_client_done) {
				bh_say("hakios: backlight: no BrightnessSystemClient in this program after a minute\n");
			}
			dispatch_source_cancel(timer);
		}
	});
	dispatch_resume(timer);
}

#endif
