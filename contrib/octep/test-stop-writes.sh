#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# A TEST for a lab appliance with octep loaded - never a firewall in service: it stops the datapath,
# and the front ports stay gone until the appliance restarts.
#
# Once a ring is stopped the coprocessor must write nothing more into its memory, or memory given
# back would be written by a device (#962). This stops the datapath with dp.stop_check set: the
# receive buffers are poisoned and the coprocessor's writes counted for a second while the rings
# still run - which proves the count can see one - and for a second after they are known stopped,
# where it must find none. Frames have to be arriving at the front ports meanwhile: a broadcast
# stream from a machine on a LAN port does, since it needs no address resolved.
#
#   sh contrib/octep/test-stop-writes.sh
#
# Exit 0 when writes were seen before the stop and none after it, 1 otherwise.

O=dev.octep.0
kldstat -q -n octep || { echo "octep is not loaded - nothing to test"; exit 1; }
sysctl -N ${O}.dp.stop_check > /dev/null 2>&1 ||
    { echo "this octep has no dp.stop_check - it predates #962"; exit 1; }
[ "$(sysctl -n ${O}.dp.up)" = 1 ] || { echo "the datapath is not up - nothing to stop"; exit 1; }

sysctl ${O}.dp.stop_check=1 > /dev/null
sysctl ${O}.dp.stop=1 > /dev/null 2>&1 || { echo "dp.stop refused or failed"; exit 1; }
res=$(sysctl -n ${O}.dp.stop_check_result)
echo "  ${res}"
sysctl ${O}.dp.kept_bytes ${O}.dp.quiet_us_max | sed 's/^/  /'
before=$(echo "${res}" | awk '{print $1}')
after=$(echo "${res}" | sed -n 's/.*, \([0-9]*\) in the second after.*/\1/p')

if [ -z "${before}" ] || [ -z "${after}" ]; then
    echo "the result could not be read"
    exit 1
fi
if [ "${before}" = 0 ]; then
    echo "no write seen while the rings ran - no traffic arrived, so the check proved nothing"
    exit 1
fi
if [ "${after}" != 0 ]; then
    echo "the coprocessor wrote ${after} word(s) after its rings were stopped"
    exit 1
fi
echo "writes seen while the rings ran, none once they were stopped"
exit 0
