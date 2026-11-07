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
 * already chosen is recorded without being written. A factory reset empties /conf, the record with
 * it, and the start hook 74-xgs-defaults runs this again at the boot after one (since 2026-11-06).
 *
 * TAKEN OUT BY ANYTHING BUT ITS PAGE. A setting recorded as set and not in the configuration now was
 * taken out by a save after the last one that had it, and OPNsense's history names that save
 * (removed_by). The owner's None is a save of its page; anything else - a configuration restored
 * from before it was set, or a script's save over it - is not the owner's choice, and the setting is
 * set again, the save that took it out said. Until 2026-11-07 the record alone decided, and an XGS
 * 107w whose configuration had lost the setting went without its CPU temperature from its next boot
 * on, the record saying it was set (issue 890). When the history no longer goes back to that save,
 * the setting is left as it is, and verify.sh says how to set it.
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

/*
 * The save that took system/$key out: the one after the newest backup that still has it, out of
 * OPNsense's history (/conf/backup - each backup the configuration as a save left it, its revision
 * naming the page or the script that saved it, and who). Its revision; null when the newest backup
 * still has it, so the configuration was written past the history; false when no backup kept has it
 * - the history does not go back that far.
 */
function removed_by($cfg, $key)
{
    $after = null;
    foreach ($cfg->getBackups() as $file) {
        $xml = @simplexml_load_file($file, 'SimpleXMLElement', LIBXML_NOERROR | LIBXML_ERR_NONE);
        if ($xml === false) {
            continue;
        }
        if (isset($xml->system->$key) && (string)$xml->system->$key !== '') {
            return $after;
        }
        $after = [
            'time' => (string)$xml->revision->time,
            'description' => (string)$xml->revision->description,
            'username' => (string)$xml->revision->username,
        ];
    }
    return false;
}

/* A revision as the history shows it: its description, who, and when (UTC). */
function revision_said($rev)
{
    if ($rev === null) {
        return 'a write of the configuration that left no revision in its history';
    }
    $when = is_numeric($rev['time']) ? gmdate('Y-m-d H:i', (int)$rev['time']) . ' UTC' : 'a time not recorded';
    return sprintf('"%s" by %s, %s', $rev['description'], $rev['username'] !== '' ? $rev['username'] : '?', $when);
}

$done = file_exists($MARK) ? array_values(array_filter(array_map('trim', file($MARK)))) : [];
$cfg = \OPNsense\Core\Config::getInstance();
$c = $cfg->object();
$changed = false;
$recorded = [];

/* The CPU's die sensor: amdtemp, where it is the module this CPU needs and it attaches. */
$key = 'thermal_hardware';
$cpu = trim((string)shell_exec('/sbin/sysctl -n hw.model 2>/dev/null'));
$again = null;
if (in_array($key, $done, true) && (string)$c->system->$key === '') {
    $by = removed_by($cfg, $key);
    if ($by === false) {
        echo "{$key}: set once already, and None now; the configuration's history no longer goes back to the save " .
            "that took it out, so it is left as it is - System: Settings: Miscellaneous, Thermal Sensors sets it\n";
    } elseif ($by !== null && strpos($by['description'], 'system_advanced_misc.php') !== false) {
        echo "{$key}: set once already, and set to None on System: Settings: Miscellaneous - " .
            revision_said($by) . " - left as the owner has it\n";
    } else {
        $again = revision_said($by);
    }
}
if ($again === null && in_array($key, $done, true)) {
    if ((string)$c->system->$key !== '') {
        echo "{$key}: set once already, left as the owner has it (" . (string)$c->system->$key . ")\n";
    }
} elseif ((string)$c->system->$key !== '') {
    echo "{$key}: the owner chose " . (string)$c->system->$key . " - left as it is\n";
    $recorded[] = $key;
} elseif (stripos($cpu, 'AMD') === false || !file_exists('/boot/kernel/amdtemp.ko')) {
    echo "{$key}: not an AMD host or no amdtemp module ({$cpu}) - nothing set\n";
} elseif ($check) {
    echo "{$key}: would be set " . ($again === null ? '' : 'again ') . "to amdtemp ({$cpu})" .
        ($again === null ? '' : " - set once, and taken out by {$again}") . "\n";
} else {
    exec('/sbin/kldstat -q -m amdtemp || /sbin/kldload amdtemp 2>/dev/null', $out, $rc);
    if (trim((string)shell_exec('/sbin/sysctl -n dev.amdtemp.0.%driver 2>/dev/null')) !== 'amdtemp') {
        echo "{$key}: amdtemp did not attach on this CPU ({$cpu}) - nothing set\n";
    } else {
        $c->system->$key = 'amdtemp';
        $changed = true;
        $recorded[] = $key;
        echo "{$key}: amdtemp - the CPU temperature, loaded by OPNsense at every boot ({$cpu})" .
            ($again === null ? '' : "; set again: it was set once, and taken out by {$again}, not on its page") . "\n";
    }
}

if ($check) {
    exit(0);
}
if ($changed) {
    $cfg->save();
}
if (!empty(array_diff($recorded, $done))) {
    file_put_contents($MARK, implode("\n", array_unique(array_merge($done, $recorded))) . "\n");
}
