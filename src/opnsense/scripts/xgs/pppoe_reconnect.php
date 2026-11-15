<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * A scheduled PPPoE reconnect (plan 01, package 3.7; XGS: Settings, "PPPoE: reconnect every day at"):
 * every enabled PPPoE interface configured again - OPNsense's own rc.configure_interface, which stops
 * its mpd5 link and dials again - at the time the setting names, from the plugin's cron job
 * (xgs_cron in plugins.inc.d/xgs.inc). For a provider that cuts a session after a day at a time of
 * its own: a reconnect at a quiet hour instead.
 *
 *   pppoe_reconnect.php     reconnect each; print {"interfaces", "message"}
 *
 * OPNsense 26.7 has no such schedule of its own, and its LCP echo is fixed (interfaces.inc writes "set
 * link keep-alive 10 60" into each link's mpd5 configuration): changing that would mean editing a file
 * OPNsense writes again at every configure, so it is shown, not changed.
 */

/* The keys of the enabled PPPoE interfaces in a configuration's interfaces section. */
function xgs_pppoe_interfaces(array $interfaces): array
{
    $keys = [];
    foreach ($interfaces as $key => $intf) {
        if (is_array($intf) && ($intf['ipaddr'] ?? '') === 'pppoe' && !empty($intf['enable'])) {
            $keys[] = (string)$key;
        }
    }
    return $keys;
}

/* "HH:MM" into the cron job's minute and hour, or null for none. */
function xgs_pppoe_reconnect_when(string $at): ?array
{
    if (!preg_match('/^([01][0-9]|2[0-3]):([0-5][0-9])$/', trim($at), $m)) {
        return null;
    }
    return [(string)(int)$m[2], (string)(int)$m[1]];
}

// The tests include this file for the functions above alone.
if (getenv('XGS_PPPOE_FUNCTIONS_ONLY') === '1') {
    return;
}

require_once('config.inc');
require_once('util.inc');

$keys = xgs_pppoe_interfaces($config['interfaces'] ?? []);
foreach ($keys as $key) {
    syslog(LOG_NOTICE, 'xgs: PPPoE on ' . $key . ' reconnected at the scheduled time');
    mwexecf('/usr/local/etc/rc.configure_interface %s', [$key]);
}
echo json_encode(['interfaces' => $keys, 'message' => empty($keys) ? 'no enabled PPPoE interface' : '']) . "\n";
