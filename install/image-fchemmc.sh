#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# fchemmc, the driver of the XGS 87's eMMC controller (contrib/fchemmc), in a ready-made image.
#
#	sh install/image-fchemmc.sh put <image root> [<sys>]   at an image build, with the image mounted
#	sh install/image-fchemmc.sh settle [unknown-board]   at an install, from install.sh, first of all
#
# WHY AN IMAGE NEEDS IT BUILT IN. On the XGS 87 the kernel's own sdhci_pci takes the eMMC controller
# (AMD FCH, PCI 1022:7906) and leaves the card silent until its power is removed; only fchemmc, loaded
# by the loader before the PCI bus is probed, brings it up. An image's first boot is too late for the
# plugin to build it: install.sh runs from the image's first-boot hook, after the kernel has probed the
# bus, and a warm reboot does not bring the card back. So without a build in the image, the live system
# of an XGS 87 has no eMMC, and its installer offers none to install onto.
#
# PUT builds fchemmc.ko at the image build for the image's own kernel - read out of
# <image root>/boot/kernel/kernel, the version line the module then probes on and on no other - against
# <sys>, the sys directory of that kernel's sources (opnsense/src at the commit the version line names).
# Without <sys> it takes sys/ out of the archive of those sources the image already carries for the
# plugin's own builds, in /usr/local/share/os-xgs-npu/kernel-sources (any name with the commit in it).
# It builds with the make and cc of the machine it runs on, which must run the image's own major
# FreeBSD release, as an appliance builds with its own; it refuses any other. MAKE names another make.
# It puts into the image:
#
#	/boot/modules/fchemmc.ko and fchemmc.ko.kernel      the module and its stamp, as the kernel
#	                                                    follower installs them
#	/boot/loader.conf.d/fchemmc.conf                    fchemmc_load="YES"
#	/usr/local/share/os-xgs-npu/fchemmc/                its sources, as install.sh lays them down
#	/usr/local/share/os-xgs-npu/fchemmc.image           the mark: the kernel it was built for
#
# Nothing else in the image changes, and a step that fails takes away whatever it had put there, so an
# image carries all of it or none of it. On a machine without the controller the module loads and
# attaches to nothing.
#
# SETTLE is what install.sh runs at an install, before anything else, wherever the mark is: on the live
# system of an image at its first boot - before its installer can copy the system to a disk - and on an
# installed system that did not run it then. Where a 1022:7906 is on the bus and the board is one this
# project installs on, the plugin takes the module over as its own (the mark goes; the kernel follower
# keeps it built from then on). Anywhere else everything PUT laid down is taken away, so that after the
# first boot nothing differs from an image without it: an appliance of another model, or a board this
# project does not install on (install.sh passes unknown-board). The module stays loaded until the next
# boot, attached to nothing. Except where the running system's own disk is the eMMC: that system boots
# only with fchemmc, and it is left as it is.
#
# ROOT (default /), PCICONF, SYSTEMDISK, UNAME and TAR stand in for the running system, for testing.

set -u

SRC=$(cd "$(dirname "$0")/.." && pwd)
: ${MAKE:=/usr/bin/make}
: ${PCICONF:=/usr/sbin/pciconf}

MODREL=boot/modules/fchemmc.ko
LOADERREL=boot/loader.conf.d/fchemmc.conf
SHAREREL=usr/local/share/os-xgs-npu
MARKREL=${SHAREREL}/fchemmc.image

say() { echo "fchemmc (image): $*"; }

case "${1:-}" in
put)
	[ $# -eq 2 ] || [ $# -eq 3 ] || { echo "usage: $0 put <image root> [<sys directory of the image kernel's sources>]" >&2; exit 64; }
	IMG=${2%/}
	SYS=${3:-}
	SYS=${SYS%/}
	if [ ! -f "${IMG}/boot/kernel/kernel" ]; then
		say "no ${IMG}/boot/kernel/kernel - not an image root"
		exit 1
	fi
	# The kernel follower's own reading of a kernel file (kernel-follow.sh, version_of_kernel_file).
	KVER=$(/usr/bin/strings -a "${IMG}/boot/kernel/kernel" 2>/dev/null |
	    /usr/bin/grep -m1 -E '^FreeBSD [0-9].*stable/[^ ]+' || true)
	if [ -z "${KVER}" ]; then
		say "no version line in ${IMG}/boot/kernel/kernel"
		exit 1
	fi
	case "${KVER}" in
	*'"'*|*'\'*)
		say "the kernel's version line holds a quote or a backslash; not built"
		exit 1
		;;
	esac
	# Built with the make and cc of this machine: they must be of the image's own major release, as on
	# an appliance, where the follower builds with the system's own. Refused on any other.
	kmajor=$(printf '%s\n' "${KVER}" | /usr/bin/sed -n 's/^FreeBSD \([0-9][0-9]*\)\..*/\1/p')
	hmajor=$(${UNAME:-/usr/bin/uname} -r 2>/dev/null | /usr/bin/sed -n 's/^\([0-9][0-9]*\)\..*/\1/p')
	if [ -z "${kmajor}" ] || [ "${kmajor}" != "${hmajor}" ]; then
		say "the image's kernel is FreeBSD ${kmajor:-<unknown>} and this machine runs FreeBSD ${hmajor:-<unknown>}: build on a FreeBSD ${kmajor:-of the image's release}; nothing put into the image"
		exit 1
	fi
	BUILD=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/fchemmc-image.XXXXXX") || exit 1
	trap '/bin/rm -rf "${BUILD}"' EXIT
	# Without <sys>: the sys directory out of the kernel sources the image carries for the plugin's own
	# builds (/usr/local/share/os-xgs-npu/kernel-sources, any archive with the kernel's commit in its
	# name, as contrib/npuep/fetch-sources.sh takes it).
	if [ -z "${SYS}" ]; then
		sha=$(printf '%s\n' "${KVER}" | /usr/bin/sed -n 's/.*-n[0-9]*-\([0-9a-f][0-9a-f]*\).*/\1/p')
		archive=
		for a in "${IMG}/${SHAREREL}/kernel-sources/"*"${sha}"*.tar.gz; do
			[ -n "${sha}" ] && [ -f "${a}" ] && archive=${a} && break
		done
		if [ -z "${archive}" ]; then
			say "no kernel sources for ${sha:-<no commit>} in /${SHAREREL}/kernel-sources of the image, and none given; nothing put into the image"
			exit 1
		fi
		/bin/mkdir -p "${BUILD}/kernel-src" &&
		    ${TAR:-/usr/bin/tar} -xzf "${archive}" -C "${BUILD}/kernel-src" --strip-components=1 '*/sys/*' 2>/dev/null
		SYS=${BUILD}/kernel-src/sys
	fi
	if [ ! -f "${SYS}/sys/param.h" ]; then
		say "${SYS} is not the sys directory of a kernel's sources (no sys/param.h); nothing put into the image"
		exit 1
	fi
	# The sources as install.sh's copy_driver() lays them down: every real .c and .h, none generated.
	for f in "${SRC}"/contrib/fchemmc/Makefile "${SRC}"/contrib/fchemmc/*.c "${SRC}"/contrib/fchemmc/*.h; do
		[ -f "${f}" ] || continue
		case "${f##*/}" in
		bus_if.h|device_if.h|pci_if.h|mmcbr_if.h|sdhci_if.h|opt_*.h|fchemmc_kernel.h) continue ;;
		esac
		/bin/mkdir -p "${BUILD}/src" "${BUILD}/obj" && /bin/cp "${f}" "${BUILD}/src/" && /bin/cp "${f}" "${BUILD}/obj/" || exit 1
	done
	if ! ( cd "${BUILD}/obj" && ${MAKE} SYSDIR="${SYS}" FCHEMMC_KERNEL="${KVER}" ) > "${BUILD}/build.log" 2>&1 ||
	    [ ! -f "${BUILD}/obj/fchemmc.ko" ]; then
		say "the build failed for ${KVER}; nothing put into the image:"
		/usr/bin/tail -20 "${BUILD}/build.log" | /usr/bin/sed 's/^/  /'
		exit 1
	fi
	# As the follower checks its own builds: the machine headers from the sources given, not another tree.
	mlink=$(/usr/bin/readlink "${BUILD}/obj/machine" 2>/dev/null || true)
	case "${mlink}" in
	"${SYS}"/*) ;;
	*)
		say "the machine headers came from ${mlink:-nowhere}, not from ${SYS}; nothing put into the image"
		exit 1
		;;
	esac
	undo() {
		/bin/rm -rf "${IMG}/${SHAREREL}/fchemmc"
		/bin/rm -f "${IMG}/${MODREL}" "${IMG}/${MODREL}.kernel" "${IMG}/${LOADERREL}" "${IMG}/${MARKREL}"
	}
	if ! { /bin/mkdir -p "${IMG}/boot/modules" "${IMG}/boot/loader.conf.d" "${IMG}/${SHAREREL}/fchemmc" &&
	    /bin/cp "${BUILD}/obj/fchemmc.ko" "${IMG}/${MODREL}" &&
	    printf '%s\n' "${KVER}" > "${IMG}/${MODREL}.kernel" &&
	    /bin/chmod 0555 "${IMG}/${MODREL}" && /bin/chmod 0444 "${IMG}/${MODREL}.kernel" &&
	    /bin/cp "${BUILD}"/src/* "${IMG}/${SHAREREL}/fchemmc/" &&
	    /bin/chmod 0644 "${IMG}/${SHAREREL}"/fchemmc/* &&
	    printf '%s\n' 'fchemmc_load="YES"' > "${IMG}/${LOADERREL}" &&
	    printf '%s\n' "${KVER}" > "${IMG}/${MARKREL}"; }; then
		undo
		say "could not write into ${IMG}; nothing put into the image"
		exit 1
	fi
	say "built for ${KVER}"
	say "  /${MODREL}, /${LOADERREL}, /${SHAREREL}/fchemmc, /${MARKREL}"
	exit 0
	;;
settle)
	ROOT=${ROOT:-}
	ROOT=${ROOT%/}
	: ${SYSTEMDISK:=${SRC}/src/opnsense/scripts/xgs/system-disk.sh}
	[ -f "${ROOT}/${MARKREL}" ] || exit 0
	if [ "${2:-}" != unknown-board ] && ${PCICONF} -l 2>/dev/null |
	    /usr/bin/grep -qE 'chip=0x79061022|vendor=0x1022[[:space:]]+device=0x7906'; then
		/bin/rm -f "${ROOT}/${MARKREL}"
		say "kept: this machine has the eMMC controller it drives, and the kernel follower keeps it built from now on"
		exit 0
	fi
	if [ -f "${SYSTEMDISK}" ] && /bin/sh "${SYSTEMDISK}" -e > /dev/null 2>&1; then
		say "kept: the system disk is the eMMC, which this system reaches through it"
		exit 0
	fi
	built=$(/bin/cat "${ROOT}/${MARKREL}" 2>/dev/null)
	# Only the image's own build: a module of the same name the follower built is not this one's to take.
	if [ "$(/bin/cat "${ROOT}/${MODREL}.kernel" 2>/dev/null)" = "${built}" ]; then
		/bin/rm -f "${ROOT}/${MODREL}" "${ROOT}/${MODREL}.kernel" "${ROOT}/${LOADERREL}"
	fi
	/bin/rm -rf "${ROOT}/${SHAREREL}/fchemmc"
	/bin/rm -f "${ROOT}/${MARKREL}"
	if [ "${2:-}" = unknown-board ]; then
		say "taken away: this board is not one this project installs on"
	else
		say "taken away: no 1022:7906 eMMC controller here"
	fi
	say "  a loaded fchemmc stays, attached to nothing, until the next boot"
	exit 0
	;;
*)
	echo "usage: $0 put <image root> [<sys>] | settle [unknown-board]" >&2
	exit 64
	;;
esac
