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
# THE TRANSPORT, by family. On the XGS 136 the coprocessor's sshd over the PCIe management link
# (scripts/npuctl/mgmt-link.sh), when the link is up. On the XGS 3300, which has no such link, and
# on a 136 whose link is down, its root console on /dev/cuau2: logged in only if the console asks,
# the command sent between two markers that are each written in two quoted halves (the console
# echoes the command line, and the echo must not match), the output cut out between them, logged out
# again if this logged in. <seconds> is how long the console is read for, so it has to cover the
# command: a command that has to outlast it is started in the background on the coprocessor.
#
# ONE AT A TIME on the console. A second caller waits for the first, up to a few seconds, and then
# gives up with exit 75; the start hook's switch-jumbo.sh, which reads the console at boot, is
# refused the same way while a reader holds it. Exit 0 with the output, 1 on a transport that did
# not answer, 64 on a usage error.

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

if [ "${FAMILY}" = armada ] && [ -x ${LIB}/npuctl/mgmt-link.sh ] &&
    /usr/bin/timeout 10 /bin/sh ${LIB}/npuctl/mgmt-link.sh ssh true > /dev/null 2>&1; then
	exec /usr/bin/timeout $(( SECS + 10 )) /bin/sh ${LIB}/npuctl/mgmt-link.sh ssh "${CMD}"
fi

# The console. Called again under lockf, which holds the lock for the whole session.
if [ -z "${NPU_RUN_LOCKED:-}" ]; then
	NPU_RUN_LOCKED=1 exec /usr/bin/lockf -k -t 5 "${LOCK}" /bin/sh "$0" "$@"
fi
[ -c "${DEV}" ] || { echo "no ${DEV}" >&2; exit 1; }
/bin/pgrep -f "cat ${DEV}" > /dev/null 2>&1 && { echo "another reader holds ${DEV}" >&2; exit 75; }

clean() { tr -d '\r' < "${TMP}" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g'; }
trap 'rm -f "${TMP}"' EXIT

stty -f "${DEV}.init" speed 115200 raw -echo > /dev/null 2>&1
stty -f "${DEV}" speed 115200 raw -echo > /dev/null 2>&1
/usr/bin/timeout $(( SECS + 6 )) cat "${DEV}" > "${TMP}" 2>/dev/null &
sleep 1; printf '\r' > "${DEV}"; sleep 1
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
