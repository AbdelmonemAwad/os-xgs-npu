#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Keeps the next boot on a kernel that can reach its system disk, where that disk is the XGS 87's eMMC.
#
# A kernel booted without an fchemmc built for it has no root disk there, and the appliance does not
# boot at all. The kernel follower (install/kernel-follow.sh) builds fchemmc for each new kernel; this
# is what stands behind it, and it needs nothing of the plugin: the follower copies it to
# /boot/fchemmc-guard with its own copy of system-disk.sh, and writes the two boot hooks that run it
# (early/04-fchemmc-guard and stop/79-fchemmc-guard), on an eMMC system disk only. They are in no
# package's list and uninstall.sh leaves them there, so a kernel update after the plugin was removed
# is still safe.
#
# EARLY, because nextboot is one shot and spent by the boot it steers: the fall-back is armed again
# before anything else can go wrong in this boot. AT STOP, numbered after every plugin's follower and
# before 80-freebsd, so that the last word at a reboot is this one, whoever cleared nextboot before it.
#
# It only ever arms. Where /boot/kernel has its fchemmc it does nothing, and letting a fall-back go is
# the follower's. It takes the first of kernel.old and kernel.emmc-good (the last kernel that booted
# from the eMMC, which no kernel update rotates) with an fchemmc built for it, across a major release
# too: a boot with its disk can be repaired, one without cannot. Always exits 0.
#
# BOOT, NEXTBOOT, ARMED and SYSTEMDISK stand in for the running system, for testing.

: ${BOOT:=/boot}
: ${NEXTBOOT:=/sbin/nextboot}
: ${ARMED:=/var/run/octep-follow.armed}
: ${SYSTEMDISK:=$(dirname "$0")/system-disk.sh}

say() {
	echo "fchemmc guard: $*"
	/usr/bin/logger -t fchemmc-guard -p daemon.notice "$*" 2>/dev/null || true
}

version() {
	[ -r "$1/kernel" ] || return 0
	/usr/bin/strings -a "$1/kernel" 2>/dev/null | /usr/bin/grep -m1 -E '^FreeBSD [0-9].*stable/[^ ]+'
}

# As the loader finds it: the build in the kernel's own directory, or the one in /boot/modules.
has_its_fchemmc() {
	v=$(version "$1")
	[ -n "${v}" ] || return 1
	if [ -f "$1/fchemmc.ko" ]; then
		[ "$(/bin/cat "$1/fchemmc.ko.kernel" 2>/dev/null)" = "${v}" ]
	else
		[ -f "${BOOT}/modules/fchemmc.ko" ] &&
		    [ "$(/bin/cat "${BOOT}/modules/fchemmc.ko.kernel" 2>/dev/null)" = "${v}" ]
	fi
}

/bin/sh "${SYSTEMDISK}" -e > /dev/null 2>&1 || exit 0
has_its_fchemmc "${BOOT}/kernel" && exit 0

for d in kernel.old kernel.emmc-good; do
	has_its_fchemmc "${BOOT}/${d}" || continue
	if "${NEXTBOOT}" -k "${d}" > /dev/null 2>&1; then
		/usr/bin/touch "${ARMED}" 2>/dev/null || true
		say "/boot/kernel has no fchemmc built for it; the next boot takes /boot/${d}"
	else
		say "/boot/kernel has no fchemmc built for it, and ${NEXTBOOT} -k ${d} failed"
	fi
	exit 0
done
say "no kernel in /boot has an fchemmc built for it: the next boot finds no system disk"
exit 0
