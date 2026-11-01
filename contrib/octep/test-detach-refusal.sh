#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# A TEST for a lab appliance with octep loaded - never a firewall in service: what frees anything a
# receive servicer uses must refuse while one cannot leave its ring.
#
# A servicer still inside a ring a second after it was told to stop holds mbufs that point at the
# interfaces. Removing the interfaces, stopping the datapath or detaching the device used to go on
# regardless and free them under it (a review of the code, 2026-11-01); now each refuses with EBUSY and
# changes nothing. This holds one ring's busy flag the way such a servicer would (dp.hold_ring), asks
# for each of the four in turn, checks that every one refused and that the front ports are all still
# there, lets the ring go and checks again. Run it after any change to the datapath's stop or detach.
#
#   sh contrib/octep/test-detach-refusal.sh [ring]     the ring to hold, 1 by default
#
# Exit 0 when every check passed, otherwise the number that failed. The ring is let go on every path.

ring=${1:-1}
fail=0

check() {
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s (%s, not %s)\n' "$1" "$2" "$3"
        fail=$((fail + 1))
    fi
}
ports() { ifconfig -l | tr ' ' '\n' | grep -c '^oxp'; }

kldstat -q -n octep || { echo "octep is not loaded - nothing to test"; exit 1; }
sysctl -N dev.octep.0.dp.hold_ring > /dev/null 2>&1 ||
    { echo "this octep has no dp.hold_ring - it predates the refusal"; exit 1; }
before=$(ports)
trap 'sysctl dev.octep.0.dp.hold_ring=-1 > /dev/null 2>&1' EXIT INT TERM HUP

echo "== ring ${ring} held as a servicer that cannot leave would hold it"
sysctl dev.octep.0.dp.hold_ring="${ring}" > /dev/null 2>&1
check "the ring is held" "$(sysctl -n dev.octep.0.dp.hold_ring)" "${ring}"
sysctl dev.octep.0.dp.if_del=1 > /dev/null 2>&1
check "dp.if_del refused" $? 1
sysctl dev.octep.0.dp.stop=1 > /dev/null 2>&1
check "dp.stop refused" $? 1
sysctl dev.octep.0.dp.rx_quiesce=1 > /dev/null 2>&1
check "dp.rx_quiesce refused" $? 1
check "the quiesce let go again" "$(sysctl -n dev.octep.0.dp.rx_quiesce)" 0
kldunload octep > /dev/null 2>&1
check "kldunload refused" $? 1
kldstat -q -n octep
check "octep still loaded" $? 0
check "every front port still there" "$(ports)" "${before}"

echo "== the ring let go"
sysctl dev.octep.0.dp.hold_ring=-1 > /dev/null 2>&1
check "the ring is free" "$(sysctl -n dev.octep.0.dp.hold_ring)" -1
check "every front port still there" "$(ports)" "${before}"
printf '  %s\n' "$(sysctl -n dev.octep.0.dp.oq_busy)"

echo
if [ "${fail}" = 0 ]; then
    echo "every path refused while the ring was held, and nothing went"
else
    echo "${fail} check(s) failed"
fi
exit "${fail}"
