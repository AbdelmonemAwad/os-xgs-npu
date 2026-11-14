#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The XGS 3300's fail-to-wire relay pair, Port1 and Port2, put back to normal - out of bypass, the
# relay's watchdog off, its trigger latch cleared - when it is found otherwise.
#
#   ftw-normal.sh         once, now: says what it found and did
#   ftw-normal.sh boot    the start hook's (10-xgs-ftw): the knob below first, then tried every 15 s,
#                         twenty times at most, while the coprocessor comes up; quiet when there is
#                         nothing to do, but for one line on the latch every boot leaves (below)
#
# WHY. Read and driven on the second XGS 3300 on 2026-11-11 (docs/measurements/relay-3300-2026-11-11.md):
# armed, with its watchdog left to expire, the relay went to bypass 3.8 s later and cut the WAN on
# Port2 for 9 s, while this host saw the port's link up the whole time - the host cannot tell a bypass
# from its links; only the CPLD says it. And it stayed in bypass: the pair unarmed and the watchdog
# off by their raw registers did not release it; only the tool's -d did. Nothing under OPNsense runs
# that, so a pair that went to bypass - a power fault while armed, a relay watchdog nobody reset -
# kept Port1 and Port2 joined past the firewall through every boot after, until somebody ran the tool
# by hand.
#
# WHAT IT RUNS. The vendor's open tool, xgs-ftw (the GPL board package xgs-bsp: tools/xgs-ftw.c,
# lib/xgs_ftw.c), on the coprocessor through npu-run.sh, and only two of its options:
#
#   -t disable   the relay's watchdog off, with the bit that clears the trigger latch (0x3a written
#                0x1000003e: bit 31 clear, the time 31 s, bit 28 set) - the tool's one way to clear
#                the latch that arms nothing. Run when the watchdog has expired or a trigger is
#                latched, and first: an expired watchdog stays expired until its bit 31 is cleared
#                (the tool's source says so), and could put an armed pair back in bypass behind -d.
#   -d           disable-bypass: the pair's command field (0x39, bits 11:8) to normal, 0x1, its arm
#                bit and the rest of the register as read. Run when the pair is not normal.
#
# Then read back and compared: a latch still set once the pair is normal is cleared once more the same
# way, since it is not cleared while the relay holds the pair in bypass (0x3b read 0x80000001 after the
# clear bit on 2026-11-11, and 0 after -d). Nothing here arms - no -a, no -e, no -t with seconds - and
# the arm bit is left as found: whether the pair bypasses when the appliance fails is the owner's
# choice, made with the tool, and an armed pair still does on a power fault, which is what arming is
# for.
#
# A LATCH ALONE IS LEFT AS IT IS. Every boot of the host latches a trigger, "power good lost: ATX"
# (0x3b read 0x2), measured on the XGS 3300 on 2026-11-13; and the host's restart disarms an armed
# pair - it came back 0x39 0x800, not 0x801, and not in bypass. So with the pair normal, not armed and
# its watchdog not expired, the latch is only recorded - in the state file, and in one line of the log,
# "latched at boot, left as is" - and nothing is written: clearing it there wrote 0x3a at every boot
# for a latch that bypasses nothing while the pair is not armed. It is cleared (-t disable) only for
# a pair that is armed - or whose arm bit could not be read - or that is in bypass or another state
# that is not normal, or whose watchdog expired.
#
# Every change is said in the system log, tag xgs-ftw: what was found, what ran, what reads back - a
# warning while the pair was in bypass, or when it did not come back. The last outcome, with its time,
# is kept in /var/db/os-xgs-npu/ftw-normal.state, a run with nothing to do included.
#
# THE KNOB. The kernel environment's xgs.ftw.boot: 0 keeps the boot hook from doing anything at all,
# not even a read - the boot as it was before this file. It is the loader's: a line xgs.ftw.boot="0" in
# /boot/loader.conf.local, read at the next boot. Unset, or anything else, the hook runs. A run by hand
# does not ask it.
#
# Exit 0 when the pair is normal, found so or put back; 1 when the coprocessor did not answer; 2 not
# the XGS 3300, or no relay to read; 3 when it was told and still reads otherwise.

: ${LIB:=/usr/local/opnsense/scripts}
: ${NPU_RUN:=${LIB}/xgs/npu-run.sh}
: ${STATUS:=${LIB}/xgs/ftw-status.sh}
: ${BOARD_SH:=${LIB}/xgs/board.sh}
: ${LOGGER:=/usr/bin/logger}
: ${KENV:=/bin/kenv}
: ${STATE:=/var/db/os-xgs-npu/ftw-normal.state}
: ${TRIES:=20}
: ${GAP:=15}
case ${FTW_SECS:-} in
''|*[!0-9]*) FTW_SECS=20 ;;
esac
# Waited for up to 20 s behind another session on the console, for this script's sessions and for
# ftw-status.sh's: at boot the console is busy with the other hooks' first reads.
case ${NPU_RUN_WAIT:-} in
''|*[!0-9]*) NPU_RUN_WAIT=20 ;;
esac
export FTW_SECS NPU_RUN_WAIT

BOOT=0
WHEN=now
case ${1:-} in
'') ;;
boot) BOOT=1; WHEN="at boot" ;;
*) echo "usage: ftw-normal.sh [boot]" >&2; exit 64 ;;
esac

say() { [ ${BOOT} = 1 ] || echo "$*"; }
note() { ${LOGGER} -t xgs-ftw -p daemon.notice "$*"; say "$*"; }
warn() { ${LOGGER} -t xgs-ftw -p daemon.warning "$*"; say "$*"; }
keep() {
	/bin/mkdir -p "${STATE%/*}" 2>/dev/null
	echo "$(/bin/date -u '+%Y-%m-%d %H:%M UTC') ${WHEN}: $*" 2>/dev/null > "${STATE}"
}

if [ ${BOOT} = 1 ] && [ "$(${KENV} -q xgs.ftw.boot 2>/dev/null)" = 0 ]; then
	exit 0
fi
# The XGS 3300 alone: the family's one board run here - board.sh marks the others "rnd" - and the one
# whose relay was measured.
case "$(/bin/sh "${BOARD_SH}" 2>/dev/null)" in
"202 octeontx") ;;
*) say "not the XGS 3300: nothing done"; exit 2 ;;
esac

# The pair as ftw-status.sh -k reads it, its lines kept in KV; kv <key> is one of its values.
KV=
kv() { printf '%s\n' "${KV}" | /usr/bin/sed -n "s/^$1=//p" | /usr/bin/head -1; }
read_pair() {
	if [ ${BOOT} = 1 ]; then
		KV=$(/bin/sh "${STATUS}" -k 2>/dev/null)
	else
		KV=$(/bin/sh "${STATUS}" -k)
	fi
	case $? in
	0) ;;
	2) WHY=$(kv reason); return 2 ;;
	*) return 1 ;;
	esac
	PAIR=$(kv pair | /usr/bin/sed 's/,/ \& /')
	# the tool where the coprocessor has it, as a plain path, or by its name
	TOOL=$(kv tool)
	case ${TOOL} in
	/*[!A-Za-z0-9/._-]*) TOOL=xgs-ftw ;;
	/*) ;;
	*) TOOL=xgs-ftw ;;
	esac
	return 0
}
# Normal: not in bypass, no trigger latched, and the relay's watchdog not expired.
normal() { [ "$(kv mode)" = normal ] && [ "$(kv trigger)" = 0 ] && [ "$(kv watchdog)" != expired ]; }
# Normal but for a latched trigger, and not armed: what every boot leaves (A LATCH ALONE, above).
latch_only() {
	[ "$(kv mode)" = normal ] && [ "$(kv armed)" = 0 ] && [ "$(kv trigger)" = 1 ] &&
	    [ "$(kv watchdog)" != expired ]
}
# The pair in words, with its three registers, for the log.
words() {
	case $(kv mode) in
	normal) m="normal" ;;
	bypass) m="in bypass" ;;
	*) m="in a state xgs-ftw calls unknown" ;;
	esac
	case $(kv armed) in
	1) a="armed" ;;
	0) a="not armed" ;;
	*) a="armed or not unread" ;;
	esac
	s=$(kv watchdog_seconds)
	case $(kv watchdog) in
	off) w="relay watchdog off" ;;
	on) w="relay watchdog on${s:+, ${s} s}" ;;
	expired) w="relay watchdog expired${s:+ after ${s} s}" ;;
	*) w="relay watchdog unread" ;;
	esac
	case $(kv trigger) in
	0) t="no trigger latched" ;;
	1) t="trigger latched: $(kv cause_text)" ;;
	*) t="trigger unread" ;;
	esac
	[ "$(kv ok_signal)" = low ] && t="${t}, the relay's OK output low"
	r1=$(kv cpld_39)
	r2=$(kv cpld_3a)
	r3=$(kv cpld_3b)
	echo "${m}, ${a}, ${w}, ${t} (0x39 ${r1:-unread}, 0x3a ${r2:-unread}, 0x3b ${r3:-unread})"
}
# One session on the coprocessor through npu-run.sh. Whole only with its last line - over the link
# npu-run.sh's exit is the command's own - and waited for well past the session's own most, so that
# the timeout never fires while npu-run.sh's lockf holds the console.
tell() {
	OUT=$(/usr/bin/timeout $((FTW_SECS + NPU_RUN_WAIT + 60)) /bin/sh "${NPU_RUN}" "${FTW_SECS}" \
	    "( $1; echo end=act )" < /dev/null)
	[ $? = 0 ] && printf '%s\n' "${OUT}" | /usr/bin/grep -q '^end=act'
}
# What a session ran, in words, with the tool's exit where it was not 0 and its first error line.
ran() {
	r=
	for x in t d; do
		e=$(printf '%s\n' "${OUT}" | /usr/bin/sed -n "s/^${x}=//p" | /usr/bin/head -1)
		[ -n "${e}" ] || continue
		case ${x} in t) o="-t disable" ;; d) o="-d" ;; esac
		[ "${e}" = 0 ] && e= || e=" (exit ${e})"
		r="${r:+${r}, then }xgs-ftw ${o}${e}"
	done
	err=$(printf '%s\n' "${OUT}" | /usr/bin/grep '^Error' | /usr/bin/head -1)
	echo "${r}${err:+: ${err}}"
}

PAIR="Port1 & Port2"
FOUND=
WAS=
RAN=
TO=
SAID=
WHY=
ACTS=0
# Said once the pair reads normal after this run wrote it: <what ran>.
back() {
	if [ "${WAS}" = normal ]; then
		note "relay pair ${PAIR}: its trigger latch cleared ${WHEN} ($1): ${NOW}; it was ${FOUND}"
		keep "trigger latch cleared ($1): ${NOW}; it was ${FOUND}"
	else
		note "relay pair ${PAIR} back to normal ${WHEN} ($1): ${NOW}; it was ${FOUND}"
		keep "back to normal ($1): ${NOW}; it was ${FOUND}"
	fi
}
once() {
	read_pair || return $?
	if normal; then
		NOW=$(words)
		if [ ${ACTS} -gt 0 ]; then
			# what an earlier try ran has taken, though an answer was lost on the way
			back "${RAN:-${TO}, its answer lost}"
		else
			say "relay pair ${PAIR} normal: ${NOW}"
			keep "normal, nothing to do: ${NOW}"
		fi
		if [ "$(kv watchdog)" = on ]; then
			note "relay pair ${PAIR}: its watchdog is on and is left so - nothing here resets it, and" \
			    "once it expires an armed pair goes to bypass: ${NOW}"
		fi
		return 0
	fi
	# Not after an earlier try of this run wrote it: a pair this found in bypass has its latch cleared.
	if [ -z "${FOUND}" ] && latch_only; then
		NOW=$(words)
		note "relay pair ${PAIR}: trigger latched at boot, left as is (not armed, nothing written): ${NOW}"
		keep "trigger latched at boot, left as is (not armed, nothing written): ${NOW}"
		if [ "$(kv watchdog)" = on ]; then
			note "relay pair ${PAIR}: its watchdog is on and is left so - nothing here resets it, and" \
			    "once it expires an armed pair goes to bypass: ${NOW}"
		fi
		return 0
	fi
	if [ -z "${FOUND}" ]; then
		FOUND=$(words)
		WAS=$(kv mode)
	fi
	DO=
	TO=
	if [ "$(kv watchdog)" = expired ] || [ "$(kv trigger)" != 0 ]; then
		DO="${TOOL} -t disable 2>&1; echo t=\$?"
		TO="xgs-ftw -t disable"
	fi
	if [ "$(kv mode)" != normal ]; then
		DO="${DO:+${DO}; }${TOOL} -d 2>&1; echo d=\$?; sleep 1"
		TO="${TO:+${TO}, then }xgs-ftw -d"
		# said before anything is written, and once: the pair in bypass is the news, whatever follows
		if [ -z "${SAID}" ]; then
			case $(kv mode) in
			bypass) warn "relay pair ${PAIR} found in bypass ${WHEN}: ${FOUND}; running ${TO}" ;;
			*) warn "relay pair ${PAIR} found not normal ${WHEN}: ${FOUND}; running ${TO}" ;;
			esac
			SAID=1
		fi
	fi
	# A try whose session ran the tool and lost its answer, or lost the read after it, is made again
	# - three times at most, and then said as a pair that did not come back.
	ACTS=$((ACTS + 1))
	if [ ${ACTS} -gt 3 ]; then
		NOW=$(words)
		warn "relay pair ${PAIR} NOT back to normal ${WHEN}: ${TO} was run three times, and each time its" \
		    "answer or the read after it was lost; it reads ${NOW}; it was ${FOUND}"
		keep "NOT back to normal: ${TO} was run three times, its answer or the read after it lost each time;" \
		    "it reads ${NOW}"
		return 3
	fi
	tell "${DO}" || return 1
	RAN="${RAN:+${RAN}; }$(ran)"
	read_pair || return $?
	n=1
	while [ "$(kv mode)" != normal ] && [ ${n} -lt 3 ]; do
		sleep 1
		read_pair || return $?
		n=$((n + 1))
	done
	if [ "$(kv mode)" = normal ] && [ "$(kv trigger)" != 0 ]; then
		# the latch outlived -d: cleared now that the relay no longer holds the pair
		if tell "${TOOL} -t disable 2>&1; echo t=\$?"; then
			RAN="${RAN}; again $(ran)"
			read_pair || return $?
		fi
	fi
	NOW=$(words)
	if normal; then
		back "${RAN}"
		return 0
	fi
	hint=
	[ "$(kv mode)" = normal ] || hint=" - ${PAIR} still joined by the relay, past this firewall"
	warn "relay pair ${PAIR} NOT back to normal ${WHEN} after ${RAN}: ${NOW}${hint}; it was ${FOUND}"
	keep "NOT back to normal after ${RAN}: ${NOW}; it was ${FOUND}"
	return 3
}

if [ ${BOOT} = 0 ]; then
	once
	rc=$?
	case ${rc} in
	1) echo "the relay pair was not read: the coprocessor did not answer${RAN:+, after ${RAN}}" >&2 ;;
	2) echo "no relay to put back: ${WHY}" ;;
	esac
	exit ${rc}
fi

# At boot: the coprocessor may still be coming up, so a try it does not answer is made again GAP
# seconds on, TRIES times in all - about five minutes when each fails at once. A try the coprocessor
# does not answer is one read, inside the timeout around every session (FTW_SECS + NPU_RUN_WAIT + 60,
# 100 s), so the whole run ends when the coprocessor never answers: twenty times 115 s at the very
# most, which only a session that ran into its timeout every time would reach. A try that read the
# pair and had its answers ends the tries - found normal, put back, or told and still otherwise - and
# so does one that found no relay to read; the tool is run in three tries at most (ACTS).
i=0
while [ ${i} -lt "${TRIES}" ]; do
	once
	case $? in
	0|3) exit 0 ;;
	2) note "relay pair not read ${WHEN}: ${WHY}"; keep "not read: ${WHY}"; exit 0 ;;
	esac
	i=$((i + 1))
	[ ${i} -lt "${TRIES}" ] && sleep "${GAP}"
done
warn "relay pair ${PAIR} not read in the boot's ${TRIES} tries${SAID:+, after it was found not normal}${RAN:+ and ${RAN} ran}:" \
    "the coprocessor did not answer (ftw-status.sh says why)"
keep "not read in ${TRIES} tries${RAN:+, after ${RAN}}"
exit 1
