#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The fail-to-wire relay pair of the OCTEON TX family, read and never written.
#
#   ftw-status.sh        one line a field: the pair, armed or not, normal or bypass, the relay's
#                        watchdog and its seconds, what last triggered the relay
#   ftw-status.sh -k     the same as key=value lines, for a page
#
# WHAT IT IS. The XGS 3300 has one pair of front ports, Port1 and Port2, behind signal relays that the
# board's CPLD drives. Armed, the relays join the two ports' cables to each other when the appliance
# fails - a power-good line lost, or the relay's own watchdog left to expire - so traffic passes with
# no firewall between them: bypass. The vendor ships the pair unarmed. Its registers are in the
# CPLD's face on the coprocessor's SPI bus (npu0.cpld.location=spi:0:1:3); the same chip's face on
# this host's SMBus is another map, the sensors', with no relay register known in it
# (docs/families/octeon-tx-peripherals.md). So the relay is read through the coprocessor, which
# carries the vendor's open tool for it: xgs-ftw, from the GPL board package xgs-bsp
# (include/xgs_ftw.h, lib/xgs_ftw.c, tools/xgs-ftw.c).
#
# HOW. One session through npu-run.sh - the management link's ssh, or the coprocessor's console -
# runs `xgs-ftw -s`, which only reads: the pair's arm bit and state, the relay's watchdog and the
# trigger latch. Then the three registers themselves, with the same package's xgs-cpld given one
# argument, which is a read - it writes only when given a value too, and none is given here - for
# what -s does not print: the watchdog's seconds, and which line tripped the relay. The registers are
# the board file's (xgs-bsp/data, AMDA0202-0001 to -0004, the same on all four):
#
#   0x39   the pair: bit 0 armed; bits 5:4 its state, 0 normal and 1 bypass; bits 11:8 the command
#          field, which xgs-ftw writes 0x1 (normal) or 0x3 (bypass) into
#   0x3a   the relay's watchdog: bit 31 on, bit 29 still counting, bits 5:0 its time in half seconds
#   0x3b   the trigger latch: bit 0 the watchdog expired, bits 1 to 12 a power-good line seen low,
#          bit 31 the relay's OK output low now
#
# Read and driven on the second XGS 3300 on 2026-11-11 (docs/measurements/relay-3300-2026-11-11.md),
# and nothing here writes: what puts the pair back to normal is ftw-normal.sh.
#
# Exit 0 with the state; 2 where there is no relay to read - not the OCTEON TX family, no xgs-ftw on
# the coprocessor, or no pair on its board - said on stdout, and with -k as relay=0 and its reason;
# 1 when the coprocessor did not answer, or answered something this does not read (why, on stderr).

: ${LIB:=/usr/local/opnsense/scripts}
: ${NPU_RUN:=${LIB}/xgs/npu-run.sh}
: ${BOARD_SH:=${LIB}/xgs/board.sh}
# How long npu-run.sh reads the console for at most; the session ends as soon as the answer is in.
case ${FTW_SECS:-} in
''|*[!0-9]*) FTW_SECS=20 ;;
esac
# and how long it waits for the console's lock behind another session (npu-run.sh's own default, 5)
case ${NPU_RUN_WAIT:-} in
''|*[!0-9]*) LOCK_WAIT=5 ;;
*) LOCK_WAIT=${NPU_RUN_WAIT} ;;
esac

KV=0
case ${1:-} in
'') ;;
-k) KV=1 ;;
*) echo "usage: ftw-status.sh [-k]" >&2; exit 64 ;;
esac

none() {
	if [ ${KV} = 1 ]; then
		echo "relay=0"
		echo "reason=$1"
	else
		echo "no fail-to-wire relay read: $1"
	fi
	exit 2
}

case "$(/bin/sh "${BOARD_SH}" 2>/dev/null | /usr/bin/awk '{print $2}')" in
octeontx) ;;
*) none "not the OCTEON TX family" ;;
esac

# One line, as the console takes it too: in a subshell, so nothing it sets stays in the console's
# shell, and with no exit, which would end that shell. Its last line says the answer came whole -
# over the link npu-run.sh's exit is the command's own.
CMD='( f=$(command -v xgs-ftw 2>/dev/null); [ -n "$f" ] || f=/bin/xgs-ftw; if [ -x "$f" ]; then echo "tool=$f"; "$f" -s 2>&1; echo "rc=$?"; for r in 0x39 0x3a 0x3b; do echo "cpld$r=$(xgs-cpld $r 2>&1 | head -n 1)"; done; else echo tool=none; fi; echo end=status )'

# Waited for well past the session's own most - the link's try, the wait for the console's lock and
# the session - so that the timeout never fires while npu-run.sh's lockf holds the console.
OUT=$(/usr/bin/timeout $((FTW_SECS + LOCK_WAIT + 60)) /bin/sh "${NPU_RUN}" "${FTW_SECS}" "${CMD}" < /dev/null)
rc=$?
if [ ${rc} != 0 ]; then
	echo "the coprocessor did not answer (npu-run.sh exit ${rc})" >&2
	exit 1
fi
field() { printf '%s\n' "${OUT}" | /usr/bin/sed -n "s/^$1=//p" | /usr/bin/head -1; }
if [ "$(field end)" != status ]; then
	echo "the coprocessor's answer was cut short" >&2
	exit 1
fi
TOOL=$(field tool)
case ${TOOL} in
none) none "no xgs-ftw on the coprocessor" ;;
/*) ;;
*) echo "the coprocessor answered nothing this reads" >&2; exit 1 ;;
esac
printf '%s\n' "${OUT}" | /usr/bin/grep -q '^No FTW ports found' && none "xgs-ftw finds no pair on this board"
if [ "$(field rc)" != 0 ]; then
	echo "xgs-ftw -s failed: $(printf '%s\n' "${OUT}" | /usr/bin/grep '^Error' | /usr/bin/head -1)" >&2
	exit 1
fi

# The pair's line, as the tool prints it: "Port   1 & 2  : enabled-bypass  armed" - the two labels,
# padded, then its state and whether it is armed. Its labels are the board file's, 1 and 2.
PAIRLINE=$(printf '%s\n' "${OUT}" | /usr/bin/sed -n '/^Port .*:/{p;q;}')
if [ -z "${PAIRLINE}" ]; then
	echo "xgs-ftw -s printed no pair" >&2
	exit 1
fi
PARTS=$(printf '%s\n' "${PAIRLINE}" | /usr/bin/awk -F: '{
	l = $1; sub(/^Port[ \t]*/, "", l); split(l, p, "&"); gsub(/[ \t]/, "", p[1]); gsub(/[ \t]/, "", p[2])
	split($2, w, " "); print p[1], p[2], w[1], w[2] }')
read -r P1 P2 STATE ARM <<EOF
${PARTS}
EOF
case ${P1} in [0-9]*) P1=Port${P1} ;; esac
case ${P2} in [0-9]*) P2=Port${P2} ;; esac
WDT=$(printf '%s\n' "${OUT}" | /usr/bin/sed -n 's/^Watchdog timer:[[:space:]]*//p' | /usr/bin/head -1)
TRIG=$(printf '%s\n' "${OUT}" | /usr/bin/sed -n 's/^FTW trigger[[:space:]]*:[[:space:]]*//p' | /usr/bin/head -1)

case ${STATE} in
disabled-bypass) MODE=normal ;;
enabled-bypass) MODE=bypass ;;
*) MODE=unknown ;;
esac
case ${ARM} in
armed) ARMED=1 ;;
unarmed) ARMED=0 ;;
*) ARMED=unknown ;;
esac
case ${WDT} in
disabled) WD=off ;;
armed) WD=on ;;
expired) WD=expired ;;
*) WD=unknown ;;
esac

hex() {
	case $1 in
	0x[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) echo "$1" ;;
	esac
}
R39=$(hex "$(field cpld0x39)")
R3A=$(hex "$(field cpld0x3a)")
R3B=$(hex "$(field cpld0x3b)")

SECS=
if [ -n "${R3A}" ]; then
	h=$((R3A & 0x3f))
	SECS="$((h / 2)).$(((h % 2) * 5))"
fi

# The trigger latch's bits as the tool's source names them (lib/xgs_ftw.c): bit 0 the relay's
# watchdog expired, bits 1 to 12 a power-good line seen low, bit 31 the relay's OK output low now.
# The tool itself says only OK, watchdog-timer-expired or power-fault.
RAILS='1 atx ATX
2 atx-5v0 ATX 5 V
3 atx-12v0 ATX 12 V
4 vdd-3v3 3.3 V
5 sfp-3v3 SFP 3.3 V
6 phy-vdd PHY supply
7 vdda-0v9 VDDA 0.9 V
8 vdda-1v8 VDDA 1.8 V
9 holdup-3v3 hold-up 3.3 V
10 vdd-5v0 5 V
11 vddq-vtt memory VDDQ/VTT
12 vdd-core core'
CAUSE=
CAUSE_TEXT=
OKSIG=
if [ -n "${R3B}" ]; then
	v=$((R3B))
	LATCHED=0
	[ ${v} -ne 0 ] && LATCHED=1
	[ $((v & 1)) -ne 0 ] && { CAUSE=watchdog; CAUSE_TEXT="the relay watchdog expired"; }
	lost=
	while read -r bit key name; do
		[ $(((v >> bit) & 1)) -ne 0 ] || continue
		CAUSE="${CAUSE:+${CAUSE},}${key}"
		lost="${lost:+${lost}, }${name}"
	done <<EOF
${RAILS}
EOF
	n=13
	while [ ${n} -le 30 ]; do
		if [ $(((v >> n) & 1)) -ne 0 ]; then
			CAUSE="${CAUSE:+${CAUSE},}bit${n}"
			lost="${lost:+${lost}, }bit ${n} (not named in the tool's source)"
		fi
		n=$((n + 1))
	done
	[ -n "${lost}" ] && CAUSE_TEXT="${CAUSE_TEXT:+${CAUSE_TEXT}; }power good lost: ${lost}"
	if [ $(((v >> 31) & 1)) -ne 0 ]; then
		OKSIG=low
		[ -n "${CAUSE_TEXT}" ] || CAUSE_TEXT="the relay's OK output low, no cause bit set"
	else
		OKSIG=high
	fi
else
	# the tool's word alone, where xgs-cpld did not answer
	case ${TRIG} in
	OK) LATCHED=0 ;;
	watchdog-timer-expired) LATCHED=1; CAUSE=watchdog; CAUSE_TEXT="the relay watchdog expired" ;;
	power-fault) LATCHED=1; CAUSE=power; CAUSE_TEXT="a power fault, its line unread" ;;
	*) LATCHED=unknown ;;
	esac
fi

if [ ${KV} = 1 ]; then
	echo "relay=1"
	echo "tool=${TOOL}"
	echo "pair=${P1},${P2}"
	echo "armed=${ARMED}"
	echo "mode=${MODE}"
	echo "watchdog=${WD}"
	echo "watchdog_seconds=${SECS}"
	echo "trigger=${LATCHED}"
	echo "cause=${CAUSE}"
	echo "cause_text=${CAUSE_TEXT}"
	echo "ok_signal=${OKSIG}"
	echo "cpld_39=${R39}"
	echo "cpld_3a=${R3A}"
	echo "cpld_3b=${R3B}"
	exit 0
fi

show() { printf '%-15s %s\n' "$1:" "$2"; }
show "pair" "${P1} & ${P2}"
case ${ARMED} in
1) show "armed" "yes" ;;
0) show "armed" "no" ;;
*) show "armed" "unknown (xgs-ftw said: ${ARM:-nothing})" ;;
esac
case ${MODE} in
normal) show "mode" "normal" ;;
bypass) show "mode" "bypass - ${P1} and ${P2} joined by the relay, past this firewall" ;;
*) show "mode" "unknown (xgs-ftw said: ${STATE:-nothing})" ;;
esac
case ${WD} in
off) show "relay watchdog" "off" ;;
on) show "relay watchdog" "on${SECS:+, ${SECS} s}, counting" ;;
expired) show "relay watchdog" "expired${SECS:+, after ${SECS} s}" ;;
*) show "relay watchdog" "unknown (xgs-ftw said: ${WDT:-nothing})" ;;
esac
case ${LATCHED} in
0) show "trigger" "none latched${R3B:+ (0x3b ${R3B})}" ;;
1) show "trigger" "${CAUSE_TEXT} (latched${R3B:+, 0x3b ${R3B}})$([ "${OKSIG}" = low ] && echo "; the relay's OK output low now")" ;;
*) show "trigger" "unknown (xgs-ftw said: ${TRIG:-nothing})" ;;
esac
show "registers" "0x39 ${R39:-unread}, 0x3a ${R3A:-unread}, 0x3b ${R3B:-unread} (xgs-cpld, read only)"
exit 0
