#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The coprocessor's clock, set from this host's (2026-10-27). Nothing else sets it under OPNsense:
# a few minutes into a boot both XGS 3300s' coprocessors read 1970 and the XGS 136's read 2097, so
# every time the coprocessor writes - in its own log, on its console - was wrong. The vendor's host
# sets it at every boot; this does the same at the end of the boot (start hook 71-xgs-npu-clock), and
# every ten minutes from cron, so a coprocessor taken up again in place (scripts/xgs/recover.sh) is
# set again within ten minutes.
#
# Over the management link only (scripts/npuctl/mgmt-link.sh), never the console. The coprocessor's
# clock is set, in UTC, when it is more than two seconds from this host's, and that is said in the
# system log (tag xgs-npu-clock) with what it read before. Exit 0 when the clock is right or was set,
# 1 when the coprocessor did not answer or did not take it. Quiet unless asked (-v).

VERBOSE=0
[ "${1:-}" = -v ] && VERBOSE=1
say() { [ "${VERBOSE}" = 1 ] && echo "$1"; return 0; }

LINK=/usr/local/opnsense/scripts/npuctl/mgmt-link.sh
[ -x "${LINK}" ] || { say "no management link script"; exit 1; }
link() { /usr/bin/timeout 15 /bin/sh "${LINK}" ssh "$1" 2> /dev/null | /usr/bin/tail -1; }

THEIRS=$(link 'date -u +%s')
NOW=$(/bin/date -u +%s)
case ${THEIRS} in
''|*[!0-9]*) say "the coprocessor did not answer over the management link"; exit 1 ;;
esac
OFF=$((THEIRS - NOW))
[ ${OFF} -lt 0 ] && OFF=$((-OFF))
if [ ${OFF} -le 2 ]; then
	say "the coprocessor's clock is within ${OFF} s of this host's"
	exit 0
fi

WAS=$(/bin/date -u -r "${THEIRS}" '+%Y-%m-%d %H:%M:%S' 2> /dev/null || echo "${THEIRS} s")
SET=$(/bin/date -u '+%Y-%m-%d %H:%M:%S')
link "date -u -s '${SET}' > /dev/null" > /dev/null
AFTER=$(link 'date -u +%s')
case ${AFTER} in
''|*[!0-9]*) say "the coprocessor did not answer after its clock was set"; exit 1 ;;
esac
OFF=$((AFTER - $(/bin/date -u +%s)))
[ ${OFF} -lt 0 ] && OFF=$((-OFF))
if [ ${OFF} -gt 2 ]; then
	say "the coprocessor did not take the time: it still reads $(/bin/date -u -r "${AFTER}" '+%Y-%m-%d %H:%M:%S')"
	exit 1
fi
/usr/bin/logger -t xgs-npu-clock "the coprocessor's clock set from this host's: it read ${WAS} UTC"
say "set to ${SET} UTC; it read ${WAS} UTC"
exit 0
