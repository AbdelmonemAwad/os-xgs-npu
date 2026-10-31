#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The coprocessor's kernel asked for its own state over its console - Magic SysRq: a serial BREAK,
# then one key - and what it printed kept in the console's log (console_log.py keep). For a
# coprocessor whose fast path has stopped answering (#819): its Linux may still take a BREAK when
# nothing else reaches it.
#
#   npu-sysrq.sh <keys> [seconds]    each key in turn, the console read <seconds> after each
#                                    (default 5): l all CPUs' backtraces, w blocked tasks,
#                                    p registers, m memory, q timers, t every task (long)
#
# THE XGS 3300 FAMILY ONLY: its coprocessor's console is DEV, and its kernel has sysrq 1 - a BREAK
# followed by h printed SysRq's help there on 2026-10-31. Only keys that print are taken: nothing
# here may reboot, crash, kill, sync or unmount.
# NOT ON THE BOOT FIRMWARE, whose menus take a BREAK as a keystroke: only once the driver has seen
# this boot's coprocessor come up and, while its heartbeat is dead, for no longer than the
# coprocessor's own watchdog leaves a hung one before resetting it - about a minute after its last
# feed (#768).
# Shares the console as npu-run.sh does: the logger told to stand aside, the same lock.
LIB=/usr/local/opnsense/scripts
DEV=/dev/cuau2
LOCK=/var/run/xgs-npu-console.lock
TMP=/var/run/xgs-npu-sysrq.$$
KEYS=${1:-}
SECS=${2:-5}

case ${KEYS} in
''|*[!lwpmqt]*) echo "usage: npu-sysrq.sh <keys from l w p m q t> [seconds]" >&2; exit 64 ;;
esac
case ${SECS} in
''|*[!0-9]*) echo "usage: npu-sysrq.sh <keys> [seconds]" >&2; exit 64 ;;
esac
[ "$(/bin/sh ${LIB}/xgs/board.sh 2>/dev/null | /usr/bin/awk '{print $2}')" = octeontx ] ||
    { echo "only the XGS 3300 family's coprocessor console is asked this way" >&2; exit 1; }
[ -c "${DEV}" ] || { echo "no ${DEV}" >&2; exit 1; }
case "$(/sbin/sysctl -n dev.octep.0.sdp.hs_state 2>/dev/null)" in
completed*) ;;
*) echo "the coprocessor has not come up on this boot; its console is left alone" >&2; exit 1 ;;
esac
if [ "$(/sbin/sysctl -n dev.octep.0.heartbeat.state 2>/dev/null)" = dead ]; then
	since=$(/sbin/sysctl -n dev.octep.0.heartbeat.since_answer_ms 2>/dev/null)
	[ "${since:-999999}" -lt 55000 ] ||
	    { echo "its fast path last answered ${since:-?} ms ago: its own watchdog may have reset it into its boot firmware" >&2; exit 1; }
fi

if [ -z "${NPU_SYSRQ_LOCKED:-}" ]; then
	/usr/bin/touch /var/run/xgs-console-log.pause
	NPU_SYSRQ_LOCKED=1 exec /usr/bin/lockf -k -t 10 "${LOCK}" /bin/sh "$0" "$@"
fi
trap '[ -s "${TMP}" ] && /usr/local/bin/python3 ${LIB}/xgs/console_log.py keep "${TMP}" "npu-sysrq.sh ${KEYS}" > /dev/null 2>&1; rm -f "${TMP}" /var/run/xgs-console-log.pause' EXIT

sleep 1
stty -f "${DEV}.init" speed 115200 raw -echo > /dev/null 2>&1
stty -f "${DEV}" speed 115200 raw -echo > /dev/null 2>&1
n=$(printf '%s' "${KEYS}" | wc -c)
/usr/bin/timeout $(( n * (SECS + 1) + 3 )) cat "${DEV}" > "${TMP}" 2>/dev/null &
rd=$!
sleep 0.5
for key in $(printf '%s' "${KEYS}" | sed 's/./& /g'); do
	/usr/local/bin/python3 -c 'import os, sys, termios, time
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
termios.tcsendbreak(fd, 0)
time.sleep(0.3)
os.write(fd, sys.argv[2].encode())
os.close(fd)' "${DEV}" "${key}"
	sleep "${SECS}"
done
kill ${rd} 2> /dev/null; wait 2> /dev/null
tr -d '\r' < "${TMP}" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g'
exit 0
