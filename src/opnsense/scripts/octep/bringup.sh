#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Bring the OCTEON TX coprocessor's datapath up, from nothing to twelve front-port interfaces.
#
# This is the whole sequence, in the only order that works, with every step's reason beside it.
# It is written to be run twice: from 08-octep at boot, and by hand afterwards when something is
# being changed. Every step that is already done is detected and skipped rather than repeated,
# because two of them are destructive if repeated.
#
# THE TWO THINGS YOU CANNOT REDO.
#
#   The SDP handshake arms once per COPROCESSOR boot. Re-arming it against a target that has
#   already completed does not restart anything; it leaves the scratch register saying something
#   the target is no longer listening for. So this reads sdp.hs_state first and only arms from
#   "idle".
#
#   The output rings are latched by the target when its port opens, and it does not look again.
#   They therefore have to be programmed BEFORE the handshake, and a dp.stop/dp.start afterwards
#   silently invalidates addresses the target is still writing into. This programs them first and
#   never restarts them.
#
# WHY IT TAKES TWO MINUTES. The coprocessor's own fast path has to finish starting before the RPC
# facility exists, and it publishes its barmap late - nothing re-reads that on its own, which is
# what rescan is for. The wait is the coprocessor's, not this script's, and shortening it does not
# make the far side faster; it makes the next call fail.
#
# WHAT IT DOES NOT DO. The two SFP cages' lasers are held off at the CPLD, which is on the
# coprocessor's SPI and out of reach from here - see docs/families/octeon-tx-peripherals.md. Those
# bits survive a reboot, so a cage that has been enabled once stays enabled; a cage that never has
# been needs one command on the coprocessor. The eight copper panel ports need nothing outside
# this script.

set -u

S=dev.octep.0
: ${MODULE:=/boot/modules/octep.ko}
: ${FASTPATH_WAIT:=140}
: ${RSIZE:=256}
# How long to let the coprocessor get to its own handshake before the first offer, and how many
# offers to make. Twelve at six seconds apart covers about a minute and a half, which brackets the
# window comfortably from any starting point a boot can produce.
: ${COPROC_WARMUP:=20}
: ${HS_ATTEMPTS:=12}
: ${NWA_WAIT:=40}
: ${LOGTAG:=octep}

log() {
	if [ -t 1 ]; then
		echo "$1"
	else
		/usr/bin/logger -t "${LOGTAG}" -p daemon.notice "$1"
	fi
}

sc() { /sbin/sysctl "$@" > /dev/null 2>&1; }
scn() { /sbin/sysctl -n "$1" 2>/dev/null; }
fail() { log "$1"; exit 1; }

# ---------------------------------------------------------------------- the module

if ! /sbin/kldstat -q -n octep; then
	[ -f "${MODULE}" ] || fail "no ${MODULE} - build it with contrib/octep/build.sh"
	/sbin/kldload "${MODULE}" 2>/dev/null || fail "kldload ${MODULE} failed"
	log "loaded ${MODULE}"
fi
[ -n "$(scn ${S}.sdp.hs_state)" ] || fail "no ${S} - the driver loaded but did not attach"

# ------------------------------------------------------ the rings, then the handshake

case "$(scn ${S}.sdp.hs_state)" in
idle*)
	# Eight output rings: ring 0 for this driver's own traffic and 1..7 as siblings, which is
	# the split the vendor's own fast path hashes across. The grant derives from the ring size
	# and the block's unit of sixteen bytes per buffer; dp.oq_grant=0 means "work it out".
	#
	# This happens once, here, and nowhere else in the script. Everything below may run again;
	# this may not.
	sc ${S}.dp.oq_rsize=${RSIZE} ${S}.dp.oq_grant=0
	sc ${S}.dp.ring=0 ${S}.dp.sib_base=1 ${S}.dp.siblings=7 ${S}.dp.port_tag=1
	sc ${S}.dp.start=1 || fail "dp.start failed"
	log "output rings armed"
	;;
esac

# THE HANDSHAKE HAS A WINDOW, AND AT BOOT THE HOOK ARRIVES BEFORE IT OPENS.
#
# The target polls the scratch register for about eleven seconds and then gives up, and it does not
# begin until its own boot has got that far - roughly half a minute after the host resets it. An
# early rc hook runs about ten seconds into the host's boot, which is too soon: the first attempt
# finds nobody listening, every time.
#
# So this warms up and then keeps offering. Re-arming is safe in exactly one state and this checks
# for it rather than assuming it: when the attempt times out, the driver puts the register BACK TO
# ZERO itself, so the next write is a first offer and not a second one. If the register is not zero
# the target is part-way through the exchange and writing over it is how a working link gets
# wedged - so that case stops and says so instead.
i=0
while [ ${i} -lt "${HS_ATTEMPTS}" ]; do
	HS=$(scn ${S}.sdp.hs_state)
	case "${HS}" in
	completed*)
		break
		;;
	idle*)
		;;
	"timed out"*)
		case "${HS}" in
		*"scratch 0x0000000000000000"*) ;;
		*) fail "the handshake timed out with 0x${HS#*scratch 0x} still in the register - the target is part-way through the exchange and writing over it would wedge it" ;;
		esac
		;;
	*)
		# In flight. Give it the tick it needs rather than interfering.
		sleep 1
		i=$((i + 1))
		continue
		;;
	esac

	if [ ${i} -eq 0 ]; then
		log "waiting ${COPROC_WARMUP}s for the coprocessor to reach its own handshake"
		sleep "${COPROC_WARMUP}"
	fi
	sc ${S}.sdp.handshake=1
	i=$((i + 1))
	sleep 6
done

case "$(scn ${S}.sdp.hs_state)" in
completed*) log "handshake completed" ;;
*) fail "the handshake did not complete after ${i} offers; the coprocessor is not answering" ;;
esac

# ---------------------------------------------------------- wait for the fast path

if ! scn ${S}.rpc.state | grep -q 'reconfig_done 1'; then
	log "waiting up to ${FASTPATH_WAIT}s for the coprocessor's fast path"
	i=0
	while [ ${i} -lt "${FASTPATH_WAIT}" ]; do
		i=$((i + 1))
		sleep 1
		# The barmap appears late and nothing re-reads it, so rescan before every attempt.
		sc ${S}.rescan=1
		sc ${S}.rpc.desc_count=256 ${S}.rpc.desc_offset=4328 ${S}.rpc.dbell=0 ${S}.rpc.shared=0
		sc ${S}.rpc.configure=1
		if scn ${S}.rpc.state | grep -q 'reconfig_done 1'; then
			log "rpc facility configured after ${i}s"
			break
		fi
	done
fi
scn ${S}.rpc.state | grep -q 'reconfig_done 1' ||
	fail "the rpc facility never configured; nothing further will work"

# ----------------------------------------------------------------------- the ports

sc ${S}.nwa.discover=1
sc ${S}.rpc.allow_write=1
sc ${S}.rpc.lif_mtu=1500 ${S}.rpc.lif_fwd=2 ${S}.rpc.lif_mask=255

# NETAGENT ANSWERS LATER THAN THE RPC FACILITY DOES, and the gap is about ten seconds. Going
# straight on gets an empty reply to every request and the whole port loop below then skips every
# port with "no address", which is what this script did on its first real boot: twelve interfaces
# came up and not one of them was bound. So ask the uplink for something harmless until it answers.
i=0
while [ ${i} -lt "${NWA_WAIT}" ]; do
	sc ${S}.nwa.op=4 ${S}.nwa.sub=3 ${S}.nwa.port=2 ${S}.nwa.param=0 ${S}.nwa.param2=0
	sc ${S}.nwa.request=1
	if scn ${S}.nwa.last | grep -q 'status 0x00000000'; then
		log "netagent answering after ${i}s"
		break
	fi
	i=$((i + 1))
	sleep 1
done
scn ${S}.nwa.last | grep -q 'status 0x00000000' ||
	fail "netagent never answered; the ports cannot be bound"

# Raise the coprocessor's own three: the two 10G cages and the switch uplink. Their NetAgent port
# numbers are 0, 1 and 2, which is the only place in this script where a port is not named by tag.
sc ${S}.nwa.op=3 ${S}.nwa.sub=0 ${S}.nwa.param=1 ${S}.nwa.param2=0
for p in 0 1 2; do
	sc ${S}.nwa.port=${p}
	sc ${S}.nwa.request=1
done

# The two 10G cages keep the tags this driver chose, and interfaces 10 and 11 - which are also
# their lifports in the board file, so the numbering agrees with the ten below rather than by
# coincidence.
for pair in "10 1" "11 2"; do
	set -- ${pair}
	sc ${S}.rpc.lif_iface=$1 ${S}.rpc.lif_tag=$2
	sc ${S}.rpc.cmd=5; sc ${S}.rpc.post=1
	sc ${S}.rpc.cmd=3; sc ${S}.rpc.post=1
done

# And the ten behind the switch, each by the tag its DSA header carries.
UP=0
for p in 1 2 3 4 5 6 7 8 9 10; do
	TAG=$((32768 + p * 256))
	IFACE=$((p - 1))

	# Raise it. On a copper port the coprocessor does both halves of the vendor's own
	# umsd_port_up - the bridging state and the PHY's power-down bit - so this replaces
	# driving the switch over MDIO for everything except reading it back.
	sc ${S}.nwa.op=3 ${S}.nwa.sub=0 ${S}.nwa.port=${TAG} ${S}.nwa.param=1 ${S}.nwa.param2=0
	sc ${S}.nwa.request=1

	# Ask the port for its own address, and give it straight back. The GET is where this
	# driver's interface address comes from; the SET is what makes the switch's per-port TCAM
	# entry live, because UMSD leaves its octet mask at 0x00 - "Never Hit" - until the host
	# names the address. Without it the port passes broadcast and nothing else.
	W0=""
	W1=""
	t=0
	while [ ${t} -lt 3 ]; do
		sc ${S}.nwa.op=4 ${S}.nwa.sub=3 ${S}.nwa.port=${TAG} ${S}.nwa.param=0 ${S}.nwa.param2=0
		sc ${S}.nwa.request=1
		W0=$(scn ${S}.nwa.last | awk '/^  \[ 0\]/ { print $3; exit }')
		W1=$(scn ${S}.nwa.last | awk '/^  \[ 1\]/ { print $3; exit }')
		[ -n "${W0}" ] && [ -n "${W1}" ] && break
		t=$((t + 1))
		sleep 2
	done
	if [ -z "${W0}" ] || [ -z "${W1}" ]; then
		log "port ${p}: no address from NetAgent after three tries, skipping"
		continue
	fi
	H0=${W0#0x}
	H1=${W1#0x}
	MAC=$(printf '%s:%s:%s:%s:%s:%s' \
	    "$(echo ${H0} | cut -c7-8)" "$(echo ${H0} | cut -c5-6)" \
	    "$(echo ${H0} | cut -c3-4)" "$(echo ${H0} | cut -c1-2)" \
	    "$(echo ${H1} | cut -c7-8)" "$(echo ${H1} | cut -c5-6)")
	sc ${S}.nwa.op=3 ${S}.nwa.sub=3 ${S}.nwa.port=${TAG} ${S}.nwa.param=$((W0)) ${S}.nwa.param2=$((W1))
	sc ${S}.nwa.request=1

	# Bind the tag to the interface index, which is the board file's lifport, and install the
	# logical interface with the address the port just gave us.
	sc ${S}.rpc.lif_iface=${IFACE} ${S}.rpc.lif_tag=${TAG} ${S}.rpc.lif_mac=${MAC}
	sc ${S}.rpc.cmd=5; sc ${S}.rpc.post=1
	sc ${S}.rpc.cmd=3; sc ${S}.rpc.post=1
	UP=$((UP + 1))
done
log "bound ${UP} of the ten ports behind the switch"

# ------------------------------------------------------- interrupts and interfaces

sc ${S}.dp.oq_time_threshold=86 ${S}.dp.oq_intr_pkt=1
sc ${S}.dp.refresh_levels=1
sc ${S}.dp.msix=1

# One interface per front port. dp.if_port names the NetAgent port to read the address from, which
# is the tag for everything behind the switch and differs for the two direct cages.
sc ${S}.dp.if_add=1
sc ${S}.dp.if_add=2
for p in 1 2 3 4 5 6 7 8 9 10; do
	TAG=$((32768 + p * 256))
	sc ${S}.dp.if_port=${TAG}
	sc ${S}.dp.if_add=${TAG}
done
sc ${S}.dp.if_port=4294967295

N=$(/sbin/ifconfig -l | tr ' ' '\n' | grep -c '^oxp')
log "up: ${N} front-port interfaces"
exit 0
