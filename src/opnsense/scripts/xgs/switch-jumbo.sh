#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Let the coprocessor's switch chip carry jumbo frames on every port, at every boot.
#
#   switch-jumbo.sh            each of the ten switch ports to 10,240 bytes; says what it found
#   switch-jumbo.sh --check    read only
#
# WHY. The front ports behind the 88E6193X - Port1 to Port8 and the two cages beside them, on both
# appliances - take their frame limit from that chip, not from the host or the coprocessor: Port
# Control 2 bits 13:12 are 1 (2,048 bytes) on all ten as the coprocessor leaves them, and 2
# (10,240) only on port 0, its own uplink. So an MTU above 2034 on one of those ports was accepted
# by the interface and then dropped by the switch. Measured 2026-10-17: with port 8 written to
# 0x2480 on both chips of a gigabit link, MTU 4000 to 9000 ran at line rate through it, clear text
# and IPsec (docs/measurements/mtu-2026-10-17.md). The write does not survive the coprocessor's
# own boot - its network agent sets the ports again - so it is made here, from the start hook,
# every time the appliance boots.
#
# HOW. With contrib/mvsw, the switch tool, run on the coprocessor: kept in its /persistent
# partition (ext4, read-write, on both families) and put there once, by this script, when it is
# missing or not the one this host carries. The transport is the coprocessor's sshd over the
# PCIe management link where there is one (the XGS 136, scripts/npuctl/mgmt-link.sh), and its
# root console on /dev/cuau2 otherwise (the XGS 3300).
#
# CAREFUL READS. The coprocessor's network agent polls the same MDIO bus, and about one read in ten
# returns another transaction's data. So every port is read three times and only a value two reads
# agree on is used; a port with no majority is left alone and tried again; the write keeps every
# other bit of the register as read; and the result is read three times again.
#
# Never fatal: the ports work without it at 2,048, and every outcome is logged.

LIB=/usr/local/opnsense/scripts
MVSW=${MVSW:-/usr/local/share/os-xgs-npu/mvsw}
NPU_DIR=/persistent/os-xgs-npu
NPU_MVSW=${NPU_DIR}/mvsw
DEV=/dev/cuau2
TMP=/var/run/switch-jumbo.$$
CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

log() {
	/usr/bin/logger -t xgs-switch -p daemon.notice "$1"
	echo "$1"
}
# The last outcome, with its time, for the Ports page - which shows it rather than asking the
# coprocessor itself, a question that costs seconds on one family and a console session on the other.
STATE=/var/db/os-xgs-npu/switch-jumbo.state
state() {
	mkdir -p "$(dirname "${STATE}")"
	echo "$(date -u '+%Y-%m-%d %H:%M UTC') $1" > "${STATE}"
}
clean() { tr -d '\r' < "${TMP}" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g'; }

FAMILY=$(/bin/sh ${LIB}/xgs/board.sh 2>/dev/null | /usr/bin/awk '{print $2}')
case ${FAMILY} in
armada|octeontx) ;;
*) log "not an XGS appliance this project knows - nothing to do"; exit 0 ;;
esac
[ -f "${MVSW}" ] || { log "no ${MVSW} - the switch tool is not installed"; exit 0; }

# The settings page can turn it off (switch_jumbo); it is on unless it says 0.
JUMBO=$(/usr/local/bin/php -r 'require_once("config.inc"); $g = $config["OPNsense"]["XGS"]["general"] ?? []; echo (isset($g["switch_jumbo"]) && $g["switch_jumbo"] === "0") ? "0" : "1";' 2>/dev/null)
if [ "${JUMBO}" = "0" ] && [ ${CHECK} = 0 ]; then
	log "jumbo frames on the switch ports turned off on the settings page - the coprocessor's 2,048 stands"
	state "off on the settings page: the switch ports stay at 2,048 bytes"
	exit 0
fi

USE_SSH=0
if [ "${FAMILY}" = armada ] && /bin/sh ${LIB}/npuctl/mgmt-link.sh ssh true > /dev/null 2>&1; then
	USE_SSH=1
fi

# One command on the coprocessor, its output on stdout. Over ssh, or the console: logged in only if
# the console asks, the output cut out between two markers sent in two quoted halves (the console
# echoes the command line), logged out again if this logged in.
npu_run() { # <command> [seconds]
	if [ ${USE_SSH} = 1 ]; then
		/bin/sh ${LIB}/npuctl/mgmt-link.sh ssh "$1" 2>/dev/null
		return
	fi
	pgrep -f "cat ${DEV}" > /dev/null 2>&1 && return 1
	stty -f "${DEV}.init" speed 115200 raw -echo > /dev/null 2>&1
	stty -f "${DEV}" speed 115200 raw -echo > /dev/null 2>&1
	timeout $(( ${2:-8} + 6 )) cat "${DEV}" > "${TMP}" 2>/dev/null &
	sleep 1; printf '\r' > "${DEV}"; sleep 1
	in=0
	if clean | grep -q 'login:'; then
		printf 'root\r' > "${DEV}"; sleep 2
		clean | grep -q 'assword' && { printf '\r' > "${DEV}"; sleep 2; }
		in=1
	fi
	printf 'echo "---SW""BEGIN---"; %s; echo "---SW""END---"\r' "$1" > "${DEV}"
	sleep "${2:-8}"
	[ ${in} = 1 ] && printf 'exit\r' > "${DEV}"
	wait
	clean | awk '/^---SWBEGIN---/ {on=1; next} /^---SWEND---/ {on=0} on'
	rm -f "${TMP}"
}

# The tool on the coprocessor, put there once. Over ssh in one stream; over the console as base64
# in 500-character lines a second apart (1,000-character lines arrived garbled on the XGS 136), the
# text's md5 compared before decoding and the file's after.
WANT=$(md5 -q "${MVSW}")
HAVE=$(npu_run "md5sum ${NPU_MVSW} 2>/dev/null" 6 | awk '{print $1}' | tail -1)
if [ "${HAVE}" != "${WANT}" ]; then
	if [ ${CHECK} = 1 ]; then
		log "the switch tool is not on the coprocessor (--check: nothing written)"
		exit 0
	fi
	log "putting the switch tool on the coprocessor (${NPU_MVSW})"
	if [ ${USE_SSH} = 1 ]; then
		/bin/sh ${LIB}/npuctl/mgmt-link.sh ssh \
		    "mkdir -p ${NPU_DIR} && cat > ${NPU_MVSW}.new && chmod 755 ${NPU_MVSW}.new && mv ${NPU_MVSW}.new ${NPU_MVSW}" \
		    < "${MVSW}" 2>/dev/null
	else
		B64=/var/run/switch-jumbo.b64
		openssl base64 -A -in "${MVSW}" > "${B64}"
		TXT=$(md5 -q "${B64}")
		N=$(( $(fold -w 500 "${B64}" | wc -l) + 1 ))
		pgrep -f "cat ${DEV}" > /dev/null 2>&1 && { log "a reader holds ${DEV}; not shipped"; exit 0; }
		timeout $(( N + 30 )) cat "${DEV}" > "${TMP}" 2>/dev/null &
		sleep 1; printf '\r' > "${DEV}"; sleep 1
		in=0
		if clean | grep -q 'login:'; then
			printf 'root\r' > "${DEV}"; sleep 2
			clean | grep -q 'assword' && { printf '\r' > "${DEV}"; sleep 2; }
			in=1
		fi
		printf 'mkdir -p %s; rm -f %s.b64\r' "${NPU_DIR}" "${NPU_MVSW}" > "${DEV}"; sleep 1
		# fold leaves the last line without a newline, and a while-read loop skips such a line.
		{ fold -w 500 "${B64}"; echo; } | while IFS= read -r line; do
			[ -n "${line}" ] || continue
			printf 'echo -n "%s" >> %s.b64\r' "${line}" "${NPU_MVSW}" > "${DEV}"
			sleep 1
		done
		[ ${in} = 1 ] && printf 'exit\r' > "${DEV}"
		wait
		rm -f "${TMP}" "${B64}"
		GOT=$(npu_run "md5sum ${NPU_MVSW}.b64" 6 | awk '{print $1}' | tail -1)
		if [ "${GOT}" != "${TXT}" ]; then
			log "the switch tool arrived garbled (text md5 ${GOT:-none}, want ${TXT}); not used"
			exit 0
		fi
		npu_run "base64 -d ${NPU_MVSW}.b64 > ${NPU_MVSW} && chmod 755 ${NPU_MVSW} && rm -f ${NPU_MVSW}.b64" 6 > /dev/null
	fi
	HAVE=$(npu_run "md5sum ${NPU_MVSW} 2>/dev/null" 6 | awk '{print $1}' | tail -1)
	if [ "${HAVE}" != "${WANT}" ]; then
		log "the switch tool on the coprocessor does not match this host's (${HAVE:-none}); nothing written"
		exit 0
	fi
fi

# Port Control 2 of ports 1 to 10, three reads each, in one call.
read_all() {
	npu_run "for p in 1 2 3 4 5 6 7 8 9 10; do for i in 1 2 3; do echo \$p \$(${NPU_MVSW} 0 2 read \$p 8); done; done" 12
}
majority() { # <port> <file>: the value two of its three reads agree on, or nothing
	awk -v p="$1" '$1 == p {n[$2]++} END {for (v in n) if (n[v] >= 2) print v}' "$2" | head -1
}

R=/var/run/switch-jumbo.reads
round=0
while [ ${round} -lt 3 ]; do
	round=$((round + 1))
	read_all > "${R}"
	writes=""
	todo=0
	summary=""
	for p in 1 2 3 4 5 6 7 8 9 10; do
		v=$(majority ${p} "${R}")
		if [ -z "${v}" ]; then
			summary="${summary} ${p}:?"
			todo=$((todo + 1))
			continue
		fi
		cur=$((v))
		if [ $(( (cur >> 12) & 3 )) -eq 2 ]; then
			summary="${summary} ${p}:10240"
			continue
		fi
		new=$(printf '0x%04x' $(( (cur & ~0x3000) | 0x2000 )))
		summary="${summary} ${p}:${v}"
		writes="${writes} ${NPU_MVSW} 0 2 write ${p} 8 ${new};"
		todo=$((todo + 1))
	done
	if [ ${todo} = 0 ]; then
		log "switch ports 1-10 carry jumbo frames (Port Control 2 at 10,240 on all ten)"
		[ ${CHECK} = 0 ] && state "ports 1-10 at 10,240 bytes, confirmed"
		rm -f "${R}"
		exit 0
	fi
	if [ ${CHECK} = 1 ]; then
		log "switch ports, Port Control 2 as read:${summary} (--check: nothing written)"
		rm -f "${R}"
		exit 0
	fi
	[ -n "${writes}" ] && npu_run "${writes} true" 8 > /dev/null
	log "round ${round}:${summary} - written where a majority read 2,048"
	sleep 2
done
read_all > "${R}"
left=""
for p in 1 2 3 4 5 6 7 8 9 10; do
	v=$(majority ${p} "${R}")
	[ -n "${v}" ] && [ $(( (v >> 12) & 3 )) -eq 2 ] || left="${left} ${p}"
done
rm -f "${R}"
if [ -z "${left}" ]; then
	log "switch ports 1-10 carry jumbo frames (Port Control 2 at 10,240 on all ten)"
	state "ports 1-10 at 10,240 bytes, confirmed"
else
	log "switch ports not confirmed at 10,240 after three rounds:${left} - they stay at what the coprocessor set"
	state "not confirmed after three rounds on ports${left}"
fi
exit 0
