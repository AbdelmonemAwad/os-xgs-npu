#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# THE FAST PATH'S HEARTBEAT FOR MONIT (decision row 9, 2026-11-13): a program for Services: Monit, a
# Service of type Custom whose Service Test is "status != 0". One line on stdout, which Monit puts in
# its alert; exit 0 while the coprocessor's fast path answers the driver's heartbeat and the driver
# still uses it, 1 otherwise. Reads sysctls only: it changes nothing, and asks the coprocessor nothing.
#
#   fastpath-monit.sh
#
# The heartbeat is the driver's, on both families: dev.octep.0.heartbeat (XGS 2xxx, 3xxx, 4xxx) and
# dev.npuep.0.heartbeat (XGS 1xx), each with its state - off, waiting, alive, late or dead - and
# since_answer_ms. dev.<driver>.0.gone is 1 once the driver has taken the coprocessor as gone: it
# sends it nothing until it is brought up again, so a fast path that answers then carries no traffic,
# and that is 1 here too. Measured on the second XGS 3300 on 2026-11-12 with the same test in a
# Monit of its own, polling every 5 s: the fast path stopped for 45 s, the alert came about 24 s
# after the stop (late is not alive) and cleared as it answered again.

PATH=/sbin:/bin:/usr/sbin:/usr/bin

for drv in octep npuep; do
	state=$(sysctl -n dev.${drv}.0.heartbeat.state 2>/dev/null) || continue
	[ -n "${state}" ] || continue
	ms=$(sysctl -n dev.${drv}.0.heartbeat.since_answer_ms 2>/dev/null)
	gone=$(sysctl -n dev.${drv}.0.gone 2>/dev/null)
	if [ "${state}" = alive ] && [ "${gone}" != 1 ]; then
		echo "fast path heartbeat alive (${drv}), last answer ${ms:-?} ms ago"
		exit 0
	fi
	if [ "${gone}" = 1 ]; then
		echo "fast path heartbeat ${state} (${drv}), ${ms:-?} ms since its last answer; the driver has taken the coprocessor as gone and uses it no more until it is brought up again (XGS: Settings, When the fast path stops)"
	else
		echo "fast path heartbeat ${state} (${drv}), ${ms:-?} ms since its last answer"
	fi
	exit 1
done
echo "no fast path heartbeat on this host (neither dev.octep.0.heartbeat nor dev.npuep.0.heartbeat)"
exit 1
