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
# 1 when the coprocessor did not answer or did not take it. Quiet unless asked (-v), but for a set
# that did not hold, which the system log has as well - once, until it changes or the clock is right
# again, since cron would say it every ten minutes. Until 2026-11-06 only -v said that one.

VERBOSE=0
[ "${1:-}" = -v ] && VERBOSE=1
say() { [ "${VERBOSE}" = 1 ] && echo "$1"; return 0; }
SAID=/var/run/xgs-npu-clock.failed
failed() { # <what> [<detail>]: said, logged unless it was the last thing logged, and exit 1
	say "$1${2:+: $2}"
	if [ "$(/bin/cat "${SAID}" 2> /dev/null)" != "$1" ]; then
		/usr/bin/logger -t xgs-npu-clock "$1${2:+: $2}"
		echo "$1" > "${SAID}"
	fi
	exit 1
}

LINK=/usr/local/opnsense/scripts/npuctl/mgmt-link.sh
[ -x "${LINK}" ] || { say "no management link script"; exit 1; }
link() { /usr/bin/timeout 15 /bin/sh "${LINK}" ssh "$1" 2> /dev/null | /usr/bin/tail -1; }

T0=$(/bin/date -u +%s)
THEIRS=$(link 'date -u +%s')
NOW=$(/bin/date -u +%s)
case ${THEIRS} in
''|*[!0-9]*) say "the coprocessor did not answer over the management link"; exit 1 ;;
esac
OFF=$((THEIRS - NOW))
[ ${OFF} -lt 0 ] && OFF=$((-OFF))
if [ ${OFF} -le 2 ]; then
	/bin/rm -f "${SAID}"
	say "the coprocessor's clock is within ${OFF} s of this host's"
	exit 0
fi

# The time the coprocessor is given is this host's when the command reaches it: a connection over the
# link takes about as long as the read above did (NOW - T0, whole seconds). Until 2026-11-06 the time
# was taken before the connection, so the coprocessor's clock was set behind by its length - past the
# two seconds the check below allows on a slow one, and then set again, every ten minutes.
WAS=$(/bin/date -u -r "${THEIRS}" '+%Y-%m-%d %H:%M:%S' 2> /dev/null || echo "${THEIRS} s")
SET=$(/bin/date -u -r "$(( $(/bin/date -u +%s) + NOW - T0 ))" '+%Y-%m-%d %H:%M:%S')
link "date -u -s '${SET}' > /dev/null" > /dev/null
AFTER=$(link 'date -u +%s')
case ${AFTER} in
''|*[!0-9]*) failed "the coprocessor did not answer after its clock was set" ;;
esac
OFF=$((AFTER - $(/bin/date -u +%s)))
[ ${OFF} -lt 0 ] && OFF=$((-OFF))
if [ ${OFF} -gt 2 ]; then
	failed "the coprocessor did not take the time" "it still reads $(/bin/date -u -r "${AFTER}" '+%Y-%m-%d %H:%M:%S') UTC"
fi
/bin/rm -f "${SAID}"
/usr/bin/logger -t xgs-npu-clock "the coprocessor's clock set from this host's: it read ${WAS} UTC"
say "set to ${SET} UTC; it read ${WAS} UTC"
exit 0
