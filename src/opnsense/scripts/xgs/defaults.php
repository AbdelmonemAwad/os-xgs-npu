<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The appliance's own settings in OPNsense's own pages - docs/octep/appliance-defaults.md.
 *
 *   defaults.php           set what this appliance needs and nothing is chosen for yet, once each
 *   defaults.php --check   say what would be set, change nothing
 *
 * WHAT IT SETS. Settings OPNsense already has a page for, set to what this hardware needs, so the
 * page shows what the box does and the owner changes it there like any other:
 *
 *   system/thermal_hardware = amdtemp   System: Settings: Miscellaneous, Thermal Sensors. Both XGS
 *                                       hosts are AMD Family 17h (Ryzen Embedded V1780B in the XGS
 *                                       3300, R1606G in the XGS 136), and amdtemp attaches to both:
 *                                       measured 38.6 and 53.6 C on 2026-10-17. OPNsense loads the
 *                                       module named here at every boot, and the CPU temperature
 *                                       then reaches its own dashboard.
 *
 * ONCE EACH, AND NEVER OVER THE OWNER. A setting is written only when nothing is chosen for it, and
 * its name is then recorded in /conf/os-xgs-npu-defaults, so an owner who sets it back to None on
 * the page afterwards is not overridden by the next install or update. One that the owner had
 * already chosen is recorded without being written.
 */

require_once 'config.inc';
require_once 'util.inc';

$MARK = '/conf/os-xgs-npu-defaults';
$check = in_array('--check', $argv, true);

$board = trim((string)shell_exec('/bin/sh /usr/local/opnsense/scripts/xgs/board.sh 2>/dev/null'));
$family = explode(' ', $board)[1] ?? '';
if ($family !== 'octeontx' && $family !== 'armada') {
    echo "not an XGS appliance this project knows - nothing set\n";
    exit(0);
}

$done = file_exists($MARK) ? array_values(array_filter(array_map('trim', file($MARK)))) : [];
$cfg = \OPNsense\Core\Config::getInstance();
$c = $cfg->object();
$changed = false;
$recorded = [];

/* The CPU's die sensor: amdtemp, where it is the module this CPU needs and it attaches. */
$key = 'thermal_hardware';
$cpu = trim((string)shell_exec('/sbin/sysctl -n hw.model 2>/dev/null'));
if (in_array($key, $done, true)) {
    echo "{$key}: set once already, left as the owner has it (" . ((string)$c->system->$key ?: 'None') . ")\n";
} elseif ((string)$c->system->$key !== '') {
    echo "{$key}: the owner chose " . (string)$c->system->$key . " - left as it is\n";
    $recorded[] = $key;
} elseif (stripos($cpu, 'AMD') === false || !file_exists('/boot/kernel/amdtemp.ko')) {
    echo "{$key}: not an AMD host or no amdtemp module ({$cpu}) - nothing set\n";
} elseif ($check) {
    echo "{$key}: would be set to amdtemp ({$cpu})\n";
} else {
    exec('/sbin/kldstat -q -m amdtemp || /sbin/kldload amdtemp 2>/dev/null', $out, $rc);
    if (trim((string)shell_exec('/sbin/sysctl -n dev.amdtemp.0.%driver 2>/dev/null')) !== 'amdtemp') {
        echo "{$key}: amdtemp did not attach on this CPU ({$cpu}) - nothing set\n";
    } else {
        $c->system->$key = 'amdtemp';
        $changed = true;
        $recorded[] = $key;
        echo "{$key}: amdtemp - the CPU temperature, loaded by OPNsense at every boot ({$cpu})\n";
    }
}

if ($check) {
    exit(0);
}
if ($changed) {
    $cfg->save();
}
if (!empty($recorded)) {
    file_put_contents($MARK, implode("\n", array_merge($done, $recorded)) . "\n");
}
