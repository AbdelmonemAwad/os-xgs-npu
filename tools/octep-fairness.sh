#!/bin/sh
#
# Measure what load on one front port costs the other eleven.
#
# All twelve interfaces share one transmit ring into the coprocessor, and the coprocessor has one
# queue onward to the switch. Neither fact is visible in a throughput number taken on an idle box,
# so this sweep offers a known rate on a dark front port and measures, underneath it, the latency
# and loss of an ordinary ping on the port that carries the WAN.
#
# Read the result as a knee: the rate at which the other ports start losing packets is the rate
# this driver can be trusted with until the transmit path is per-interface.
#
# It is transient - an address on a dark port and one static ARP entry, both removed on exit - and
# it writes nothing to the configuration. It does put a few seconds of heavy load on the appliance
# at each step, so do not run it on something that matters at that moment.
#
# Usage:  octep-fairness.sh
#
#   IFACE  dark front port to load             (default oxp4)
#   RATES  packets per second per step, 0 last (default 5000 .. 400000 then unpaced)
#   SECS   seconds per step                    (default 6)
#   LEN    UDP payload bytes                   (default 1472, a full frame)
#
set -u

IFACE=${IFACE:-oxp4}
RATES=${RATES:-"5000 10000 25000 50000 100000 200000 400000 0"}
SECS=${SECS:-6}
LEN=${LEN:-1472}
SRC=${SRC:-192.0.2.1}
DST=${DST:-192.0.2.2}
DMAC=${DMAC:-02:00:00:00:00:02}
BLASTER=${BLASTER:-/root/npu/blast}
FPCNT=${FPCNT:-/root/npu/fpcnt.sh}

kldstat -q -n octep || { echo "octep is not loaded" >&2; exit 1; }
[ -x "$BLASTER" ] || { echo "$BLASTER is not there; build blast.c first" >&2; exit 1; }
ifconfig "$IFACE" | grep -q 'status: active' && { echo "$IFACE has a carrier; use a dark port" >&2; exit 1; }

GW=${GW:-$(netstat -rn -f inet | awk '$1 == "default" { print $2; exit }')}
WANIF=${WANIF:-$(netstat -rn -f inet | awk '$1 == "default" { print $4; exit }')}
[ -n "$GW" ] || { echo "no default route" >&2; exit 1; }

pingrun() {
	ping -c "$1" -i 0.1 -t "$2" -q "$GW" 2>/dev/null | awk '
		/packet loss/ { for (i = 1; i <= NF; i++) if ($i ~ /%$/) { sub("%", "", $i); loss = $i } }
		/min\/avg\/max/ { split($4, a, "/"); avg = a[2]; max = a[3] }
		END { printf "%s %s %s\n", (loss == "" ? "100.0" : loss), (avg == "" ? "-" : avg),
		    (max == "" ? "-" : max) }'
}

# Three of the far side's 182 named counters: what it took from the wire, what it sent to the wire,
# and what it threw away because the queue to the wire was full.
fp() { "$FPCNT" | awk '$1 == 0 { w = $2 } $1 == 97 { t = $2 } $1 == 9 { d = $2 } END { print w, t, d }'; }

trap 'arp -d "$DST" >/dev/null 2>&1; ifconfig "$IFACE" -alias "$SRC" >/dev/null 2>&1; exit 130' INT TERM

ifconfig "$IFACE" inet "$SRC"/24 alias up || exit 1
arp -s "$DST" "$DMAC" > /dev/null 2>&1

echo "octep fairness sweep on $(hostname), $(date)"
echo "load on $IFACE, ${LEN}-byte payload; WAN $WANIF pinging $GW underneath it"
echo ""
set -- $(pingrun 30 6)
echo "with no load at all: $1% loss, $2 ms average, $3 ms worst"
echo ""
printf '%10s %12s %10s   %7s %8s %8s   %12s %12s\n' \
    target_pps offered_pps wire_Mbit wan_loss wan_avg wan_worst to_wire drop_q_full

for r in $RATES; do
	set -- $(fp); w0=$1 t0=$2 d0=$3
	pingrun $((SECS * 8)) $((SECS + 4)) > /tmp/fair.$$ &
	pp=$!
	if [ "$r" = 0 ]; then
		off=$("$BLASTER" "$SRC" "$DST" "$LEN" "$SECS" | sed -n 's/.*-> \([0-9]*\) pps.*/\1/p')
	else
		off=$("$BLASTER" "$SRC" "$DST" "$LEN" "$SECS" "$r" | sed -n 's/.*-> \([0-9]*\) pps.*/\1/p')
	fi
	wait $pp
	read loss avg max < /tmp/fair.$$
	rm -f /tmp/fair.$$
	set -- $(fp); d_t=$(($2 - t0)) d_d=$(($3 - d0))

	# On-wire bits include the preamble and the inter-frame gap, which is how the 1,023 Mbit/s
	# figure already published for this appliance was computed.
	mbit=$(echo "$off $LEN" | awk '{ printf "%.0f", $1 * ($2 + 42 + 20) * 8 / 1000000 }')
	printf '%10s %12s %10s   %6s%% %8s %8s   %12s %12s\n' \
	    "${r:-max}" "${off:-0}" "$mbit" "$loss" "$avg" "$max" "$d_t" "$d_d"
done

echo ""
set -- $(pingrun 30 6)
echo "with no load again:  $1% loss, $2 ms average, $3 ms worst"

arp -d "$DST" > /dev/null 2>&1
ifconfig "$IFACE" -alias "$SRC" > /dev/null 2>&1
