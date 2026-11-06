#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Give a bridged front port the RSTP path cost its link speed deserves.
#
# WHY THIS EXISTS AT ALL, because a script that adjusts spanning tree by hand deserves a reason.
#
# if_bridge computes a member's path cost exactly once, in bstp_create, at the moment the member is
# added, and recomputes it at most one more time - only if the link was DOWN then, and only on the
# first carrier it sees afterwards. See bstp_calc_path_cost and BSTP_PORT_PNDCOST.
#
# These ports cannot satisfy that. OPNsense adds its bridge members seconds after npuep loads, the
# coprocessor's network agent does not publish its mailbox for about fourteen, and when carrier
# finally arrives the switch spends the first three seconds reporting 10baseT while it negotiates a
# gigabit link. Measured, once a second, from module load:
#
#	t+9	10baseT/UTP	<- the one moment RSTP looks
#	t+11	10baseT/UTP
#	t+12	none		<- the PHY renegotiates
#	t+15	1000baseT	<- settled, and what it stays
#
# So RSTP spends its single recalculation on 10 Mbit/s and latches 2000000 where the 802.1D formula
# gives 20000 for the link that is actually running. The driver cannot reach this: by the time it
# knows the truth, nothing will ask again. Userland can, with ifpathcost, which is what this does.
#
# It only matters on a bridge with a redundant path, where a hundredfold error in one port's cost
# elects the wrong root port. On a bridge without one it changes nothing, and it is written to be
# safe to run anyway: it reads, compares, and writes only what differs - and never over a cost
# somebody else set (A COST SET BY HAND, below).
#
# WHY CRON AND NOT devd. The obvious hook is IFNET/LINK_UP, and it cannot be used. devd runs only
# ONE matching statement per event - "if two statements match the same event, only the action of
# the statement with highest priority will be executed", devd.conf(5) - and OPNsense already claims
# that event at priority 101 to run `configctl interface linkup start`. A rule below that never
# fires, which is what a rule at 50 did here: measured, nothing happened. A rule above it would
# fire and SILENCE OPNsense's own linkup handling for these ports, which is not a trade worth
# making on a firewall. So this runs on a timer instead, and is built to be cheap enough for one.
#
#	bridge-pathcost.sh		 every bridge, every npup member
#	bridge-pathcost.sh npup1	 only the bridge npup1 belongs to
#
set -u

ONLY=${1:-}

# A COST SET BY HAND IS LEFT AS IT WAS SET. Every cost set from outside the kernel - this script's,
# or an owner's own, with ifconfig ifpathcost or through OPNsense - carries ADMCOST in the member's
# flags (0x400 in the hexadecimal number ifconfig prints after "flags="), and the kernel's own
# figures never do. So a member with ADMCOST is this script's to change only while its cost is the
# one this script set there, kept in COSTS one line a member, "bridge member cost"; any other is the
# owner's. Until 2026-11-06 this set the speed's figure over whatever differed, and a cost an owner
# gave a port lasted a minute. An owner's cost that happens to be the one this script last set on
# that member is taken for this script's. A line outlives its member harmlessly: a member made
# again, at a boot or a reload, starts without ADMCOST, so its old line is not asked about until a
# cost is set on it again, and this script's own setting replaces the line.
: ${COSTS:=/var/run/xgs-bridge-pathcost}

# ONE ifconfig for the whole decision, because this runs once a minute.
#
# The obvious shape - a call per bridge and a call per member - is a dozen processes a minute for a
# job that changes nothing on almost every run. Everything needed is in `ifconfig -a`: each port's
# media and status in its own block, each bridge's members and their current costs in the bridge's.
# So it is read once and the answer worked out in a single awk.
#
# The awk prints one line per member that needs changing, and nothing at all when none do, which is
# the normal case. The 802.1D-2004 section 17.14 figure is the same one bstp_calc_path_cost uses:
#
#	20000000000 / (baudrate / 1000)   ==   20000000 / megabits
#
work=$(ifconfig -a 2>/dev/null | awk -v only="${ONLY}" -v costs="${COSTS}" '
	# The flags number is hexadecimal with no 0x in front: read by hand, as every awk can.
	function hex(s,    i, v) {
		v = 0
		for (i = 1; i <= length(s); i++)
			v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
		return v
	}
	# The costs this script set, one line a member: see A COST SET BY HAND.
	BEGIN {
		while ((getline line < costs) > 0)
			if (split(line, w, " ") == 3)
				mine[w[1] " " w[2]] = w[3]
		close(costs)
	}
	/^[a-z][a-z0-9.]*:/ {
		cur = substr($1, 1, length($1) - 1)
		next
	}
	/^\tstatus:/ { status[cur] = $2; next }
	/^\tmedia:/  { media[cur]  = tolower($0); next }
	/^\tmember:/ {
		n++
		br[n] = cur
		mb[n] = $2
		# ADMCOST, 0x400, read from the number whether or not a name is printed for it
		f = tolower($3)
		sub(/^flags=/, "", f)
		sub(/<.*/, "", f)
		if (f ~ /^[0-9a-f]+$/)
			adm[n] = int(hex(f) / 1024) % 2
		next
	}
	# The cost, and the protocol the port speaks - shown only for a member in spanning tree.
	/path cost/ {
		if (n > 0 && cost[n] == "")
			for (i = 1; i < NF; i++)
				if ($i == "cost" && cost[n] == "")
					cost[n] = $(i + 1)
				else if ($i == "proto")
					proto[n] = $(i + 1)
		next
	}
	END {
		# If an interface was named, work out which bridge holds it and do only that one.
		if (only != "") {
			for (i = 1; i <= n; i++)
				if (mb[i] == only)
					wanted_br = br[i]
			if (wanted_br == "")
				exit 0
		}

		for (i = 1; i <= n; i++) {
			if (wanted_br != "" && br[i] != wanted_br)
				continue

			m = mb[i]

			# Only the ports belonging to this driver. Every other NIC on this appliance
			# reports its speed before OPNsense gets round to bridging it, so the figure
			# FreeBSD worked out is already right and meddling would be worse.
			if (m !~ /^npup[0-9]+$/)
				continue

			# A dark port is deliberately left alone. Setting a cost sets
			# BSTP_PORT_ADMCOST, which tells RSTP never to compute one again - so pinning
			# a guess for a port with no cable would outlast the guess. It keeps the
			# neutral default until a cable arrives, and this runs again a minute later.
			if (status[m] != "active")
				continue

			# A cost set by hand that this script did not set is one the owner chose,
			# and stays (A COST SET BY HAND). Its own is known at 65535 too, which is
			# all a port keeps of it once the port has gone over to the old STP (below).
			k = br[i] " " m
			own = (k in mine) && (cost[i] + 0 == mine[k] + 0 ||
			    (mine[k] + 0 > 65535 && cost[i] + 0 == 65535))
			if (adm[i] && !own)
				continue

			# Longest first: 2500Base-T before 1000baseT before 100baseTX before
			# 10baseT/UTP, in lower case as media keeps them. FreeBSD writes the 2.5G
			# medium with a capital B - IFM_2500_T is 2500Base-T, what npugiu_media_word
			# reports at 2500 - and until 2026-11-06 this looked for 2500base as written,
			# so the two 2.5G ports, labels 11 and 12, matched nothing and kept the cost
			# RSTP latched.
			s = 0
			if      (media[m] ~ /2500base/) s = 2500
			else if (media[m] ~ /1000base/) s = 1000
			else if (media[m] ~ /100base/)  s = 100
			else if (media[m] ~ /10base/)   s = 10
			if (s == 0)
				continue

			# The old STP keeps sixteen bits of a cost: a port speaking it is given
			# 65535 for anything above (bstp_set_path_cost), and a port that falls back
			# to it, for a neighbour speaking nothing newer, has its cost cut to that
			# (bstp_set_port_proto). Until 2026-11-06 this asked such a port at 100 or
			# 10 Mb/s for 200000 or 2000000, read 65535 a minute later and asked again -
			# every minute, each time starting the spanning tree of the whole bridge
			# over (bstp_reinit) and saying so in the log. So a member shown speaking
			# STP is asked for what it keeps; one outside spanning tree shows no
			# protocol, and once it is at 65535 it is taken to keep no more.
			want = int(20000000 / s)
			if (want > 65535 && (proto[i] == "stp" ||
			    (proto[i] == "" && cost[i] + 0 == 65535)))
				want = 65535
			if (cost[i] == want "")
				continue

			print br[i], m, want, s, (cost[i] == "" ? "unset" : cost[i])
		}
	}')

[ -n "${work}" ] || exit 0

# What this script set, kept for the next run (A COST SET BY HAND): the member's line replaced, the
# whole written aside and then moved into place, so that a run cut short leaves the last complete one.
remember() {
	{
		[ -f "${COSTS}" ] && awk -v b="$1" -v m="$2" '$1 != b || $2 != m' "${COSTS}"
		echo "$1 $2 $3"
	} 2>/dev/null > "${COSTS}.$$" && mv -f "${COSTS}.$$" "${COSTS}"
}

echo "${work}" | while read -r br m want speed had; do
	# daemon.notice, which is what 06-npuctl and 07-npuep already use, and NOT daemon.info.
	#
	# The collector on this box keeps notice and above and drops info. Measured, by sending one
	# message at each of info, notice and warn and grepping /var/log for all three: notice and
	# warn arrived in system_<date>.log, info went nowhere. A first version of this logged at
	# daemon.info and was silent for exactly that reason - which looked like the script not
	# running.
	if ifconfig "${br}" ifpathcost "${m}" "${want}" 2>/dev/null; then
		remember "${br}" "${m}" "${want}" || logger -t npuctl -p daemon.warn \
		    "${br}: ${m} path cost not kept in ${COSTS} - the next run takes it for an owner's own"
		logger -t npuctl -p daemon.notice \
		    "${br}: ${m} path cost ${had} -> ${want} (${speed} Mb/s)"
	else
		logger -t npuctl -p daemon.warn \
		    "${br}: could not set ${m} path cost to ${want}"
	fi
done

exit 0
