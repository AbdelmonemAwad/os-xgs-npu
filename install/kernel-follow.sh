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
# This script is that moment, made automatic. It is driven from rc.syshook.d/upgrade/20-octep, which
# OPNsense's firmware upgrade runs between installing the packages and applying the kernel, and from
# a cron entry every five minutes. It is also safe to run by hand at any time: when the stamp already
# matches the kernel to follow it does nothing at all.
#
#	sh install/kernel-follow.sh                  follow the kernel the next boot will use
#	DRY=1 sh install/kernel-follow.sh            say what it would do and change nothing
#	FORCE=1 sh install/kernel-follow.sh          build even when the stamp matches, or when no
#	                                             module is installed yet - install.sh uses this
#	NOFETCH=1 ...                                never download kernel sources; build only if they
#	                                             are already here
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
# The driver's sources, exactly as this repository has them, laid down by install.sh. Nothing is ever
# built in here, so nothing generated can collect in it.
: ${SRCTREE:=${PREFIX}/share/os-xgs-npu/octep}
# Where it is built: emptied and refilled from SRCTREE before every build. See the build section for
# why a build that starts from anything other than nothing cannot be trusted here.
: ${BUILDROOT:=/var/db/os-xgs-npu/build}
: ${DRY:=0}
: ${FORCE:=0}
: ${NOFETCH:=0}
# The kernel a boot falls back to when the module cannot be made to match the new one. See
# pin_old_kernel().
: ${KERNEL_OLD:=/boot/kernel.old/kernel}
: ${NEXTBOOT:=/sbin/nextboot}

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

# ---------------------------------------------------------------- the local fall-back
#
# THE ONE THING HERE THAT NEEDS NO NETWORK, and that is the whole reason it exists.
#
# Everything else in this file repairs the module before the reboot, and all of it needs the
# internet: the sources for a kernel this appliance has never built for have to be fetched. But the
# WAN is one of the coprocessor's own front ports, so it is there only while the module matches the
# running kernel. Lose that and the one route that could repair it is the one that is gone. Measured
# on 2026-10-02: the stop hook ran exactly as designed, asked for the sources, and got "Transient
# resolver failure" - because that boot had already come up without the ports.
#
# So when the module cannot be made to match /boot/kernel, do not boot /boot/kernel. OPNsense keeps
# the kernel it replaced as /boot/kernel.old, and the module on disk is the one built for THAT. The
# loader takes the directory to boot from the `kernel` variable, so one line in loader.conf.local
# sends the next boot to the kernel this module is for. The appliance comes up whole - ports, WAN,
# default route - and the ordinary cron tick then has what it needs to fetch, build, and take the
# line out again. A firewall that boots one release behind is a working firewall; one with no ports
# is not.
# `nextboot -k`, and not a line in loader.conf.local, which was tried first and does not work here:
# OPNsense regenerates that file at every boot from the Tunables in config.xml, so the line was gone
# by the time the loader could have read it. nextboot(8) writes the ZFS bootenv the loader consults,
# and it is ONE SHOT - consumed by the boot it steers. That is the right shape for this: if the
# repair then succeeds there is nothing to clean up, and if the appliance is power-cycled instead of
# rebooted it simply tries the new kernel again rather than being stuck a release behind for good.
pin_old_kernel() {
	OLDV=$(version_of_kernel_file "${KERNEL_OLD}" || true)
	if [ -z "${OLDV}" ]; then
		warn "no usable /boot/kernel.old, so there is nothing to fall back to"
		return 1
	fi
	if [ "${OLDV}" != "${STAMP}" ]; then
		warn "the module does not match /boot/kernel.old either, so falling back would not help"
		warn "  kernel.old : ${OLDV}"
		warn "  module     : ${STAMP:-<none>}"
		return 1
	fi
	if [ ! -x "${NEXTBOOT}" ]; then
		warn "no ${NEXTBOOT}, so the next boot cannot be steered"
		return 1
	fi
	if [ "${DRY}" = "1" ]; then
		log "DRY=1 - would send the next boot to kernel.old"
		return 0
	fi
	if ! "${NEXTBOOT}" -k kernel.old > /dev/null 2>&1; then
		warn "${NEXTBOOT} -k kernel.old failed"
		return 1
	fi
	warn "the next boot will take /boot/kernel.old, which this module matches"
	warn "  the appliance comes up with its front ports and WAN, one kernel behind"
	warn "  with the WAN back, the five-minute tick fetches, builds, and the boot after is current"
	return 0
}

unpin_old_kernel() {
	[ -x "${NEXTBOOT}" ] || return 0
	NEWV=$(version_of_kernel_file "${KERNEL}" || true)
	# Only once the module really is the one /boot/kernel wants.
	[ -n "${NEWV}" ] && [ "${NEWV}" = "${TARGET}" ] || return 0
	if [ "${DRY}" = "1" ]; then
		log "DRY=1 - would cancel any fall-back to kernel.old"
		return 0
	fi
	# Unconditional and quiet: clearing a fall-back that was never armed is a no-op, and asking
	# first would mean knowing where nextboot keeps it - which on ZFS is the bootenv, not the file.
	"${NEXTBOOT}" -D > /dev/null 2>&1 || true
	log "any fall-back to kernel.old is cleared; the next boot takes /boot/kernel"
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

# A machine with no module is a machine this driver is not installed on - the ARMADA appliance runs
# this same cron entry - so without FORCE that is the end of it.
if [ ! -f "${MODULE}" ] && [ "${FORCE}" != "1" ]; then
	log "no ${MODULE} - the driver is not installed on this machine, nothing to follow"
	exit 0
fi

if [ "${STAMP}" = "${TARGET}" ] && [ "${FORCE}" != "1" ]; then
	log "the module already matches the kernel to follow - nothing to do"
	exit 0
fi

if [ "${STAMP}" = "${TARGET}" ]; then
	log "FORCE=1 - rebuilding for the kernel the module already matches"
else
	log "the module does not match the kernel to follow; this is the window to rebuild it"
fi
if [ "${DRY}" = "1" ]; then
	log "DRY=1 - stopping here, nothing changed"
	exit 0
fi

# ---------------------------------------------------------------- the sources

if [ ! -x "${FETCH}" ]; then
	warn "no ${FETCH} - cannot fetch kernel sources, so the module cannot follow"
	pin_old_kernel || warn "the next boot will refuse it and the front ports will not come up"
	exit 1
fi

# fetch-sources.sh takes the version to pin to, prints a SYSDIR= line, and is a no-op when the tree
# is already there - so running this script twice costs nothing the second time.
#
# NOFETCH=1 is for the installer, which should not start a 350 MB download nobody asked for. It
# computes the same path fetch-sources.sh would and builds only if the tree is already there.
if [ "${NOFETCH}" = "1" ]; then
	SID=$(echo "${TARGET}" | /usr/bin/awk '{for (i = 1; i <= NF; i++) if ($i ~ /^stable\//) print $i}')
	SHA=${SID##*-}
	SERIES=${SID%%-*}
	SERIES=${SERIES#stable/}
	if [ -z "${SID}" ] || [ ! -f "/usr/src-${SERIES}-${SHA}/sys/sys/param.h" ]; then
		warn "NOFETCH=1 and no kernel sources for ${TARGET} are here; nothing built"
		warn "fetch them with: sh ${FETCH}"
		pin_old_kernel || true
		exit 1
	fi
	FETCHLOG="SYSDIR=/usr/src-${SERIES}-${SHA}/sys"
else
	FETCHLOG=$(/bin/sh "${FETCH}" "${TARGET}" 2>&1)
fi
# Leading space tolerated, and everything after the path dropped.
#
# This used to be anchored hard at `^SYSDIR=`, and fetch-sources.sh prints that line bare on the
# path where the tree is already there and - until it was fixed beside this - only inside an
# indented "  SYSDIR=... sh build.sh" hint on the path where it had just fetched one. So the
# follower worked in testing, where the tree was always already present, and failed on the one
# case it exists for: a kernel this appliance has never built for. It fetched 341 MB, verified the
# tree, reported "could not get kernel sources" and left the module unbuilt. Both ends are fixed;
# this end is the one that also covers an older fetch-sources.sh left behind by a previous install.
SYSDIR=$(echo "${FETCHLOG}" |
    /usr/bin/sed -n 's/^[[:space:]]*SYSDIR=\([^[:space:]]*\).*/\1/p' | /usr/bin/tail -1)
if [ -z "${SYSDIR}" ] || [ ! -f "${SYSDIR}/sys/param.h" ]; then
	warn "could not get kernel sources for ${TARGET}"
	echo "${FETCHLOG}" | /usr/bin/tail -8 | while read -r l; do warn "  ${l}"; done
	warn "the module cannot be built for ${TARGET} here"
	pin_old_kernel || warn "the next boot will refuse the module and the front ports will not come up"
	exit 1
fi
log "building against ${SYSDIR}"

# ---------------------------------------------------------------- the build

# FROM NOTHING, EVERY TIME, and this is the most important paragraph in the file.
#
# The first version built in a directory that persisted between builds and ran `make clean` first.
# That clean never ran. It was given no SYSDIR, so the Makefile fell back to /usr/src/sys, which does
# not exist on an OPNsense appliance; bsd.sysdir.mk stopped with `.error Unable to locate the kernel
# source tree`, and the error went to /dev/null. Nothing said so. And kmod.mk only creates the machine,
# x86 and i386 include links when they are missing - it never re-points one - while the generated
# bus_if.h, device_if.h and pci_if.h are remade only when their .m file is newer.
#
# So every build after the first compiled the new kernel's sys/ headers against the FIRST kernel's
# machine headers, and stamped the result for the new kernel. It was found by review, on the appliance:
# a module stamped p3 whose machine link pointed into the p1 tree. It was correct only because those
# two include trees happen to be byte-identical. And when the objects were newer than everything make
# could see, make rebuilt nothing at all, and the old module went out under the new kernel's stamp -
# which is the silent wrong-offset load the stamp exists to prevent.
#
# A directory emptied and refilled from the pristine sources cannot carry anything from one build to
# the next, so there is nothing for a clean to forget. And the build is checked to have compiled every
# source file, because "make exited 0" is what the old version trusted.
if [ ! -f "${SRCTREE}/Makefile" ]; then
	warn "no driver sources at ${SRCTREE} - run install.sh from os-xgs-npu to lay them down"
	exit 1
fi

/bin/rm -rf "${BUILDROOT}"
if ! /bin/mkdir -p "${BUILDROOT}"; then
	warn "cannot create ${BUILDROOT}; ${MODULE} is untouched"
	exit 1
fi
/bin/cp "${SRCTREE}"/Makefile "${SRCTREE}"/*.c "${SRCTREE}"/*.h "${BUILDROOT}/"

BUILDLOG=/var/db/os-xgs-npu/build.log
if ! ( cd "${BUILDROOT}" && /usr/bin/make SYSDIR="${SYSDIR}" ) > "${BUILDLOG}" 2>&1; then
	warn "the build failed; ${MODULE} is untouched and still matches ${STAMP:-<nothing>}"
	/usr/bin/grep -E ' error:|Error [0-9]' "${BUILDLOG}" | /usr/bin/head -10 |
	    while read -r l; do warn "  ${l}"; done
	warn "the whole log is ${BUILDLOG}"
	pin_old_kernel || true
	exit 1
fi
if [ ! -f "${BUILDROOT}/octep.ko" ]; then
	warn "the build reported success but produced no octep.ko; ${MODULE} is untouched"
	exit 1
fi
NSRC=$(/bin/ls "${BUILDROOT}"/*.c | /usr/bin/wc -l | /usr/bin/tr -d ' ')
NOBJ=$(/bin/ls "${BUILDROOT}"/*.o 2>/dev/null | /usr/bin/wc -l | /usr/bin/tr -d ' ')
if [ "${NOBJ}" -lt "${NSRC}" ]; then
	warn "only ${NOBJ} of ${NSRC} sources were compiled; refusing to install a partial build"
	pin_old_kernel || true
	exit 1
fi
MLINK=$(/usr/bin/readlink "${BUILDROOT}/machine" 2>/dev/null || true)
case "${MLINK}" in
"${SYSDIR}"/*) ;;
*)
	warn "the machine headers came from ${MLINK:-nowhere}, not from ${SYSDIR}; refusing to install"
	pin_old_kernel || true
	exit 1
	;;
esac
log "built $(/usr/bin/stat -f%z "${BUILDROOT}/octep.ko") bytes from ${NSRC} sources, machine headers from ${MLINK}"

# ---------------------------------------------------------------- install, keeping the old one

# Keep what is being replaced, with its stamp - but only when it is for a different kernel. See the
# header: a module stamped for the next kernel is wrong for the running one, and a host that boots
# the old kernel again needs the old module. The bring-up script looks for this pair.
#
# A rebuild for the SAME kernel (FORCE=1, which install.sh uses) must leave .prev alone. Copying the
# current module over it there would replace the only module for an older kernel - /boot/kernel.old
# is one boot-menu choice away - with a second copy of the module for this one.
if [ -n "${STAMP}" ] && [ "${STAMP}" != "${TARGET}" ] && [ -f "${MODULE}" ]; then
	/bin/cp -p "${MODULE}" "${MODULE}.prev" 2>/dev/null &&
	    printf '%s\n' "${STAMP}" > "${MODULE}.prev.kernel" &&
	    log "kept the previous module as ${MODULE}.prev, stamped ${STAMP}"
fi

# Write to a temporary name and move it into place, so a full disk or an interrupted copy cannot
# leave a truncated module where a working one was.
/bin/mkdir -p "${MODDIR}"
if ! /bin/cp "${BUILDROOT}/octep.ko" "${MODULE}.new"; then
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

# And if an earlier failure had sent the next boot to kernel.old, it no longer has to.
STAMP=${TARGET}
unpin_old_kernel
exit 0
