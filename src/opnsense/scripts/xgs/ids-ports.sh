#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# THE INTRUSION DETECTION BACK ON THE PORTS THAT ARE THERE NOW (#1153): run once the front ports have
# been made again - by recover.sh at the end of a recovery in place, by contrib/npuep/reload.sh, by
# scripts/octep/bringup.sh when it made the ports itself - it restarts OPNsense's Intrusion Detection
# when it runs in Netmap (IPS) mode on one of them, and does nothing otherwise. One line on stdout when
# it did something or refused to, nothing at all when there was nothing to do; the caller logs that
# line the way it logs its own. Exit 0, or lockf's 75 while another run of this has its lock: a
# caller's own result never depends on this.
#
#   ids-ports.sh
#
# WHY. A driver reload destroys every front port's ifnet and makes new ones under the same names.
# Suricata in Netmap mode keeps the rings it opened on the old ones and never opens them again: on the
# XGS 136 on 2026-11-11 (item M1, the driver reloaded under Suricata in IPS mode on Port8) it went on
# writing 30,000 errors a second at 98 % CPU, and the traffic crossed uninspected, until the Intrusion
# Detection was restarted by hand. Suricata in PCAP mode reopens a capture that failed by itself, and
# in Divert mode is not on an interface, so those are left alone.
#
# WHAT IS READ. The Intrusion Detection's own model (OPNsense\IDS\IDS: general.enabled, general.mode,
# general.interfaces), each interface it names resolved to its device in the configuration as
# OPNsense's own template does (helpers.physical_interface), and oxips0 when this plugin's overlay
# gives it to Suricata (xgs-inspector.yaml, "Inspect, then hardware"). A device is one of ours when it
# is a front port, the inspector's or the management link's interface, or a VLAN or LAGG over one of
# them - those are destroyed with their parent. A bridge over the ports is not: it is the same ifnet
# before and after, and Suricata's rings on it stay good.
#
# ONCE PER RELOAD. Nothing is done while a recovery in place runs, unless it is the recovery asking:
# it restarts the Intrusion Detection itself at its end, once the interfaces have been reloaded, and a
# restart in its middle would meet ports not configured yet. Two callers at once give one restart
# (a lock of this script's own, not waited for). Nothing is done either while Suricata is not running:
# at boot it starts after the ports are made, and an Intrusion Detection stopped by hand stays stopped.

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
LOCK=/var/run/xgs-ids-ports.lock
RECOVER_LOCK=/var/run/xgs-recover.lock
OVERLAY=/usr/local/etc/suricata/conf.d/xgs-inspector.yaml
OURS='^(npup|oxp|oxips|mvmgmt|oxmgmt)[0-9]+$'

if [ -z "${XGS_IDS_LOCKED:-}" ]; then
	XGS_IDS_LOCKED=1 exec lockf -s -k -t 0 "${LOCK}" /bin/sh "$0" "$@"
fi

# No Suricata running: nothing on the old rings, nothing to do - and nothing asked of configd, which
# the early boot hooks run before.
pgrep -x suricata > /dev/null 2>&1 || exit 0

# A recovery in place holds its lock for minutes; fastpath.py and feeder.py take it for a moment to
# see whether one runs, which two seconds outwait. Not looked at when the recovery is the caller.
if [ -z "${XGS_RECOVER_LOCKED:-}" ] && ! lockf -s -k -t 2 "${RECOVER_LOCK}" /usr/bin/true; then
	echo "the Intrusion Detection not restarted here: a recovery in place runs, and restarts it at its end"
	exit 0
fi

# "<enabled> <mode>" on the first line, then each device the Intrusion Detection is given, one a line.
settings=$(/usr/local/bin/php -r '
	require_once("config.inc");
	$g = (new \OPNsense\IDS\IDS())->general;
	echo (string)$g->enabled, " ", (string)$g->mode, "\n";
	$c = \OPNsense\Core\Config::getInstance()->object();
	foreach (explode(",", (string)$g->interfaces) as $k) {
		if ($k !== "" && isset($c->interfaces->$k->if)) {
			echo (string)$c->interfaces->$k->if, "\n";
		}
	}' 2>/dev/null)
set -- $(echo "${settings}" | head -n 1)
[ "${1:-0}" = 1 ] && [ "${2:-}" = netmap ] || exit 0
devs=$(echo "${settings}" | sed 1d)
grep -q 'interface: oxips0' "${OVERLAY}" 2>/dev/null && devs="${devs} oxips0"

# Each device that is one of ours, or a VLAN or LAGG over one; the first found names the line.
on=
for dev in ${devs}; do
	if echo "${dev}" | grep -Eq "${OURS}"; then
		on="${on} ${dev}"
		continue
	fi
	for parent in $(ifconfig "${dev}" 2>/dev/null | awk '/parent interface:/ {print $NF} /laggport:/ {print $2}'); do
		if echo "${parent}" | grep -Eq "${OURS}"; then
			on="${on} ${dev}"
			break
		fi
	done
done
[ -n "${on}" ] || exit 0

# Its devices back first, for up to 60 s: Suricata does not start when an interface it is given is
# missing. A device still missing then is said, and the restart made all the same - the old Suricata
# spins on rings that are gone, which is worse than one that did not start.
i=0
missing=
while [ "${i}" -lt 60 ]; do
	missing=
	for dev in ${on}; do
		ifconfig "${dev}" > /dev/null 2>&1 || missing="${missing} ${dev}"
	done
	[ -z "${missing}" ] && break
	i=$((i + 1))
	sleep 1
done

if configctl ids restart > /dev/null 2>&1; then
	result="restarted"
else
	result="asked to restart, and configd said it failed"
fi
line="the Intrusion Detection ${result}: in Netmap mode on${on}, whose interfaces were made again"
[ -n "${missing}" ] && line="${line} (still missing after 60 s:${missing})"
echo "${line}"
exit 0
