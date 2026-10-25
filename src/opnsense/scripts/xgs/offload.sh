#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
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
# is set or cleared in it - and since 2026-10-23 bit 2, the IPS switch, follows "Cut in hardware" -
# and nothing else in the word is touched by this script.
set -u

S=dev.octep.0
: ${LOGTAG:=octep}
: ${CONFIG_READER:=/usr/local/bin/php}
# Where the ARMADA branch keeps the word the coprocessor accepted, for the status page: that family
# cannot be asked for it. Not under /var/run: the boot hook writes the gate before cleanvar empties
# that directory (measured 2026-10-14: the hook's write logged, the file gone by the time the page
# asked). So the word is kept with the boot it belongs to - kern.boottime - and the status reads it
# as written-since-boot only while the two agree.
: ${STATE:=/var/db/os-xgs-npu/offload-fw_cfg}
: ${BOOT_TOKEN:=/var/run/xgs-npuep-boot}

sc() { /sbin/sysctl "$@" > /dev/null 2>&1; }
scn() { /sbin/sysctl -n "$1" 2>/dev/null; }

log() { /usr/bin/logger -t "${LOGTAG}" -p daemon.notice "$1" 2>/dev/null || true; }

# JSON by hand, because the whole reply is three fields and a sentence. The sentence is the only
# part that could carry a quote, and every one of them is written below, so there is nothing here
# to escape - but it is printed through a single function so that stays true if one is ever added.
reply() {
	# A refusal is logged as well as returned: the boot hook's caller cannot show it, and a
	# write-only gate that silently stays closed looks like success (issue 406).
	[ "$1" = false ] && log "gate not written: $3"
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

# The fast path's IPS switch, bit 2 (0x004), written with the gate: XGS: Settings "Cut in hardware"
# makes connections with the microflow action IPS, and only with this switch on does the fast path
# ask their verdict - so a cut can drop a connection there (discovery 3). Off, an IPS-action frame
# is sent on to the wire unasked, as a FWD one is. On unless the page turned it off; XGS_CUT_HW
# gives the answer to a caller that has already read it.
cut_hw=${XGS_CUT_HW:-}
if [ -z "${cut_hw}" ] && [ -x "${CONFIG_READER}" ]; then
	cut_hw=$("${CONFIG_READER}" -r \
	    'require_once("config.inc"); $g = $config["OPNsense"]["XGS"]["general"] ?? []; echo (isset($g["cut_in_hardware"]) && $g["cut_in_hardware"] === "0") ? 0 : 1;' \
	    2>/dev/null)
fi
case ${cut_hw} in
0)	ips=0 ;;
*)	ips=4 ;;
esac

# The fast path's copies for a packet capture, bit 4 (0x010, FP_PKT_DUMP), written with the gate:
# XGS: Settings "Copies for packet capture" has the coprocessor send a copy of every frame it
# forwards, which the driver shows to that port's capture and nothing else (discoveries 12 and 17,
# 2026-10-25). Off unless the page turned it on; XGS_COPIES gives the answer to a caller that has
# already read it.
copies=${XGS_COPIES:-}
if [ -z "${copies}" ] && [ -x "${CONFIG_READER}" ]; then
	copies=$("${CONFIG_READER}" -r 	    'require_once("config.inc"); $g = $config["OPNsense"]["XGS"]["general"] ?? []; echo (isset($g["capture_copies"]) && $g["capture_copies"] === "1") ? 1 : 0;' 	    2>/dev/null)
fi
case ${copies} in
1)	mrr=16 ;;
*)	mrr=0 ;;
esac

# ---------------------------------------------------------------------- which coprocessor

# One family per appliance. The ARMADA's driver (npuep) has no fw_cfg sysctl, and that coprocessor
# cannot be asked for the word at all, so its gate is written by hand on the driver's rpc channel:
# command 2 with the four-byte word, and the reply's own text read for the rc. What is written is
# TCP_SEQ_CHK (0x2, what the fast path holds at boot) with bit 0 set or cleared, and bit 2 as "Cut in
# hardware" says (since 2026-10-23: 0x7 or 0x6 with it, 0x3 or 0x2 without), bit 4 as "Copies for
# packet capture" says (since 2026-10-25: 0x17 with all three) - on this family that
# is the one word this script decides, since there is nothing to read first, and the message says
# so. Measured 2026-10-14 (docs/npuep/offload-plan.md, step 2): rc 0 for both values, and the fast
# path's FORCED and MFLOW_NOT_ACTIVE counters moving as the OCTEON TX's do behind the same bit.
if [ -n "$(scn dev.npuep.0.rpc_channel.state)" ]; then
	S=dev.npuep.0
	LOGTAG=npuep
	case "$(scn ${S}.rpc_channel.state | head -1)" in
	open*)	;;
	*)	reply false "${want}" "the rpc channel to the coprocessor is not open yet, so the gate cannot be written"
		exit 1 ;;
	esac
	if [ "${want}" = 1 ]; then
		new=$((3 | ips | mrr))
	else
		new=$((2 | ips | mrr))
	fi
	# the word's low byte as two hex digits: "0${new}" was right only while the word stayed below 10
	sc ${S}.rpc_channel.command="02 $(printf '%02x' ${new}) 00 00 00"
	last=$(scn ${S}.rpc_channel.command)
	rc=$(echo "${last}" | sed -n 's/.*answered, rc \([0-9][0-9]*\).*/\1/p' | head -1)
	case ${rc} in
	0)
		mkdir -p "$(dirname "${STATE}")" 2>/dev/null || true
		# The stamp is the token the start hook (07-npuep-gate) leaves in /var/run for this boot,
		# or "-" before it has run. Not kern.boottime, which moves by a second when the clock is
		# stepped (seen 2026-10-14, ...447 then ...446 within one boot), and not kern.boot_id,
		# which this kernel does not have. /var/run is emptied at boot, so the token is the boot's.
		boot=$(cat "${BOOT_TOKEN}" 2>/dev/null); [ -n "${boot}" ] || boot=-
		printf '%s %s
' "${boot}" "${new}" > "${STATE}" 2>/dev/null || true
		if [ "${want}" = 1 ]; then
			log "offload gate opened, fw_cfg 0x$(printf '%x' ${new}) written (this family has no read-back)"
			reply true 1 "offloading is on: the coprocessor accepted fw_cfg 0x$(printf '%x' ${new}) (written, not read back: this family cannot be asked for the word)"
		else
			log "offload gate closed, fw_cfg 0x$(printf '%x' ${new}) written (this family has no read-back)"
			reply true 0 "offloading is off: the coprocessor accepted fw_cfg 0x$(printf '%x' ${new}) (written, not read back: this family cannot be asked for the word)"
		fi
		exit 0 ;;
	'')
		reply false "${want}" "the coprocessor did not answer the request at all"
		exit 1 ;;
	*)
		reply false "${want}" "the coprocessor refused the request with rc ${rc}"
		exit 1 ;;
	esac
fi

# ---------------------------------------------------------------------- is there anything to write to

# rpc.ready, and not rpc.state. state is a multi-line dump of the cfg word, the ring offsets and a
# command tally - useful to read, impossible to test - while ready is the one boolean that says the
# target has acknowledged a configuration. An absent sysctl means the driver is not loaded at all.
ready=$(scn ${S}.rpc.ready)
if [ -z "${ready}" ]; then
	reply false "${want}" "no coprocessor driver is loaded (neither octep nor npuep), so there is no gate to write"
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
new=$(( (new & ~20) | ips | mrr ))

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
