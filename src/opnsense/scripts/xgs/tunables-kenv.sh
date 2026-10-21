#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Put the driver's loader tunables from the configuration into the kernel environment, just before
# the driver is loaded - so that a configuration restored since the last boot reaches the driver on
# this boot, not the next (issue 669).
#
#   tunables-kenv.sh hw.npuep.|hw.octep.     prints each name=value it set, one a line
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
# in place and changes nothing. Only numbers, and only names under the prefix; a variable the
# configuration does not name is left alone, since it may be the owner's own, in loader.conf.local.

PREFIX=${1:-}
case ${PREFIX} in
hw.npuep.|hw.octep.) ;;
*) echo "usage: tunables-kenv.sh hw.npuep.|hw.octep." >&2; exit 64 ;;
esac
: ${KENV:=/bin/kenv}
: ${PHP:=/usr/local/bin/php}

${PHP} -r '
require_once("config.inc");
$items = $config["sysctl"]["item"] ?? [];
foreach ((isset($items["tunable"]) ? [$items] : $items) as $item) {
    $name = trim((string)($item["tunable"] ?? ""));
    $value = trim((string)($item["value"] ?? ""));
    if (strpos($name, $argv[1]) === 0 && preg_match("/^[a-z0-9_.]+$/", $name) &&
        preg_match("/^[0-9]+$/", $value)) {
        echo $name, "=", $value, "\n";
    }
}' "${PREFIX}" 2>/dev/null | while IFS='=' read -r name value; do
	[ "$(${KENV} -q "${name}")" = "${value}" ] && continue
	${KENV} "${name}=${value}" > /dev/null 2>&1 && echo "${name}=${value}"
done
exit 0
