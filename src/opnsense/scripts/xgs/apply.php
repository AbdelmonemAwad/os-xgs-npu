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
 *   cut_in_hardware    the running driver's dp.mflow_action or flow.mflow_action (and at boot), and
 *                      the fast path's IPS switch, which offload.sh writes with the gate
 *   inspect_then_hardware  the marker /var/db/os-xgs-npu/inspector-vf (the inspector's function, at
 *                      boot), the running driver's dp.ips_new or flow.ips_new, and the Intrusion
 *                      Detection's configuration through the template overlay OPNsense/IDS/xgs-inspector.yaml
 *   count_to_pf        the running driver, dev.octep.0.dp.hw_to_pf or dev.npuep.0.flow.hw_to_pf
 *                      (and at boot)
 *   count_ports        the running driver, dev.octep.0.dp.lif_apply or dev.npuep.0.flow.lif_apply
 *                      (and at boot)
 *   ipsec_offload      a loader tunable, hw.octep.ipsec_on or hw.npuep.ipsec_on - the driver reads it
 *                      when it attaches, before strongSwan installs a tunnel, so a reboot takes it
 *   ipsec_flows        the loader tunable hw.octep.ipsec_flows (never 2) or hw.npuep.ipsec_flows, 0 or 1
 *   rx_poll_ms         the running driver and the loader tunable hw.npuep.giu.poll_ms, XGS 136 only
 *   rx_budget          the same for hw.npuep.giu.rx_budget
 *   rx_doorbells       the loader tunable hw.npuep.giu.rx_msix, read when the receive queues are
 *                      added, so a reboot takes it; XGS 136 only
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

/*
 * Cut in hardware: the action plain connections are made with from now on - IPS, so that a cut can
 * drop one on the coprocessor by its verdict, or FWD as before 2026-10-23. offload.sh, above, has
 * written the fast path's IPS switch with the gate. Connections already in hardware keep theirs.
 */
$act_oid = $family === 'octeontx' ? 'dev.octep.0.dp.mflow_action' : 'dev.npuep.0.flow.mflow_action';
$cut_hw = $get('cut_in_hardware', '1') === '0' ? '0' : '1';
if (sysctl_get($act_oid) === null) {
    $steps[] = 'cut in hardware: this driver cannot yet, applied when it can';
} elseif (sysctl_set($act_oid, $cut_hw === '1' ? '2' : '1')) {
    $steps[] = 'cut in hardware ' . ($cut_hw === '1' ? 'on' : 'off') . ', for connections made from now on';
} else {
    $ok = false;
    $steps[] = 'cut in hardware: the driver refused ' . $act_oid;
}

/*
 * Inspect, then hardware (discovery 4). The marker has the driver make the coprocessor's inspector
 * function at boot, and oxips0 with it; while oxips0 is here, new connections are made inspected
 * (ips_new) and /var/run/os-xgs-npu.oxips0 says so to the Intrusion Detection's template overlay,
 * which adds oxips0 to what Suricata reads (OPNsense/IDS/xgs-inspector.yaml). The Intrusion
 * Detection's configuration is written again for it, and Suricata restarted when it runs and what it
 * reads has changed. The boot hooks do the same at every boot.
 */
$marker = '/var/db/os-xgs-npu/inspector-vf';
$present = '/var/run/os-xgs-npu.oxips0';
$overlay = '/usr/local/etc/suricata/conf.d/xgs-inspector.yaml';
$inspect = $get('inspect_then_hardware', '0') === '1';
exec('/sbin/ifconfig oxips0 > /dev/null 2>&1', $ifout, $ifrc);
$have_if = $ifrc === 0;
if ($inspect) {
    @mkdir(dirname($marker), 0755, true);
    @touch($marker);
} else {
    @unlink($marker);
}
if ($have_if) {
    @touch($present);
} else {
    @unlink($present);
}
$new_oid = $family === 'octeontx' ? 'dev.octep.0.dp.ips_new' : 'dev.npuep.0.flow.ips_new';
$ids = $c->OPNsense->IDS->general ?? null;
$ids_on = $ids !== null && (string)$ids->enabled === '1';
$ids_ips = $ids !== null && (string)$ids->mode === 'netmap';
if ($inspect && !$have_if) {
    $reboot = true;
    $steps[] = "inspect, then hardware: the coprocessor's inspector function is made at the next reboot";
} elseif ($inspect) {
    exec('/sbin/ifconfig oxips0 up > /dev/null 2>&1');
    if (sysctl_set($new_oid, '1')) {
        $steps[] = 'inspect, then hardware on: new connections go to the Intrusion Detection through oxips0 until it has let ' .
            round((int)sysctl_get(str_replace('ips_new', 'ips_depth', $new_oid)) / 1048576, 1) . ' MiB of each through';
    } else {
        $ok = false;
        $steps[] = 'inspect, then hardware: the driver refused ' . $new_oid;
    }
    if ($cut_hw !== '1') {
        $steps[] = 'inspect, then hardware: needs "Cut in hardware" on - without it the coprocessor never asks a connection\'s verdict, and connections forward as before';
    }
    if (!$ids_on || !$ids_ips) {
        $steps[] = 'inspect, then hardware: the Intrusion Detection is not on in IPS mode, so nothing reads oxips0 and connections forward as before';
    }
} else {
    if (sysctl_get($new_oid) !== null) {
        sysctl_set($new_oid, '0');
    }
    $steps[] = 'inspect, then hardware off' . ($have_if ? "; the coprocessor's inspector function stays until the next reboot" : '');
}
$before = @file_get_contents($overlay);
$backend = new \OPNsense\Core\Backend();
$backend->configdRun('template reload OPNsense/IDS');
if ($before === false) {
    /* suricata.yaml includes what conf.d held when it was written, and it is written before this. */
    $backend->configdRun('template reload OPNsense/IDS');
}
$after = @file_get_contents($overlay);
if ($after !== false && $after !== $before && $ids_on) {
    $backend->configdRun('ids restart');
    $steps[] = 'the Intrusion Detection restarted to read ' . (strpos($after, 'oxips0') !== false ? 'oxips0 as well' : 'only its own interfaces');
}

/* The coprocessor's counts, to pf and to the interfaces: both drivers read them. */
$cnt = $family === 'octeontx' ? 'dev.octep.0.dp.' : 'dev.npuep.0.flow.';
foreach (['count_to_pf' => [$cnt . 'hw_to_pf', 'hardware traffic counted in pf'],
          'count_ports' => [$cnt . 'lif_apply', 'hardware traffic counted on the interfaces']] as $name => $w) {
    $v = $get($name, '1') === '0' ? '0' : '1';
    if (sysctl_get($w[0]) === null) {
        $steps[] = $w[1] . ': this driver does not read the counts yet, applied at the next boot';
    } elseif (sysctl_set($w[0], $v)) {
        $steps[] = $w[1] . ($v === '1' ? ' on' : ' off');
    } else {
        $ok = false;
        $steps[] = $w[1] . ': the driver refused ' . $w[0] . '=' . $v;
    }
}

/* The block lists whose additions cut open connections - lists_cut.py, also every minute from cron. */
$cut = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/lists_cut.py 2>/dev/null'), true);
$steps[] = is_array($cut) && isset($cut['message']) ? $cut['message'] : 'cut when listed: no answer from lists_cut.py';

/* Unusual traffic, reported and never blocked - anomaly.py, every minute from cron while on. */
$steps[] = 'watching for unusual traffic ' . ($get('anomaly_watch', '0') === '1' ? 'on, every minute' : 'off');

/* The TLS fingerprints whose connections are cut - ja4_cut.py, also every two seconds from cron. */
$ja4 = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/ja4_cut.py 2>/dev/null'), true);
$steps[] = is_array($ja4) && isset($ja4['message']) ? $ja4['message'] : 'cut by TLS fingerprint: no answer from ja4_cut.py';

/* The Captive Portal's clients as live users - cp_users.py, also every two seconds from cron. Before
   the list below: it decides which zones run in hardware, which that list no longer keeps. */
$cp = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/cp_users.py 2>/dev/null'), true);
$steps[] = is_array($cp) && isset($cp['message']) ? $cp['message'] : 'Captive Portal clients in hardware: no answer from cp_users.py';
if ($get('cp_hardware', '0') === '1') {
    /* watched from now, not from cron's next minute: a login and a logout can both fall before it */
    exec('/usr/sbin/daemon -f /usr/local/opnsense/scripts/xgs/cp_users.py --for 58');
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
    $tunables['hw.npuep.ipsec_flows'] = $get('ipsec_flows', '0') === '1' ? '1' : '0';
    $tunables['hw.npuep.giu.poll_ms'] = (string)max(1, min(100, (int)$get('rx_poll_ms', '1')));
    $tunables['hw.npuep.giu.rx_budget'] = (string)max(4, min(4096, (int)$get('rx_budget', '1024')));
    $tunables['hw.npuep.giu.rx_msix'] = $get('rx_doorbells', '1') === '1' ? '1' : '0';
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
foreach (['hw.octep.ipsec_on', 'hw.octep.ipsec_flows', 'hw.npuep.ipsec_on', 'hw.npuep.ipsec_flows',
          'hw.npuep.giu.rx_msix'] as $name) {
    if (!isset($tunables[$name])) {
        continue;
    }
    $now = trim((string)shell_exec('/bin/kenv -q ' . escapeshellarg($name) . ' 2>/dev/null'));
    /* No tunable at the boot is the driver's own default: off, but for the doorbells, on since
       2026-10-20. */
    if (($now === '' ? ($name === 'hw.npuep.giu.rx_msix' ? '1' : '0') : $now) !== $tunables[$name]) {
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
