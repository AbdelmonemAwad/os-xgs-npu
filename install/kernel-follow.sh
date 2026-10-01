#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Make the module follow the kernel, automatically, in the one window where that is possible.
#
# WHY THIS EXISTS, and it is not a convenience.
#
# A FreeBSD module declares its kernel dependency as a range - from the __FreeBSD_version it was
# compiled with to the end of that branch - so within 15.x `kldload` will happily load a module
# built against a different 15.x kernel instead of refusing it, and any structure that moved is then
# read at the wrong offset. Because that failure is silent, this driver is stamped with the kernel it
# was built against and the bring-up script refuses a module whose stamp does not match.
#
# That refusal is correct and it is also, on this appliance, expensive. The WAN is one of the
# coprocessor's own front ports. So a kernel update produces a circle:
#
#	new kernel -> stamp no longer matches -> bring-up refuses -> no front ports -> no WAN
#	-> nothing can fetch the kernel sources that would rebuild the module
#
# and it costs more than the link. OPNsense configures its interfaces from devices that exist at that
# moment in the boot; the ones that do not exist are dropped from `/conf/config.xml` outright. This
# was measured here on 2026-10-01: a single boot with the module refused removed `wan`, `opt1`,
# `opt2` and `opt3` from the configuration, taking the WAN's own DHCP assignment with them. Getting
# the module right afterwards does not put them back - they have to be restored from a backup.
#
# THE WINDOW. Between a kernel being installed and the host rebooting onto it, all three things
# needed are present at once: the old kernel is still running, so the old module is loaded and the
# WAN is alive; and the new kernel is already on disk, where it names its own source commit. That is
# the moment to fetch, build, install and stamp. Then one reboot brings everything up together.
#
# This script is that moment, made automatic. It is driven from rc.syshook.d/update/20-octep, which
# OPNsense runs after an update has installed its packages. It is also safe to run by hand at any
# time: when the stamp already matches the kernel on disk it does nothing at all.
#
#	sh install/kernel-follow.sh            follow the kernel on disk
#	DRY=1 sh install/kernel-follow.sh      say what it would do and change nothing
#
# WHAT IT WILL NOT DO. It never replaces a working module with one that failed to build, and it never
# stamps a module it did not just compile. If anything fails it leaves what is installed exactly
# where it was and says so loudly, because an appliance that still has its ports and a warning in the
# log is better than one that has neither.
#
# AND IT KEEPS THE PREVIOUS MODULE. The new module is stamped for the kernel on disk, which is not
# the kernel that is running while this script runs. If the host ends up booting the old kernel again
# - a rollback, a boot menu choice, an update that did not take - that stamp would be wrong in the
# other direction and the ports would be refused for the opposite reason. So the module being
# replaced is kept beside it as octep.ko.prev with its own stamp, and the bring-up script tries that
# one when the first does not match. Following the kernel forwards must not mean falling off it
# backwards.

set -u

: ${PREFIX:=/usr/local}
: ${MODDIR:=/boot/modules}
: ${MODULE:=${MODDIR}/octep.ko}
: ${KERNEL:=/boot/kernel/kernel}
: ${SRCDIR:=${PREFIX}/opnsense/scripts/octep}
: ${FETCH:=${SRCDIR}/fetch-sources.sh}
: ${BUILDDIR:=/root/npu/octep-sdp}
: ${DRY:=0}

# Everything goes to syslog as well as to stdout. An update runs unattended, so the only account of
# what happened is the log, and a hook that fails quietly is the thing this whole file is against.
log() {
	echo "$*"
	/usr/bin/logger -t octep-follow -p daemon.notice "$*" 2>/dev/null || true
}
warn() {
	echo "$*" >&2
	/usr/bin/logger -t octep-follow -p daemon.err "$*" 2>/dev/null || true
}

# ---------------------------------------------------------------- what kernel are we following

# NOT THE RUNNING KERNEL. The one the next boot will use, and there are two places it can be.
#
# The version string is compiled into a kernel binary, so `strings` reads it without booting it. The
# match is anchored on the FreeBSD banner and the stable/ token together, because the binary carries
# other strings containing a source path.
#
# WHERE IT IS, AND WHY BOTH PLACES MATTER. This was worked out by reading OPNsense's own update path
# rather than assumed, after the first version of this script assumed wrongly:
#
#	/usr/local/opnsense/scripts/firmware/upgrade.sh
#	    opnsense-update -u                    the packages
#	    rc.syshook upgrade                    <-- the only hook level anything calls
#	    opnsense-update -K -c ; -K            the pending kernel is applied to /boot HERE
#	    reboot
#
# So at the hook, the new kernel is downloaded but is NOT yet in /boot - it is a set waiting in
# ${PENDINGDIR} with a marker beside it - and after it is applied the reboot follows with no hook in
# between. Reading only /boot/kernel/kernel is therefore too late at the one place we are called, and
# reading only the staged set misses the window cron has to work with. Both, staged first.
#
# A note on the `update` hook level, because it looks like the obvious home for this and is not: the
# directory rc.syshook.d/update exists and OPNsense ships a script in it, and nothing on this
# appliance passes `update` to rc.syshook. The callers pass facility, stop, start, monitor, upgrade,
# import, early and carp. A hook placed there is read by nobody.

: ${WORKPREFIX:=/var/cache/opnsense-update}
: ${PENDINGDIR:=${WORKPREFIX}/.sets.pending}
: ${PENDING_KERNEL:=${WORKPREFIX}/.kernel.pending}

version_of_kernel_file() {
	[ -r "$1" ] || return 1
	/usr/bin/strings -a "$1" 2>/dev/null |
	    /usr/bin/grep -m1 -E '^FreeBSD [0-9].*stable/[^ ]+' || return 1
}

# The kernel waiting to be applied, read out of its set without unpacking it to disk.
#
# `tar -xOf set boot/kernel/kernel` writes that one member to stdout and `strings` takes the first
# match, so nothing is extracted and the pipe closes as soon as the version is found. The set is
# named kernel-<release>-<arch>[-<device>].txz, and the release is whatever the marker holds - but the
# glob is used rather than the marker's contents, because a device suffix is part of the name on some
# appliances and reconstructing it is a second thing to get wrong.
kernel_staged() {
	[ -f "${PENDING_KERNEL}" ] || return 1
	for set in "${PENDINGDIR}"/kernel-*.txz; do
		[ -r "${set}" ] || continue
		v=$(/usr/bin/tar -xOf "${set}" boot/kernel/kernel 2>/dev/null |
		    /usr/bin/strings -a 2>/dev/null |
		    /usr/bin/grep -m1 -E '^FreeBSD [0-9].*stable/[^ ]+')
		if [ -n "${v}" ]; then
			echo "${v}"
			return 0
		fi
	done
	return 1
}

WHERE=
TARGET=$(kernel_staged || true)
if [ -n "${TARGET}" ]; then
	WHERE="staged, waiting to be applied"
else
	TARGET=$(version_of_kernel_file "${KERNEL}" || true)
	WHERE="${KERNEL}"
fi

if [ -z "${TARGET}" ]; then
	# Not a failure worth shouting about on a machine that is not this appliance, but this script
	# cannot do its job without it.
	warn "cannot read a kernel version from a staged set or from ${KERNEL}; nothing to follow"
	exit 0
fi

RUNNING=$(/usr/bin/uname -v)
STAMP=$(/bin/cat "${MODULE}.kernel" 2>/dev/null || true)

log "kernel to follow: ${TARGET}"
log "  read from     : ${WHERE}"
log "kernel running : ${RUNNING}"
log "module stamp   : ${STAMP:-<none>}"

if [ ! -f "${MODULE}" ]; then
	log "no ${MODULE} - the driver is not installed on this machine, nothing to follow"
	exit 0
fi

if [ "${STAMP}" = "${TARGET}" ]; then
	log "the module already matches the kernel to follow - nothing to do"
	exit 0
fi

log "the module does not match the kernel to follow; this is the window to rebuild it"
if [ "${DRY}" = "1" ]; then
	log "DRY=1 - stopping here, nothing changed"
	exit 0
fi

# ---------------------------------------------------------------- the sources

if [ ! -x "${FETCH}" ]; then
	warn "no ${FETCH} - cannot fetch kernel sources, so the module cannot follow"
	warn "the next boot will refuse it and the front ports will not come up"
	exit 1
fi

# fetch-sources.sh takes the version to pin to, prints a SYSDIR= line, and is a no-op when the tree
# is already there - so running this script twice costs nothing the second time.
FETCHLOG=$(/bin/sh "${FETCH}" "${TARGET}" 2>&1)
SYSDIR=$(echo "${FETCHLOG}" | /usr/bin/sed -n 's/^SYSDIR=//p' | /usr/bin/tail -1)
if [ -z "${SYSDIR}" ] || [ ! -f "${SYSDIR}/sys/param.h" ]; then
	warn "could not get kernel sources for ${TARGET}"
	echo "${FETCHLOG}" | /usr/bin/tail -8 | while read -r l; do warn "  ${l}"; done
	warn "the next boot will refuse the module and the front ports will not come up"
	exit 1
fi
log "building against ${SYSDIR}"

# ---------------------------------------------------------------- the build

if [ ! -d "${BUILDDIR}" ]; then
	warn "no build tree at ${BUILDDIR} - cannot rebuild"
	exit 1
fi

BUILDLOG=/tmp/octep-follow-build.log
if ! ( cd "${BUILDDIR}" && /usr/bin/make clean > /dev/null 2>&1; \
       cd "${BUILDDIR}" && /usr/bin/make SYSDIR="${SYSDIR}" ) > "${BUILDLOG}" 2>&1; then
	warn "the build failed; ${MODULE} is untouched and still matches ${STAMP:-<nothing>}"
	/usr/bin/grep -E ' error:|Error [0-9]' "${BUILDLOG}" | /usr/bin/head -10 |
	    while read -r l; do warn "  ${l}"; done
	exit 1
fi
if [ ! -f "${BUILDDIR}/octep.ko" ]; then
	warn "the build reported success but produced no octep.ko; ${MODULE} is untouched"
	exit 1
fi
log "built $(/usr/bin/stat -f%z "${BUILDDIR}/octep.ko") bytes"

# ---------------------------------------------------------------- install, keeping the old one

# Keep what is being replaced, with its stamp. See the header: a module stamped for the kernel on
# disk is wrong for the kernel that is running, and a host that boots the old kernel again needs the
# old module. The bring-up script looks for this pair.
if [ -n "${STAMP}" ]; then
	/bin/cp -p "${MODULE}" "${MODULE}.prev" 2>/dev/null &&
	    printf '%s\n' "${STAMP}" > "${MODULE}.prev.kernel" &&
	    log "kept the previous module as ${MODULE}.prev, stamped ${STAMP}"
fi

# Write to a temporary name and move it into place, so a full disk or an interrupted copy cannot
# leave a truncated module where a working one was.
if ! /bin/cp "${BUILDDIR}/octep.ko" "${MODULE}.new"; then
	warn "could not write ${MODULE}.new; ${MODULE} is untouched"
	exit 1
fi
printf '%s\n' "${TARGET}" > "${MODULE}.kernel.new"
/bin/mv "${MODULE}.new" "${MODULE}"
/bin/mv "${MODULE}.kernel.new" "${MODULE}.kernel"
/bin/chmod 0555 "${MODULE}" 2>/dev/null || true
/bin/chmod 0444 "${MODULE}.kernel" 2>/dev/null || true

log "installed ${MODULE}, stamped for the kernel the next boot will use"
log "the front ports will come up on that boot without anything else being done"
exit 0
