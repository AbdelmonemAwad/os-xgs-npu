#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The management link to the coprocessor: a point-to-point Ethernet over PCIe with a fixed
# link-local pair, 169.254.254.1 at this host and 169.254.254.2 at the coprocessor, whose own end is
# mvmgmt0 on both families. This host's end is mvmgmt0 on the XGS 136 (ARMADA), up from the
# driver's load, and oxmgmt0 on the XGS 3300 (OCTEON TX), made at the driver's load and down until
# the link is started once the coprocessor is ready (dev.octep.0.mgmt_start). Measured on the XGS 136 (issue
# 374) and on the XGS 3300 (2026-10-23): the host reaches the coprocessor at once, the coprocessor
# reaches the host only once a rule admits it; the coprocessor runs sshd on both, so the link is
# the fast channel to everything that used to go over the 115200 console.
#
#   mgmt-link.sh assign        make the link an OPNsense interface, NPUMGMT, with its address, and
#                              reload what that touches - idempotent, the installer runs it
#   mgmt-link.sh start         the XGS 3300: start the link, waiting for the coprocessor to be ready
#   mgmt-link.sh coprocessor   give the coprocessor its address over the console - at every boot,
#                              from the plugin's bootup hook, because its root filesystem forgets;
#                              on the XGS 3300 the link is started and assigned here first
#   mgmt-link.sh trust         put this host's key on the coprocessor: its authorized_keys is on a
#                              read-only root, so the vendor's own remount script opens it for the
#                              one appended line and closes it again; `coprocessor` runs it at boot
#                              when the link's ssh is refused
#   mgmt-link.sh status        the link, the addresses, the rule, the ssh - what works right now
#   mgmt-link.sh ssh [cmd]     a shell, or one command, on the coprocessor over the link
#
# Every path is quiet about a machine without the link.
set -u

: ${HOST_ADDR:=169.254.254.1}
: ${NPU_ADDR:=169.254.254.2}
: ${PREFIX_LEN:=30}
: ${CONSOLE:=/dev/cuau2}
: ${KEY:=/root/npu/id_ed25519_npu}
: ${PHP:=/usr/local/bin/php}
: ${ASSIGN:=/usr/local/opnsense/scripts/npuctl/mgmt-link.php}
: ${NPU_RUN:=/usr/local/opnsense/scripts/xgs/npu-run.sh}
: ${START_WAIT:=300}

FAMILY=$(/bin/sh /usr/local/opnsense/scripts/xgs/board.sh 2>/dev/null | /usr/bin/awk '{print $2}')
case "${FAMILY}" in
octeontx) LINK=oxmgmt0 ;;
*) LINK=mvmgmt0 ;;
esac

log() { /usr/bin/logger -t npuctl -p daemon.notice "mgmt-link: $*"; echo "$*"; }

have_link() { /sbin/ifconfig "${LINK}" > /dev/null 2>&1; }

# One line to the coprocessor's shell over the console, blind: nothing is read back here. The
# console is shared with the kernel log and with whoever has a reader on it, so this writes and
# waits and judges the result by the link afterwards, never by the console's echo. The XGS 136's.
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

# One command on the coprocessor's shell before the link carries ssh. On the XGS 3300 through
# npu-run.sh, never blind: its console is also its boot firmware's menu (#559), and the
# temperatures read it every minute, so it takes the console's lock, sends only to a Linux prompt,
# and is asked again while the console is busy. NPU_RUN_NO_LINK: it is the link being set up.
# The coprocessor restarts with the host, and its console prints its own start with no prompt for a
# while after the link is up - six tries in 40 s were all refused at a boot on 2026-10-23, and the
# coprocessor's end stayed without an address - so it is asked until START_WAIT has passed.
on_coprocessor() {
	if [ "${FAMILY}" = octeontx ]; then
		until=$(( $(/bin/date +%s) + START_WAIT ))
		while [ "$(/bin/date +%s)" -lt "${until}" ]; do
			NPU_RUN_NO_LINK=1 /bin/sh "${NPU_RUN}" 15 "$1" > /dev/null 2>&1 && return 0
			sleep 10
		done
		log "the coprocessor's console showed no prompt in ${START_WAIT} s - nothing was sent over it"
		return 1
	fi
	console_login
	console_say "$1" "${2:-2}"
	console_say 'exit' 1
}

# The XGS 3300's end carries nothing until the link is started, and the driver refuses to start it
# before the coprocessor says it is ready - which at boot can be a minute after this is asked.
start_link() {
	[ "${FAMILY}" = octeontx ] || return 0
	[ "$(/sbin/sysctl -n dev.octep.0.mgmt_up 2>/dev/null)" = 1 ] && return 0
	waited=0
	while [ "${waited}" -lt "${START_WAIT}" ]; do
		/sbin/sysctl -q dev.octep.0.mgmt_start=1 > /dev/null 2>&1
		if [ "$(/sbin/sysctl -n dev.octep.0.mgmt_up 2>/dev/null)" = 1 ]; then
			log "${LINK} started, ${waited} s after it was first asked"
			return 0
		fi
		sleep 5
		waited=$((waited + 5))
	done
	log "the coprocessor's management link would not start in ${START_WAIT} s - its console stays the way in"
	return 1
}

# The assignment, made if there is none, and its address put on the link: on the XGS 3300 the link
# comes up after OPNsense has configured its interfaces at boot, so it is configured again once up.
assign_link() {
	[ -x "${PHP}" ] || { log "no php - cannot write the assignment"; return 1; }
	KEYNAME=$("${PHP}" "${ASSIGN}" show "${LINK}" | /usr/bin/awk '{print $1}')
	if [ "${KEYNAME}" = none ] || [ -z "${KEYNAME}" ]; then
		KEYNAME=$("${PHP}" "${ASSIGN}" assign "${HOST_ADDR}" "${PREFIX_LEN}" "${LINK}") ||
		    { log "the assignment failed: ${KEYNAME}"; return 1; }
		log "${LINK} is interface ${KEYNAME} (NPUMGMT), ${HOST_ADDR}/${PREFIX_LEN}"
		/usr/local/sbin/configctl filter reload > /dev/null 2>&1 || true
	fi
	/usr/local/sbin/configctl interface reconfigure "${KEYNAME}" > /dev/null 2>&1 || true
	/sbin/ifconfig "${LINK}" up 2>/dev/null || true
	return 0
}

case "${1:-status}" in
assign)
	have_link || { log "no ${LINK} on this machine - nothing to assign"; exit 0; }
	[ -x "${PHP}" ] || { log "no php - cannot write the assignment"; exit 1; }
	KEYNAME=$("${PHP}" "${ASSIGN}" assign "${HOST_ADDR}" "${PREFIX_LEN}" "${LINK}") || { log "the assignment failed: ${KEYNAME}"; exit 1; }
	log "${LINK} is interface ${KEYNAME} (NPUMGMT), ${HOST_ADDR}/${PREFIX_LEN}"
	/usr/local/sbin/configctl interface reconfigure "${KEYNAME}" > /dev/null 2>&1 || true
	/usr/local/sbin/configctl filter reload > /dev/null 2>&1 || true
	/sbin/ifconfig "${LINK}" up 2>/dev/null || true
	log "interface reconfigured and the ruleset reloaded; run: mgmt-link.sh coprocessor"
	;;
start)
	start_link || exit 1
	;;
coprocessor)
	start_link || exit 0
	have_link || exit 0
	[ -c "${CONSOLE}" ] || { log "no ${CONSOLE} - the coprocessor cannot be told its address"; exit 0; }
	if [ "${FAMILY}" = octeontx ]; then
		assign_link || true
	else
		/sbin/ifconfig "${LINK}" up 2>/dev/null || true
	fi
	on_coprocessor "ip addr replace ${NPU_ADDR}/${PREFIX_LEN} dev mvmgmt0; ip link set mvmgmt0 up" 2
	if /sbin/ping -c 2 -t 3 -q "${NPU_ADDR}" > /dev/null 2>&1; then
		log "the coprocessor answers at ${NPU_ADDR}"
		# And takes this host's key, or is given it: until 2026-10-21 that was a step done by hand
		# once, so an appliance installed from an image never had the link's ssh, and every reader
		# of the coprocessor - PoE, the transceivers, health - queued on the console instead,
		# where a page that came second was told it was busy.
		if ! ssh -i "${KEY}" -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new \
		    "root@${NPU_ADDR}" true 2>/dev/null; then
			log "this host's key is not taken over the link; giving it to the coprocessor"
			exec /bin/sh "$0" trust
		fi
	else
		log "the coprocessor does not answer at ${NPU_ADDR} yet - the link may still be establishing; try again in a minute"
	fi
	;;
trust)
	have_link || exit 0
	[ -c "${CONSOLE}" ] || { log "no ${CONSOLE}"; exit 1; }
	if [ ! -f "${KEY}" ]; then
		/bin/mkdir -p -m 700 "$(/usr/bin/dirname "${KEY}")" || exit 1
		ssh-keygen -q -t ed25519 -N '' -f "${KEY}" -C "xgs-host-to-npu" || exit 1
		log "made ${KEY}"
	fi
	PUB=$(cat "${KEY}.pub")
	BODY=$(echo "${PUB}" | /usr/bin/awk '{print $2}')
	# The vendor's remount script takes the root filesystem to rw and back; one line, guarded by
	# the key itself. Until 2026-10-21 the guard was the comment, so a host installed again - a new
	# key under the same comment - found the old line, added nothing, and was refused: the XGS 136
	# reinstalled from an image. The coprocessor's file outlives the host's disk; a line of an
	# earlier install is replaced, not added to.
	on_coprocessor "xgs-remount.sh rw; grep -qF '${BODY}' /root/.ssh/authorized_keys || { sed -i '/xgs-host-to-npu/d' /root/.ssh/authorized_keys; echo '${PUB}' >> /root/.ssh/authorized_keys; }; xgs-remount.sh ro" 4
	if ssh -i "${KEY}" -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new "root@${NPU_ADDR}" true 2>/dev/null; then
		log "the coprocessor trusts ${KEY}.pub: ssh root@${NPU_ADDR} works"
	else
		log "ssh to ${NPU_ADDR} still refused - is the link up and the address set? (mgmt-link.sh status)"
		exit 1
	fi
	;;
status)
	have_link || { echo "no ${LINK} on this machine"; exit 0; }
	echo "${LINK}: $(/sbin/ifconfig "${LINK}" | /usr/bin/grep -E 'flags|inet ' | /usr/bin/awk '{$1=$1; print}' | /usr/bin/tr '\n' '|')"
	echo "assignment: $(/usr/local/bin/php "${ASSIGN}" show "${LINK}" 2>/dev/null || echo none)"
	echo "rules admitting ${NPU_ADDR}: $(/sbin/pfctl -sr 2>/dev/null | /usr/bin/grep -c "from ${NPU_ADDR} ")"
	echo "ping ${NPU_ADDR}: $(/sbin/ping -c 2 -t 3 -q "${NPU_ADDR}" 2>&1 | /usr/bin/grep -o '[0-9]* packets received')"
	echo "ssh: $(ssh -i "${KEY}" -o BatchMode=yes -o ConnectTimeout=6 "root@${NPU_ADDR}" 'echo ok, $(uname -r)' 2>&1 | /usr/bin/tail -1)"
	;;
ssh)
	shift
	have_link || exit 1
	exec ssh -i "${KEY}" -o BatchMode=yes -o ConnectTimeout=8 "root@${NPU_ADDR}" "$@"
	;;
*)
	echo "usage: $0 assign|start|coprocessor|trust|status|ssh [command]"
	exit 2
	;;
esac
