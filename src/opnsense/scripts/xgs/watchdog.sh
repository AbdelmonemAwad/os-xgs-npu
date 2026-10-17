#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The host's hardware watchdog, armed when the setting says so.
#
#   watchdog.sh start     load the timer's driver and start watchdogd, if the setting is on
#   watchdog.sh stop      stop watchdogd, which disarms the timer on its way out
#   watchdog.sh status    one line: armed, or why not
#
# WHY. Both appliances' hosts carry the AMD FCH watchdog timer - `amdsbwd0: <AMD FCH Rev 41h+
# Watchdog Timer> at iomem 0xfed80b00` attaches on the XGS 3300 and on the XGS 136 alike
# (2026-10-12 and 2026-10-17) - and nothing on OPNsense arms it, so a host that stops scheduling
# stays stopped until somebody pulls the power. With watchdogd running, a host that stops for
# longer than the timeout is reset by the hardware. The setting is xgs.general.watchdog, on by the
# owner's decision (2026-10-17).
#
# HOW. watchdogd(8) pats the timer every second and asks for TIMEOUT seconds each time. It is
# started from the boot's start hook and stopped first thing at shutdown, because watchdogd
# disarms the timer only when it is asked to stop: a timer left armed through a halt would reset
# the appliance a couple of minutes after somebody asked it to power off.
#
# Never fails the caller: a board without the timer, or the setting off, is reported and exits 0.

TIMEOUT=128
PIDFILE=/var/run/xgs-watchdogd.pid
CONF=/conf/config.xml

wanted() {
	# On unless the setting says 0; the model's own default is 1.
	v=$(/usr/bin/sed -n '/<XGS[ >]/,/<\/XGS>/s:.*<watchdog>\([01]\)</watchdog>.*:\1:p' "${CONF}" 2>/dev/null | /usr/bin/head -1)
	[ "${v:-1}" = 1 ]
}

running() {
	[ -s "${PIDFILE}" ] && /bin/kill -0 "$(/bin/cat "${PIDFILE}")" 2>/dev/null
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
		echo "not armed"
	fi
	;;
status)
	if running; then
		echo "armed: $(/sbin/sysctl -n dev.amdsbwd.0.%desc 2>/dev/null), ${TIMEOUT} s, watchdogd $(/bin/cat "${PIDFILE}")"
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
