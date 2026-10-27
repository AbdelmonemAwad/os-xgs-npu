#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Feed the coprocessor's temperatures to the driver, so they reach OPNsense's own temperature
# widget - docs/octep/appliance-defaults.md. Run every minute by the plugin's cron entry (xgs.inc).
#
# OPNsense's widget lists every sysctl of format IK, and the readings that matter most on these
# appliances are ones no FreeBSD driver can take:
#
#   XGS 3300   the coprocessor's die and the inlet air, CPLD registers that xgs3300-sensors reads
#              over the host's SMBus -> dev.octep.0.temperature.npu and .inlet
#   XGS 136    the coprocessor's SoC sensor, the thermal zone ap-thermal-ic of its own Linux, read
#              over the management link -> dev.npuep.0.temperature
#
# The driver holds each value and refuses it once it is five minutes old, so if this stops running
# the widget shows nothing rather than a frozen number. Read-only on the hardware: nothing here is
# written anywhere but those sysctls, the XGS 3300 flags' state file and the system log. Quiet
# unless asked (-v), because it runs every minute.
#
# The XGS 3300's bank also holds flags (2026-10-27): a fan under speed, the power supplies, the
# reset flags. They are said in the system log, tag xgs-sensors, when one changes, and at a boot's
# first reading when one is not as a healthy board has it. The external supply is said only when it
# changes: a board with one supply reads it 0 from the start, and the bank cannot tell an absent
# supply from a failed one.

VERBOSE=0
[ "${1:-}" = -v ] && VERBOSE=1
say() { [ "${VERBOSE}" = 1 ] && echo "$1"; return 0; }
tell() { /usr/bin/logger -t xgs-sensors "$1"; say "$1"; }

FLAGS=/var/db/os-xgs-npu/sensor-flags
# flags <fan 0 under> <fan 1 under> <internal good> <external good> <reset flag 0> <reset flag 1>
flags() {
	BOOT=$(/sbin/sysctl -n kern.boottime | /usr/bin/sed -n 's/^{ sec = \([0-9]*\),.*/\1/p')
	OBOOT=
	[ -f "${FLAGS}" ] && read -r OBOOT OF0 OF1 OPI OPE OR0 OR1 < "${FLAGS}"
	/bin/mkdir -p "${FLAGS%/*}"
	echo "${BOOT} $*" > "${FLAGS}"
	if [ "${OBOOT}" != "${BOOT}" ]; then
		[ "$1" = 1 ] && tell "fan 0 under speed"
		[ "$2" = 1 ] && tell "fan 1 under speed"
		[ "$3" = 0 ] && tell "power: the internal supply not good"
		[ "$5" = 1 ] && tell "reset flag 0 set: the reset button was pressed"
		[ "$6" = 1 ] && tell "reset flag 1 set"
		return 0
	fi
	[ "$1" != "${OF0}" ] && tell "fan 0 $([ "$1" = 1 ] && echo 'under speed' || echo 'back up to speed')"
	[ "$2" != "${OF1}" ] && tell "fan 1 $([ "$2" = 1 ] && echo 'under speed' || echo 'back up to speed')"
	[ "$3" != "${OPI}" ] && tell "power: the internal supply $([ "$3" = 1 ] && echo 'good again' || echo 'not good')"
	[ "$4" != "${OPE}" ] && tell "power: the external supply $([ "$4" = 1 ] && echo good || echo 'not good or gone')"
	[ "$5" != "${OR0}" ] && tell "reset flag 0 $([ "$5" = 1 ] && echo 'set: the reset button was pressed' || echo cleared)"
	[ "$6" != "${OR1}" ] && tell "reset flag 1 $([ "$6" = 1 ] && echo set || echo cleared)"
	return 0
}

# The value as sysctl(8) takes it for an IK node: a temperature with its unit. A bare number -
# the deci-kelvin the kernel keeps - is refused as "invalid integer" (measured on the XGS 3300,
# 2026-10-17), so every write here says Celsius and sysctl(8) converts it.
celsius() { echo "$1" | /usr/bin/awk '{ printf "%.1fC", $1 }'; }

FAMILY=$(/bin/sh /usr/local/opnsense/scripts/xgs/board.sh 2>/dev/null | /usr/bin/awk '{ print $2 }')
case ${FAMILY} in
octeontx)
	/sbin/sysctl -N dev.octep.0.temperature.npu > /dev/null 2>&1 || { say "driver without the temperature nodes"; exit 0; }
	[ -x /usr/local/sbin/xgs3300-sensors ] || { say "no xgs3300-sensors"; exit 0; }
	J=$(/usr/bin/timeout 15 /usr/local/sbin/xgs3300-sensors json 2>/dev/null | /usr/bin/tail -1)
	for k in npu inlet; do
		C=$(echo "${J}" | /usr/bin/sed -n "s/.*\"${k}_c\": *\(-\{0,1\}[0-9.]*\).*/\1/p")
		if [ -n "${C}" ] && /sbin/sysctl -q dev.octep.0.temperature.${k}=$(celsius "${C}") > /dev/null 2>&1; then
			say "${k} ${C} C"
		else
			say "${k}: no reading"
		fi
	done
	val() { echo "${J}" | /usr/bin/sed -n "s/.*\"$1\": *\([0-9]*\).*/\1/p"; }
	NOW="$(val fan0_under) $(val fan1_under) $(val psu_internal_good) $(val psu_external_good) $(val reset_flag0) $(val reset_flag1)"
	case ${NOW} in
	[01]\ [01]\ [01]\ [01]\ [01]\ [01]) flags ${NOW} ;;
	*) say "no flags from xgs3300-sensors (an older build)" ;;
	esac
	;;
armada)
	/sbin/sysctl -N dev.npuep.0.temperature > /dev/null 2>&1 || { say "driver without the temperature node"; exit 0; }
	LINK=/usr/local/opnsense/scripts/npuctl/mgmt-link.sh
	[ -x "${LINK}" ] || { say "no management link script"; exit 0; }
	M=$(/usr/bin/timeout 20 /bin/sh "${LINK}" ssh \
	    'for z in /sys/class/thermal/thermal_zone*; do [ "$(cat $z/type)" = ap-thermal-ic ] && cat $z/temp; done' \
	    2>/dev/null | /usr/bin/tail -1)
	case ${M} in
	''|*[!0-9-]*) say "no reading over the management link"; exit 0 ;;
	esac
	C=$(echo "${M}" | /usr/bin/awk '{ printf "%.1f", $1 / 1000 }')
	if /sbin/sysctl -q dev.npuep.0.temperature=$(celsius "${C}") > /dev/null 2>&1; then
		say "npu ${C} C"
	fi
	;;
*)
	exit 0
	;;
esac
exit 0
