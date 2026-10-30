#!/bin/bash
# Build the driver of miatoll's Wi-Fi radio for our kernel: ath10k, Qualcomm's driver for the WCN3990 (the
# Redmi Note 9S's radio, run by firmware in the modem), as it is in Linux, unmodified, on the same layer as the
# POCO's ath11k (tools/mk-ath11k.sh says how that layer is made and why: FreeBSD's LinuxKPI headers, wifi/lx).
# What differs from ath11k's build is only which driver and which of its files: the radio is on the SoC's own
# bus here (snoc.c), its firmware's service is reached through the modem's QRTR (drivers/modem/hakios.mpss.c),
# and the firmware has been put in mission mode before the driver runs only when wlfw_mission=1.
#
# What comes out, in xnu-deps/build/ath10k:
#   src/ath10k/   the driver's files, untouched        out/ath10k.o   the object for the kernel
#   out/stubs.txt what of the driver is not built yet
# usage: tools/mk-ath10k.sh [fetch]
set -e
VERSION=7.0.14
SHA256=de9999b784d2293f00d39c62d8f92a08ab8a54bc4e80ffd250a0c09cb07a0f98
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
B=$ROOT/xnu-deps/build/ath10k
ATH=drivers/net/wireless/ath
mkdir -p "$B"
cd "$B"

if [ "$(cat src/.release 2>/dev/null)" != "$VERSION $SHA256" ]; then
	[ -f linux-$VERSION.tar.xz ] || curl -sL --fail -o linux-$VERSION.tar.xz \
		https://cdn.kernel.org/pub/linux/kernel/v${VERSION%%.*}.x/linux-$VERSION.tar.xz
	echo "$SHA256  linux-$VERSION.tar.xz" | sha256sum -c --quiet
	rm -rf src unpacked
	mkdir unpacked
	# ath10k and what of drivers/net/wireless/ath it uses: the common Atheros code (ath.h, the regulatory
	# domains, DFS's pattern detector), which Linux builds as its own module, ath.ko
	tar xJf linux-$VERSION.tar.xz -C unpacked --strip-components=1 linux-$VERSION/$ATH/ath10k \
		$(for f in ath.h reg.h regd.h regd_common.h regd.c main.c key.c hw.c debug.c dfs_pattern_detector.h \
			dfs_pattern_detector.c dfs_pri_detector.h dfs_pri_detector.c spectral_common.h testmode_i.h trace.h; do
			echo linux-$VERSION/$ATH/$f; done)
	# every file must say itself that it may be used so: the licence is checked, not assumed
	for f in unpacked/$ATH/ath10k/*.[ch] unpacked/$ATH/*.[ch]; do
		case "$(head -6 "$f")" in
		*"SPDX-License-Identifier: ISC"*) ;;
		*"SPDX-License-Identifier: BSD-3-Clause-Clear"*) ;;
		*"Permission to use, copy, modify, and/or distribute this software"*) ;;      # (ISC, spelt out)
		*) echo "mk-ath10k.sh: $f does not carry the licence expected" >&2; exit 1 ;;
		esac
	done
	mkdir src
	mv unpacked/$ATH/ath10k src/ath10k
	mv unpacked/$ATH/*.[ch] src/
	rm -rf unpacked
	echo "$VERSION $SHA256" > src/.release
fi
[ "$1" = fetch ] && { echo "mk-ath10k.sh: ath10k of Linux $VERSION: $B/src/ath10k"; exit 0; }
W=$ROOT/wifi/lx

# --- FreeBSD's LinuxKPI and the kernel headers under it, with our patches on top. When the patches
# change, the tree is taken from the archive again (as tools/mk-mesa.sh does with Mesa's).
FREEBSD=15.1
FREEBSD_SHA256=cf5762da53fd52e1eaf0f9ceee9bf58cbe314c821031d0d9ffa76823185a89e1
FSYS="compat/linuxkpi sys arm64/include net net80211 netinet netinet6 vm kern tools libkern ddb
	security fs/pseudofs crypto opencrypto geom bsm isa cam rpc xdr x86/include ofed/include
	dev/pci dev/ofw dev/fdt dev/acpica dev/usb dev/iicbus dev/backlight
	contrib/ck/include contrib/dev/acpica/include contrib/libnv"
PATCHES=$(cat "$W"/patches/*.patch 2>/dev/null | sha256sum | cut -d' ' -f1)
if [ "$(cat freebsd/.release 2>/dev/null)" != "$FREEBSD $FREEBSD_SHA256 $PATCHES" ]; then
	[ -f freebsd-$FREEBSD-src.txz ] || curl -sL --fail -o freebsd-$FREEBSD-src.txz \
		https://download.freebsd.org/releases/amd64/$FREEBSD-RELEASE/src.txz
	echo "$FREEBSD_SHA256  freebsd-$FREEBSD-src.txz" | sha256sum -c --quiet
	rm -rf freebsd gen obj
	mkdir freebsd
	tar xJf freebsd-$FREEBSD-src.txz -C freebsd --strip-components=2 usr/src/COPYRIGHT \
		$(for d in $FSYS; do echo usr/src/sys/$d; done)
	for p in "$W"/patches/*.patch; do
		[ -f "$p" ] && patch -s --no-backup-if-mismatch -d freebsd -p1 < "$p"
	done
	echo "$FREEBSD $FREEBSD_SHA256 $PATCHES" > freebsd/.release
fi
S=$B/freebsd/sys

# --- what FreeBSD's build would have generated: the interfaces of its device framework, and the
# option headers, which here say only that there is an Internet
if [ ! -d gen ]; then
	mkdir gen
	(
		cd gen
		ln -s $S/arm64/include machine
		for m in kern/device_if.m kern/bus_if.m dev/pci/pci_if.m dev/pci/pci_iov_if.m dev/pci/pcib_if.m \
			dev/iicbus/iicbus_if.m dev/iicbus/iicbb_if.m dev/backlight/backlight_if.m dev/usb/usb_if.m \
			dev/ofw/ofw_bus_if.m; do
			awk -f $S/tools/makeobjops.awk $S/$m -h
		done
		for o in h p q; do awk -f $S/tools/vnode_if.awk $S/kern/vnode_if.src -$o; done
		for o in $(grep -rhoE "opt_[a-z0-9_]+\.h" $S | sort -u) lkpi_iic_if.h; do : > $o; done
		printf '#define INET 1\n#define INET6 1\n' > opt_inet.h
		printf '#define INET6 1\n' > opt_inet6.h
		printf '#define SMP 1\n' > opt_global.h
	)
fi

# --- compile: the driver's files that are built so far, and ours
DRIVER="core hw ce htc htt htt_rx htt_tx txrx mac wmi wmi-tlv qmi qmi_wlfw_v01 bmi p2p snoc"
CC="clang -target arm64-apple-ios16.5"
LD=$ROOT/xnu-deps/host/bin/ld64
CFLAGS="-mkernel -mgeneral-regs-only -fno-builtin -fno-common -ffreestanding -fno-strict-aliasing
	-funsigned-char -nostdinc -std=gnu17 -O2 -g -w
	-D__FreeBSD__=15 -D_KERNEL -DKLD_MODULE -DHAVE_KERNEL_OPTION_HEADERS -include opt_global.h
	-I$W/include -Igen -I$S -I$S/contrib/ck/include
	-I$S/compat/linuxkpi/common/include -I$S/compat/linuxkpi/dummy/include
	-include $S/compat/linuxkpi/common/include/linux/kconfig.h -DLINUXKPI_VERSION=70000"
DFLAGS="-Isrc/ath10k -Isrc -include $W/include/lx_prelude.h -DCONFIG_ATH10K_SNOC -DCONFIG_ATH10K_DEBUG
	-DKBUILD_MODNAME=\"ath10k\""
# two headers are the kernel's: what it offers this object (wifi/lx/include/lx_os.h), and its QMI
# helpers (wifi/lx/include/linux/soc/qcom/qmi.h)
source "$ROOT/xnu-env.sh"
# (the kernel's tree once xnu/apply.sh has made it, else the files it is made of)
KH=$XNU_SRC/bsd/sys
[ -f "$KH/hakios.lx.h" ] || KH=$ROOT/xnu/files/bsd/sys
for h in lx qmi; do
	[ -f "$KH/hakios.$h.h" ] || { echo "mk-ath10k.sh: no bsd/sys/hakios.$h.h" >&2; exit 1; }
done
DFLAGS="$DFLAGS -DLX_HAKIOS_LX_H=\"$KH/hakios.lx.h\" -DLX_HAKIOS_QMI_H=\"$KH/hakios.qmi.h\""
mkdir -p obj out
rm -f obj/*.err
compile() {		# compile SOURCE OBJECT [FLAGS]: only when the object is older than anything it was made of
	local src=$1 obj=$2
	shift 2
	if [ -f "$obj" ] && [ -f "$obj.d" ] && [ -z "$(find $(sed -e 's/^[^:]*://' -e 's/\\$//' "$obj.d") "$0" -newer "$obj" 2>/dev/null | head -1)" ]; then
		return
	fi
	$CC $CFLAGS "$@" -MMD -MF "$obj.d" -c "$src" -o "$obj" 2> "$obj.err" || rm -f "$obj"
}
for f in $DRIVER; do compile src/ath10k/$f.c obj/ath10k_$f.o $DFLAGS & done
ATH_COMMON="main regd key hw dfs_pattern_detector dfs_pri_detector"
for f in $ATH_COMMON; do compile src/$f.c obj/ath_$f.o $DFLAGS & done
# the layer's own files that are not ath11k's (lx_bus.c is ath11k's bus), and ath10k's bus here (wifi/lx/src10)
LX="$W/src/lx_base.c $W/src/lx_80211.c $W/src/lx_wpa.c $(ls "$W"/src10/*.c 2>/dev/null || true)"
for f in $LX; do compile "$f" obj/lx_$(basename "$f" .c).o $DFLAGS -I$W/src -I$W/src10 & done
wait
bad=
for f in obj/*.err; do
	[ -s "$f" ] && [ ! -f "${f%.err}" ] && { echo "--- $(basename "$f" .o.err)"; grep -m6 -A3 "error:" "$f"; bad=1; }
done
[ -z "$bad" ] || exit 1
OBJECTS=$(for f in $DRIVER; do echo obj/ath10k_$f.o; done; for f in $ATH_COMMON; do echo obj/ath_$f.o; done; for f in $LX; do echo obj/lx_$(basename "$f" .c).o; done)

# --- tie it all into one object. What it then still wants is one of three things: a call of the
# kernel's (hakios.lx.h; the QMI helpers; the handful of C library functions XNU has under the same
# names with the same meaning), a function of a part of the driver that is not built yet, or a
# mistake. The second kind gets a stub each that says its name and answers 0; anything else
# missing ends the build.
KERNEL="memcmp memcpy memmove memset strcmp strncmp strlen strchr strrchr strlcpy strlcat bzero
	snprintf vsnprintf __stack_chk_fail __stack_chk_guard"
$LD -r -arch arm64 $OBJECTS -o out/all.o 2> out/ld.err || { cat out/ld.err; exit 1; }
llvm-nm -u --format=just-symbols out/all.o | sed 's/^_//' | sort > out/undefined.txt
: > out/stubs.txt
: > out/stubs.s
missing=
for s in $(cat out/undefined.txt); do
	case " $(echo $KERNEL) " in *" $s "*) continue ;; esac
	case $s in
	lx_os_*|qmi_*|hakios_qmi_*|section\$*) continue ;;
	ath10k_*|__ath10k_*|ath_*) ;;
	*) echo "mk-ath10k.sh: nothing defines $s" >&2; missing=1; continue ;;
	esac
	echo $s >> out/stubs.txt
	printf '\t.globl _%s\n_%s:\n\tadrp x0, 1f@PAGE\n\tadd x0, x0, 1f@PAGEOFF\n\tb _lx_stub\n\t.cstring\n1:\t.asciz "%s"\n\t.text\n\t.p2align 2\n' \
		$s $s $s >> out/stubs.s
done
[ -z "$missing" ] || exit 1
$CC -c out/stubs.s -o out/stubs.o
# four names are shown: where the kernel enters (lx_prepare(), lx_start(), lx_stop() and lx_transmit() of hakios.lx.h)
printf '_lx_prepare\n_lx_start\n_lx_stop\n_lx_transmit\n' > out/exported.txt
$LD -r -arch arm64 -exported_symbols_list out/exported.txt out/all.o out/stubs.o -o out/ath10k.o 2> out/ld.err ||
	{ cat out/ld.err; exit 1; }
rm -f out/all.o
echo "mk-ath10k.sh: out/ath10k.o: $(llvm-size out/ath10k.o | awk 'NR == 2 { print $1 + $2 }') bytes of code and data from" \
	"$(echo $DRIVER | wc -w) of the driver's files; $(wc -l < out/stubs.txt) functions of the others are stubs (out/stubs.txt);" \
	"asks the kernel for $(llvm-nm -u --format=just-symbols out/ath10k.o | grep -vc 'section\$') names"
