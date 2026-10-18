<?php
/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * Apply the coprocessor's settings page: each setting where it takes effect, and a JSON report.
 *
 *   apply.php            apply what config.xml holds, print {"applied", "reboot", "message", "steps"}
 *
 * WHERE EACH ONE GOES.
 *   offload            the coprocessor, through offload.sh (it forgets the gate at every boot)
 *   flow_auto          the running driver: dev.octep.0.dp.auto or dev.npuep.0.flow.auto
 *   ipsec_offload      a loader tunable, hw.octep.ipsec_on or hw.npuep.ipsec_on - the driver reads it
 *                      when it attaches, before strongSwan installs a tunnel, so a reboot takes it
 *   ipsec_flows        the loader tunable hw.octep.ipsec_flows, 0 or 1 (never 2), XGS 3300 only
 *   rx_poll_ms         the running driver and the loader tunable hw.npuep.giu.poll_ms, XGS 136 only
 *   rx_budget          the same for hw.npuep.giu.rx_budget
 *   ipsec_soft_crypto  net.inet.ipsec.crypto_support now, and 07-npuep at every boot, XGS 136 only
 *   switch_jumbo       read by switch-jumbo.sh at the end of every boot; nothing to apply now
 *   watchdog           watchdog.sh start or stop now, and the start hook 08-xgs-watchdog at every boot
 *   flow_max           the loader tunable hw.octep.flow_max or hw.npuep.flow_max; 0 removes it
 *   tx_queues          the loader tunable hw.octep.txq or hw.npuep.giu.txq; 0 removes it
 *
 * The tunables go through OPNsense's own Tunables model, so they are visible and editable under
 * System: Settings: Tunables like any other, and loader.conf is written by the function the
 * Tunables page's apply runs (system_login_configure). A tunable already holding the wanted value
 * is not rewritten, so a save that changes nothing writes nothing.
 */

require_once 'config.inc';
require_once 'auth.inc';
require_once 'util.inc';
require_once 'system.inc';

$cfg = \OPNsense\Core\Config::getInstance();
$c = $cfg->object();
$g = $c->OPNsense->XGS->general ?? null;
$get = function ($name, $default) use ($g) {
    return ($g !== null && isset($g->$name) && (string)$g->$name !== '') ? (string)$g->$name : $default;
};
$board = trim((string)shell_exec('/bin/sh /usr/local/opnsense/scripts/xgs/board.sh 2>/dev/null'));
$family = explode(' ', $board)[1] ?? '';

$steps = [];
$ok = true;
$reboot = false;

function sysctl_get($oid)
{
    $out = [];
    exec('/sbin/sysctl -n ' . escapeshellarg($oid) . ' 2>/dev/null', $out, $rc);
    return $rc === 0 ? trim(implode("\n", $out)) : null;
}
function sysctl_set($oid, $value)
{
    exec('/sbin/sysctl ' . escapeshellarg($oid . '=' . $value) . ' 2>/dev/null', $out, $rc);
    return $rc === 0;
}

if ($family !== 'octeontx' && $family !== 'armada') {
    echo json_encode(['applied' => false, 'reboot' => false, 'steps' => [],
        'message' => 'not an XGS appliance this project knows - nothing applied']) . "\n";
    exit(0);
}

/* The offload gate, as it always was. */
$want = $get('offload', '0') === '1' ? 'on' : 'off';
$gate = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/offload.sh ' . $want . ' 2>/dev/null'), true);
if (!is_array($gate) || empty($gate['applied'])) {
    $ok = false;
    $steps[] = 'offloading ' . $want . ': not written (' . ($gate['message'] ?? 'no answer') . ')';
} else {
    $steps[] = 'offloading ' . $want;
}

/* The flow maker, on the running driver. */
$auto_oid = $family === 'octeontx' ? 'dev.octep.0.dp.auto' : 'dev.npuep.0.flow.auto';
$auto = $get('flow_auto', '0') === '1' ? '1' : '0';
if (sysctl_get($auto_oid) === null) {
    $steps[] = 'automatic acceleration: the driver is not loaded, applied at the next boot';
} elseif (sysctl_set($auto_oid, $auto)) {
    $steps[] = 'automatic acceleration ' . ($auto === '1' ? 'on' : 'off');
} else {
    $ok = false;
    $steps[] = 'automatic acceleration: the driver refused ' . $auto_oid . '=' . $auto;
}

/* What is never offloaded, on the running driver - flow_exclude.php reads and writes it. */
$excl = json_decode((string)shell_exec('/usr/local/bin/php /usr/local/opnsense/scripts/xgs/flow_exclude.php 2>/dev/null'), true);
if (!is_array($excl)) {
    $steps[] = 'never offloaded: no answer from flow_exclude.php';
} elseif ($excl['oid'] === null) {
    $steps[] = 'never offloaded: the driver is not loaded, applied at the next boot';
} elseif (empty($excl['applied'])) {
    $ok = false;
    $steps[] = 'never offloaded: ' . $excl['message'];
} else {
    $steps[] = 'never offloaded: ' . (count($excl['entries']) ? implode(', ', $excl['entries']) : 'nothing') .
        (count($excl['refused']) ? ' - not read, so not applied: ' . implode(', ', $excl['refused']) : '');
    if (count($excl['refused'])) {
        $ok = false;
    }
}

/* The tunables this family reads at attach. */
$tunables = [];
if ($family === 'octeontx') {
    $tunables['hw.octep.ipsec_on'] = $get('ipsec_offload', '0') === '1' ? '1' : '0';
    $tunables['hw.octep.ipsec_flows'] = $get('ipsec_flows', '0') === '1' ? '1' : '0';
} else {
    $tunables['hw.npuep.ipsec_on'] = $get('ipsec_offload', '0') === '1' ? '1' : '0';
    $tunables['hw.npuep.giu.poll_ms'] = (string)max(1, min(100, (int)$get('rx_poll_ms', '1')));
    $tunables['hw.npuep.giu.rx_budget'] = (string)max(4, min(4096, (int)$get('rx_budget', '1024')));
}
/*
 * The flow table's size and the transmit queues: a number is written, 0 removes the tunable - the
 * driver's own choice - because the driver reads a 0 as a request for its minimum.
 */
$sized = [
    ($family === 'octeontx' ? 'hw.octep.flow_max' : 'hw.npuep.flow_max') => (int)$get('flow_max', '0'),
    ($family === 'octeontx' ? 'hw.octep.txq' : 'hw.npuep.giu.txq') => (int)$get('tx_queues', '0'),
];
$unset = [];
foreach ($sized as $name => $value) {
    if ($value > 0) {
        $tunables[$name] = (string)$value;
    } else {
        $unset[] = $name;
    }
}
$model = new \OPNsense\Core\Tunables();
$changed = false;
foreach ($unset as $name) {
    foreach ($model->item->iterateItems() as $uuid => $item) {
        if ((string)$item->tunable === $name) {
            $model->item->del($uuid);
            $changed = true;
            $steps[] = $name . ' removed: the driver chooses';
        }
    }
}
foreach ($tunables as $name => $value) {
    $found = null;
    foreach ($model->item->iterateItems() as $item) {
        if ((string)$item->tunable === $name) {
            $found = $item;
        }
    }
    if ($found === null) {
        $found = $model->item->Add();
        $found->tunable = $name;
        $found->descr = 'XGS coprocessor - set on its settings page';
        $found->value = $value;
        $changed = true;
    } elseif ((string)$found->value !== $value) {
        $found->value = $value;
        $changed = true;
    }
}
if ($changed) {
    $bad = false;
    foreach ($model->performValidation() as $msg) {
        $steps[] = 'tunable refused: ' . $msg->getMessage();
        $bad = true;
    }
    if ($bad) {
        $ok = false;
    } else {
        $model->serializeToConfig();
        $cfg->save();
        /* system_login_configure() writes loader.conf from the legacy $config array, which
           config.inc filled when this script started; without this it writes the rows as they
           were before the save, and the new tunable never reaches loader.conf - measured. */
        $config = $cfg->toArray(listtags());
        system_login_configure();
        $steps[] = 'loader tunables written: ' . implode(', ', array_map(function ($k) use ($tunables) {
            return $k . '=' . $tunables[$k];
        }, array_keys($tunables)));
    }
}
/* What the running driver was started with, against what the next boot will give it. */
foreach (['hw.octep.ipsec_on', 'hw.octep.ipsec_flows', 'hw.npuep.ipsec_on'] as $name) {
    if (!isset($tunables[$name])) {
        continue;
    }
    $now = trim((string)shell_exec('/bin/kenv -q ' . escapeshellarg($name) . ' 2>/dev/null'));
    if (($now === '' ? '0' : $now) !== $tunables[$name]) {
        $reboot = true;
        $steps[] = $name . ' becomes ' . $tunables[$name] . ' at the next reboot';
    }
}
foreach (array_keys($sized) as $name) {
    $now = trim((string)shell_exec('/bin/kenv -q ' . escapeshellarg($name) . ' 2>/dev/null'));
    $next = $tunables[$name] ?? '';
    if ($now !== $next) {
        $reboot = true;
        $steps[] = $name . ' becomes ' . ($next === '' ? "the driver's own choice" : $next) . ' at the next reboot';
    }
}

if ($family === 'armada') {
    /* The receive poll takes effect at once on the running driver as well. */
    foreach (['poll_ms' => 'hw.npuep.giu.poll_ms', 'rx_budget' => 'hw.npuep.giu.rx_budget'] as $leaf => $name) {
        $oid = 'dev.npuep.0.giu.' . $leaf;
        if (sysctl_get($oid) !== null && sysctl_get($oid) !== $tunables[$name]) {
            if (sysctl_set($oid, $tunables[$name])) {
                $steps[] = $oid . ' = ' . $tunables[$name];
            } else {
                $ok = false;
                $steps[] = 'the driver refused ' . $oid . '=' . $tunables[$name];
            }
        }
    }
    /* IPsec's software sessions: now for the sessions made from here on, and at every boot. */
    if ($get('ipsec_soft_crypto', '1') === '1' && sysctl_get('net.inet.ipsec.crypto_support') !== null &&
        sysctl_get('net.inet.ipsec.crypto_support') !== '33554432') {
        if (sysctl_set('net.inet.ipsec.crypto_support', '33554432')) {
            $steps[] = "IPsec's software sessions held to the CPU's AES instructions";
        }
    }
}

/* The host's hardware watchdog follows its setting now, not only at the next boot. */
$dog = trim((string)shell_exec('/bin/sh /usr/local/opnsense/scripts/xgs/watchdog.sh ' .
    ($get('watchdog', '1') === '1' ? 'start' : 'stop') . ' 2>/dev/null'));
if ($dog !== '') {
    $steps[] = 'hardware watchdog: ' . $dog;
}

echo json_encode([
    'applied' => $ok,
    'reboot' => $reboot,
    'steps' => $steps,
    'message' => implode('; ', $steps) . ($reboot ? ' - reboot to finish.' : '.'),
]) . "\n";
