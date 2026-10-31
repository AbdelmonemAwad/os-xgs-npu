#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# THE COPROCESSOR TAKEN UP AGAIN IN PLACE (#768), with the host running: what fastpath.py does
# first when the driver has taken its coprocessor as gone, or the fast path has stopped answering
# its heartbeat. Lines on stdout for the log; exit 0 once the fast path answers again, 1 when it
# could not be done - fastpath.py then restarts the appliance.
#
#   recover.sh
#
# Measured 2026-10-26 on both families before it was written: a coprocessor reset under a host
# that does not read it leaves the host running, and the driver brought up again takes it back
# with every port - the first XGS 3300 in 231 s from the stop to traffic, the XGS 136 the same way.
#
# ARMADA (XGS 1xx): the coprocessor reset from the host (its MCP2210, as at boot), its config space
# waited for, the driver unloaded and loaded again, and its settings and the bridge put back.
#
# OCTEON TX (XGS 2xxx, 3xxx, 4xxx): the host has no reset for this coprocessor that it survives,
# but the coprocessor's own does no harm once the driver reads nothing (dev.octep.0.gone, which
# the driver sets as the heartbeat dies): its watchdog resets it, or, when it has not, its own
# console is asked to reboot - and its console log says when the new boot waits for the host.
# Then the driver unloaded - or, while IPsec associations point into it, its device detached and
# attached again with the module loaded - and brought up as the boot hook does (bringup.sh).
#
# Both: OPNsense's interfaces reloaded (rc.reload_all), and the heartbeat waited for.

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
LIB=/usr/local/opnsense/scripts
CON=/var/log/xgs/npu-console.log
LOCK=/var/run/xgs-recover.lock
WAITING='Waiting for USFP application to connect'

if [ -z "${XGS_RECOVER_LOCKED:-}" ]; then
	XGS_RECOVER_LOCKED=1 exec lockf -k -t 0 "${LOCK}" /bin/sh "$0" "$@"
fi

say() { echo "$(date -u '+%H:%M:%S') $*"; }
T0=$(date +%s)
since() { echo $(( $(date +%s) - T0 )); }

FAMILY=$(/bin/sh ${LIB}/xgs/board.sh 2>/dev/null | awk '{print $2}')
case ${FAMILY} in
armada)		DRV=npuep; HOOK=/usr/local/etc/rc.syshook.d/early/07-npuep ;;
octeontx)	DRV=octep ;;
*)		say "not an XGS appliance this project knows; nothing done"; exit 1 ;;
esac

# The heartbeat, alive within limit seconds: 0, or 1.
hb_alive() {
	i=0
	while [ "${i}" -lt "$1" ]; do
		[ "$(sysctl -n dev.${DRV}.0.heartbeat.state 2>/dev/null)" = alive ] && return 0
		i=$((i + 5))
		sleep 5
	done
	return 1
}

# The coprocessor's console has printed, since this recovery began, a boot of its waiting for the
# host: 0, or 1. Only what was written to its log after START counts. Until 2026-10-31 it was the
# log's last 40 lines and its time, and a coprocessor that printed nothing once its fast path
# stopped left its last boot's line there: on the first XGS 3300 the device was detached and
# attached again eight seconds after its console was asked to reboot it, before that boot had
# begun (#819). A coprocessor reset before the recovery began is still seen: its boot takes about
# a hundred seconds to wait for the host, and the recovery begins within a minute of the stop.
START=$(stat -f %z "${CON}" 2>/dev/null || echo 0)
console_waits() {
	[ -s "${CON}" ] || return 1
	at=$(sysctl -n dev.octep.0.gone_at 2>/dev/null || echo 0)
	[ "${at:-0}" -gt 0 ] || return 1
	from=${START}
	# moved to its .1 at a megabyte since: all of the new file is since
	[ "$(stat -f %z "${CON}" 2>/dev/null || echo 0)" -lt "${from}" ] && from=0
	tail -c +$((from + 1)) "${CON}" | grep -a -q "${WAITING}"
}

say "taking the coprocessor up again in place (${FAMILY}, ${DRV}.ko)"

if [ "${FAMILY}" = armada ]; then
	SEL=$(pciconf -l 2>/dev/null | awk '/chip=0x708011ab|vendor=0x11ab/ {s = $1; sub(/^[^@]*@/, "", s); sub(/:$/, "", s); print s; exit}')
	[ -n "${SEL}" ] || SEL=pci0:1:0:0
	# The driver sees the coprocessor go first, when it is going to. Then the coprocessor is reset
	# from the host and its config space waited for, and only then is the driver unloaded and loaded
	# again - the boot's own order (06-npuctl's pulse, 07-npuep's load), with the unload between,
	# while the coprocessor answers. Unloaded with it running, the detach raced its own watchdog: an
	# unload just after its reset went through (2026-10-26, 15:59), one its reset came part-way
	# through froze the XGS 136 (18:55, the first function's detach never finished; its hardware
	# watchdog restarted it). And unloaded with it held in reset, the detach kept its config space as
	# it read then - all ones, as pci_cfg_save reads it - for the next load to write back, and that
	# load brought no port up in 150 s (19:09; the appliance restarted cleanly). No timeout on the
	# unload: killing kldunload stops nothing in the kernel, and only made this script give up while
	# the detach went on.
	i=0
	while [ "${i}" -lt 90 ] && [ "$(sysctl -n dev.npuep.0.gone 2>/dev/null)" = 0 ]; do
		i=$((i + 5))
		sleep 5
	done
	say "resetting the coprocessor"
	python3 ${LIB}/npuctl/mcp2210.py pulse > /dev/null 2>&1 || true
	i=0
	while [ "${i}" -lt 90 ]; do
		v=$(pciconf -r "${SEL}" 0:1 2>/dev/null || echo ffffffff)
		case "${v}" in *11ab*) break ;; esac
		i=$((i + 1))
		sleep 1
	done
	if [ "${i}" -ge 90 ]; then
		# a read of its memory in this state never completes: the driver is left as it is
		say "the coprocessor never answered its config space in 90 s; not taken up in place"
		exit 1
	fi
	say "the coprocessor answers again after ${i} s; the driver unloaded and loaded"
	if kldstat -q -n npuep; then
		kldunload npuep || { say "the driver did not unload; not taken up in place"; exit 1; }
	fi
	kldload /boot/modules/npuep.ko || { say "the driver did not load"; exit 1; }
	# the attach itself waits up to 120 s for the coprocessor's facility table
	i=0
	while [ "${i}" -lt 150 ] && ! ifconfig npup1 > /dev/null 2>&1; do
		i=$((i + 1))
		sleep 1
	done
	ifconfig npup1 > /dev/null 2>&1 || { say "the ports did not come back in 150 s"; exit 1; }
	[ -x "${HOOK}" ] && "${HOOK}" settings > /dev/null 2>&1
	configctl interface bridge configure > /dev/null 2>&1 || true
else
	# its own boot first: the watchdog's, or one asked of its console
	i=0
	while [ "${i}" -lt 120 ] && ! console_waits; do
		i=$((i + 5))
		sleep 5
	done
	if ! console_waits; then
		say "the coprocessor has not restarted on its own; asking its console to"
		# busybox's reboot, forced: vendor versions 22 and 23 have busybox's reboot but no link to it
		# on the coprocessor's path ("reboot: command not found", 2026-10-26), and -f skips the
		# shutdown; the kernel's own as a last resort. Asked again while another caller still holds
		# the console (one that had begun before the driver took it as gone), for up to a minute.
		# What npu-run.sh says is kept for the log - on 2026-10-26 it was thrown away, and with it why
		# the console had taken nothing. A reboot gives no end marker, so "did not answer" is its.
		n=0
		while :; do
			said=$(NPU_RUN_RECOVER=1 /bin/sh ${LIB}/xgs/npu-run.sh 5 'busybox reboot -f || reboot -f || echo b > /proc/sysrq-trigger' 2>&1 > /dev/null | tail -3 | tr '\n' ' ')
			n=$((n + 1))
			case "${said}" in *"already locked"*) [ "${n}" -lt 6 ] && { sleep 5; continue; } ;; esac
			break
		done
		[ -n "${said}" ] && say "its console, asked ${n} time(s): ${said}"
		i=0
		while [ "${i}" -lt 180 ] && ! console_waits; do
			i=$((i + 5))
			sleep 5
		done
	fi
	if ! console_waits; then
		say "the coprocessor's new boot was not seen waiting for the host; not taken up in place"
		exit 1
	fi
	say "the coprocessor has booted again and waits for the host ($(since) s); bringing the driver up"
	if kldstat -q -n octep && ! kldunload octep 2>/dev/null; then
		# IPsec associations point into the module, which stays loaded: its device is detached and
		# attached again instead (since 2026-10-26; until then this restarted the appliance)
		SEL=$(pciconf -l 2>/dev/null | awk '/^octep0@/ {s = $1; sub(/^[^@]*@/, "", s); sub(/:$/, "", s); print s}')
		[ -n "${SEL}" ] || { say "the driver did not unload, and its device was not found; not taken up in place"; exit 1; }
		devctl detach octep0 || { say "the driver did not unload, nor its device detach; not taken up in place"; exit 1; }
		devctl attach "${SEL}" || { say "its device did not attach again at ${SEL}; not taken up in place"; exit 1; }
		say "the driver stays loaded (IPsec associations point into it); its device detached and attached again"
	fi
	${LIB}/octep/bringup.sh > /dev/null 2>&1 || { say "the driver's bring-up did not finish"; exit 1; }
fi

say "driver up ($(since) s); OPNsense reloads its interfaces"
/usr/local/etc/rc.reload_all > /dev/null 2>&1
if hb_alive 120; then
	say "the fast path answers again: taken up in place in $(since) s"
	# The management link last, in the background as at boot: the coprocessor's end lost its address
	# with its reset, and what gives it one (mgmt-link.sh coprocessor, xgs.inc) runs at boot only -
	# the first XGS 3300's link stayed down after it was taken up in place on 2026-10-26.
	daemon -f /bin/sh ${LIB}/npuctl/mgmt-link.sh coprocessor
	exit 0
fi
say "the fast path did not answer within 120 s of the driver coming up"
exit 1
