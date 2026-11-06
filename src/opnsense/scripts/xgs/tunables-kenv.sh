#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Put the driver's loader tunables from the configuration into the kernel environment, just before
# the driver is loaded - so that a configuration restored since the last boot reaches the driver on
# this boot, not the next (issue 669).
#
#   tunables-kenv.sh hw.npuep.|hw.octep.     prints each name=value it set, one a line, and
#                                            "name removed" for each it took away
#
# WHY. XGS: Settings keeps what the driver reads when it attaches - IPsec offload, tunnel flows, the
# receive path's polling - as rows of System: Settings: Tunables, and OPNsense writes those rows into
# /boot/loader.conf. It writes the file during the boot, though, after the loader has read the one
# before it. So after a restore (System: Configuration: Backups, then the reboot it asks for) the
# driver attached with the old file's values, and the restored ones reached it only at the boot
# after. Measured on the XGS 136 on 2026-10-21: IPsec offload off at the first boot after a restore,
# on at the second.
#
# HOW. Both families load their driver with kldload from an early hook (07-npuep; bringup.sh from
# 08-octep), and a module reads its tunables from the kernel environment when it loads; kenv(1) sets
# that environment for whatever loads next. So each row of the configuration under the prefix whose
# value the environment does not already hold is set there. Any other boot finds every value already
# in place and changes nothing. Only numbers, and only names under the prefix.
#
# AND WHAT THE CONFIGURATION NO LONGER HAS. A row the restored configuration - or a factory reset's -
# does not have is still in the old /boot/loader.conf, which the loader read, so the driver attached
# with it all the same; until 2026-11-06 it went only at the boot after (the other half of issue 669:
# a flow table's size or a transmit queue count the owner had taken out). So a variable under the
# prefix that the configuration does not name is taken out of the environment when it holds the
# number /boot/loader.conf gives it - the file OPNsense writes from the configuration - and no line of
# the owner's own loader files (loader.conf.local, loader.conf.d) names it. Anything else is left
# alone: set at the loader's prompt, by the owner's files, or by the hooks themselves. And nothing is
# taken out unless the configuration was read: a PHP that failed would otherwise take out every one.

PREFIX=${1:-}
case ${PREFIX} in
hw.npuep.|hw.octep.) ;;
*) echo "usage: tunables-kenv.sh hw.npuep.|hw.octep." >&2; exit 64 ;;
esac
: ${KENV:=/bin/kenv}
: ${PHP:=/usr/local/bin/php}
: ${LOADER_CONF:=/boot/loader.conf}

# "#read" once the configuration is read, a name=value line for each row to set, and "#named name" for
# a row under the prefix whose value is not a number: not set, and not taken out either.
ROWS=$(${PHP} -r '
require_once("config.inc");
if (isset($config["system"])) {
    echo "#read\n";
}
$items = $config["sysctl"]["item"] ?? [];
foreach ((isset($items["tunable"]) ? [$items] : $items) as $item) {
    $name = trim((string)($item["tunable"] ?? ""));
    $value = trim((string)($item["value"] ?? ""));
    if (strpos($name, $argv[1]) === 0 && preg_match("/^[a-z0-9_.]+$/", $name)) {
        echo preg_match("/^[0-9]+$/", $value) ? $name . "=" . $value : "#named " . $name, "\n";
    }
}' "${PREFIX}" 2>/dev/null)

echo "${ROWS}" | while IFS='=' read -r name value; do
	case ${name} in ''|'#'*) continue ;; esac
	[ "$(${KENV} -q "${name}")" = "${value}" ] && continue
	${KENV} "${name}=${value}" > /dev/null 2>&1 && echo "${name}=${value}"
done

case ${ROWS} in
'#read'*) ;;
*) exit 0 ;;
esac
# A name's dots matched as dots: the names are [a-z0-9_.] (the sed below), nothing else to escape.
pattern() { echo "$1" | /usr/bin/sed 's/\./\\./g'; }
${KENV} 2> /dev/null | /usr/bin/sed -n 's/^\([a-z0-9_.]*\)=.*/\1/p' | while read -r name; do
	case ${name} in "${PREFIX}"*) ;; *) continue ;; esac
	re=$(pattern "${name}")
	echo "${ROWS}" | /usr/bin/grep -Eq "^(${re}=|#named ${re}\$)" && continue
	written=$(/usr/bin/sed -n -E "s/^[[:space:]]*${re}[[:space:]]*=[[:space:]]*\"?([0-9]+)\"?[[:space:]]*(#.*)?\$/\1/p" \
	    "${LOADER_CONF}" 2> /dev/null | /usr/bin/tail -1)
	[ -n "${written}" ] && [ "$(${KENV} -q "${name}")" = "${written}" ] || continue
	cat "${LOADER_CONF}.local" "${LOADER_CONF}.d"/*.conf 2> /dev/null |
	    /usr/bin/grep -Eq "^[[:space:]]*${re}[[:space:]]*=" && continue
	${KENV} -u "${name}" > /dev/null 2>&1 && echo "${name} removed"
done
exit 0
