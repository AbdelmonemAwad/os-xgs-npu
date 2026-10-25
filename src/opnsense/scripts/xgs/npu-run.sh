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
# ONE AT A TIME on the console. A second caller waits for the first, up to a few seconds, and then
# gives up with exit 75; the start hook's switch-jumbo.sh, which reads the console at boot, holds
# the same lock for all of its console work. Exit 0 with the output, 1 on a transport that did not
# answer, 64 on a usage error.

LIB=/usr/local/opnsense/scripts
DEV=/dev/cuau2
LOCK=/var/run/xgs-npu-console.lock
TMP=/var/run/xgs-npu-run.$$

case ${1:-} in
''|*[!0-9]*) echo "usage: npu-run.sh <seconds> <command>" >&2; exit 64 ;;
esac
SECS=$1
CMD=${2:-}
[ -n "${CMD}" ] || { echo "usage: npu-run.sh <seconds> <command>" >&2; exit 64; }

FAMILY=$(/bin/sh ${LIB}/xgs/board.sh 2>/dev/null | /usr/bin/awk '{print $2}')
case ${FAMILY} in
armada|octeontx) ;;
*) echo "not an XGS appliance this project knows" >&2; exit 1 ;;
esac

# The link first, on both families since 2026-10-23 (the XGS 3300's oxmgmt0) - but not while it is
# the link being set up that asks (mgmt-link.sh coprocessor and trust, NPU_RUN_NO_LINK).
if [ -z "${NPU_RUN_NO_LINK:-}" ] && [ -x ${LIB}/npuctl/mgmt-link.sh ] &&
    /usr/bin/timeout 10 /bin/sh ${LIB}/npuctl/mgmt-link.sh ssh true > /dev/null 2>&1; then
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

# Called again under lockf, which holds the lock for the whole session. The console's logger
# (console_log.py) holds the port under the same lock: asked to stand aside for this session, it lets
# go within half a second.
if [ -z "${NPU_RUN_LOCKED:-}" ]; then
	/usr/bin/touch /var/run/xgs-console-log.pause
	NPU_RUN_LOCKED=1 exec /usr/bin/lockf -k -t 5 "${LOCK}" /bin/sh "$0" "$@"
fi
[ -c "${DEV}" ] || { echo "no ${DEV}" >&2; exit 1; }
/bin/pgrep -f "cat ${DEV}" > /dev/null 2>&1 && { echo "another reader holds ${DEV}" >&2; exit 75; }

clean() { tr -d '\r' < "${TMP}" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g'; }
trap 'rm -f "${TMP}" /var/run/xgs-console-log.pause' EXIT

stty -f "${DEV}.init" speed 115200 raw -echo > /dev/null 2>&1
stty -f "${DEV}" speed 115200 raw -echo > /dev/null 2>&1
/usr/bin/timeout $(( SECS + 6 )) cat "${DEV}" > "${TMP}" 2>/dev/null &
rd=$!
sleep 1; printf '\r' > "${DEV}"; sleep 1
# Its last line has to be Linux's login prompt or the root shell's '#'; anything else gets nothing more.
if ! clean | grep -v '^[[:space:]]*$' | tail -1 | grep -q -E 'login:[[:space:]]*$|#[[:space:]]*$'; then
	kill ${rd} 2> /dev/null; wait
	echo "the coprocessor's console shows no Linux prompt; nothing was sent" >&2
	exit 1
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
