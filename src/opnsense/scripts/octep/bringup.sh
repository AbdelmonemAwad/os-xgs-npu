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
: ${RSIZE:=1024}
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

# The two loader commands, as variables.
#
# Not for flexibility - there is one right path for each - but so the module decision below can be
# tested. Proving the refusal branch used to mean unloading the driver, which on this appliance takes
# the twelve interfaces, the WAN lease and the default route with it and needs a host reboot to put
# back. With these, a test overrides kldstat to report the module absent and runs the real script.
: ${KLDSTAT:=/sbin/kldstat}
: ${KLDLOAD:=/sbin/kldload}

sc() { /sbin/sysctl "$@" > /dev/null 2>&1; }
scn() { /sbin/sysctl -n "$1" 2>/dev/null; }
fail() { log "$1"; exit 1; }

# ---------------------------------------------------------------------- the module

# REFUSE A MODULE BUILT AGAINST A DIFFERENT KERNEL, and do not merely warn about it.
#
# A FreeBSD module declares its kernel dependency as a RANGE, from the __FreeBSD_version it was
# compiled against upward, so the loader accepts one built against a different 15.x kernel instead
# of refusing it. The loud failure one might expect from an OPNsense update does not arrive. What
# arrives is a module that loads and reads every structure whose layout changed at the wrong
# offset, inside the kernel, on a machine carrying traffic.
#
# contrib/octep/build.sh writes the kernel it was built against beside the module for exactly this,
# and for a while nothing read it except the installer. Set FORCE=1 to load it anyway, which is a
# thing to do deliberately and never from a boot hook.
#
# WHAT THE REFUSAL COSTS, because it is more than the front ports and that was learned the
# expensive way. OPNsense configures its interfaces from the devices that exist at that point in
# the boot, and drops the assignments whose devices do not - out of /conf/config.xml, for good. One
# boot with this module refused removed wan, opt1, opt2 and opt3 from the configuration on this
# appliance, the WAN's DHCP assignment with them, and getting the module right afterwards did not
# put them back; they came from a backup.
#
# So the refusal is not meant to be reached. install/kernel-follow.sh rebuilds the module in the
# window between a kernel being installed and the host rebooting onto it, driven automatically from
# rc.syshook.d/upgrade/20-octep and from cron. This check is what catches the case where that did not
# happen.
#
# The check is deliberately OUTSIDE the "is it already loaded" test, and says different things in
# the two cases. A stale module on disk with nothing loaded is a refusal; a stale module on disk
# with a working one already running is a warning, because taking a running firewall's interfaces
# away over a file on disk would be the worse mistake.
if [ -f "${MODULE}" ]; then
	BUILT=$(cat "${MODULE}.kernel" 2>/dev/null || true)
	NOW=$(/usr/bin/uname -v)
	if [ -z "${BUILT}" ]; then
		log "no kernel stamp beside ${MODULE} - nothing can tell whether it matches this kernel"
	elif [ "${BUILT}" != "${NOW}" ]; then
		log "${MODULE} was built against a different kernel"
		log "  built  : ${BUILT}"
		log "  running: ${NOW}"
		if ${KLDSTAT} -q -n octep; then
			log "a working octep is already loaded, so carrying on - but rebuild it with install/kernel-follow.sh before the next boot"
		elif [ "$(cat ${MODULE}.prev.kernel 2>/dev/null)" = "${NOW}" ] && [ -f "${MODULE}.prev" ]; then
			#
			# The module that was replaced, and it matches this kernel.
			#
			# install/kernel-follow.sh stamps a new module for the kernel ON DISK, which is the
			# right thing to do before a reboot and the wrong thing if that reboot does not
			# happen - a rollback, a boot menu choice, an update that did not take. It keeps the
			# module it replaced beside the new one for exactly this case, so the ports do not
			# go away for the opposite reason to the one they were being saved from.
			#
			# This is not FORCE. The previous module's stamp is an exact match for the running
			# kernel; nothing is being loaded on a guess.
			log "but ${MODULE}.prev matches this kernel exactly - loading that instead"
			MODULE=${MODULE}.prev
		elif [ "${FORCE:-0}" = "1" ]; then
			log "FORCE=1, loading it anyway"
		else
			fail "refusing to load it; run install/kernel-follow.sh while a kernel this module matches is still running, or set FORCE=1 if you know the layouts did not change"
		fi
	fi
fi

#
# CHECK=1 stops here, having said which module it would load and nothing else.
#
# The decision above is the one that costs the most when it is wrong - it is what stands between a
# silently mismatched module and a firewall reading its own structures at the wrong offsets - and
# for a long time the only way to exercise it was to unload the driver, which on this appliance
# takes the twelve interfaces, the WAN lease and the default route with it and needs a host reboot
# to undo. So the decision is reachable on its own, with the real script and the real files.
#
if [ "${CHECK:-0}" = "1" ]; then
	log "CHECK=1: would load ${MODULE}"
	exit 0
fi

if ! ${KLDSTAT} -q -n octep; then
	[ -f "${MODULE}" ] || fail "no ${MODULE} - build and install it with install/install.sh from os-xgs-npu"
	${KLDLOAD} "${MODULE}" 2>/dev/null || fail "kldload ${MODULE} failed"
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
# The rpc write gate, opened because the port loop below installs a logical interface and a port
# mapping, and both are writes. It is closed again at the end of this script: the gate exists so
# that nobody writes the coprocessor's forwarding state by accident, and a gate left open after
# the one job that needed it is not a gate.
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
	CIF=$1
	CTAG=$2

	# Ask the cage for its own address, exactly as the ten below do.
	#
	# This was missing, and it is the whole of why unicast did not reach a cage. The loop set
	# the interface and the tag and posted LIF_ADD_UPDATE with rpc.lif_mac still at its
	# boot-time zeros, so the coprocessor held a logical interface whose address was
	# 00:00:00:00:00:00 and matched nothing. Broadcast passed, which is why DHCP and ARP
	# worked and made the port look alive; every unicast frame was dropped before anything
	# counted it. Measured 2026-10-03: with the address corrected, unicast works with no
	# promiscuous mode at all, which is what the vendor relies on - its own source contains no
	# promiscuous setting anywhere.
	# THE STATUS IS PART OF THE ANSWER. nwa.last is the driver's one last-reply buffer, so a
	# request the firmware answered with an error leaves in it whatever was there before - and
	# two payload words being present says only that some transaction once put them there. The
	# warm-up loop above checks the status for exactly this reason; this did not.
	CW0=""
	CW1=""
	ct=0
	while [ ${ct} -lt 3 ]; do
		CW0=""
		CW1=""
		sc ${S}.nwa.op=4 ${S}.nwa.sub=3 ${S}.nwa.port=${CTAG} ${S}.nwa.param=0 ${S}.nwa.param2=0
		sc ${S}.nwa.request=1
		if scn ${S}.nwa.last | grep -q 'status 0x00000000'; then
			CW0=$(scn ${S}.nwa.last | awk '/^  \[ 0\]/ { print $3; exit }')
			CW1=$(scn ${S}.nwa.last | awk '/^  \[ 1\]/ { print $3; exit }')
			[ -n "${CW0}" ] && [ -n "${CW1}" ] && break
		fi
		ct=$((ct + 1))
		sleep 2
	done

	# No address, no logical interface: skip the cage, exactly as the ten below skip a port.
	#
	# Carrying on here was a defect and a worse one than it looks. rpc.lif_mac is a single
	# sysctl that keeps its value, so the second cage would have installed its logical
	# interface with the FIRST cage's address still sitting in it - not a zero address that
	# matches nothing, but a valid address belonging to another port, which resolves that
	# port's frames to this one.
	if [ -z "${CW0}" ] || [ -z "${CW1}" ]; then
		log "cage ${CTAG}: no address from NetAgent after three tries, skipping"
		continue
	fi
	CH0=${CW0#0x}
	CH1=${CW1#0x}
	CMAC=$(printf '%s:%s:%s:%s:%s:%s' \
	    "$(echo ${CH0} | cut -c7-8)" "$(echo ${CH0} | cut -c5-6)" \
	    "$(echo ${CH0} | cut -c3-4)" "$(echo ${CH0} | cut -c1-2)" \
	    "$(echo ${CH1} | cut -c7-8)" "$(echo ${CH1} | cut -c5-6)")
	sc ${S}.rpc.lif_mac=${CMAC}

	# The logical interface first, then the tag that resolves to it - the vendor's order, from
	# usfp_netdev_mv.c, which adds the LIF and only then updates the port tables. Posting the
	# tag first leaves a window in which a frame off the wire resolves to an interface that has
	# no usable logical interface behind it yet.
	sc ${S}.rpc.lif_iface=${CIF} ${S}.rpc.lif_tag=${CTAG}
	sc ${S}.rpc.cmd=3; sc ${S}.rpc.post=1
	sc ${S}.rpc.cmd=5; sc ${S}.rpc.post=1

	# And, as for the ten below, nothing here opens the port's unicast filter. The driver
	# follows IFF_PROMISC; see the note in the switch-port loop.
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
	# The status is checked before the payload is believed, for the reason given in the cage
	# loop above: one reply buffer, so stale words outlive a failed request.
	W0=""
	W1=""
	t=0
	while [ ${t} -lt 3 ]; do
		W0=""
		W1=""
		sc ${S}.nwa.op=4 ${S}.nwa.sub=3 ${S}.nwa.port=${TAG} ${S}.nwa.param=0 ${S}.nwa.param2=0
		sc ${S}.nwa.request=1
		if scn ${S}.nwa.last | grep -q 'status 0x00000000'; then
			W0=$(scn ${S}.nwa.last | awk '/^  \[ 0\]/ { print $3; exit }')
			W1=$(scn ${S}.nwa.last | awk '/^  \[ 1\]/ { print $3; exit }')
			[ -n "${W0}" ] && [ -n "${W1}" ] && break
		fi
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
	# The logical interface first, then the tag - the vendor's order, as above.
	sc ${S}.rpc.lif_iface=${IFACE} ${S}.rpc.lif_tag=${TAG} ${S}.rpc.lif_mac=${MAC}
	sc ${S}.rpc.cmd=3; sc ${S}.rpc.post=1
	sc ${S}.rpc.cmd=5; sc ${S}.rpc.post=1

	# Nothing here asks the port to accept frames addressed elsewhere.
	#
	# It used to. A front port's hardware filter drops incoming unicast whose destination is an
	# address the port does not own - recorded on 2026-09-30, when a hundred frames reached a PC
	# at the cage only because they were broadcast - and in a bridge that rule bites every
	# reply, because they all carry the bridge's address. So this script turned promiscuous on
	# for all ten, unconditionally, forever, which is a blunt instrument: a routed port pays for
	# a bridge it is not in, and the setting is in a shell script where nothing in the system
	# can see or reverse it.
	#
	# The driver now follows IFF_PROMISC instead, so a port is promiscuous exactly while
	# something has asked it to be - if_bridge when the port is a member, or an operator with
	# `ifconfig`. There is nothing to set here and nothing to undo on the way out.
	UP=$((UP + 1))
done
log "bound ${UP} of the ten ports behind the switch"

# ------------------------------------------------------- interrupts and interfaces

sc ${S}.dp.oq_time_threshold=86 ${S}.dp.oq_intr_pkt=1
sc ${S}.dp.refresh_levels=1
sc ${S}.dp.msix=1

# One interface per front port. dp.if_port names the NetAgent port to read the address from, which
# is the tag for everything behind the switch and differs for the two direct cages. dp.if_iface
# names the logical interface the port belongs to - the same number this script bound a moment ago
# with LIF_ADD_UPDATE - so the driver can set that LIF's forwarding mode when the port joins or
# leaves a bridge.
#
# The driver cannot derive it. The ten behind the switch follow 0x8000 | ((iface + 1) << 8), but the
# two cages were given tags 1 and 2 against interfaces 10 and 11, which that rule does not produce.
# So it is told here, where the binding is made, and there is one place it is written down.
sc ${S}.dp.if_iface=10
sc ${S}.dp.if_add=1
sc ${S}.dp.if_iface=11
sc ${S}.dp.if_add=2
for p in 1 2 3 4 5 6 7 8 9 10; do
	TAG=$((32768 + p * 256))
	sc ${S}.dp.if_port=${TAG}
	sc ${S}.dp.if_iface=$((p - 1))
	sc ${S}.dp.if_add=${TAG}
done
sc ${S}.dp.if_port=4294967295
sc ${S}.dp.if_iface=4294967295

# The acceleration gate, as the settings page left it.
#
# This has to happen here and nowhere else. FW_CFG_OFFLOAD is not remembered by the coprocessor -
# it comes up at 0x2, offloading off, every time - so a setting that is not re-applied at boot is a
# setting that lasts until the next reboot and then quietly reverts, which is the worst way for a
# switch in a GUI to behave. It has to be after LIF_ADD_UPDATE, because opening the gate with the
# LIFs unprogrammed drops the LAN, and it has to be before allow_write is shut.
#
# The setting is read here rather than inside offload.sh so the php startup is paid once, and
# offload.sh is given the answer. A failure does not stop the bring-up: twelve working interfaces
# with the gate shut is a working appliance, and a bring-up that aborted over an accelerator would
# turn a missed optimisation into a dead firewall.
#
# The log() calls below are nearly worthless at boot and that is worth saying rather than relying
# on them. This script runs from an early syshook, before syslogd, so logger writes to a socket
# nothing is reading and the message is dropped without an error - measured: not one line of this
# script's output from a boot is in any log file, including "up: N front-port interfaces". They are
# left in because the script is also run by hand, where stdout is a tty and log() echoes. What an
# operator can actually rely on is the settings page, which reads the gate's value back out of the
# driver rather than trusting that a message arrived.
if [ -x /usr/local/opnsense/scripts/xgs/offload.sh ] && [ -x /usr/local/bin/php ]; then
	WANT=$(/usr/local/bin/php -r \
	    'require_once("config.inc"); echo empty($config["OPNsense"]["XGS"]["general"]["offload"]) ? "off" : "on";' \
	    2>/dev/null)
	case ${WANT} in
	on|off)
		if OUT=$(/usr/local/opnsense/scripts/xgs/offload.sh "${WANT}" 2>&1); then
			log "offload ${WANT}"
		else
			log "offload ${WANT} refused: ${OUT}"
		fi
		;;
	*)
		log "offload setting unreadable, leaving the gate as the coprocessor left it"
		;;
	esac
fi

# Make the two sides agree about the firewall revision, once, by writing it.
#
# A flow entry carries the revision it was authorised under and the far side refuses one that does
# not match what it is checking against. There is no command that reads fw_state back, so agreement
# cannot be verified - but it can be established, because the bump POSTS the value: afterwards the
# far side holds what this driver believes whatever it held before. That matters here because the
# driver's own counter starts at zero on every module load while the coprocessor keeps its value
# across one, so a module reload alone would leave them disagreeing and every flow refused, silently.
#
# It needs no write gate and takes no operand - see the task in contrib/octep/octep_rpc.c - and it
# returns before the command is posted, so this does not wait for it. rpc.fw_rev and the three
# counters beside it say what happened.
sc ${S}.rpc.fw_rev_bump=1

# Shut the write gate behind us. What writes over rpc in steady state does not use this gate: the
# flow programmer and the revision bump open and restore it themselves under the driver's lock, and
# the link poll and both receive filters go through NetAgent. So anyone who needs a write from
# outside the driver opens it deliberately, which is the whole point of it.
sc ${S}.rpc.allow_write=0

N=$(/sbin/ifconfig -l | tr ' ' '\n' | grep -c '^oxp')
log "up: ${N} front-port interfaces"
exit 0
