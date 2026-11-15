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
 *   capture_copies     the fast path's copies for a packet capture, bit 4 of the word offload.sh
 *                      writes with the gate (and at boot)
 *   inspect_then_hardware  the marker /var/db/os-xgs-npu/inspector-vf (the inspector's function, at
 *                      boot), the running driver's dp.ips_new or flow.ips_new, and the Intrusion
 *                      Detection's configuration through the template overlay OPNsense/IDS/xgs-inspector.yaml;
 *                      while on, the running driver's dp.ips_depth or flow.ips_depth a quarter over
 *                      Suricata's stream depth (xgs_ips_depth)
 *   failclose          the running driver's dp.ips_loop or ips.loop, through failclose.py (and from cron)
 *   service_aliases    scripts/xgs/service_aliases.php while on: the missing ready-made port aliases added
 *   ra_guard, ra_allowed  rules of the firewall hook (plugins.inc.d/xgs.inc): a firewall reload when
 *                      either changed since the last save (/var/db/os-xgs-npu/ra-guard)
 *   ids_threads        dev.netmap.generic_rings now and as a row of System: Settings: Tunables, the
 *                      running driver's dp.rx_flowid or giu.rx_flowid (and at boot), and the Intrusion
 *                      Detection restarted when it runs in IPS mode and the rings changed (xgs_ids_rings)
 *   flood_cut          lists_cut.py, which watches pf's overload tables as well while it is on
 *   inspect_pass_invalid  the same overlay (stream.drop-invalid: no), while inspect_then_hardware is on,
 *                      and the running driver's dp.netmap_gate_cut or flow.netmap_gate_cut, 1 while it
 *                      is on and 0 otherwise
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
 *   ipsec_rand_time, ipsec_ipcomp  swanctl/conf.d/xgs.conf through the template OPNsense/XGS, and
 *                      strongSwan reloaded when it changed and charon runs
 *   switch_jumbo       read by switch-jumbo.sh at the end of every boot; nothing to apply now
 *   watchdog           watchdog.sh start or stop now, and the start hook 08-xgs-watchdog at every boot
 *   flow_max           the loader tunable hw.octep.flow_max or hw.npuep.flow_max; 0 removes it
 *   tx_queues          the loader tunable hw.octep.txq or hw.npuep.giu.txq; 0 removes it
 *
 * The tunables go through OPNsense's own Tunables model, so they are visible and editable under
 * System: Settings: Tunables like any other, and loader.conf is written by the function the
 * Tunables page's apply runs (system_login_configure). A tunable already holding the wanted value
 * is not rewritten, so a save that changes nothing writes nothing. The flow maker has no tunable: a
 * row of its own, set by hand before version 1.1.0, is removed (xgs_rows_removed, below).
 */

/*
 * What net.inet.ipsec.crypto_support is written to now for ipsec_soft_crypto, or null for nothing to
 * write: $now is the sysctl as it reads (null without IPsec's module, which 07-npuep loads at boot),
 * $row the value a row of System: Settings: Tunables gives it, or null. On holds the sessions to
 * software providers (CRYPTOCAP_F_SOFTWARE, 33554432). Off gives back what a boot leaves where the
 * setting holds them - the row's number, or the kernel's own hardware or software
 * (CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SOFTWARE, 50331648) - and leaves any other value as it is, which
 * this setting did not write. Until 2026-11-06 off wrote nothing: the sessions stayed held to software
 * until the next boot, and the page said it was applied.
 */
function xgs_soft_crypto(bool $on, ?string $now, ?string $row): ?string
{
    if ($now === null) {
        return null;
    }
    if ($on) {
        return $now === '33554432' ? null : '33554432';
    }
    if ($now !== '33554432') {
        return null;
    }
    $boot = ($row !== null && preg_match('/^[0-9]+$/', $row)) ? $row : '50331648';
    return $boot === $now ? null : $boot;
}

/*
 * The rows of System: Settings: Tunables an apply removes, by uuid, each with its step: $rows is every
 * row's tunable by its uuid, $unset the sized tunables set to 0 - the driver's own choice. And the flow
 * maker's own two, dev.octep.0.dp.auto and dev.npuep.0.flow.auto, whatever they hold. They were set
 * there by hand before version 1.1.0 put the flow maker on this page, and its migration read them into
 * flow_auto and left them. OPNsense sets every row at each boot - rc.bootup, then the start hook
 * 90-sysctl - after the early hooks (07-npuep, bringup.sh) have set flow_auto's value, so a row turned
 * the flow maker back on, or off, whatever the page said. The setting is the flow maker's one place,
 * applied here and at every boot. Until 2026-11-06 nothing took the rows away (the review's G5).
 */
function xgs_rows_removed(array $rows, array $unset): array
{
    $removed = [];
    foreach ($rows as $uuid => $name) {
        if ($name === 'dev.octep.0.dp.auto' || $name === 'dev.npuep.0.flow.auto') {
            $removed[$uuid] = $name . ' removed from the tunables: this page sets the flow maker, now and at every boot';
        } elseif ($name === 'dev.netmap.generic_rings' && in_array($name, $unset, true)) {
            $removed[$uuid] = $name . ' removed: one netmap ring a port, FreeBSD\'s own';
        } elseif (in_array($name, $unset, true)) {
            $removed[$uuid] = $name . ' removed: the driver chooses';
        }
    }
    return $removed;
}

/*
 * Suricata's size string - stream.reassembly.depth as --dump-config prints it, "1mb", "512kb",
 * "1048576" - in bytes, kb, mb and gb counted in 1024s as util-misc.c's ParseSizeString counts them;
 * null for anything else.
 */
function xgs_suricata_size(?string $s): ?int
{
    if ($s === null || !preg_match('/^\s*([0-9]+)\s*(kb|mb|gb)?\s*$/i', $s, $m)) {
        return null;
    }
    $mul = ['' => 1, 'kb' => 1024, 'mb' => 1048576, 'gb' => 1073741824][strtolower($m[2] ?? '')];
    return (int)$m[1] * $mul;
}

/*
 * What the driver's inspector depth (dev.npuep.0.flow.ips_depth, dev.octep.0.dp.ips_depth) is raised
 * to while "Inspect, then hardware" is on, or null to leave it: $now as the sysctl reads, $depth
 * Suricata's stream.reassembly.depth as --dump-config prints it.
 *
 * The two count different things. Suricata's depth is TCP payload, each direction on its own,
 * counted from the connection's first sequence number. The driver's is whole frames - Ethernet, IP
 * and TCP headers with the payload - of both directions together, counted as oxips0 hands them back
 * (npuips_input, octep_ips_input). With the overlay's stream.bypass, a connection the driver cuts
 * before Suricata's stream reaches its depth is never bypassed, and its closing frames are dropped
 * when they come back (issue 1148). A full frame at MSS 1448 is 1514 bytes, 4.6 % over its payload,
 * and the acknowledgements the other way add 66 bytes every second frame, 2.3 % more; at MSS 536
 * the two are 12.3 % and 6.2 %. So the driver's depth is at least a quarter over Suricata's, which
 * covers both. The first frames, before the connection is in hardware, count for Suricata only, so
 * Suricata is ahead by them. Measured 2026-11-12: HTTP 100 MB complete with ips_depth 1 MiB and 2 MiB
 * against Suricata's 1 MiB; the first had the pre-hardware frames to thank.
 *
 * Left alone: 0, which inspects for good; a depth already at or over the mark, the owner's choice;
 * a Suricata depth of 0 (no limit: it never bypasses, so nothing to reach) or one this cannot read;
 * a sysctl that reads as no number (no driver).
 */
function xgs_ips_depth(?string $now, ?string $depth): ?string
{
    $s = xgs_suricata_size($depth);
    if ($now === null || !preg_match('/^[0-9]+$/', $now) || $now === '0' || $s === null || $s === 0) {
        return null;
    }
    $want = intdiv($s * 5 + 3, 4);
    return (int)$now >= $want ? null : (string)$want;
}

/*
 * Inspection threads per port (ids_threads, plan 01 package 3.1): the rings netmap's emulated
 * adapter gives each port Suricata reads in IPS mode, and with them Suricata's threads - it takes one
 * a ring ("threads: auto") and opens one ring per thread with no check against their number (Suricata
 * 8.0, runmode-netmap.c and source-netmap.c), so a thread count of its own that differs from the
 * rings leaves rings unread or stops it. $field is the setting as config.xml holds it. The answer: the
 * rings, as the row of System: Settings: Tunables for dev.netmap.generic_rings holds them (null: no
 * row, one ring, FreeBSD's own), and the drivers' flow-id switch (dp.rx_flowid, giu.rx_flowid), on
 * with more than one ring so that a connection's frames stay in the ring of the receive queue the
 * coprocessor chose for it. At most 8, the XGS 3300's receive rings.
 */
function xgs_ids_rings(string $field): array
{
    $f = trim($field);
    $n = preg_match('/^[0-9]+$/', $f) ? (int)$f : 0;
    if ($n <= 1) {
        return ['rings' => null, 'flowid' => '0'];
    }
    return ['rings' => (string)min($n, 8), 'flowid' => '1'];
}

// The tests include this file for the functions above alone.
if (getenv('XGS_APPLY_FUNCTIONS_ONLY') === '1') {
    return;
}

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
$want = $get('offload', '1') === '1' ? 'on' : 'off';
$gate = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/offload.sh ' . $want . ' 2>/dev/null'), true);
if (!is_array($gate) || empty($gate['applied'])) {
    $ok = false;
    $steps[] = 'offloading ' . $want . ': not written (' . ($gate['message'] ?? 'no answer') . ')';
} else {
    $steps[] = 'offloading ' . $want;
    /* written in the same word, by the same call (discoveries 12 and 17) */
    $steps[] = 'copies for packet capture ' . ($get('capture_copies', '0') === '1' ? 'on' : 'off');
}

/* The flow maker, on the running driver. */
$auto_oid = $family === 'octeontx' ? 'dev.octep.0.dp.auto' : 'dev.npuep.0.flow.auto';
$auto = $get('flow_auto', '1') === '1' ? '1' : '0';
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
$inspect = $get('inspect_then_hardware', '1') === '1';
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
/*
 * Inspection threads per port (xgs_ids_rings, above): the rings now, and the drivers' flow id. A
 * port's emulated adapter is made with the rings when Suricata opens it, so Suricata is restarted
 * below with the overlay's restart when the rings changed; the row of System: Settings: Tunables,
 * written with the others further down, gives them at every boot, before the Intrusion Detection
 * starts.
 */
$rings = xgs_ids_rings($get('ids_threads', ''));
$rings_want = $rings['rings'] ?? '1';
$rings_now = sysctl_get('dev.netmap.generic_rings');
$rings_changed = false;
if ($rings_now !== null && $rings_now !== $rings_want) {
    if (sysctl_set('dev.netmap.generic_rings', $rings_want)) {
        $rings_changed = true;
        $steps[] = 'inspection threads per port: ' . $rings_want . ' netmap ring' . ($rings_want === '1' ? '' : 's') .
            ' a port, a Suricata thread for each';
    } else {
        $ok = false;
        $steps[] = 'inspection threads per port: dev.netmap.generic_rings refused ' . $rings_want;
    }
}
$flowid_oid = $family === 'octeontx' ? 'dev.octep.0.dp.rx_flowid' : 'dev.npuep.0.giu.rx_flowid';
$flowid_now = sysctl_get($flowid_oid);
if ($flowid_now !== null && $flowid_now !== $rings['flowid'] && !sysctl_set($flowid_oid, $rings['flowid'])) {
    $ok = false;
    $steps[] = 'inspection threads per port: the driver refused ' . $flowid_oid . '=' . $rings['flowid'];
}
$before = @file_get_contents($overlay);
$backend = new \OPNsense\Core\Backend();
$backend->configdRun('template reload OPNsense/IDS');
if ($before === false) {
    /* suricata.yaml includes what conf.d held when it was written, and it is written before this. */
    $backend->configdRun('template reload OPNsense/IDS');
}
$after = @file_get_contents($overlay);
if ($ids_on && (($after !== false && $after !== $before) || ($rings_changed && $ids_ips))) {
    $backend->configdRun('ids restart');
    $steps[] = 'the Intrusion Detection restarted to read ' . (strpos((string)$after, 'oxips0') !== false ? 'oxips0 as well' : 'only its own interfaces') .
        ($rings_changed && $ids_ips ? ', ' . $rings_want . ' ring' . ($rings_want === '1' ? '' : 's') . ' a port' : '');
}
/*
 * A front port the Intrusion Detection inspects in IPS mode at an MTU its netmap adapter cuts
 * (ids_mtu.py: emulated, 2,030; native on the XGS 136, netmap's buffer less 18). Suricata on such
 * a port cuts every larger frame, or exits without a word (measured on an XGS 136, 2026-11-11), and
 * nothing in OPNsense says so - said here, in the log, and on XGS: Overview.
 */
exec('/usr/local/opnsense/scripts/xgs/ids_mtu.py 2>/dev/null', $mtu_over);
foreach ($mtu_over as $line) {
    if (trim($line) === '') {
        continue;
    }
    syslog(LOG_WARNING, 'xgs: ' . $line);
    $steps[] = $line;
}

/*
 * The inspector's depth against Suricata's own (xgs_ips_depth, above): only while the overlay has
 * Suricata read oxips0 with stream.bypass, for the connections made from now on. The running driver
 * only - it starts at its own 1 MiB at every boot, until the next save.
 */
if ($inspect && $have_if && $ids_ips && $after !== false && strpos($after, 'stream.bypass') !== false) {
    $depth_oid = str_replace('ips_new', 'ips_depth', $new_oid);
    $dump = [];
    exec('/usr/local/bin/suricata --dump-config -c /usr/local/etc/suricata/suricata.yaml 2>/dev/null', $dump);
    $sdepth = null;
    foreach ($dump as $line) {
        if (strpos($line, 'stream.reassembly.depth = ') === 0) {
            $sdepth = trim(substr($line, strlen('stream.reassembly.depth = ')));
        }
    }
    $raise = xgs_ips_depth(sysctl_get($depth_oid), $sdepth);
    if ($raise !== null) {
        if (sysctl_set($depth_oid, $raise)) {
            $steps[] = 'inspect, then hardware: ' . $depth_oid . ' ' . $raise . ', a quarter over Suricata\'s stream depth (' .
                $sdepth . '), so Suricata reaches its depth and stops following a connection before it is put in hardware';
        } else {
            $ok = false;
            $steps[] = 'inspect, then hardware: the driver refused ' . $depth_oid . '=' . $raise;
        }
    }
}

/*
 * The cut on a port in netmap mode (flow.netmap_gate_cut, dp.netmap_gate_cut): an inspected
 * connection that a port Suricata reads in IPS mode carries is put in hardware after the inspected
 * part only while Suricata lets the segments its TCP state machine rejects through - the overlay's
 * stream.drop-invalid: no, "Let rejected TCP segments through". Otherwise its last frames come back
 * through that Suricata past what it saw, are dropped, and the connection hangs (item 16, measured
 * 2026-11-12 on the XGS 136); stream.bypass alone saves only the streams whose protocol Suricata
 * identifies. 0, the drivers' default, keeps such a connection inspected for its whole life. The
 * running driver only, as the depth above: it starts at 0 at every boot, until the next save.
 */
$gate_cut_oid = str_replace('ips_new', 'netmap_gate_cut', $new_oid);
$pass_invalid = $inspect && $get('inspect_pass_invalid', '0') === '1';
$gate_cut = ($pass_invalid && $after !== false && strpos($after, 'stream.drop-invalid: no') !== false) ? '1' : '0';
if (sysctl_get($gate_cut_oid) === null) {
    if ($pass_invalid) {
        $steps[] = 'let rejected TCP segments through: this driver has no ' . $gate_cut_oid . ', and puts every inspected connection in hardware after the inspected part, as before';
    }
} elseif (sysctl_set($gate_cut_oid, $gate_cut)) {
    if ($inspect) {
        $steps[] = $gate_cut === '1'
            ? 'let rejected TCP segments through on: an inspected connection on a port Suricata reads in IPS mode is put in hardware after the inspected part as well'
            : 'an inspected connection on a port Suricata reads in IPS mode stays inspected for its whole life (' .
              ($pass_invalid ? 'the Intrusion Detection\'s configuration does not let rejected TCP segments through' : '"Let rejected TCP segments through" is off') . ')';
    }
} else {
    $ok = false;
    $steps[] = 'let rejected TCP segments through: the driver refused ' . $gate_cut_oid . '=' . $gate_cut;
}

/*
 * Drop when the inspection stops (failclose.py): the inspector's knob, dp.ips_loop or ips.loop, 0 only
 * while the setting and "Inspect, then hardware" are on and the Intrusion Detection is on in IPS mode,
 * 1 otherwise; cron keeps it so every two seconds, as the Intrusion Detection is turned on or off.
 */
$fc = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/failclose.py 2>/dev/null'), true);
if (!is_array($fc)) {
    $steps[] = 'drop when the inspection stops: no answer from failclose.py';
} else {
    if (empty($fc['applied'])) {
        $ok = false;
    }
    $steps[] = $fc['message'];
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
$steps[] = 'watching for unusual traffic ' . ($get('anomaly_watch', '1') === '1' ? 'on, every minute' : 'off');

/* The TLS fingerprints whose connections are cut - ja4_cut.py, also every two seconds from cron. */
$ja4 = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/ja4_cut.py 2>/dev/null'), true);
$steps[] = is_array($ja4) && isset($ja4['message']) ? $ja4['message'] : 'cut by TLS fingerprint: no answer from ja4_cut.py';

/* The Captive Portal's clients as live users - cp_users.py, also every two seconds from cron. Before
   the list below: it decides which zones run in hardware, which that list no longer keeps. */
$cp = json_decode((string)shell_exec('/usr/local/opnsense/scripts/xgs/cp_users.py 2>/dev/null'), true);
$steps[] = is_array($cp) && isset($cp['message']) ? $cp['message'] : 'Captive Portal clients in hardware: no answer from cp_users.py';
if ($get('cp_hardware', '1') === '1') {
    /* watched from now, not from cron's next minute: a login and a logout can both fall before it */
    exec('/usr/sbin/daemon -f /usr/local/opnsense/scripts/xgs/cp_users.py --for 58');
}

/*
 * Router advertisements only from allowed senders: the firewall hook's rules, so a firewall reload -
 * which also empties the coprocessor's hardware, as every reload does - when the two settings differ
 * from what the last save left, and not otherwise.
 */
$ra_now = trim($get('ra_guard', '')) . '|' . preg_replace('/\s+/', ' ', trim($get('ra_allowed', '')));
$ra_mark = '/var/db/os-xgs-npu/ra-guard';
$ra_was = @file_get_contents($ra_mark);
if (($ra_was === false ? '|' : $ra_was) !== $ra_now) {
    @mkdir(dirname($ra_mark), 0755, true);
    @file_put_contents($ra_mark, $ra_now);
    (new \OPNsense\Core\Backend())->configdRun('filter reload');
    $steps[] = 'router advertisements: ' . ($get('ra_guard', '') === '' ? 'from anyone, as OPNsense has it' :
        'only from the allowed senders on ' . str_replace(',', ', ', $get('ra_guard', ''))) . ' - the firewall reloaded';
}

/*
 * IPsec's additions to swanctl.conf (OPNsense/XGS/xgs-swanctl.conf): written now, and strongSwan reloaded
 * when they changed and it runs. Compression is not written while IPsec is in the coprocessor's engine.
 */
$swan = '/usr/local/etc/swanctl/conf.d/xgs.conf';
$swan_before = @file_get_contents($swan);
(new \OPNsense\Core\Backend())->configdRun('template reload OPNsense/XGS');
$swan_after = @file_get_contents($swan);
if ($swan_after !== $swan_before) {
    $running = trim((string)shell_exec('/bin/pgrep -x charon 2>/dev/null')) !== '';
    if ($running) {
        shell_exec('/usr/local/etc/rc.d/strongswan onereload > /dev/null 2>&1');
    }
    $steps[] = 'IPsec: ' . ($get('ipsec_rand_time', '') === '' ? 'rekey times as strongSwan has them' :
        'up to ' . $get('ipsec_rand_time', '') . ' s taken off rekey times at random') .
        ($get('ipsec_ipcomp', '') === '' ? '' : ($get('ipsec_offload', '1') === '1' ?
        '; compression not written while IPsec is in the coprocessor\'s engine' : '; compression on the tunnels chosen')) .
        ($running ? ' - strongSwan reloaded' : '');
}

/* Ready-made service aliases, while on: those missing added to Firewall: Aliases, none changed. */
if ($get('service_aliases', '0') === '1') {
    $sa = json_decode((string)shell_exec('/usr/local/bin/php /usr/local/opnsense/scripts/xgs/service_aliases.php 2>/dev/null'), true);
    if (!is_array($sa)) {
        $ok = false;
        $steps[] = 'ready-made service aliases: no answer from service_aliases.php';
    } elseif (!empty($sa['refused'])) {
        $ok = false;
        $steps[] = 'ready-made service aliases: refused - ' . implode('; ', $sa['refused']);
    } else {
        $steps[] = 'ready-made service aliases: ' . (empty($sa['added']) ? 'all there' :
            'added ' . implode(', ', $sa['added'])) . (empty($sa['kept']) ? '' : '; kept as they were: ' . implode(', ', $sa['kept']));
    }
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
    if (($excl['tag'] ?? null) !== null) {
        $steps[] = 'never offloaded by a rule\'s tag: ' . ($excl['tag'] === '' ? 'none' : $excl['tag']) .
            (($excl['tag_message'] ?? '') !== '' ? ' - ' . $excl['tag_message'] : '');
        if (strpos((string)($excl['tag_message'] ?? ''), 'refused') !== false) {
            $ok = false;
        }
    }
    if (count($excl['refused'])) {
        $ok = false;
    }
}

/* The tunables this family reads at attach. */
$tunables = [];
if ($family === 'octeontx') {
    $tunables['hw.octep.ipsec_on'] = $get('ipsec_offload', '1') === '1' ? '1' : '0';
    $tunables['hw.octep.ipsec_flows'] = $get('ipsec_flows', '1') === '1' ? '1' : '0';
} else {
    $tunables['hw.npuep.ipsec_on'] = $get('ipsec_offload', '1') === '1' ? '1' : '0';
    $tunables['hw.npuep.ipsec_flows'] = $get('ipsec_flows', '1') === '1' ? '1' : '0';
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
/* Inspection threads per port: the rings at every boot, or no row for one (xgs_ids_rings). */
if ($rings['rings'] !== null) {
    $tunables['dev.netmap.generic_rings'] = $rings['rings'];
} else {
    $unset[] = 'dev.netmap.generic_rings';
}
$model = new \OPNsense\Core\Tunables();
$changed = false;
/* those set to 0, and the flow maker's own (xgs_rows_removed, above) */
$rows = [];
foreach ($model->item->iterateItems() as $uuid => $item) {
    $rows[(string)$uuid] = (string)$item->tunable;
}
foreach (xgs_rows_removed($rows, $unset) as $uuid => $step) {
    $model->item->del((string)$uuid);
    $changed = true;
    $steps[] = $step;
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
    /* IPsec's software sessions: now for the sessions made from here on, and at every boot - on and
       off (xgs_soft_crypto, above). */
    $row = null;
    foreach ($model->item->iterateItems() as $item) {
        if ((string)$item->tunable === 'net.inet.ipsec.crypto_support') {
            $row = trim((string)$item->value);
        }
    }
    $crypto = xgs_soft_crypto($get('ipsec_soft_crypto', '1') === '1',
        sysctl_get('net.inet.ipsec.crypto_support'), $row);
    if ($crypto !== null) {
        if (sysctl_set('net.inet.ipsec.crypto_support', $crypto)) {
            $steps[] = $crypto === '33554432' ? "IPsec's software sessions held to the CPU's AES instructions" :
                "IPsec's software sessions no longer held to the CPU's AES instructions";
        } else {
            $ok = false;
            $steps[] = 'the kernel refused net.inet.ipsec.crypto_support=' . $crypto;
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
