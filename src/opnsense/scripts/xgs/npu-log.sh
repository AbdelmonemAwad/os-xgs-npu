#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The coprocessor's own log sent to this host (2026-10-28). Its syslogd keeps the log in RAM - half
# a megabyte, lost at every reset of the coprocessor - and hands each line to a small forwarder on
# the coprocessor, which sends nothing until it is told where. The vendor's host tells it at every
# boot; under OPNsense nothing did. This does: it writes this host's end of the management link
# into the forwarder's address file and signals it (SIGUSR1), and from then on every line goes to
# 169.254.254.1, port 514, which pf takes to syslog-ng (xgs.inc,
# etc/syslog-ng.conf.d/xgs-coprocessor.conf) and the Coprocessor Log page shows.
#
# The forwarder's address file reads <address>%<its interface>%<port>; the coprocessor's end of
# the link is mvmgmt0 on both families (scripts/npuctl/mgmt-link.sh). The vendor's own script writes
# it from a second file, which the coprocessor's start-up also reads to choose its path, so that
# one is left alone and the address file written directly.
#
# At the end of the boot (start hook 72-xgs-npu-log) and every ten minutes from cron, so a
# coprocessor taken up again in place (scripts/xgs/recover.sh), whose address file was in RAM too,
# is told again within ten minutes; nothing is done while the file holds this host's address.
# Over the management link only, never the console. Telling it is said in the system log (tag
# xgs-npu-log). Exit 0 when the forwarder was told or already knew, 1 when the coprocessor did not
# answer or runs no forwarder. Quiet unless asked (-v).
#
#   npu-log.sh [-v]        tell the forwarder, when it does not know
#   npu-log.sh stop [-v]   stop the forwarder until the coprocessor's next start, its address file
#                          gone - for the uninstaller: once the redirect is gone, every line it sent
#                          would be refused, and logged, by the firewall's default rule. It has no
#                          signal to stop sending, and until it is told it only drops what it gets.

STOP=0
VERBOSE=0
for a in "$@"; do
	case ${a} in
	stop) STOP=1 ;;
	-v) VERBOSE=1 ;;
	esac
done
say() { [ "${VERBOSE}" = 1 ] && echo "$1"; return 0; }

LINK=/usr/local/opnsense/scripts/npuctl/mgmt-link.sh
[ -x "${LINK}" ] || { say "no management link script"; exit 1; }
link() { /usr/bin/timeout 15 /bin/sh "${LINK}" ssh "$1" 2> /dev/null | /usr/bin/tail -1; }

TO='169.254.254.1%mvmgmt0%514'
FILE=/tmp/remote_syslog.txt

if [ "${STOP}" = 1 ]; then
	GOT=$(link "rm -f ${FILE}; pkill xgs_log_fwd; sleep 1; pidof xgs_log_fwd > /dev/null && echo running || echo stopped")
	case ${GOT} in
	stopped)
		/usr/bin/logger -t xgs-npu-log "the coprocessor's log no longer sent to this host: its forwarder stopped until the coprocessor's next start"
		say "the forwarder stopped until the coprocessor's next start"
		exit 0
		;;
	running)
		say "the forwarder did not stop"
		exit 1
		;;
	esac
	say "the coprocessor did not answer over the management link"
	exit 1
fi

GOT=$(link "pidof xgs_log_fwd > /dev/null || { echo none; exit 0; }; [ \"\$(cat ${FILE} 2> /dev/null)\" = '${TO}' ] && { echo known; exit 0; }; echo '${TO}' > ${FILE} && pkill -USR1 xgs_log_fwd && echo told")
case ${GOT} in
known)
	say "the coprocessor's forwarder already sends its log here"
	exit 0
	;;
told)
	/usr/bin/logger -t xgs-npu-log "the coprocessor's log sent to this host from now: its forwarder told ${TO}"
	say "the forwarder told ${TO}"
	exit 0
	;;
none)
	say "the coprocessor runs no log forwarder"
	exit 1
	;;
esac
say "the coprocessor did not answer over the management link"
exit 1
