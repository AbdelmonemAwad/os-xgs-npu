#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Run one command on the coprocessor's own Linux and print its output - for the other plugins
# (os-linkhealth's port identify) and for the scripts here that need it.
#
#   npu-run.sh <seconds> <command>
#
# THE TRANSPORT. The coprocessor's sshd over the PCIe management link (scripts/npuctl/mgmt-link.sh),
# when the link is up and its ssh answers - the XGS 136's mvmgmt0, and the XGS 3300's oxmgmt0 since
# 2026-10-23. Otherwise its root console on /dev/cuau2: logged in only if the console asks,
# the command sent between two markers that are each written in two quoted halves (the console
# echoes the command line, and the echo must not match), the output cut out between them, logged out
# again if this logged in. <seconds> is how long the console is read for, so it has to cover the
# command: a command that has to outlast it is started in the background on the coprocessor.
#
# ONE AT A TIME on the console. A second caller waits for the first, up to a few seconds
# (NPU_RUN_WAIT, 5), and then gives up with exit 75; the start hook's switch-jumbo.sh, which reads the
# console at boot, holds the same lock for all of its console work. The watch over the coprocessor's
# Linux (feeder.py) goes first: THE WATCH FIRST below. Exit 0 with the output, 69 when the console
# showed no Linux prompt, 1 on another transport that did not answer, 64 on a usage error.

LIB=/usr/local/opnsense/scripts
DEV=/dev/cuau2
LOCK=/var/run/xgs-npu-console.lock
URGENT=/var/run/xgs-npu-console.urgent
TMP=/var/run/xgs-npu-run.$$

case ${1:-} in
''|*[!0-9]*) echo "usage: npu-run.sh <seconds> <command>" >&2; exit 64 ;;
esac
SECS=$1
CMD=${2:-}
[ -n "${CMD}" ] || { echo "usage: npu-run.sh <seconds> <command>" >&2; exit 64; }
case ${NPU_RUN_WAIT:-5} in
''|*[!0-9]*) WAIT=5 ;;
*) WAIT=${NPU_RUN_WAIT:-5} ;;
esac

FAMILY=$(/bin/sh ${LIB}/xgs/board.sh 2>/dev/null | /usr/bin/awk '{print $2}')
case ${FAMILY} in
armada|octeontx) ;;
*) echo "not an XGS appliance this project knows" >&2; exit 1 ;;
esac

# A coprocessor the driver has taken as gone (#768) is left to the recovery, recover.sh, which says
# so with NPU_RUN_RECOVER. Its handshake reads "completed" from before, so its console may be its
# boot firmware's by now; and the callers that ask every minute held the console's lock when the
# recovery came to ask it to reboot, on 2026-10-26, and it was refused.
case ${FAMILY} in
octeontx) GONE=dev.octep.0.gone ;;
armada) GONE=dev.npuep.0.gone ;;
esac
if [ "$(/sbin/sysctl -n ${GONE} 2>/dev/null)" = 1 ] && [ -z "${NPU_RUN_RECOVER:-}" ]; then
	echo "the driver has taken the coprocessor as gone; it is left to the recovery" >&2
	exit 1
fi

# The link first, on both families since 2026-10-23 (the XGS 3300's oxmgmt0) - but not while it is
# the link being set up that asks (mgmt-link.sh coprocessor and trust, NPU_RUN_NO_LINK), nor in this
# script's second run under the console's lock below (NPU_RUN_LOCKED): the first run tried the link a
# moment before, and trying it again held the console for nothing, as long as ssh waits for a link or
# an sshd that does not answer (the second XGS 3300, 2026-11-04: an 8 s look held it 21.5 and 22.4 s,
# a 4 s ask that skips the link 11 s).
if [ -z "${NPU_RUN_NO_LINK:-}" ] && [ -z "${NPU_RUN_LOCKED:-}" ] && [ -x ${LIB}/npuctl/mgmt-link.sh ] &&
    /usr/bin/timeout 10 /bin/sh ${LIB}/npuctl/mgmt-link.sh ssh true > /dev/null 2>&1; then
	# Said on stderr to feeder.py, which asks (NPU_RUN_SAY_LINK): a look answered over the link's ssh is
	# what its watch over a stopped userland weighs - ssh working, and then not. Not to the other
	# callers, which show the last thing said on stderr as their reason when nothing answers.
	[ -n "${NPU_RUN_SAY_LINK:-}" ] && echo "over the link" >&2
	exec /usr/bin/timeout $(( SECS + 10 )) /bin/sh ${LIB}/npuctl/mgmt-link.sh ssh "${CMD}"
fi

# The console, which is also the coprocessor's boot firmware's: its menus take what is typed as
# choices, and a keystroke can stop its countdown. So it is used only once the driver has seen this
# boot's coprocessor come up - the 3300's handshake completed, the 136's network agent ready - and
# only while it shows a Linux prompt (#559).
case ${FAMILY} in
octeontx)
	case "$(/sbin/sysctl -n dev.octep.0.sdp.hs_state 2>/dev/null)" in
	completed*) ;;
	*) echo "the coprocessor has not come up on this boot; its console is left alone" >&2; exit 1 ;;
	esac
	;;
armada)
	[ "$(/sbin/sysctl -n dev.npuep.0.nwa.ready 2>/dev/null)" = 1 ] ||
	    { echo "the coprocessor has not come up on this boot; its console is left alone" >&2; exit 1; }
	;;
esac

# THE WATCH FIRST. feeder.py asks the console with NPU_RUN_URGENT when the management link has gone
# silent: its answer has to come before the coprocessor's own watchdog resets it, about 55 s after its
# feeder stopped (#768), at the very time every other reader falls back to the console too. It marks
# URGENT first, and while the mark is fresh every other caller gives the console up at once with 75 -
# before it queues for the lock, and again once it has it, for one that had queued before the mark -
# so that the watch waits for no more than the session already under way (#1002, #1012). Not the
# recovery (NPU_RUN_RECOVER): the watch has decided by then, and a mark left by a feeder.py that
# ended while it asked must not hold it back.
wanted() {
	[ -z "${NPU_RUN_URGENT:-}" ] && [ -z "${NPU_RUN_RECOVER:-}" ] || return 1
	m=$(/usr/bin/stat -f %m "${URGENT}" 2>/dev/null) || return 1
	# younger than a minute, and not from the future: with the clock stepped back - as it can be at
	# boot - a mark left behind refused every caller until the clock caught up with it
	d=$(( $(/bin/date +%s) - m ))
	[ "${d}" -ge 0 ] && [ "${d}" -lt 60 ]
}

# Called again under lockf, which holds the lock for the whole session. The console's logger
# (console_log.py) holds the port under the same lock: asked to stand aside for this session, it lets
# go within half a second.
if [ -z "${NPU_RUN_LOCKED:-}" ]; then
	wanted && { echo "the console is kept for the watch over the coprocessor" >&2; exit 75; }
	/usr/bin/touch /var/run/xgs-console-log.pause
	NPU_RUN_LOCKED=1 exec /usr/bin/lockf -k -t "${WAIT}" "${LOCK}" /bin/sh "$0" "$@"
fi
if wanted; then
	# refused once it has the lock, for a caller that queued before the mark: the logger let back in,
	# as at the end of every session - the trap that does it is not set yet, and the logger stood aside
	# until another session's end, or five minutes
	rm -f /var/run/xgs-console-log.pause
	echo "the console is kept for the watch over the coprocessor" >&2
	exit 75
fi
# (the two refusals below let the logger back in as well, before the trap that would is set)
[ -c "${DEV}" ] || { rm -f /var/run/xgs-console-log.pause; echo "no ${DEV}" >&2; exit 1; }
/bin/pgrep -f "cat ${DEV}" > /dev/null 2>&1 &&
    { rm -f /var/run/xgs-console-log.pause; echo "another reader holds ${DEV}" >&2; exit 75; }

clean() { tr -d '\r' < "${TMP}" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g'; }
# What the coprocessor printed while this held its console goes into the console's log, not away
# with TMP - before the logger is let back in, so the two do not interleave. On 2026-10-31 the log
# kept the first line of a kernel oops on the coprocessor and nothing after it (#819). Asked twice,
# both reads: the first, kept in FIRST while the second takes TMP over, is what a console found
# without a prompt - and a quiesce - rests on.
FIRST=
ended() {
	[ -n "${FIRST}" ] && { cat "${TMP}" >> "${FIRST}" 2>/dev/null; mv -f "${FIRST}" "${TMP}"; }
	[ -s "${TMP}" ] && /usr/local/bin/python3 ${LIB}/xgs/console_log.py keep "${TMP}" npu-run.sh > /dev/null 2>&1
	rm -f "${TMP}" "${TMP}.0" /var/run/xgs-console-log.pause
}
trap ended EXIT

UNIT=${DEV#/dev/cuau}
# The host's UART's interrupts so far: a byte written to it is followed by at least one.
irqs() { /usr/bin/vmstat -i 2>/dev/null | /usr/bin/awk -v u="uart${UNIT}" '$2 == u {print $3}'; }
# A fresh read of the console, and a carriage return for it to answer - with "interrupt", a Ctrl-C a
# second before the carriage return (the second ask, below).
listen() {
	stty -f "${DEV}.init" speed 115200 raw -echo > /dev/null 2>&1
	stty -f "${DEV}" speed 115200 raw -echo > /dev/null 2>&1
	/usr/bin/timeout $(( SECS + 6 )) cat "${DEV}" > "${TMP}" 2>/dev/null &
	rd=$!
	n0=$(irqs)
	sleep 1
	[ -n "${1:-}" ] && { printf '\003' > "${DEV}"; sleep 1; }
	printf '\r' > "${DEV}"; sleep 1
}
listen
if [ "$(irqs)" = "${n0}" ]; then
	# Written, and not one interrupt: the host's UART has lost its interrupt, and sends nothing more
	# until it is attached again - the first XGS 3300's for a whole boot on 2026-10-26, which left
	# the coprocessor's console unreachable (#768). Detached and attached by its ACPI handle: nothing
	# is sent to the coprocessor by it.
	kill ${rd} 2> /dev/null; wait
	where=$(/sbin/sysctl -n "dev.uart.${UNIT}.%location" 2>/dev/null | sed -n 's/.*handle=\([^ ]*\).*/\1/p')
	if [ -n "${where}" ] && /usr/sbin/devctl detach "uart${UNIT}" > /dev/null 2>&1 &&
	    /usr/sbin/devctl attach "${where}" > /dev/null 2>&1 && sleep 1 && [ -c "${DEV}" ]; then
		echo "the host's UART for the console had stopped interrupting; it was attached again" >&2
		listen
	else
		echo "the host's UART for the console has stopped interrupting, and could not be attached again" >&2
		exit 1
	fi
fi
# Its last line has to be Linux's login prompt or the root shell's '#'; anything else gets nothing more.
prompt() { clean | grep -v '^[[:space:]]*$' | tail -1 | grep -q -E 'login:[[:space:]]*$|#[[:space:]]*$'; }
if ! prompt && [ -n "${NPU_RUN_TWICE:-}" ]; then
	# Asked for by feeder.py, which takes a console that shows no prompt for a Linux that has stopped:
	# asked once more on a fresh read, a few seconds on - a login that was asking for a password, or a
	# getty slow to answer, prompts by then; a Linux that has stopped does not. A Ctrl-C a second before
	# that carriage return: a shell left in a continuation prompt - one quote of a command line lost on
	# a console that drops characters - or running a command in the foreground prompts after it, where
	# a carriage return alone left it as it was; a stopped one prompts after neither. To the boot
	# firmware, whose countdown any key stops, it is one key more like the carriage return, and the
	# console is used only once this boot's Linux has come up (a review of the watch, 2026-11-04).
	kill ${rd} 2> /dev/null; wait
	FIRST=${TMP}.0
	cat "${TMP}" > "${FIRST}" 2>/dev/null
	sleep 3
	listen interrupt
fi
if ! prompt; then
	kill ${rd} 2> /dev/null; wait
	echo "the coprocessor's console shows no Linux prompt; nothing was sent" >&2
	exit 69
fi
in=0
if clean | grep -q 'login:'; then
	printf 'root\r' > "${DEV}"; sleep 2
	clean | grep -q 'assword' && { printf '\r' > "${DEV}"; sleep 2; }
	in=1
fi
printf 'echo "---NPU""BEGIN---"; %s; echo "---NPU""END---"\r' "${CMD}" > "${DEV}"
sleep "${SECS}"
[ ${in} = 1 ] && printf 'exit\r' > "${DEV}"
wait
OUT=$(clean | awk '/^---NPUBEGIN---/ {on=1; seen=1; next} /^---NPUEND---/ {on=0; done=1} on; END {exit !done}')
rc=$?
[ -n "${OUT}" ] && echo "${OUT}"
[ ${rc} = 0 ] || { echo "the coprocessor's console did not answer within ${SECS}s" >&2; exit 1; }
exit 0
