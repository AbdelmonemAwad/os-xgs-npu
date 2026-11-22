#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
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
#	DRIVER=octep|npuep ...                       which module to follow. Without it, the one that is
#	                                             installed: octep.ko on the OCTEON TX appliance,
#	                                             npuep.ko on the ARMADA one; install.sh names it
#	                                             when nothing is installed yet
#	PINONLY=1 sh install/kernel-follow.sh        only send the next boot to kernel.old, where the
#	                                             module is not built for /boot/kernel and is for
#	                                             kernel.old; no lock, no fetch, no build - the stop
#	                                             hook runs this first
#
# WHAT IT WILL NOT DO. It never replaces a working module with one that failed to build, and it never
# stamps a module it did not just compile. If anything fails it leaves what is installed exactly
# where it was and says so loudly, because an appliance that still has its ports and a warning in the
# log is better than one that has neither. The one step that cannot be taken back is the new module's
# move into place: if its stamp then cannot follow it, the module is left under a stamp that matches
# no kernel, which the bring-up refuses, and not under the old one's (see "install, keeping the old
# one").
#
# AND IT KEEPS THE PREVIOUS MODULE. The new module is stamped for the kernel on disk, which is not
# the kernel that is running while this script runs. If the host ends up booting the old kernel again
# - a rollback, a boot menu choice, an update that did not take - that stamp would be wrong in the
# other direction and the ports would be refused for the opposite reason. So the module being
# replaced is kept beside it as octep.ko.prev with its own stamp, and the bring-up script tries that
# one when the first does not match. Following the kernel forwards must not mean falling off it
# backwards.

set -u

# ONE AT A TIME. The cron entry and the upgrade hook can start this in the same minute, and two
# runs at once would build into one BUILDROOT and install under each other's stamp - worse than
# either alone. lockf(1) holds the lock for the whole run and makes a second run wait up to
# fifteen minutes for the first; the one that waited then finds the stamp already right and does
# nothing. A run that cannot get the lock in that time exits 75 (EX_TEMPFAIL) with lockf's own
# message in the log, and touches nothing.
: ${LOCK:=/var/run/octep-follow.lock}
# PINONLY=1 waits for no lock: it has to arm the fall-back to kernel.old while another run holds
# this one, and it writes no module (see "the fall-back alone" below).
: ${PINONLY:=0}
# EMMCCHECK=1 neither: it reads and writes nothing ("the upgrade's check", below).
if [ -z "${OCTEP_FOLLOW_LOCKED:-}" ] && [ "${PINONLY}" != "1" ] && [ "${EMMCCHECK:-0}" != "1" ]; then
	OCTEP_FOLLOW_LOCKED=1; export OCTEP_FOLLOW_LOCKED
	exec /usr/bin/lockf -t 900 -k "$LOCK" /bin/sh "$0" "$@"
fi

: ${PREFIX:=/usr/local}
: ${MODDIR:=/boot/modules}
# WHICH DRIVER. Two appliance families, two modules, one follower: octep.ko for the OCTEON TX
# coprocessor and npuep.ko for the ARMADA one. Each appliance has exactly one of them installed, so
# the installed one is the one to follow; the installer names it when nothing is installed yet.
# Until 2026-10-13 this file knew only octep, and on the ARMADA appliance it answered every tick
# with "the driver is not installed on this machine" while npuep.ko sat beside it unfollowed - a
# kernel update there would have loaded a module stamped for the old kernel with no check at all.
# The installer keeps it to one on both families: until 2026-11-06 it took npuep.ko off an OCTEON TX
# appliance and left an octep.ko on an ARMADA one, which this then followed instead of npuep.ko (the
# review's U11).
: ${DRIVER:=}
if [ -z "${DRIVER}" ]; then
	if [ -f "${MODDIR}/octep.ko" ]; then
		DRIVER=octep
	elif [ -f "${MODDIR}/npuep.ko" ]; then
		DRIVER=npuep
	else
		DRIVER=octep
	fi
fi
: ${MODULE:=${MODDIR}/${DRIVER}.ko}
: ${KERNEL:=/boot/kernel/kernel}
: ${SRCDIR:=${PREFIX}/opnsense/scripts/octep}
: ${FETCH:=${SRCDIR}/fetch-sources.sh}
# The driver's sources, exactly as this repository has them, laid down by install.sh. Nothing is ever
# built in here, so nothing generated can collect in it.
: ${SRCTREE:=${PREFIX}/share/os-xgs-npu/${DRIVER}}
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
# Marks the fall-back armed for this boot (emptied at boot with the rest of /var/run).
: ${PINNED:=/var/run/octep-follow.pinned}
# And one this follower armed itself, marked only once nextboot has taken it: PINNED is written as well
# by the first run of a boot that tried and could not arm one, so that it tries once a boot. Until
# 2026-11-06 nothing said which, and uninstall.sh left the fall-back armed after the package had gone
# (the review's U12); it takes back the one marked here, and no other.
: ${ARMED:=/var/run/octep-follow.armed}
# Set below when the kernel to follow is a staged set, not yet in /boot; pin_old_kernel() reads it.
STAGED=

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

# The release a kernel's version names, and its major number: 15.1-RELEASE-p6 and 15 in
# "FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP".
release_of() {
	printf '%s\n' "$1" | /usr/bin/sed -n 's/^FreeBSD \([^ ]*\).*/\1/p'
}
major_of() {
	printf '%s\n' "$1" | /usr/bin/sed -n 's/^FreeBSD \([0-9][0-9]*\).*/\1/p'
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
# and it is ONE SHOT - consumed by the boot it steers. That was taken for the right shape - nothing
# to clean up after a repair, and a power cycle trying the new kernel again rather than staying a
# release behind - and it was not (2026-10-30, an XGS 107w in issue 890): the fall-back was armed only
# when a step had failed, and spent by the boot it steered, so a reboot while the sources were still
# downloading - by the owner on the fall-back boot, or by the stop hook's time limit at shutdown -
# took the new kernel with the old module, and there were no ports. Now it is armed BEFORE the
# rebuild starts (below, "kept until the module matches") and armed again by the first run of every
# boot until the module matches /boot/kernel; unpin_old_kernel() lets go then. A firewall one
# release behind until its module is built is still a working firewall.
# NOT ACROSS A MAJOR RELEASE (2026-11-06). A major upgrade of OPNsense is one of FreeBSD as well -
# 26.1 to 26.7 is 14.3 to 15.1 - and after it /boot/kernel.old is the old release's kernel, while
# /boot/kernel and the userland the upgrade installs are the new one's. FreeBSD supports an older
# userland on a newer kernel, not a newer userland on an older one, so a boot sent to kernel.old then
# trades the module the network needs for a system it does not support. Until 2026-11-06 nothing here
# compared the two, and the stop hook's PINONLY=1 run asks for the fall-back at every orderly reboot
# (the review's P19; os-kernelfollow 1.0.3 has the same rule since that day). So pin_old_kernel()
# arms it only where the kernel it would boot is of the major release of /boot/kernel and, with a set
# still staged, of the kernel to follow; otherwise it says why not, and answers as where falling back
# would not help. The appliance then boots the new release without its front ports, a supported
# system reached on its console or on a port of the host's own where it has one, and the module is
# built for it there.
#
# across_major <the kernel a fall-back would boot, named> <its version> <version>...: true, and
# said, where one of the versions after it is of another major release; one that cannot be read is
# passed over.
across_major() {
	aw=$1 av=$2
	shift 2
	for v in "$@"; do
		[ -z "${v}" ] || [ "$(major_of "${v}")" = "$(major_of "${av}")" ] && continue
		${say} "${aw} is FreeBSD $(release_of "${av}"), of another major release than FreeBSD" \
		    "$(release_of "${v}"); the next boot is not sent to it"
		return 0
	done
	return 1
}

# pin_old_kernel [log]: its lines are errors when a step has failed (the default), and notices when
# it is armed before a rebuild as a precaution - an ordinary kernel update is not an error, and the
# problem reporter files every error line as an issue.
pin_old_kernel() {
	say=${1:-warn}
	# A KERNEL STILL STAGED is applied before the reboot that follows, and from then on the kernel in
	# /boot now is kernel.old: that is the one a boot can fall back to, and the stop hook arms it at
	# that reboot (PINONLY=1, below). Nothing is armed here, where kernel.old is still the one before.
	# Until 2026-11-06 this judged that one - two kernels back from the staged one - and an upgrade
	# whose rebuild failed was told that the next boot would refuse the module, when that boot takes
	# kernel.old with its front ports wherever the module is built for the kernel in /boot now and that
	# kernel is of the staged one's major release (across_major, above). A module built for neither is
	# judged as before.
	if [ -n "${STAGED}" ] && [ -n "${STAMP}" ] &&
	    [ "$(version_of_kernel_file "${KERNEL}" || true)" = "${STAMP}" ]; then
		across_major "once the staged kernel is applied, /boot/kernel.old" "${STAMP}" "${TARGET}" && return 1
		if [ ! -x "${NEXTBOOT}" ]; then
			${say} "no ${NEXTBOOT}, so the next boot cannot be steered"
			return 1
		fi
		${say} "once the staged kernel is applied, /boot/kernel.old is the kernel this module is built for"
		${say} "  the reboot that follows sends the next boot to it: one kernel behind, with its front ports and WAN"
		return 0
	fi
	OLDV=$(version_of_kernel_file "${KERNEL_OLD}" || true)
	if [ -z "${OLDV}" ]; then
		${say} "no usable /boot/kernel.old, so there is nothing to fall back to"
		return 1
	fi
	if [ "${OLDV}" != "${STAMP}" ]; then
		${say} "the module does not match /boot/kernel.old either, so falling back would not help"
		${say} "  kernel.old : ${OLDV}"
		${say} "  module     : ${STAMP:-<none>}"
		return 1
	fi
	across_major /boot/kernel.old "${OLDV}" "$(version_of_kernel_file "${KERNEL}" || true)" \
	    "${STAGED:+${TARGET}}" && return 1
	# On the eMMC system disk, never to a kernel the loader would load no matching fchemmc for: that
	# boot would have its front ports and no disk to boot from ("the eMMC as the system disk", below).
	if emmc_root && ! fchemmc_boots "${KERNDIR_OLD}"; then
		${say} "the system disk is the eMMC, and a boot of /boot/kernel.old would find no fchemmc built for it, so no system disk; the next boot is not sent to it"
		return 1
	fi
	if [ ! -x "${NEXTBOOT}" ]; then
		${say} "no ${NEXTBOOT}, so the next boot cannot be steered"
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
	: > "${ARMED}" 2>/dev/null || true
	${say} "the next boot will take /boot/kernel.old, which this module matches"
	${say} "  the appliance comes up with its front ports and WAN, one kernel behind"
	${say} "  with the WAN back, the five-minute tick fetches, builds, and the boot after is current"
	return 0
}

# LET GO WHEREVER THE MODULE IS BUILT FOR THE KERNEL THE NEXT BOOT TAKES (2026-11-17). Until then a
# fall-back this follower armed was let go by one run alone: one that rebuilt the module while the kernel
# to follow was already in /boot/kernel. Three runs that leave the module built for the kernel the next
# boot takes left it standing: a rebuild for a kernel still staged, a run that finds the module already
# built for the kernel to follow, and PINONLY=1 at a reboot that finds it built for /boot/kernel. A boot
# that had fallen back to kernel.old and then took an upgrade showed what that costs (the review of
# 2026-11-17, measured on copies of this file): the upgrade hook rebuilt the module for the staged
# kernel, the update applied that kernel - so kernel.old became the one in /boot/kernel before it - and
# the reboot after it still went to kernel.old, with the module built for the staged kernel and the one
# kept beside it, .prev, for the kernel before both. The bring-up refuses both: no front ports, no WAN.
# Each of those runs now lets it go too, where this follower armed it (${ARMED}); one somebody else
# armed - os-kernelfollow, for a module of its own - is left to them, as before. A module built for a
# kernel still staged is built for the kernel the next boot takes, as everywhere in this file: the update
# applies the staged kernel before the reboot that follows (OPNsense's scripts/firmware/upgrade.sh,
# "pending kernel applies before reboot").
unpin_old_kernel() {
	[ -x "${NEXTBOOT}" ] || return 0
	# On the eMMC system disk the fall-back stays while the next boot would find no fchemmc built for its
	# kernel, whatever the module above is built for ("the eMMC as the system disk", below).
	if emmc_root && ! fchemmc_next_ok; then
		return 0
	fi
	NEWV=$(version_of_kernel_file "${KERNEL}" || true)
	if [ -n "${NEWV}" ] && [ "${NEWV}" = "${TARGET}" ]; then
		# The module really is the one /boot/kernel wants: any fall-back goes, as it always has.
		gone="any fall-back to kernel.old is cleared; the next boot takes /boot/kernel"
	elif [ -n "${STAGED}" ] && [ "${STAMP}" = "${TARGET}" ] && [ -f "${ARMED}" ]; then
		gone="the fall-back to kernel.old this follower armed is cleared; the next boot takes the staged kernel, which the update applies before it"
	else
		return 0
	fi
	if [ "${DRY}" = "1" ]; then
		log "DRY=1 - would cancel any fall-back to kernel.old"
		return 0
	fi
	# Unconditional and quiet: clearing a fall-back that was never armed is a no-op, and asking
	# first would mean knowing where nextboot keeps it - which on ZFS is the bootenv, not the file.
	"${NEXTBOOT}" -D > /dev/null 2>&1 || true
	/bin/rm -f "${PINNED}" "${ARMED}"
	log "${gone}"
}

# What a failed step leaves for the next boot. With no module installed at all - a first install that
# could not build one - there is nothing to refuse and nothing to fall back with: until 2026-11-05 such
# a run said "the next boot will refuse the module", and an XGS 2300's report carried that line from its
# installer's first try, which the second - fetching the sources - followed with a module (#818).
no_fallback() {
	if [ ! -f "${MODULE}" ]; then
		warn "no ${MODULE} is installed, so the next boot brings no front ports up"
		return 0
	fi
	pin_old_kernel || warn "$1"
}

# ---------------------------------------------------------------- the eMMC as the system disk
#
# fchemmc.ko, the driver of the XGS 87's eMMC controller (contrib/fchemmc, AMD FCH 1022:7906), is built
# by this follower beside the module above ("the eMMC driver beside it", below). Where the eMMC holds
# data only it is not critical, and nothing in this section does anything. WHERE THE SYSTEM DISK IS THE
# eMMC it is: the kernel's own sdhci_pci leaves that controller unusable, so a kernel booted without an
# fchemmc built for it - the module does not probe on any other kernel - has no root disk, and the
# appliance does not boot at all. There, and only there, fchemmc is treated as the boot needs it:
#
#	- the next boot is never sent to a kernel the loader would load no matching fchemmc for while
#	  another kernel has one: the fall-back - to kernel.old, or to kernel.emmc-good where kernel.old
#	  has none - is armed before a rebuild, again at every boot and at every orderly reboot, and let
#	  go only once fchemmc is built for /boot/kernel - and never let go for the module above alone;
#	- all of this whether or not the module above is installed or builds: fchemmc is followed first;
#	- the last kernel that booted from the eMMC is kept whole, with its fchemmc, in
#	  /boot/kernel.emmc-good, which no kernel update rotates, and replaced only once a newer kernel
#	  has booted from the eMMC: two kernel updates with no build between them leave neither
#	  /boot/kernel nor kernel.old with an fchemmc, and this one still has;
#	- a small guard that needs nothing of the plugin (install/fchemmc-guard.sh) is laid down in
#	  /boot/fchemmc-guard with a boot hook early and one at stop: it arms the fall-back again at each
#	  boot, has the last word at a reboot, and stays when the plugin is removed;
#	- the fall-back is never armed for the module above where kernel.old would find no fchemmc: a boot
#	  without front ports is still a boot, one without a system disk is not;
#	- an upgrade whose staged kernel fchemmc could not be built for is stopped before that kernel is
#	  applied (EMMCCHECK=1, run by the upgrade hook; OPNsense's upgrade.sh applies the kernel only
#	  when every upgrade hook succeeded);
#	- the build that is replaced is kept, for the kernel it was built for, inside that kernel's own
#	  directory.
#
# WHERE THE BUILDS LIVE, AND WHY IN THE KERNEL'S DIRECTORY. The loader, not anything here, loads
# fchemmc, and it looks for a module in the booted kernel's own directory before /boot/modules: lua's
# config.loadKernel() sets module_path to "/boot/<kernel>;" and the rest, and mod_searchmodule() takes
# the first directory that has the module, by its linker.hints or, where they do not name it, by the
# file name fchemmc.ko (stand/common/module.c). /boot/modules holds one build - for the kernel to follow
# - so a boot of kernel.old would take that one, built for another kernel, and find no disk. A kernel
# update moves the whole directory: opnsense-update's install_kernel() unpacks the new kernel into
# /boot/kernel.new, removes /boot/kernel.old, renames /boot/kernel to /boot/kernel.old and kernel.new to
# kernel. So a build kept as /boot/kernel.old/fchemmc.ko, or as /boot/kernel/fchemmc.ko just before that
# rename, stays with the kernel it was built for, and the next kernel update removes it with that kernel.
# It is put there only for the kernel that is or is about to become kernel.old (fchemmc_keep, from the
# build that replaces it), and taken out of /boot/kernel again where /boot/modules has the same kernel's
# build: OPNsense's health check compares /boot/kernel's linker.hints with the kernel set's own
# (opnsense/tools build/kernel.sh, scripts/firmware/health.sh), and opnsense-update runs kldxref on
# /boot/kernel at every base update, which would add a module left there. Nothing here runs kldxref.
#
# Asked only where something of fchemmc is on this machine and a 1022:7906 is on the bus, so every other
# appliance spends a few file tests on it. EMMCROOT=1 or 0 answers the question for testing; otherwise
# system-disk.sh does (scripts/xgs).
: ${SYSTEMDISK:=${PREFIX}/opnsense/scripts/xgs/system-disk.sh}
: ${FCHEMMC_SRCTREE:=${PREFIX}/share/os-xgs-npu/fchemmc}
: ${FCHEMMC_MODULE:=${MODDIR}/fchemmc.ko}
: ${FCHEMMC_BUILDROOT:=/var/db/os-xgs-npu/build-fchemmc}
: ${FCHEMMC_BUILDLOG:=/var/db/os-xgs-npu/build-fchemmc.log}
: ${FCHEMMC_SAID:=/var/db/os-xgs-npu/fchemmc-follow.said}
: ${FCHEMMC_LOADER:=/boot/loader.conf.d/fchemmc.conf}
# Marks that this boot has tried a build that failed before, so that it is tried once a boot.
: ${FCHEMMC_TRIED:=/var/run/octep-follow.fchemmc-tried}
: ${PCICONF:=/usr/sbin/pciconf}
KERNDIR=${KERNEL%/*}
KERNDIR_OLD=${KERNEL_OLD%/*}
: ${KERNDIR_GOOD:=${KERNDIR%/*}/kernel.emmc-good}
# Puts kernel.emmc-good on the boot menu's Kernel entry (k): the loader lists only what `kernels` names.
: ${FCHEMMC_GOODCONF:=/boot/loader.conf.d/fchemmc-good.conf}
: ${GOOD_FAILED:=/var/run/octep-follow.emmc-good-failed}
: ${GUARD_SRC:=${SRCDIR}/fchemmc-guard.sh}
: ${GUARD_DIR:=/boot/fchemmc-guard}
: ${HOOKROOT:=${PREFIX}/etc/rc.syshook.d}

EMMC=
emmc_root() {
	if [ -z "${EMMC}" ]; then
		case "${EMMCROOT:-}" in
		1|0) EMMC=${EMMCROOT} ;;
		*)
			EMMC=0
			if { [ -f "${FCHEMMC_MODULE}" ] || [ -f "${KERNDIR}/fchemmc.ko" ] ||
			    [ -f "${KERNDIR_OLD}/fchemmc.ko" ] || [ -d "${KERNDIR_GOOD}" ]; } &&
			    [ -f "${SYSTEMDISK}" ] && ${PCICONF} -l 2>/dev/null |
			    /usr/bin/grep -qE 'chip=0x79061022|vendor=0x1022[[:space:]]+device=0x7906' &&
			    /bin/sh "${SYSTEMDISK}" -e > /dev/null 2>&1; then
				EMMC=1
			fi
			;;
		esac
	fi
	[ "${EMMC}" = 1 ]
}

# The stamp of the fchemmc the loader takes for a boot of the kernel in <directory>: the build in that
# directory where there is one, the one in /boot/modules otherwise; nothing where there is neither.
fchemmc_taken() {
	if [ -f "$1/fchemmc.ko" ]; then
		/bin/cat "$1/fchemmc.ko.kernel" 2>/dev/null
	elif [ -f "${FCHEMMC_MODULE}" ]; then
		/bin/cat "${FCHEMMC_MODULE}.kernel" 2>/dev/null
	fi
	return 0
}
# True where a boot of the kernel in <directory> finds an fchemmc built for it.
fchemmc_boots() {
	fb=$(version_of_kernel_file "$1/kernel" || true)
	[ -n "${fb}" ] && [ "$(fchemmc_taken "$1")" = "${fb}" ]
}
# True where the next boot finds its system disk: the kernel the next boot takes is /boot/kernel, or the
# staged kernel once the update has applied it, whose directory holds no build of fchemmc yet.
fchemmc_next_ok() {
	if [ -n "${STAGED}" ]; then
		[ -f "${FCHEMMC_MODULE}" ] && [ "$(/bin/cat "${FCHEMMC_MODULE}.kernel" 2>/dev/null)" = "${TARGET}" ]
	else
		fchemmc_boots "${KERNDIR}"
	fi
}

# Before /boot/modules/fchemmc.ko is replaced by a build for another kernel: the build there is kept in
# the directory of the kernel it is built for, where that is /boot/kernel or /boot/kernel.old and that
# directory has no build of its own for it yet. False, and said, where it cannot be kept; the new build
# must then not replace it.
fchemmc_keep() {
	emmc_root || return 0
	[ -f "${FCHEMMC_MODULE}" ] || return 0
	kept=$(/bin/cat "${FCHEMMC_MODULE}.kernel" 2>/dev/null || true)
	[ -n "${kept}" ] && [ "${kept}" != "${TARGET}" ] || return 0
	for kd in "${KERNDIR}" "${KERNDIR_OLD}"; do
		[ "$(version_of_kernel_file "${kd}/kernel" || true)" = "${kept}" ] || continue
		[ -f "${kd}/fchemmc.ko" ] && [ "$(/bin/cat "${kd}/fchemmc.ko.kernel" 2>/dev/null)" = "${kept}" ] && continue
		if ! { /bin/cp "${FCHEMMC_MODULE}" "${kd}/fchemmc.ko.new" &&
		    printf '%s\n' "${kept}" > "${kd}/fchemmc.ko.kernel.new" &&
		    /bin/mv "${kd}/fchemmc.ko.kernel.new" "${kd}/fchemmc.ko.kernel" &&
		    /bin/mv "${kd}/fchemmc.ko.new" "${kd}/fchemmc.ko"; } 2>/dev/null; then
			/bin/rm -f "${kd}/fchemmc.ko.new" "${kd}/fchemmc.ko.kernel.new"
			warn "fchemmc: could not keep the build for ${kept} in ${kd}; the system disk is the eMMC, so it stays in ${FCHEMMC_MODULE} and the new build is not installed"
			return 1
		fi
		/bin/chmod 0555 "${kd}/fchemmc.ko" 2>/dev/null || true
		log "fchemmc: kept the build for ${kept} in ${kd}, where a boot of that kernel finds it"
	done
	return 0
}

# Which kernel a boot can fall back to with its system disk: kernel.old where it has its fchemmc,
# kernel.emmc-good otherwise. Nothing, and false, where neither has.
emmc_fallback() {
	if fchemmc_boots "${KERNDIR_OLD}"; then
		echo "${KERNDIR_OLD##*/}"
	elif fchemmc_boots "${KERNDIR_GOOD}"; then
		echo "${KERNDIR_GOOD##*/}"
	else
		return 1
	fi
}

# The fall-back for the system disk, whatever the module above is built for - a boot without front ports
# is still a boot - and across a major release too: a boot with its disk can be repaired, and one
# without cannot. Its lines are notices; a step that failed says so as an error.
pin_for_emmc() {
	say=${1:-warn}
	if ! to=$(emmc_fallback); then
		warn "fchemmc: the system disk is the eMMC, and no kernel in /boot has an fchemmc built for it where the loader looks: the next boot finds no system disk"
		return 1
	fi
	if [ ! -x "${NEXTBOOT}" ]; then
		warn "no ${NEXTBOOT}, so the next boot cannot be steered away from a kernel without its system disk"
		return 1
	fi
	if [ "${DRY}" = "1" ]; then
		log "DRY=1 - would send the next boot to ${to}, which has its fchemmc"
		return 0
	fi
	if ! "${NEXTBOOT}" -k "${to}" > /dev/null 2>&1; then
		warn "${NEXTBOOT} -k ${to} failed"
		return 1
	fi
	/usr/bin/touch "${ARMED}" 2>/dev/null || true
	${say} "fchemmc: the system disk is the eMMC and fchemmc is not built for ${KERNEL}: the next boot takes /boot/${to}, which has its fchemmc"
	tov=$(version_of_kernel_file "${KERNDIR%/*}/${to}/kernel" || true)
	if [ -f "${MODULE}" ] && [ "$(/bin/cat "${MODULE}.kernel" 2>/dev/null)" != "${tov}" ] &&
	    [ "$(/bin/cat "${MODULE}.prev.kernel" 2>/dev/null)" != "${tov}" ]; then
		${say} "  ${MODULE} is not built for ${to}, so that boot may come up without its front ports; it has its system disk"
	fi
	return 0
}

# The last kernel that booted from the eMMC, kept whole with its fchemmc where no kernel update rotates
# it. Replaced only by the kernel running now, where that is /boot/kernel and has its fchemmc: being up
# on an eMMC root is the proof that it boots from there. Built beside and swapped in by renames, so the
# one before stays until the new one is complete, and a swap cut off half way is put right by the next
# run. The whole directory, because a kernel loads its other modules from it.
emmc_good_keep() {
	[ "${DRY}" = "1" ] && return 0
	if [ ! -d "${KERNDIR_GOOD}" ] && [ -d "${KERNDIR_GOOD}.del" ]; then
		/bin/mv "${KERNDIR_GOOD}.del" "${KERNDIR_GOOD}"
	fi
	/bin/rm -rf "${KERNDIR_GOOD}.new" "${KERNDIR_GOOD}.del"
	gv=$(${UNAME} -v 2>/dev/null || true)
	[ -n "${gv}" ] && [ "$(version_of_kernel_file "${KERNEL}" || true)" = "${gv}" ] || return 0
	fchemmc_boots "${KERNDIR}" || return 0
	if ! { [ "$(version_of_kernel_file "${KERNDIR_GOOD}/kernel" || true)" = "${gv}" ] &&
	    fchemmc_boots "${KERNDIR_GOOD}"; }; then
		gfrom=${FCHEMMC_MODULE}
		[ -f "${KERNDIR}/fchemmc.ko" ] && gfrom=${KERNDIR}/fchemmc.ko
		if { /bin/cp -Rp "${KERNDIR}" "${KERNDIR_GOOD}.new" &&
		    /bin/rm -f "${KERNDIR_GOOD}.new/fchemmc.ko" "${KERNDIR_GOOD}.new/fchemmc.ko.kernel" &&
		    /bin/cp "${gfrom}" "${KERNDIR_GOOD}.new/fchemmc.ko" &&
		    printf '%s\n' "${gv}" > "${KERNDIR_GOOD}.new/fchemmc.ko.kernel" &&
		    fchemmc_boots "${KERNDIR_GOOD}.new"; } 2>/dev/null; then
			[ ! -d "${KERNDIR_GOOD}" ] || /bin/mv "${KERNDIR_GOOD}" "${KERNDIR_GOOD}.del"
			/bin/mv "${KERNDIR_GOOD}.new" "${KERNDIR_GOOD}"
			/bin/rm -rf "${KERNDIR_GOOD}.del"
			/bin/rm -f "${GOOD_FAILED}"
			log "fchemmc: ${KERNDIR_GOOD} now holds ${gv}, the last kernel that booted from the eMMC"
		else
			/bin/rm -rf "${KERNDIR_GOOD}.new"
			if [ ! -f "${GOOD_FAILED}" ]; then
				/usr/bin/touch "${GOOD_FAILED}" 2>/dev/null || true
				warn "fchemmc: could not keep a copy of the running kernel in ${KERNDIR_GOOD}; what was there stays"
			fi
		fi
	fi
	if [ -d "${KERNDIR_GOOD}" ] && [ ! -f "${FCHEMMC_GOODCONF}" ]; then
		{ /bin/mkdir -p "${FCHEMMC_GOODCONF%/*}" &&
		    printf 'kernels="kernel kernel.old %s"\n' "${KERNDIR_GOOD##*/}" > "${FCHEMMC_GOODCONF}"; } 2>/dev/null || true
	fi
	return 0
}

# The guard (install/fchemmc-guard.sh) and its two hooks, written where they differ.
emmc_guard_put() {
	[ "${DRY}" = "1" ] && return 0
	[ -f "${GUARD_SRC}" ] && [ -f "${SYSTEMDISK}" ] || return 0
	/bin/mkdir -p "${GUARD_DIR}" "${HOOKROOT}/early" "${HOOKROOT}/stop" 2>/dev/null || return 0
	/usr/bin/cmp -s "${GUARD_SRC}" "${GUARD_DIR}/guard.sh" ||
	    /bin/cp "${GUARD_SRC}" "${GUARD_DIR}/guard.sh" 2>/dev/null
	/usr/bin/cmp -s "${SYSTEMDISK}" "${GUARD_DIR}/system-disk.sh" ||
	    /bin/cp "${SYSTEMDISK}" "${GUARD_DIR}/system-disk.sh" 2>/dev/null
	for gh in early/04-fchemmc-guard stop/79-fchemmc-guard; do
		gline="exec /bin/sh ${GUARD_DIR}/guard.sh"
		[ "$(/usr/bin/tail -1 "${HOOKROOT}/${gh}" 2>/dev/null)" = "${gline}" ] && continue
		printf '#!/bin/sh\n# The eMMC system disk: see %s/guard.sh\n%s\n' "${GUARD_DIR}" "${gline}" \
		    > "${HOOKROOT}/${gh}" 2>/dev/null && /bin/chmod 0755 "${HOOKROOT}/${gh}"
	done
	return 0
}

# After a whole run, on the eMMC system disk with no kernel staged: where fchemmc is built for
# /boot/kernel, a fall-back this follower armed can go (unpin_old_kernel, which looks at the module above
# too) and a build of the same kernel's left in /boot/kernel is taken out; where it is not, the fall-back
# is armed, once a boot.
emmc_after() {
	emmc_root || return 0
	[ -z "${STAGED}" ] || return 0
	[ "${DRY}" = "1" ] && return 0
	if fchemmc_boots "${KERNDIR}"; then
		if [ -f "${KERNDIR}/fchemmc.ko" ] &&
		    [ "$(/bin/cat "${FCHEMMC_MODULE}.kernel" 2>/dev/null)" = "$(/bin/cat "${KERNDIR}/fchemmc.ko.kernel" 2>/dev/null)" ]; then
			/bin/rm -f "${KERNDIR}/fchemmc.ko" "${KERNDIR}/fchemmc.ko.kernel"
		fi
		[ -f "${ARMED}" ] && unpin_old_kernel
	elif [ ! -f "${PINNED}" ]; then
		pin_for_emmc log || true
		/usr/bin/touch "${PINNED}" 2>/dev/null || true
	fi
	return 0
}

# ---------------------------------------------------------------- the upgrade's check, for the upgrade hook
#
# EMMCCHECK=1 answers one question and changes nothing: on the eMMC system disk, is fchemmc built for the
# kernel an upgrade has staged? 0 yes, or no kernel is staged, or the system disk is not the eMMC; 3 no,
# with the reason - a code of its own, so that nothing else that can stop this run (a time limit) can
# read as that answer. A kernel already in /boot is past holding: the fall-back above takes it from there.
# The upgrade hook runs it after the follower and fails where it says 3, and OPNsense's upgrade.sh then
# applies no kernel, empties what it staged and does not reboot ("The upgrade was aborted due to an
# error"). No lock: it reads, as PINONLY=1 does.
: ${EMMCCHECK:=0}

# ---------------------------------------------------------------- the fall-back alone, before a reboot
#
# PINONLY=1 arms the fall-back and does nothing else: no lock, no sources, no build. The stop hook
# runs it at every orderly reboot, before the whole follower, and says what it answered.
#
# Until 2026-11-06 only the run holding the lock could arm it, and only a run that found the new
# kernel already in /boot ("kept until the module matches", below). A major upgrade over a slow line
# fell between the two. A run that started while the kernel was still a staged set - a cron tick's,
# in the middle of the upgrade - arms nothing, because kernel.old is then the kernel two back, and it
# was still fetching sources when the host rebooted; the stop hook's run waited for its lock until
# the time limit cut it off, before it reached the fall-back. The next boot took the new kernel with
# a module built for the old one: refused, so no front ports, the WAN among them.
#
# No lock, because nothing here needs one: it writes no module, and the one thing it changes is the
# nextboot setting, which pin_old_kernel() makes only for a module built for kernel.old, and
# unpin_old_kernel() takes away here only where the module is built for /boot/kernel and this follower
# armed it. If a run holding the lock installs the rebuilt module and clears the fall-back just as this
# arms it, the next boot takes kernel.old with the module that run replaced - kept as .prev, stamped for
# kernel.old, and loaded by the bring-up for exactly that case - one boot behind, ports and all.
#
# /boot/kernel and not a staged set, because the question is what the loader boots: with a kernel
# still staged, /boot/kernel is the one running and kernel.old the one before it.
#
# The answer is the exit status, so that the stop hook says only what is so: 0 the next boot is sent
# to kernel.old; 2 the module is built for /boot/kernel, nothing is armed, and a fall-back this
# follower armed is let go (since 2026-11-17: until then it stood, and the stop hook said the module
# was built for the kernel the next boot takes of a boot that took kernel.old); 3 no module is
# installed; 1 the module is not built for /boot/kernel and the next boot could not be sent to
# kernel.old, for the reason in the lines before it.
if [ "${PINONLY}" = "1" ]; then
	# On the eMMC system disk, fchemmc first: a boot of a kernel without it has no disk to boot from.
	if emmc_root && ! fchemmc_boots "${KERNDIR}"; then
		log "the system disk is the eMMC, and fchemmc is not built for ${KERNEL}, the kernel the next boot takes"
		log "  kernel     : $(version_of_kernel_file "${KERNEL}" || echo '<unreadable>')"
		taken=$(fchemmc_taken "${KERNDIR}")
		log "  fchemmc    : ${taken:-<none>}"
		if pin_for_emmc log; then
			[ "${DRY}" = "1" ] || /usr/bin/touch "${PINNED}" 2>/dev/null || true
			exit 0
		fi
		exit 1
	fi
	if [ ! -f "${MODULE}" ]; then
		# With no module above to want it, a fall-back armed for fchemmc goes once fchemmc is built.
		if emmc_root && [ -f "${ARMED}" ]; then
			TARGET=$(version_of_kernel_file "${KERNEL}" || true)
			unpin_old_kernel
		fi
		echo "no ${MODULE} - the driver is not installed on this machine, nothing to fall back with"
		exit 3
	fi
	STAMP=$(/bin/cat "${MODULE}.kernel" 2>/dev/null || true)
	NEXTV=$(version_of_kernel_file "${KERNEL}" || true)
	if [ -z "${NEXTV}" ]; then
		warn "cannot read a kernel version from ${KERNEL}, so the next boot is left as it is"
		exit 1
	fi
	if [ "${NEXTV}" = "${STAMP}" ]; then
		if [ -f "${ARMED}" ]; then
			TARGET=${NEXTV}
			unpin_old_kernel
		fi
		echo "the module is built for ${KERNEL}, the kernel the next boot takes - nothing to arm"
		exit 2
	fi
	log "the module is not built for ${KERNEL}, the kernel the next boot takes"
	log "  kernel     : ${NEXTV}"
	log "  module     : ${STAMP:-<none>}"
	if pin_old_kernel log; then
		[ "${DRY}" = "1" ] || : > "${PINNED}" 2>/dev/null || true
		exit 0
	fi
	exit 1
fi

# ---------------------------------------------------------------- the sources of kernels gone by
#
# Every kernel this appliance follows leaves its sources behind: fetch-sources.sh unpacks them into
# /usr/src-<series>-<sha>, and an image carries its kernel's archive in the pool. Nothing removed them
# (uninstall.sh says so), so each OPNsense update added hundreds of megabytes that stayed.
#
# They go, but only once the new kernel has proven itself - the owner's condition, 2026-10-21: it is
# the kernel running, the module is stamped for it, the driver is attached, and the boot is a day old
# (SETTLE). Until then the old sources are what a rebuild for the old kernel would need. After it,
# nothing does: a fall-back to kernel.old takes ${MODULE}.prev, kept with its own stamp, and needs no
# sources at all. The running kernel's own sources always stay - an update of this project rebuilds
# the driver against them.
: ${SETTLE:=86400}
: ${SRCPOOL:=${PREFIX}/share/os-xgs-npu/kernel-sources}
# Where fetch-sources.sh unpacks them (its DST), and the two commands read here - each a variable, as
# NEXTBOOT is, so the pruning can be run against a tree that is not this machine's.
: ${SRCROOT:=/usr}
: ${SYSCTL:=/sbin/sysctl}
: ${UNAME:=/usr/bin/uname}

prune_old_sources() {
	[ "${RUNNING}" = "${TARGET}" ] || return 0
	[ -n "$(${SYSCTL} -n "dev.${DRIVER}.0.%desc" 2>/dev/null)" ] || return 0
	booted=$(${SYSCTL} -n kern.boottime 2>/dev/null | /usr/bin/sed -n 's/^{ sec = \([0-9]*\),.*/\1/p')
	[ -n "${booted}" ] || return 0
	[ $(( $(/bin/date +%s) - booted )) -ge "${SETTLE}" ] || return 0
	keep=$(printf '%s\n' "${TARGET}" | /usr/bin/sed -n 's/.*-n[0-9]*-\([0-9a-f][0-9a-f]*\).*/\1/p')
	[ -n "${keep}" ] || return 0
	for tree in "${SRCROOT}"/src-*-*; do
		# only a tree fetch-sources.sh made - its name, src-<series>-<commit>, and its sys/ - and not
		# this kernel's
		printf '%s\n' "${tree##*/}" | /usr/bin/grep -Eq '^src-[0-9][0-9.]*-[0-9a-f]{7,40}$' || continue
		[ -f "${tree}/sys/sys/param.h" ] || continue
		case "${tree}" in *"-${keep}") continue ;; esac
		if [ "${DRY}" = "1" ]; then
			log "DRY=1 - would remove ${tree}, the sources of a kernel this appliance no longer runs"
		elif /bin/rm -rf "${tree}"; then
			log "removed ${tree}: the sources of a kernel this appliance no longer runs"
		fi
	done
	for archive in "${SRCPOOL}"/*.tar.gz "${SRCPOOL}"/*.tgz; do
		[ -f "${archive}" ] || continue
		case "${archive##*/}" in *"${keep}"*) continue ;; esac
		if [ "${DRY}" = "1" ]; then
			log "DRY=1 - would remove ${archive}, a carried archive of a kernel no longer run"
		elif /bin/rm -f "${archive}"; then
			log "removed ${archive}: a carried archive of a kernel no longer run"
		fi
	done
	return 0
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

# ---------------------------------------------------------------- what the last run found, for the pages
#
# XGS: Updates says whether the driver is built for the kernel the next boot takes, and what this
# follower last did (scripts/xgs/update.py, driver), and os-kernelfollow's status (its status.py)
# shows this follower's module beside its own: both read this record of the last whole run - not DRY=1,
# not PINONLY=1 - written as os-kernelfollow records each of its own modules. One KEY=value a line: the
# module followed; one word for what was found or done - ok (it matches the kernel to follow), built
# (rebuilt and installed for it), failed (not, for the reason in message) or skipped (not installed); the
# line that says why; the kernel followed; when, in seconds since the epoch; and built_at, when this
# follower last built and installed the module, carried from one record to the next while that module is
# installed. Until 2026-11-17 a run left its lines in the log and the build's own log alone, and nothing
# said when the module was built. Written beside and moved into place, and never a reason for a run to
# stop.
: ${RESULT:=/var/db/os-xgs-npu/kernel-follow.result}
recorded() {
	/usr/bin/sed -n "s/^$1=//p" "${RESULT}" 2>/dev/null | /usr/bin/tail -1
}
record() {
	[ "${DRY}" = "1" ] && return 0
	rec_at=$(/bin/date +%s)
	rec_built=
	if [ "$1" = built ]; then
		rec_built=${rec_at}
	elif [ -f "${MODULE}" ] && [ "$(recorded module)" = "${MODULE}" ]; then
		rec_built=$(recorded built_at)
	fi
	{ /bin/mkdir -p "${RESULT%/*}" &&
	    printf 'module=%s\nresult=%s\nmessage=%s\ntarget=%s\nat=%s\nbuilt_at=%s\n' "${MODULE}" "$1" "$2" \
	        "${TARGET}" "${rec_at}" "${rec_built}" > "${RESULT}.new" &&
	    /bin/mv "${RESULT}.new" "${RESULT}"; } 2>/dev/null
	return 0
}

# ---------------------------------------------------------------- the eMMC driver beside it
#
# fchemmc.ko, the driver of the XGS 87's eMMC controller (contrib/fchemmc, AMD FCH 1022:7906), follows
# the same kernel with the same sources, after the module above - but it is not that module. Where the
# eMMC holds data only, nothing here is critical: a build that fails, or sources that are not here, are
# said once for the kernel to follow and leave the boot as it is, with no fall-back to kernel.old. Where
# the system disk is the eMMC, "the eMMC as the system disk" above decides what the next boot takes. The
# loader loads fchemmc.ko before anything can check a stamp, so the module carries the version line of
# the kernel it was built for and does not probe on any other (fchemmc.c, fchemmc_kernel_matches): a
# boot of a kernel it was not rebuilt for leaves the controller to the built-in sdhci_pci. The stamp
# beside it is for this follower only, to know when to build. Its sources are laid down by install.sh
# only where a 1022:7906 controller is, so on every other appliance this does nothing at all.
# Its paths are set in "the eMMC as the system disk", above.

# Said once for each kernel to follow and outcome, and at every FORCE=1 (an install, which reports it);
# a build that failed for this kernel is not tried again every five minutes, only at FORCE=1 or for
# another kernel - and, on the eMMC system disk, once at each boot and at each upgrade and reboot
# (FCHEMMC_RETRY=1, from their hooks), because there "run the upgrade again" has to mean a new try.
fchemmc_said() {
	[ "${FORCE}" != "1" ] && [ "$(/bin/cat "${FCHEMMC_SAID}" 2>/dev/null)" = "${TARGET}|$1" ] && return 0
	[ "${DRY}" = "1" ] || { /bin/mkdir -p "${FCHEMMC_SAID%/*}" &&
	    printf '%s\n' "${TARGET}|$1" > "${FCHEMMC_SAID}"; } 2>/dev/null
	return 1
}

# The loader line, beside a module and never without one: a line for a missing module is an error at
# every boot.
fchemmc_loader() {
	if [ -f "${FCHEMMC_MODULE}" ]; then
		[ -f "${FCHEMMC_LOADER}" ] && return 0
		{ /bin/mkdir -p "${FCHEMMC_LOADER%/*}" &&
		    printf '%s\n' 'fchemmc_load="YES"' > "${FCHEMMC_LOADER}"; } 2>/dev/null &&
		    log "fchemmc: ${FCHEMMC_LOADER} loads it at the next boot"
	else
		/bin/rm -f "${FCHEMMC_LOADER}"
	fi
}

# $1: SYSDIR when the module above was just built against it; empty to look for the sources here only.
# Whatever the build does, the loader line is then made to agree with what is in ${MODDIR}.
follow_fchemmc() {
	[ -f "${FCHEMMC_SRCTREE}/Makefile" ] || return 0
	build_fchemmc "$@"
	[ "${DRY}" = "1" ] || fchemmc_loader
	return 0
}
# True, once a run, where a build that failed before is to be tried again.
FCHEMMC_RETRIED=0
fchemmc_retry() {
	emmc_root || return 1
	[ "${FCHEMMC_RETRIED}" = 0 ] || return 1
	[ "${FCHEMMC_RETRY:-0}" = 1 ] || [ ! -f "${FCHEMMC_TRIED}" ] || return 1
	FCHEMMC_RETRIED=1
	/usr/bin/touch "${FCHEMMC_TRIED}" 2>/dev/null || true
	return 0
}
build_fchemmc() {
	fstamp=$(/bin/cat "${FCHEMMC_MODULE}.kernel" 2>/dev/null || true)
	if [ -f "${FCHEMMC_MODULE}" ] && [ "${fstamp}" = "${TARGET}" ] && [ "${FORCE}" != "1" ]; then
		return 0
	fi
	if [ "${DRY}" = "1" ]; then
		log "DRY=1 - would build fchemmc.ko for ${TARGET}"
		return 0
	fi
	if [ "${FORCE}" != "1" ] &&
	    [ "$(/bin/cat "${FCHEMMC_SAID}" 2>/dev/null)" = "${TARGET}|failed" ] && ! fchemmc_retry; then
		return 0
	fi
	fsys=${1:-}
	if [ -z "${fsys}" ]; then
		# On the eMMC system disk this run may be the only one that builds it, so it may fetch.
		if emmc_root && [ "${NOFETCH}" != "1" ]; then
			fsys=$(/bin/sh "${FETCH}" "${TARGET}" 2>/dev/null |
			    /usr/bin/sed -n 's/^[[:space:]]*SYSDIR=\([^[:space:]]*\).*/\1/p' | /usr/bin/tail -1)
		else
			fsys=$(NONET=1 /bin/sh "${FETCH}" "${TARGET}" 2>/dev/null |
			    /usr/bin/sed -n 's/^[[:space:]]*SYSDIR=\([^[:space:]]*\).*/\1/p' | /usr/bin/tail -1)
		fi
	fi
	if [ -z "${fsys}" ] || [ ! -f "${fsys}/sys/param.h" ]; then
		fchemmc_said nosources ||
		    warn "fchemmc: no kernel sources for ${TARGET} here; the eMMC driver is not rebuilt, and a boot of that kernel leaves the eMMC to the built-in driver"
		return 0
	fi
	case "${TARGET}" in
	*'"'*|*'\'*)
		fchemmc_said badversion || warn "fchemmc: the kernel's version line holds a quote or a backslash; not built"
		return 0
		;;
	esac
	/bin/rm -rf "${FCHEMMC_BUILDROOT}"
	if ! /bin/mkdir -p "${FCHEMMC_BUILDROOT}"; then
		warn "fchemmc: cannot create ${FCHEMMC_BUILDROOT}; ${FCHEMMC_MODULE} is untouched"
		return 0
	fi
	for f in "${FCHEMMC_SRCTREE}"/Makefile "${FCHEMMC_SRCTREE}"/*.c "${FCHEMMC_SRCTREE}"/*.h; do
		[ -f "${f}" ] && /bin/cp "${f}" "${FCHEMMC_BUILDROOT}/"
	done
	if ! ( cd "${FCHEMMC_BUILDROOT}" &&
	    /usr/bin/make SYSDIR="${fsys}" FCHEMMC_KERNEL="${TARGET}" ) > "${FCHEMMC_BUILDLOG}" 2>&1 ||
	    [ ! -f "${FCHEMMC_BUILDROOT}/fchemmc.ko" ]; then
		fchemmc_said failed ||
		    warn "fchemmc: the build failed; ${FCHEMMC_MODULE} is untouched (the whole log is ${FCHEMMC_BUILDLOG})"
		return 0
	fi
	fmlink=$(/usr/bin/readlink "${FCHEMMC_BUILDROOT}/machine" 2>/dev/null || true)
	case "${fmlink}" in
	"${fsys}"/*) ;;
	*)
		fchemmc_said failed ||
		    warn "fchemmc: the machine headers came from ${fmlink:-nowhere}, not from ${fsys}; not installed"
		return 0
		;;
	esac
	/bin/mkdir -p "${MODDIR}"
	# On the eMMC system disk the build being replaced is the one a boot of its kernel needs; it is kept
	# in that kernel's directory first, or the new one does not go in ("the eMMC as the system disk").
	fchemmc_keep || return 0
	if ! /bin/cp "${FCHEMMC_BUILDROOT}/fchemmc.ko" "${FCHEMMC_MODULE}.new" ||
	    ! /bin/mv "${FCHEMMC_MODULE}.new" "${FCHEMMC_MODULE}"; then
		/bin/rm -f "${FCHEMMC_MODULE}.new"
		warn "fchemmc: could not move the new module into place; ${FCHEMMC_MODULE} is untouched"
		return 0
	fi
	{ printf '%s\n' "${TARGET}" > "${FCHEMMC_MODULE}.kernel.new" &&
	    /bin/mv "${FCHEMMC_MODULE}.kernel.new" "${FCHEMMC_MODULE}.kernel"; } 2>/dev/null ||
	    warn "fchemmc: could not write the stamp beside ${FCHEMMC_MODULE}; the next run builds it again"
	/bin/chmod 0555 "${FCHEMMC_MODULE}" 2>/dev/null || true
	/bin/chmod 0444 "${FCHEMMC_MODULE}.kernel" 2>/dev/null || true
	/bin/rm -f "${FCHEMMC_SAID}"
	log "fchemmc: installed ${FCHEMMC_MODULE}, built for ${TARGET}"
	return 0
}

if [ "${EMMCCHECK}" = "1" ]; then
	emmc_root || exit 0
	# A kernel already in /boot is the stop hook's at the reboot (PINONLY=1): only a staged one is held.
	[ -f "${PENDING_KERNEL}" ] || exit 0
	TARGET=$(kernel_staged || true)
	if [ -z "${TARGET}" ]; then
		echo "the system disk is the eMMC, and no kernel version can be read from the staged set"
		exit 3
	fi
	STAGED=1
	if fchemmc_next_ok; then
		echo "the system disk is the eMMC, and fchemmc is built for the staged kernel"
		exit 0
	fi
	echo "the system disk is the eMMC, and fchemmc is not built for the staged kernel:"
	echo "  kernel     : ${TARGET}"
	echo "  fchemmc    : $(/bin/cat "${FCHEMMC_MODULE}.kernel" 2>/dev/null || echo '<none>')"
	exit 3
fi

WHERE=
TARGET=$(kernel_staged || true)
if [ -n "${TARGET}" ]; then
	WHERE="staged, waiting to be applied"
	STAGED=1
else
	TARGET=$(version_of_kernel_file "${KERNEL}" || true)
	WHERE="${KERNEL}"
fi

if [ -z "${TARGET}" ]; then
	# Not a failure worth shouting about on a machine that is not this appliance, but this script
	# cannot do its job without it.
	warn "cannot read a kernel version from a staged set or from ${KERNEL}; nothing to follow"
	record failed "cannot read a kernel version from a staged set or from ${KERNEL}"
	exit 0
fi

RUNNING=$(${UNAME} -v)
STAMP=$(/bin/cat "${MODULE}.kernel" 2>/dev/null || true)

# NOTHING TO DO IS SAID ONCE. The cron entry runs this every five minutes, and on a machine with
# nothing to follow it logged the same six lines each time: 960 in a day on an XGS 3300, every one a
# Notice that the module already matched (2026-10-28). Those lines go to the log once for each
# boot, and again whenever what they say changes - a kernel staged, a module rebuilt - and to the
# screen of whoever runs this by hand every time. Anything that is not "nothing to do" is logged as
# it always was.
: ${SAID:=/var/db/os-xgs-npu/kernel-follow.said}
BOOTED=$(/sbin/sysctl -n kern.boottime 2>/dev/null | /usr/bin/sed -n 's/^{ sec = \([0-9]*\),.*/\1/p')
SAYING="${BOOTED}|${TARGET}|${WHERE}|${RUNNING}|${MODULE}|${STAMP}"
quiet=0
if [ "${FORCE}" != "1" ] && { [ ! -f "${MODULE}" ] || [ "${STAMP}" = "${TARGET}" ]; } &&
    [ "$(/bin/cat "${SAID}" 2>/dev/null)" = "${SAYING}" ]; then
	quiet=1
fi
# These lines only; what prune_old_sources removes, and every failure, is logged as before.
note() { if [ "${quiet}" = 1 ]; then echo "$*"; else log "$*"; fi; }
said() { [ "${DRY}" = 1 ] || { /bin/mkdir -p "${SAID%/*}" && echo "${SAYING}" > "${SAID}"; } 2>/dev/null; }

note "kernel to follow: ${TARGET}"
note "  read from     : ${WHERE}"
note "kernel running : ${RUNNING}"
note "module         : ${MODULE}"
note "module stamp   : ${STAMP:-<none>}"

# ON THE eMMC SYSTEM DISK, all of this first, and whatever becomes of the module below - which may not
# be installed, or may fail to build ("the eMMC as the system disk", above): the guard, the copy of the
# kernel that booted, the fall-back where /boot/kernel has no fchemmc built for it, before anything that
# can take long and once a boot, and then fchemmc's own build.
if emmc_root; then
	emmc_guard_put
	emmc_good_keep
	if [ -z "${STAGED}" ] && ! fchemmc_boots "${KERNDIR}" && [ ! -f "${PINNED}" ] && [ "${DRY}" != "1" ]; then
		pin_for_emmc log || true
		/usr/bin/touch "${PINNED}" 2>/dev/null || true
	fi
	follow_fchemmc ""
fi

# A machine with neither module is a machine no driver of this project is installed on - the cron
# entry runs wherever the plugin is - so without FORCE that is the end of it.
if [ ! -f "${MODULE}" ] && [ "${FORCE}" != "1" ]; then
	note "no ${MODULE} - the driver is not installed on this machine, nothing to follow"
	record skipped "not installed"
	said
	emmc_after
	exit 0
fi

if [ "${STAMP}" = "${TARGET}" ] && [ "${FORCE}" != "1" ]; then
	note "the module already matches the kernel to follow - nothing to do"
	# But a fall-back to kernel.old this follower armed: it goes now (unpin_old_kernel, 2026-11-17).
	[ -f "${ARMED}" ] && unpin_old_kernel
	record ok "matches the kernel to follow"
	said
	follow_fchemmc ""
	emmc_after
	prune_old_sources
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

# ---------------------------------------------------------------- the fall-back, kept until the module matches
#
# Before anything that can take long: the sources can be hundreds of megabytes on a slow line, and
# a reboot can come in the middle of them - the owner's, or the stop hook's time limit at shutdown.
# So when the kernel to follow is already in /boot (not a set still staged, where kernel.old is the
# kernel two back) and the module is not built for it, the next boot is sent to kernel.old first, if
# the module matches that one. Once each boot: /var/run is emptied at boot, and nextboot's one shot
# is spent by the boot it steers, so the first run of the next boot arms it again. unpin_old_kernel()
# takes it away the moment the module matches.
# With no module installed there is nothing to boot kernel.old with (no_fallback()).
# A run that started while the kernel was still staged never arms it here, and it can still hold the
# lock at the reboot, so the stop hook arms it first, without the lock ("the fall-back alone", above).
if [ "${WHERE}" = "${KERNEL}" ] && [ "${STAMP}" != "${TARGET}" ] && [ ! -f "${PINNED}" ] &&
    [ -f "${MODULE}" ]; then
	pin_old_kernel log || true
	: > "${PINNED}" 2>/dev/null || true
fi

# ---------------------------------------------------------------- the sources

if [ ! -x "${FETCH}" ]; then
	warn "no ${FETCH} - cannot fetch kernel sources, so the module cannot follow"
	no_fallback "the next boot will refuse it and the front ports will not come up"
	record failed "no ${FETCH}, so no kernel sources"
	exit 1
fi

# fetch-sources.sh takes the version to pin to, prints a SYSDIR= line, and is a no-op when the tree
# is already there - so running this script twice costs nothing the second time.
#
# NOFETCH=1 is for the installer, which should not start a 350 MB download nobody asked for. It is
# NOT "use nothing": it is "use nothing that comes over the wire". An extracted tree counts, and so
# does an archive carried onto the appliance beside the plugin - unpacking one is a local copy, and
# on an appliance whose WAN is downstream of the module being built it is the only way an install
# can work at all. This used to compute the extracted path itself and look only there, so the
# carried archive the installer had just put in place was ignored and a first install on a machine
# with no internet built nothing. Deciding it here also meant a second, shorter copy of what
# fetch-sources.sh already does; now there is one.
if [ "${NOFETCH}" = "1" ]; then
	FETCHLOG=$(NONET=1 /bin/sh "${FETCH}" "${TARGET}" 2>&1)
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
	no_fallback "the next boot will refuse the module and the front ports will not come up"
	record failed "could not get kernel sources for ${TARGET}"
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
	record failed "no driver sources at ${SRCTREE}"
	exit 1
fi

/bin/rm -rf "${BUILDROOT}"
if ! /bin/mkdir -p "${BUILDROOT}"; then
	warn "cannot create ${BUILDROOT}; ${MODULE} is untouched"
	record failed "cannot create ${BUILDROOT}"
	exit 1
fi
/bin/cp "${SRCTREE}"/Makefile "${SRCTREE}"/*.c "${SRCTREE}"/*.h "${BUILDROOT}/"
# Said once this run's copy is in place, to this run's output alone - syslog has "building against"
# already: XGS: Updates counts the objects in ${BUILDROOT} as its update's build from this line on,
# while it is the last line of the update's account (update.py, progress). Until 2026-11-06 it counted
# them from "building against" above, when the directory still held the build before - whichever run
# of this script had made it - so the page could show that build, finished, for a moment as its
# update's.
echo "compiling $(/bin/ls "${BUILDROOT}"/*.c | /usr/bin/wc -l | /usr/bin/tr -d ' ') sources in ${BUILDROOT}"

BUILDLOG=/var/db/os-xgs-npu/build.log
if ! ( cd "${BUILDROOT}" && /usr/bin/make SYSDIR="${SYSDIR}" ) > "${BUILDLOG}" 2>&1; then
	warn "the build failed; ${MODULE} is untouched and still matches ${STAMP:-<nothing>}"
	/usr/bin/grep -E ' error:|Error [0-9]' "${BUILDLOG}" | /usr/bin/head -10 |
	    while read -r l; do warn "  ${l}"; done
	warn "the whole log is ${BUILDLOG}"
	pin_old_kernel || true
	record failed "the build failed; the whole log is ${BUILDLOG}"
	exit 1
fi
if [ ! -f "${BUILDROOT}/${DRIVER}.ko" ]; then
	warn "the build reported success but produced no ${DRIVER}.ko; ${MODULE} is untouched"
	record failed "the build reported success but produced no ${DRIVER}.ko"
	exit 1
fi
NSRC=$(/bin/ls "${BUILDROOT}"/*.c | /usr/bin/wc -l | /usr/bin/tr -d ' ')
NOBJ=$(/bin/ls "${BUILDROOT}"/*.o 2>/dev/null | /usr/bin/wc -l | /usr/bin/tr -d ' ')
if [ "${NOBJ}" -lt "${NSRC}" ]; then
	warn "only ${NOBJ} of ${NSRC} sources were compiled; refusing to install a partial build"
	pin_old_kernel || true
	record failed "only ${NOBJ} of ${NSRC} sources were compiled"
	exit 1
fi
MLINK=$(/usr/bin/readlink "${BUILDROOT}/machine" 2>/dev/null || true)
case "${MLINK}" in
"${SYSDIR}"/*) ;;
*)
	warn "the machine headers came from ${MLINK:-nowhere}, not from ${SYSDIR}; refusing to install"
	pin_old_kernel || true
	record failed "the machine headers came from ${MLINK:-nowhere}, not from ${SYSDIR}"
	exit 1
	;;
esac
log "built $(/usr/bin/stat -f%z "${BUILDROOT}/${DRIVER}.ko") bytes from ${NSRC} sources, machine headers from ${MLINK}"

# ---------------------------------------------------------------- install, keeping the old one

# Keep what is being replaced, with its stamp - but only when it is for a different kernel. See the
# header: a module stamped for the next kernel is wrong for the running one, and a host that boots
# the old kernel again needs the old module. The bring-up script looks for this pair.
#
# A rebuild for the SAME kernel (FORCE=1, which install.sh uses) must leave .prev alone. Copying the
# current module over it there would replace the only module for an older kernel - /boot/kernel.old
# is one boot-menu choice away - with a second copy of the module for this one. Nor is a module
# stamped VOID (below) kept: its stamp names no kernel, and .prev made of it would replace the module
# kept for the old kernel with one no boot loads.
#
# Its old stamp goes first: .prev without one is never loaded - the bring-up takes it only where its
# stamp is the kernel booted - while a copy that failed part-way under the stamp of the module it was
# replacing would be loaded on that one's kernel (the review's P15, below).
VOID="none: kernel-follow.sh was moving a module into place"
if [ -n "${STAMP}" ] && [ "${STAMP}" != "${TARGET}" ] && [ "${STAMP}" != "${VOID}" ] && [ -f "${MODULE}" ]; then
	/bin/rm -f "${MODULE}.prev.kernel"
	/bin/cp -p "${MODULE}" "${MODULE}.prev" 2>/dev/null &&
	    printf '%s\n' "${STAMP}" > "${MODULE}.prev.kernel" &&
	    log "kept the previous module as ${MODULE}.prev, stamped ${STAMP}"
fi

# Write to a temporary name and move it into place, so a full disk or an interrupted copy cannot
# leave a truncated module where a working one was.
/bin/mkdir -p "${MODDIR}"
if ! /bin/cp "${BUILDROOT}/${DRIVER}.ko" "${MODULE}.new"; then
	warn "could not write ${MODULE}.new; ${MODULE} is untouched"
	record failed "could not write ${MODULE}.new"
	exit 1
fi
# AND NO MODULE IN PLACE UNDER ANOTHER KERNEL'S STAMP. The module and its stamp are two files, and no
# rename moves two. Until 2026-11-06 the module was moved and then its stamp, neither step checked: a
# failure between the two - or the stop hook's time limit - left the new module stamped for the kernel
# running now, which the bring-up loads on that kernel, the silent wrong-offset load the stamp is
# there to prevent; and a stamp that went in without its module was the same the other way (the
# review's P15). So the new stamp is written and read back first. Then, where the module in place is
# built for another kernel, its stamp becomes VOID, which matches no kernel: the bring-up refuses it,
# and loads .prev where that is built for the kernel booted. Only then does the module move, and its
# own stamp last. Each step is checked, and one that fails puts back what it can and says what is left.
# Where the module in place is already built for this kernel, or there is none, the stamp held through
# the move is this kernel's own, and nothing in between is refused.
HOLD=${TARGET}
if [ -f "${MODULE}" ] && [ "${STAMP}" != "${TARGET}" ]; then
	HOLD=${VOID}
fi
printf '%s\n' "${TARGET}" > "${MODULE}.kernel.new" 2>/dev/null
if [ "$(/bin/cat "${MODULE}.kernel.new" 2>/dev/null)" != "${TARGET}" ] ||
    ! { printf '%s\n' "${HOLD}" > "${MODULE}.kernel.hold" && /bin/mv "${MODULE}.kernel.hold" "${MODULE}.kernel"; } 2>/dev/null; then
	/bin/rm -f "${MODULE}.new" "${MODULE}.kernel.new" "${MODULE}.kernel.hold"
	warn "could not write the stamp of the new module; ${MODULE} is untouched"
	record failed "could not write the stamp of the new module"
	exit 1
fi
if ! /bin/mv "${MODULE}.new" "${MODULE}"; then
	/bin/rm -f "${MODULE}.new" "${MODULE}.kernel.new"
	if [ -z "${STAMP}" ]; then
		/bin/rm -f "${MODULE}.kernel"
	elif ! { printf '%s\n' "${STAMP}" > "${MODULE}.kernel.hold" && /bin/mv "${MODULE}.kernel.hold" "${MODULE}.kernel"; } 2>/dev/null; then
		warn "could not move ${MODULE}.new into place, nor put back the stamp of the module that is there: it is stamped for no kernel now, so the bring-up refuses it and loads ${MODULE}.prev where that is built for the kernel booted"
		record failed "could not move ${MODULE}.new into place, nor put back the stamp of the module there"
		exit 1
	fi
	warn "could not move ${MODULE}.new into place; ${MODULE} is untouched and still matches ${STAMP:-<nothing>}"
	record failed "could not move ${MODULE}.new into place"
	exit 1
fi
if ! /bin/mv "${MODULE}.kernel.new" "${MODULE}.kernel"; then
	if [ "${HOLD}" = "${TARGET}" ]; then
		/bin/rm -f "${MODULE}.kernel.new"
	else
		warn "${MODULE} is the module built for ${TARGET}, and its stamp could not be put beside it: it is stamped for no kernel, so the bring-up refuses it and loads ${MODULE}.prev where that is built for the kernel booted"
		record failed "the new module's stamp could not be put beside it"
		exit 1
	fi
fi
/bin/chmod 0555 "${MODULE}" 2>/dev/null || true
/bin/chmod 0444 "${MODULE}.kernel" 2>/dev/null || true

log "installed ${MODULE}, stamped for the kernel the next boot will use"
log "the front ports will come up on that boot without anything else being done"
record built "rebuilt for the kernel to follow"

# And if an earlier failure had sent the next boot to kernel.old, it no longer has to - with the kernel
# still staged too, where this follower armed it (unpin_old_kernel, 2026-11-17).
STAMP=${TARGET}
unpin_old_kernel
follow_fchemmc "${SYSDIR}"
emmc_after
exit 0
