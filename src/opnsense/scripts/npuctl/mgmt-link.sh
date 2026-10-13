#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The management link to the ARMADA coprocessor, mvmgmt0: a point-to-point Ethernet over PCIe with
# a fixed link-local pair, 169.254.254.1 at this host and 169.254.254.2 at the coprocessor.
# Measured on the XGS 136 (issue 374): the link establishes when this end comes up, the host
# reaches the coprocessor at once, and the coprocessor reaches the host only once a rule admits it;
# the coprocessor runs sshd, so the link is the fast channel to everything that used to go over
# the 115200 console.
#
#   mgmt-link.sh assign        make mvmgmt0 an OPNsense interface, NPUMGMT, with its address, and
#                              reload what that touches - idempotent, the installer runs it
#   mgmt-link.sh coprocessor   give the coprocessor its address over the console - at every boot,
#                              from the plugin's bootup hook, because its root filesystem forgets
#   mgmt-link.sh trust         put this host's key on the coprocessor: its authorized_keys is on a
#                              read-only root, so the vendor's own remount script opens it for the
#                              one appended line and closes it again
#   mgmt-link.sh status        the link, the addresses, the rule, the ssh - what works right now
#   mgmt-link.sh ssh [cmd]     a shell, or one command, on the coprocessor over the link
#
# Every path is quiet about a machine that has no mvmgmt0: the OCTEON TX appliance runs this
# plugin too and has nothing to do here.
set -u

: ${HOST_ADDR:=169.254.254.1}
: ${NPU_ADDR:=169.254.254.2}
: ${PREFIX_LEN:=30}
: ${CONSOLE:=/dev/cuau2}
: ${KEY:=/root/npu/id_ed25519_npu}
: ${PHP:=/usr/local/bin/php}
: ${ASSIGN:=/usr/local/opnsense/scripts/npuctl/mgmt-link.php}

log() { /usr/bin/logger -t npuctl -p daemon.notice "mgmt-link: $*"; echo "$*"; }

have_link() { /sbin/ifconfig mvmgmt0 > /dev/null 2>&1; }

# One line to the coprocessor's shell over the console, blind: nothing is read back here. The
# console is shared with the kernel log and with whoever has a reader on it, so this writes and
# waits and judges the result by the link afterwards, never by the console's echo.
console_say() {
	/bin/stty -f "${CONSOLE}.init" 115200 raw -echo 2>/dev/null || return 1
	printf '%s\r\n' "$1" > "${CONSOLE}" 2>/dev/null || return 1
	sleep "${2:-1}"
}

console_login() {
	console_say '' 1
	console_say 'root' 2
	console_say '' 1
}

case "${1:-status}" in
assign)
	have_link || { log "no mvmgmt0 on this machine - nothing to assign"; exit 0; }
	[ -x "${PHP}" ] || { log "no php - cannot write the assignment"; exit 1; }
	KEYNAME=$("${PHP}" "${ASSIGN}" assign "${HOST_ADDR}" "${PREFIX_LEN}") || { log "the assignment failed: ${KEYNAME}"; exit 1; }
	log "mvmgmt0 is interface ${KEYNAME} (NPUMGMT), ${HOST_ADDR}/${PREFIX_LEN}"
	/usr/local/sbin/configctl interface reconfigure "${KEYNAME}" > /dev/null 2>&1 || true
	/usr/local/sbin/configctl filter reload > /dev/null 2>&1 || true
	/sbin/ifconfig mvmgmt0 up 2>/dev/null || true
	log "interface reconfigured and the ruleset reloaded; run: mgmt-link.sh coprocessor"
	;;
coprocessor)
	have_link || exit 0
	[ -c "${CONSOLE}" ] || { log "no ${CONSOLE} - the coprocessor cannot be told its address"; exit 0; }
	/sbin/ifconfig mvmgmt0 up 2>/dev/null || true
	console_login
	console_say "ip addr replace ${NPU_ADDR}/${PREFIX_LEN} dev mvmgmt0; ip link set mvmgmt0 up" 2
	console_say 'exit' 1
	if /sbin/ping -c 2 -t 3 -q "${NPU_ADDR}" > /dev/null 2>&1; then
		log "the coprocessor answers at ${NPU_ADDR}"
	else
		log "the coprocessor does not answer at ${NPU_ADDR} yet - the link may still be establishing; try again in a minute"
	fi
	;;
trust)
	have_link || exit 0
	[ -c "${CONSOLE}" ] || { log "no ${CONSOLE}"; exit 1; }
	if [ ! -f "${KEY}" ]; then
		ssh-keygen -q -t ed25519 -N '' -f "${KEY}" -C "xgs-host-to-npu" || exit 1
		log "made ${KEY}"
	fi
	PUB=$(cat "${KEY}.pub")
	console_login
	# The vendor's remount script takes the root filesystem to rw and back; one appended line,
	# once, guarded by the key's own comment.
	console_say "xgs-remount.sh rw; grep -q 'xgs-host-to-npu' /root/.ssh/authorized_keys || echo '${PUB}' >> /root/.ssh/authorized_keys; xgs-remount.sh ro" 4
	console_say 'exit' 1
	if ssh -i "${KEY}" -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new "root@${NPU_ADDR}" true 2>/dev/null; then
		log "the coprocessor trusts ${KEY}.pub: ssh root@${NPU_ADDR} works"
	else
		log "ssh to ${NPU_ADDR} still refused - is the link up and the address set? (mgmt-link.sh status)"
		exit 1
	fi
	;;
status)
	have_link || { echo "no mvmgmt0 on this machine"; exit 0; }
	echo "mvmgmt0: $(/sbin/ifconfig mvmgmt0 | /usr/bin/grep -E 'flags|inet ' | /usr/bin/awk '{$1=$1; print}' | /usr/bin/tr '\n' '|')"
	echo "assignment: $(/usr/local/bin/php "${ASSIGN}" show 2>/dev/null || echo none)"
	echo "rules admitting ${NPU_ADDR}: $(/sbin/pfctl -sr 2>/dev/null | /usr/bin/grep -c "from ${NPU_ADDR} ")"
	echo "ping ${NPU_ADDR}: $(/sbin/ping -c 2 -t 3 -q "${NPU_ADDR}" 2>&1 | /usr/bin/grep -o '[0-9]* packets received')"
	echo "ssh: $(ssh -i "${KEY}" -o BatchMode=yes -o ConnectTimeout=6 "root@${NPU_ADDR}" 'echo ok, $(uname -r)' 2>&1 | /usr/bin/tail -1)"
	;;
ssh)
	shift
	exec ssh -i "${KEY}" -o BatchMode=yes -o ConnectTimeout=8 "root@${NPU_ADDR}" "$@"
	;;
*)
	echo "usage: $0 assign|coprocessor|trust|status|ssh [command]"
	exit 2
	;;
esac
