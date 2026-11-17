#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The host's hardware watchdog, armed when the setting says so.
#
#   watchdog.sh start     load the timer's driver and start watchdogd, if the setting is on
#   watchdog.sh stop      stop watchdogd, which disarms the timer on its way out
#   watchdog.sh status    one line: armed, held by another watchdogd, or why not
#
# WHY. Both appliances' hosts carry the AMD FCH watchdog timer - `amdsbwd0: <AMD FCH Rev 41h+
# Watchdog Timer> at iomem 0xfed80b00` attaches on the XGS 3300 and on the XGS 136 alike
# (2026-10-12 and 2026-10-17) - and nothing on OPNsense arms it, so a host that stops scheduling
# stays stopped until somebody pulls the power. With watchdogd running, a host that stops for
# longer than the timeout is reset by the hardware. The setting is xgs.general.watchdog, on by
# default since 2026-10-17.
#
# HOW. watchdogd(8) pats the timer every second and asks for TIMEOUT seconds each time. It is
# started from the boot's start hook and stopped at shutdown, because watchdogd disarms the timer
# only when it is asked to stop: a timer left armed through a halt would reset the appliance a
# couple of minutes after somebody asked it to power off. The stop hook (23-xgs-watchdog) comes once
# the kernel follower's rebuild is over, before the services and the network go; until 2026-11-06 it
# came first, and the rebuild, ten minutes at most, ran with the timer off. install/install.sh starts
# it as well, on every install and update as soon as this script and its hooks are in place, so an
# appliance is watched before its first reboot (since 2026-11-17), and the settings' Apply starts or
# stops it.
#
# ONE WATCHDOG AT A TIME. os-watchdog stands aside while this script is installed, so a watchdogd it
# started before this script came - where that plugin was armed first - is taken over: start stops it
# and arms its own, since an Apply on os-watchdog's page would stop it and, standing aside, start none,
# and the host would go unwatched. Any other watchdogd - one started by hand - is left the timer: start
# says so and starts none beside it; two watchdogd patting one timer, each disarming it as it stops,
# would leave nobody sure whether the host is covered. One started with -n is not counted: a dry run
# never opens the timer. One on the kernel's software timer only (--softtimeout) leaves the hardware
# timer unarmed, and is said to; it is left to all the same, since the kernel refuses to switch between
# the software and the hardware timer while either is armed, and a watchdogd started beside it would
# only fail.
#
# Never fails the caller: a board without the timer, the setting off, or the timer held by another
# watchdogd is reported and exits 0.

TIMEOUT=128
PIDFILE=/var/run/xgs-watchdogd.pid
OSWD_PIDFILE=/var/run/os-watchdog.pid
CONF=/conf/config.xml

wanted() {
	# On unless the setting says 0; the model's own default is 1.
	v=$(/usr/bin/sed -n '/<XGS[ >]/,/<\/XGS>/s:.*<watchdog>\([01]\)</watchdog>.*:\1:p' "${CONF}" 2>/dev/null | /usr/bin/head -1)
	[ "${v:-1}" = 1 ]
}

running() {
	[ -s "${PIDFILE}" ] && /bin/kill -0 "$(/bin/cat "${PIDFILE}")" 2>/dev/null
}

# os-watchdog's watchdogd, while it runs, or nothing.
oswatchdog() {
	[ -s "${OSWD_PIDFILE}" ] || return 0
	p=$(/bin/cat "${OSWD_PIDFILE}")
	/bin/kill -0 "${p}" 2>/dev/null && echo "${p}"
	return 0
}

# A watchdogd started with -n, alone or among -d, -S and -w: a dry run, which never opens the timer.
dry_run() (
	set -f
	for a in $(/bin/ps -o args= -p "$1" 2>/dev/null); do
		case "${a}" in
		-*[!dnSw]*) ;;
		-*n*) exit 0 ;;
		esac
	done
	exit 1
)

# The process id of a watchdogd that holds the timer while this script's own does not - one started by
# hand - or nothing when there is none: not os-watchdog's (taken over), nor a dry run. Asked only once
# running() has said no.
other_watchdogd() {
	ow=$(oswatchdog)
	for p in $(/bin/pgrep -x watchdogd 2>/dev/null); do
		[ "${p}" = "${ow}" ] && continue
		dry_run "${p}" && continue
		echo "${p}"
		return 0
	done
}

# How that watchdogd is named: one on the kernel's software timer only leaves the hardware timer unarmed.
named() {
	case " $(/bin/ps -o args= -p "$1" 2>/dev/null) " in
	*" --softtimeout "*) echo "another watchdogd, $1, on the kernel's software timer only - the hardware timer is not armed" ;;
	*) echo "another watchdogd, $1" ;;
	esac
}

timer() {
	/sbin/kldstat -q -m amdsbwd || /sbin/kldload -n amdsbwd 2>/dev/null
	/sbin/sysctl -n dev.amdsbwd.0.%desc 2>/dev/null
}

say() {
	echo "$1"
	/usr/bin/logger -t xgs-watchdog "$1"
}

case ${1:-status} in
start)
	if ! wanted; then
		say "the hardware watchdog is switched off in the settings"
		exec /bin/sh "$0" stop
	fi
	if running; then
		echo "armed already, watchdogd $(/bin/cat "${PIDFILE}")"
		exit 0
	fi
	OW=$(oswatchdog)
	if [ -n "${OW}" ]; then
		/bin/kill -TERM "${OW}" 2>/dev/null
		n=0
		while /bin/kill -0 "${OW}" 2>/dev/null && [ ${n} -lt 50 ]; do
			sleep 0.1
			n=$((n + 1))
		done
		/bin/rm -f "${OSWD_PIDFILE}"
		say "os-watchdog's watchdogd ${OW} stopped: while this script is installed it arms the timer"
	fi
	OTHER=$(other_watchdogd)
	if [ -n "${OTHER}" ]; then
		say "the timer is held by $(named "${OTHER}"); left to it"
		exit 0
	fi
	DESC=$(timer)
	if [ -z "${DESC}" ]; then
		say "no hardware watchdog timer on this host; nothing armed"
		exit 0
	fi
	/usr/sbin/watchdogd -t ${TIMEOUT} -s 1 -I "${PIDFILE}"
	sleep 1
	if running; then
		say "armed: ${DESC}, ${TIMEOUT} s, watchdogd $(/bin/cat "${PIDFILE}")"
	else
		say "watchdogd did not start; the timer is not armed"
	fi
	;;
stop)
	if running; then
		PID=$(/bin/cat "${PIDFILE}")
		/bin/kill -TERM "${PID}" 2>/dev/null
		n=0
		while /bin/kill -0 "${PID}" 2>/dev/null && [ ${n} -lt 50 ]; do
			sleep 0.1
			n=$((n + 1))
		done
		/bin/rm -f "${PIDFILE}"
		say "disarmed"
	else
		OW=$(oswatchdog)
		OTHER=$(other_watchdogd)
		if [ -n "${OW}" ]; then
			echo "not armed by this script; os-watchdog's watchdogd ${OW} holds the timer"
		elif [ -n "${OTHER}" ]; then
			echo "not armed by this script; the timer is held by $(named "${OTHER}")"
		else
			echo "not armed"
		fi
	fi
	;;
status)
	if running; then
		echo "armed: $(/sbin/sysctl -n dev.amdsbwd.0.%desc 2>/dev/null), ${TIMEOUT} s, watchdogd $(/bin/cat "${PIDFILE}")"
	elif OW=$(oswatchdog) && [ -n "${OW}" ]; then
		echo "held by os-watchdog's watchdogd ${OW}; the next start takes it over"
	elif OTHER=$(other_watchdogd) && [ -n "${OTHER}" ]; then
		echo "held by $(named "${OTHER}")"
	elif wanted; then
		echo "not armed (on in the settings)"
	else
		echo "not armed (off in the settings)"
	fi
	;;
*)
	echo "usage: watchdog.sh start|stop|status" >&2
	;;
esac
exit 0
