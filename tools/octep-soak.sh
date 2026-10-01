#!/bin/sh
#
# A long-running load test for the OCTEON TX datapath, to be run on the appliance itself.
#
# Before this script the longest run in this project was six seconds, which is long enough to read
# a rate and far too short to see a leak, a ring that goes quiet, or a counter that wraps.
#
# The default rate is deliberately modest. octep-fairness.sh established that all twelve front
# ports share one egress queue on the coprocessor, so a flood aimed at one of them costs the others
# their packets: measurable from 25,000 packets per second, severe above 50,000. The default here is
# 10,000, which measured no loss at all on the other ports, because the question a soak answers is
# whether hours of ordinary traffic break something - not how it behaves at saturation, which the
# sweep already answers. Raise RATE if the appliance is yours to disturb.
#
# Each cycle has a load window and a quiet window, and the WAN's own latency and loss are measured
# in both, so the log shows directly whether the load is costing the rest of the box anything.
#
# It stops itself if the default route disappears or the gateway stops answering, so a soak cannot
# take the appliance's internet with it.
#
# Everything it does is transient: an address on a dark front port and one static ARP entry, both
# removed on exit. It writes nothing to the configuration.
#
# Usage:  octep-soak.sh [cycles]
#
#   IFACE   dark front port to load             (default oxp4)
#   WANIF   interface holding the default route  (default: read from the routing table)
#   CYCLES  number of cycles                    (default 180, which with the defaults is 3 hours)
#   RATE    packets per second, 0 for unpaced    (default 10000)
#   BLAST   seconds of load per cycle            (default 50)
#   IDLE    seconds of quiet per cycle           (default 10)
#   LEN     UDP payload bytes                    (default 1472, a full 1514-byte frame)
#   OUT     log file                             (default /root/npu/soak-<date>.log)
#
set -u

IFACE=${IFACE:-oxp4}
CYCLES=${CYCLES:-${1:-180}}
RATE=${RATE:-10000}
BLAST=${BLAST:-50}
IDLE=${IDLE:-10}
LEN=${LEN:-1472}
SRC=${SRC:-192.0.2.1}
DST=${DST:-192.0.2.2}
DMAC=${DMAC:-02:00:00:00:00:02}
BLASTER=${BLASTER:-/root/npu/blast}
FPCNT=${FPCNT:-/root/npu/octep-fpcnt.sh}
OUT=${OUT:-/root/npu/soak-$(date +%Y%m%d-%H%M).log}

say() { echo "$*" | tee -a "$OUT"; }

# ---------------------------------------------------------------- preflight

kldstat -q -n octep || { echo "octep is not loaded" >&2; exit 1; }
ifconfig "$IFACE" > /dev/null 2>&1 || { echo "$IFACE does not exist" >&2; exit 1; }
[ -x "$BLASTER" ] || { echo "$BLASTER is not there; build blast.c first" >&2; exit 1; }

if ifconfig "$IFACE" | grep -q 'status: active'; then
	echo "$IFACE has a carrier - point this at a dark port, not a cabled one" >&2
	exit 1
fi

GW=${GW:-$(netstat -rn -f inet | awk '$1 == "default" { print $2; exit }')}
WANIF=${WANIF:-$(netstat -rn -f inet | awk '$1 == "default" { print $4; exit }')}
[ -n "$GW" ] || { echo "no default route; refusing to soak a box that is already broken" >&2; exit 1; }

# ---------------------------------------------------------------- instruments

# netstat -b -n columns, which are easy to get wrong: $7 is Idrop, not Opkts.
snap() { netstat -b -n -I "$1" | awk '/<Link/ { print $5, $7, $8, $9, $11; exit }'; }
dpsnap() { sysctl -n dev.octep.0.dp.tx_posted dev.octep.0.dp.rx_done | tr '\n' ' '; }
mbufs() { netstat -m | awk 'NR == 1 { split($1, a, "/"); print a[1]; exit }'; }
denied() { netstat -m | awk '/requests for mbufs denied/ { split($1, a, "/"); print a[1] + a[2] + a[3]; exit }'; }

# One ping run; prints "loss avg_rtt", and "100.0 -" when nothing came back.
pingrun() {
	ping -c "$1" -i 0.2 -t "$2" -q "$GW" 2>/dev/null | awk '
		/packet loss/ { for (i = 1; i <= NF; i++) if ($i ~ /%$/) { sub("%", "", $i); loss = $i } }
		/min\/avg\/max/ { split($4, a, "/"); rtt = a[2] }
		END { printf "%s %s\n", (loss == "" ? "100.0" : loss), (rtt == "" ? "-" : rtt) }'
}

# ---------------------------------------------------------------- arm and disarm

armed_addr=no
armed_arp=no

disarm() {
	[ "$armed_arp" = yes ] && arp -d "$DST" > /dev/null 2>&1
	[ "$armed_addr" = yes ] && ifconfig "$IFACE" -alias "$SRC" > /dev/null 2>&1
	return 0
}
trap 'say ""; say "interrupted at cycle ${c:-0}"; disarm; exit 130' INT TERM

ifconfig "$IFACE" inet "$SRC"/24 alias up || { echo "cannot address $IFACE" >&2; exit 1; }
armed_addr=yes
arp -s "$DST" "$DMAC" > /dev/null 2>&1 && armed_arp=yes

# ---------------------------------------------------------------- the run

say "octep soak on $(hostname), $(date)"
say "kernel $(uname -r), $(uname -v | sed 's/.*stable/stable/')"
say "load on $IFACE, $LEN-byte payload, rate $RATE; WAN $WANIF via $GW"
say "$CYCLES cycles of ${BLAST}s load + ${IDLE}s quiet"
say ""
say "cycle  offered_pps  tx_posted_d  ${IFACE}_Opkts_d  ldLoss ldRtt  idLoss idRtt   mbufs denied  rx_done_d  wan_i_d wan_o_d  q_full_d wire_err_d"

start=$(date +%s)
mb0=$(mbufs)
dn0=$(denied)
set -- $(dpsnap); txp0=$1; rxd0=$2
quiet=0
c=0

while [ "$c" -lt "$CYCLES" ]; do
	c=$((c + 1))

	set -- $(snap "$IFACE"); lo_o=$4
	set -- $(snap "$WANIF"); wa_i=$1 wa_o=$4
	set -- $(dpsnap); txp=$1 rxd=$2
	"$FPCNT" -s /tmp/soak-fp.$$ > /dev/null 2>&1

	# The load window, with the WAN measured underneath it.
	pingrun $((BLAST * 2)) $((BLAST + 5)) > /tmp/soak-load.$$ &
	pp=$!
	if [ "$RATE" = 0 ]; then
		offered=$("$BLASTER" "$SRC" "$DST" "$LEN" "$BLAST" | sed -n 's/.*-> \([0-9]*\) pps.*/\1/p')
	else
		offered=$("$BLASTER" "$SRC" "$DST" "$LEN" "$BLAST" "$RATE" | sed -n 's/.*-> \([0-9]*\) pps.*/\1/p')
	fi
	wait $pp
	read ld_loss ld_rtt < /tmp/soak-load.$$
	rm -f /tmp/soak-load.$$

	# The quiet window, measured the same way.
	pingrun $((IDLE * 2)) $((IDLE + 4)) > /tmp/soak-idle.$$
	read id_loss id_rtt < /tmp/soak-idle.$$
	rm -f /tmp/soak-idle.$$

	set -- $(snap "$IFACE"); d_o=$(($4 - lo_o))
	set -- $(snap "$WANIF"); d_wi=$(($1 - wa_i)) d_wo=$(($4 - wa_o))
	set -- $(dpsnap); d_tx=$(($1 - txp)) d_rx=$(($2 - rxd))
	set -- $("$FPCNT" -d /tmp/soak-fp.$$ 2>/dev/null | awk '$1 == 9 { q = $3 } $1 == 18 { e = $3 } END { print q + 0, e + 0 }')
	d_qf=$1 d_we=$2
	rm -f /tmp/soak-fp.$$

	printf '%-5s %11s %12s %14s  %5s %6s  %5s %6s  %6s %6s %10s %8s %7s %9s %10s\n' \
	    "$c" "${offered:-0}" "$d_tx" "$d_o" \
	    "$ld_loss" "$ld_rtt" "$id_loss" "$id_rtt" \
	    "$(mbufs)" "$(denied)" "$d_rx" "$d_wi" "$d_wo" "$d_qf" "$d_we" | tee -a "$OUT"

	# Safety. Three cycles with nothing coming back, or no default route, and we stop.
	if [ "$ld_loss" = 100.0 ] && [ "$id_loss" = 100.0 ]; then
		quiet=$((quiet + 1))
	else
		quiet=0
	fi
	if [ "$quiet" -ge 3 ]; then
		say ""
		say "ABORTED at cycle $c: the gateway has not answered for three cycles"
		break
	fi
	if ! netstat -rn -f inet | awk '$1 == "default" { found = 1 } END { exit !found }'; then
		say ""
		say "ABORTED at cycle $c: the default route is gone"
		break
	fi
done

# ---------------------------------------------------------------- the summary

set -- $(dpsnap); txp1=$1; rxd1=$2
el=$(( $(date +%s) - start ))

say ""
say "ran $c cycles in $((el / 60)) minutes"
say "tx_posted $txp0 -> $txp1, $((txp1 - txp0)) frames posted"
say "rx_done   $rxd0 -> $rxd1, $((rxd1 - rxd0)) frames delivered"
say "mbufs in use $mb0 -> $(mbufs); requests denied $dn0 -> $(denied)"
say ""
say "a leak shows as mbufs climbing cycle after cycle and never coming back;"
say "a stall shows as tx_posted_d going to zero while offered_pps does not;"
say "a shared-queue cost shows as ldLoss above idLoss, and as q_full_d or wire_err_d moving at all."

disarm
say ""
say "done, $(date). log: $OUT"
