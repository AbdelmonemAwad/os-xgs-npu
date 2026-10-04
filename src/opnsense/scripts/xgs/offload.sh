#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Write the acceleration gate to the coprocessor, as the settings page left it, and report what
# the coprocessor said.
#
# FW_CFG_OFFLOAD is bit 0 of fw_cfg, which is RPC command 2. It is the one bit that decides whether
# the fast path may forward a frame instead of handing it to the host, and the coprocessor does not
# remember it: every boot it comes up at 0x2 - TCP_SEQ_CHK alone, offloading off - whatever the
# settings page says. So this script is the only thing that ever turns it on, and it has two
# callers: the settings page, straight after a save, and bringup.sh, which runs it at every boot
# with the gate already open.
#
# Usage:
#   offload.sh            read the setting from config.xml, apply it, print JSON
#   offload.sh on|off     apply that, print JSON - for testing, and for bringup.sh, which has
#                         already read the setting and should not pay for php twice
#
# WHY THE OTHER BITS ARE READ AND NOT ASSUMED. fw_cfg is one word and command 2 writes all of it.
# Writing a constant would mean this script silently decided every other bit in it, including
# TCP_SEQ_CHK, which is on by default and is a safety check. So the current value is read and bit 0
# is set or cleared in it, and nothing else in the word is touched by this script.
set -u

S=dev.octep.0
: ${LOGTAG:=octep}
: ${CONFIG_READER:=/usr/local/bin/php}

sc() { /sbin/sysctl "$@" > /dev/null 2>&1; }
scn() { /sbin/sysctl -n "$1" 2>/dev/null; }

log() { /usr/bin/logger -t "${LOGTAG}" -p daemon.notice "$1" 2>/dev/null || true; }

# JSON by hand, because the whole reply is three fields and a sentence. The sentence is the only
# part that could carry a quote, and every one of them is written below, so there is nothing here
# to escape - but it is printed through a single function so that stays true if one is ever added.
reply() {
	printf '{"applied":%s,"offload":%s,"message":"%s"}\n' "$1" "$2" "$3"
}

# ---------------------------------------------------------------------- what is wanted

want=
case ${1:-} in
on)	want=1 ;;
off)	want=0 ;;
'')	want= ;;
*)	reply false null "offload.sh takes on, off, or nothing at all"; exit 2 ;;
esac

if [ -z "${want}" ]; then
	if [ ! -x "${CONFIG_READER}" ]; then
		reply false null "cannot read the setting: ${CONFIG_READER} is not there"
		exit 1
	fi
	want=$("${CONFIG_READER}" -r \
	    'require_once("config.inc"); echo empty($config["OPNsense"]["XGS"]["general"]["offload"]) ? 0 : 1;' \
	    2>/dev/null)
	case ${want} in
	0|1)	;;
	*)	reply false null "cannot read the setting: config.xml gave nothing usable"; exit 1 ;;
	esac
fi

# ---------------------------------------------------------------------- is there anything to write to

# rpc.ready, and not rpc.state. state is a multi-line dump of the cfg word, the ring offsets and a
# command tally - useful to read, impossible to test - while ready is the one boolean that says the
# target has acknowledged a configuration. An absent sysctl means the driver is not loaded at all.
ready=$(scn ${S}.rpc.ready)
if [ -z "${ready}" ]; then
	reply false "${want}" "the driver is not loaded, so there is no gate to write"
	exit 1
fi

# The rpc facility has to be up. Writing command 2 into a facility that is not configured is not
# refused anywhere useful - the request is staged, posted, and the reply never comes - so it is
# checked here rather than waited for.
if [ "${ready}" != 1 ]; then
	reply false "${want}" "the rpc facility is not up yet, so the gate cannot be written"
	exit 1
fi

# ---------------------------------------------------------------------- write it

cur=$(scn ${S}.rpc.fw_cfg)
case ${cur} in
''|*[!0-9]*)	reply false "${want}" "fw_cfg did not read back as a number"; exit 1 ;;
esac

if [ "${want}" = 1 ]; then
	new=$((cur | 1))
else
	new=$((cur & 2147483646))
fi

# allow_write is the guard that stops a stray sysctl from reconfiguring the datapath, and it is
# left shut in steady state. Opening it for one write and shutting it again is what bringup.sh does
# for its own writes; the gate here is restored even if the write fails, which is why it is not
# left to the exit path.
opened=0
if [ "$(scn ${S}.rpc.allow_write)" = 0 ]; then
	sc ${S}.rpc.allow_write=1
	opened=1
fi

sc ${S}.rpc.fw_cfg=${new}
sc ${S}.rpc.cmd=2
sc ${S}.rpc.post=1
last=$(scn ${S}.rpc.last)

if [ "${opened}" = 1 ]; then
	sc ${S}.rpc.allow_write=0
fi

# ---------------------------------------------------------------------- what it answered
#
# rc is the transport's answer and not the operation's - a distinction this project has had to
# learn more than once - so a zero here means the coprocessor took the request, not that the fast
# path is now accelerating anything. The message says so in those words rather than claiming more.
rc=$(echo "${last}" | sed -n 's/.*rc \(0x[0-9a-f]*\).*/\1/p' | head -1)

case ${rc} in
0x0000)
	if [ "${want}" = 1 ]; then
		log "offload gate opened, fw_cfg 0x$(printf '%x' ${new})"
		reply true 1 "offloading is on: the coprocessor accepted fw_cfg 0x$(printf '%x' ${new})"
	else
		log "offload gate closed, fw_cfg 0x$(printf '%x' ${new})"
		reply true 0 "offloading is off: the coprocessor accepted fw_cfg 0x$(printf '%x' ${new})"
	fi
	exit 0 ;;
'')
	reply false "${want}" "the coprocessor did not answer the request at all"
	exit 1 ;;
*)
	reply false "${want}" "the coprocessor refused the request with ${rc}"
	exit 1 ;;
esac
