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
# ARMADA (XGS 1xx): the driver unloaded, the coprocessor reset from the host (its MCP2210, as at
# boot), its config space waited for, the driver loaded and its settings and the bridge put back -
# contrib/npuep/reload.sh's sequence, which the XGS 136 measurement used.
#
# OCTEON TX (XGS 2xxx, 3xxx, 4xxx): the host has no reset for this coprocessor that it survives,
# but the coprocessor's own does no harm once the driver reads nothing (dev.octep.0.gone, which
# the driver sets as the heartbeat dies): its watchdog resets it, or, when it has not, its own
# console is asked to reboot - and its console log says when the new boot waits for the host.
# Then the driver unloaded and brought up as the boot hook does (bringup.sh).
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

# The tail of the coprocessor's console log is a boot of its since the driver took it as gone,
# waiting for the host: 0, or 1. The log is written as the console prints and nothing is printed
# once the boot waits, so its time is that of the line; the driver's gone_at, seconds after the
# host's boot, says when it was taken as gone.
console_waits() {
	[ -s "${CON}" ] || return 1
	boot=$(sysctl -n kern.boottime | sed 's/.*sec = \([0-9]*\),.*/\1/')
	at=$(sysctl -n dev.octep.0.gone_at 2>/dev/null || echo 0)
	[ "${at:-0}" -gt 0 ] || return 1
	[ "$(stat -f %m "${CON}")" -ge $((boot + at)) ] || return 1
	tail -n 40 "${CON}" | grep -a -q "${WAITING}"
}

say "taking the coprocessor up again in place (${FAMILY}, ${DRV}.ko)"

if [ "${FAMILY}" = armada ]; then
	SEL=$(pciconf -l 2>/dev/null | awk '/chip=0x708011ab|vendor=0x11ab/ {s = $1; sub(/^[^@]*@/, "", s); sub(/:$/, "", s); print s; exit}')
	[ -n "${SEL}" ] || SEL=pci0:1:0:0
	if kldstat -q -n npuep; then
		timeout 60 kldunload npuep || { say "the driver did not unload; not taken up in place"; exit 1; }
	fi
	say "driver unloaded; resetting the coprocessor"
	python3 ${LIB}/npuctl/mcp2210.py pulse > /dev/null 2>&1 || true
	i=0
	while [ "${i}" -lt 90 ]; do
		v=$(pciconf -r "${SEL}" 0:1 2>/dev/null || echo ffffffff)
		case "${v}" in ""|*ffffffff*|*ffff) ;; *) break ;; esac
		i=$((i + 1))
		sleep 1
	done
	if [ "${i}" -ge 90 ]; then
		# a read of its memory in this state never completes: the driver is not loaded
		say "the coprocessor never answered its config space in 90 s; not loading the driver"
		exit 1
	fi
	say "the coprocessor answers again after ${i} s; loading the driver"
	kldload /boot/modules/npuep.ko || { say "the driver did not load"; exit 1; }
	i=0
	while [ "${i}" -lt 60 ] && ! ifconfig npup1 > /dev/null 2>&1; do
		i=$((i + 1))
		sleep 1
	done
	ifconfig npup1 > /dev/null 2>&1 || { say "the ports did not come back in 60 s"; exit 1; }
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
		/bin/sh ${LIB}/xgs/npu-run.sh 5 'reboot' > /dev/null 2>&1 || true
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
	if kldstat -q -n octep; then
		timeout 60 kldunload octep || { say "the driver did not unload; not taken up in place"; exit 1; }
	fi
	${LIB}/octep/bringup.sh > /dev/null 2>&1 || { say "the driver's bring-up did not finish"; exit 1; }
fi

say "driver up ($(since) s); OPNsense reloads its interfaces"
/usr/local/etc/rc.reload_all > /dev/null 2>&1
if hb_alive 120; then
	say "the fast path answers again: taken up in place in $(since) s"
	exit 0
fi
say "the fast path did not answer within 120 s of the driver coming up"
exit 1
