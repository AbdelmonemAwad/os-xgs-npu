<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * What the owner keeps with the host: the addresses, networks and ports XGS: Settings lists under
 * "Never offloaded", written to the running driver - dev.octep.0.dp.flow_exclude on the XGS 3300,
 * dev.npuep.0.flow.exclude on the XGS 136 - in the canonical form both drivers parse.
 *
 *   flow_exclude.php            apply what config.xml holds; print {"applied", "entries", "refused",
 *                               "oid", "message"} as JSON
 *   flow_exclude.php <list>     the same for a list given here instead of config.xml
 *
 * WHY THE TRANSLATION IS HERE. The kernel parses nothing but digits: "a4:" and the eight hex digits
 * of an IPv4 network in host order, "/" and its length; "a6:" and the thirty-two of an IPv6 one; "p:"
 * and a port. Whatever the owner typed - an address, a network, "port 22" - is read and validated
 * here, where PHP's own parsers are, and an entry that cannot be read is named rather than guessed
 * at. The others are applied.
 *
 * Run by apply.php when the settings are saved, and at every boot by the driver's own hook once the
 * driver is up, because the driver forgets it: it starts with no exclusion whatever the page says.
 * And every minute from the plugin's cron with --if-changed, which writes only when the list it
 * makes differs from the one it wrote last: the Traffic Shaper and the Captive Portal are changed
 * on their own pages, which call nothing of this plugin. Never fails the caller.
 *
 * WHAT IPFW DECIDES (2026-10-20). OPNsense's Traffic Shaper and Captive Portal decide in ipfw, and
 * ipfw sees a packet only while the host forwards it: a connection the coprocessor forwards itself
 * escapes a shaper's pipe, and a portal's client that logs out keeps what it had open. So what an
 * enabled shaper rule or portal zone names is kept with the host as if the owner had listed it: a
 * rule's source and destination networks - or, when it names none, its single ports - and a zone's
 * interfaces' networks. A rule that names nothing narrower than an interface, a port range, an
 * address this cannot read, or a zone on an interface without a fixed network keeps everything
 * with the host, 0.0.0.0/0 and ::/0: offloading then forwards nothing until that rule or zone goes.
 * A portal zone XGS: Settings runs in hardware (2026-10-24, cp_users.py) is not kept: its clients
 * are the coprocessor's live users, and a logout takes their connections out of hardware at once.
 */

/* The networks of one OPNsense interface, as config.xml has them fixed; null when it has none fixed. */
function interface_networks(array $config, string $if): ?array
{
    $i = $config['interfaces'][$if] ?? null;
    if (!is_array($i)) {
        return null;
    }
    $nets = [];
    foreach ([['ipaddr', 'subnet'], ['ipaddrv6', 'subnetv6']] as $pair) {
        $a = (string)($i[$pair[0]] ?? '');
        if ($a === '') {
            continue;
        }
        if (@inet_pton($a) === false || !ctype_digit((string)($i[$pair[1]] ?? ''))) {
            return null;        /* dhcp, track6, a pppoe link: nothing fixed to name */
        }
        $nets[] = $a . '/' . $i[$pair[1]];
    }
    return $nets ? $nets : null;
}

/* Is this a network or an address PHP can read, as canonical() will? */
function readable_net(string $item): bool
{
    $parts = explode('/', $item, 2);
    return @inet_pton($parts[0]) !== false && (!isset($parts[1]) || ctype_digit($parts[1]));
}

/*
 * What ipfw decides about, as a list in the owner's own form, and why, one reason an entry. An empty
 * list when neither the shaper nor the portal has anything enabled.
 */
function ipfw_keeps(array $config, array &$why): string
{
    $keep = [];
    $all = false;
    $rules = $config['OPNsense']['TrafficShaper']['rules']['rule'] ?? [];
    if (isset($rules['enabled'])) {
        $rules = [$rules];
    }
    foreach ($rules as $r) {
        if (!is_array($r) || (string)($r['enabled'] ?? '0') !== '1') {
            continue;
        }
        $name = trim((string)($r['description'] ?? '')) ?: 'a shaper rule';
        $nets = [];
        foreach ([['source', 'source_not'], ['destination', 'destination_not']] as $side) {
            $v = trim((string)($r[$side[0]] ?? 'any'));
            if ($v === '' || strtolower($v) === 'any') {
                continue;
            }
            if ((string)($r[$side[1]] ?? '0') === '1') {
                $nets = null;   /* "not" this network: everything else, which is no narrower */
                break;
            }
            foreach (preg_split('/[\s,]+/', $v, -1, PREG_SPLIT_NO_EMPTY) as $n) {
                if (!readable_net($n)) {
                    $nets = null;
                    break 2;
                }
                $nets[] = $n;
            }
        }
        if ($nets === null) {
            $all = true;
            $why[] = 'the Traffic Shaper: ' . $name . ' names what cannot be listed - everything';
            continue;
        }
        if ($nets) {
            $keep = array_merge($keep, $nets);
            $why[] = 'the Traffic Shaper: ' . $name . ' - ' . implode(', ', $nets);
            continue;
        }
        $ports = [];
        foreach (['src_port', 'dst_port'] as $p) {
            $v = trim((string)($r[$p] ?? 'any'));
            if ($v === '' || strtolower($v) === 'any') {
                continue;
            }
            if (!ctype_digit($v)) {
                $ports = null;  /* a range or a list: kept whole, below */
                break;
            }
            $ports[] = 'port ' . $v;
        }
        if ($ports) {
            $keep = array_merge($keep, $ports);
            $why[] = 'the Traffic Shaper: ' . $name . ' - ' . implode(', ', $ports);
        } else {
            $all = true;
            $why[] = 'the Traffic Shaper: ' . $name . ' names no network or single port - everything';
        }
    }
    $zones = $config['OPNsense']['captiveportal']['zones']['zone'] ?? [];
    if (isset($zones['enabled'])) {
        $zones = [$zones];
    }
    /*
     * The zones cp_users.py runs in hardware (2026-10-24): their clients are the coprocessor's live
     * users, so a logout takes their connections out at once. Only while the setting is on; a zone
     * the file does not name, or any zone when it cannot be read, stays with the host.
     */
    $in_hw = [];
    if ((string)($config['OPNsense']['XGS']['general']['cp_hardware'] ?? '0') === '1') {
        $placed = json_decode((string)@file_get_contents('/var/db/os-xgs-npu/cp/zones.json'), true);
        foreach ((is_array($placed) ? ($placed['zones'] ?? []) : []) as $p) {
            if (is_array($p) && ($p['where'] ?? '') === 'hardware' && isset($p['zoneid'])) {
                $in_hw[] = (string)$p['zoneid'];
            }
        }
    }
    foreach ($zones as $z) {
        if (!is_array($z) || (string)($z['enabled'] ?? '0') !== '1') {
            continue;
        }
        $name = trim((string)($z['description'] ?? '')) ?: 'a zone';
        if (in_array((string)($z['zoneid'] ?? ''), $in_hw, true)) {
            $why[] = 'the Captive Portal: ' . $name . ' runs in hardware, its clients as live users';
            continue;
        }
        foreach (preg_split('/[\s,]+/', (string)($z['interfaces'] ?? ''), -1, PREG_SPLIT_NO_EMPTY) as $if) {
            $nets = interface_networks($config, $if);
            if ($nets === null) {
                $all = true;
                $why[] = 'the Captive Portal: ' . $name . ' on ' . $if . ', which has no fixed network - everything';
            } else {
                $keep = array_merge($keep, $nets);
                $why[] = 'the Captive Portal: ' . $name . ' on ' . $if . ' - ' . implode(', ', $nets);
            }
        }
    }
    if ($all) {
        return '0.0.0.0/0 ::/0';
    }
    return implode(' ', array_unique($keep));
}

function canonical(string $list, array &$entries, array &$refused): string
{
    $out = [];
    /* "port 22" and "port:22" are one entry: made one token before the list is split. */
    $list = preg_replace('/\bport[\s:]+(\d+)/i', 'port:$1', $list);
    foreach (preg_split('/[\s,;]+/', trim($list), -1, PREG_SPLIT_NO_EMPTY) as $item) {
        $item = strtolower($item);
        $port = preg_match('/^port:(\d{1,6})$/', $item, $m) ? (int)$m[1] : null;
        if ($port !== null) {
            if ($port < 1 || $port > 65535) {
                $refused[] = $item;
                continue;
            }
            $out[] = 'p:' . $port;
            $entries[] = 'port ' . $port;
            continue;
        }
        $parts = explode('/', $item, 2);
        $addr = $parts[0];
        $bin = @inet_pton($addr);
        if ($bin === false) {
            $refused[] = $item;
            continue;
        }
        $max = strlen($bin) === 4 ? 32 : 128;
        $len = isset($parts[1]) ? $parts[1] : (string)$max;
        if (!ctype_digit($len) || (int)$len > $max) {
            $refused[] = $item;
            continue;
        }
        if ($max === 32) {
            $out[] = sprintf('a4:%s/%d', bin2hex($bin), (int)$len);
        } else {
            $out[] = sprintf('a6:%s/%d', bin2hex($bin), (int)$len);
        }
        $entries[] = $item;
    }
    return implode(' ', $out);
}

$list = null;
$ifchanged = (($argv[1] ?? '') === '--if-changed');
$why = [];
if (isset($argv[1]) && !$ifchanged) {
    $list = $argv[1];
} else {
    require_once('config.inc');
    $list = (string)($config['OPNsense']['XGS']['general']['flow_exclude'] ?? '');
    $ipfw = ipfw_keeps($config, $why);
    if ($ipfw !== '') {
        $list = trim($list . ' ' . $ipfw);
    }
}

$entries = [];
$refused = [];
$text = canonical($list, $entries, $refused);
if (count($entries) > 64 && $why) {
    /* what ipfw names does not fit beside the owner's list: everything is kept with the host */
    $why[] = 'more than the 64 entries the driver keeps - everything';
    $entries = [];
    $refused = [];
    $text = canonical('0.0.0.0/0 ::/0', $entries, $refused);
}
$last = '/var/run/xgs-flow-exclude.applied';
if ($ifchanged && @file_get_contents($last) === $text . "\n") {
    exit(0);
}

$oid = null;
foreach (['dev.octep.0.dp.flow_exclude', 'dev.npuep.0.flow.exclude'] as $candidate) {
    exec('/sbin/sysctl -n ' . escapeshellarg($candidate) . ' 2>/dev/null', $o, $rc);
    if ($rc === 0) {
        $oid = $candidate;
        break;
    }
    $o = [];
}

$applied = false;
$message = '';
if ($oid === null) {
    $message = 'no driver with an exclusion list is attached';
} elseif (count($entries) > 64) {
    /* Both drivers keep 64, and refuse a longer list whole; said here in the owner's terms. */
    $message = sprintf('%d entries, and the driver keeps at most 64 - nothing changed', count($entries));
} else {
    exec('/sbin/sysctl ' . escapeshellarg($oid . '=' . $text) . ' 2>&1', $o2, $rc2);
    $applied = ($rc2 === 0);
    $message = $applied ? '' : 'the driver refused the list: ' . implode(' ', $o2);
    if ($applied) {
        @file_put_contents($last, $text . "\n");
        if ($ifchanged) {
            syslog(LOG_NOTICE, 'xgs: never offloaded, written again: ' . ($why ? implode('; ', $why) :
                'the Traffic Shaper and the Captive Portal keep nothing now'));
        }
    }
}

echo json_encode([
    'applied' => $applied,
    'entries' => $entries,
    'refused' => $refused,
    'oid' => $oid,
    'canonical' => $text,
    'ipfw' => $why,
    'message' => $message,
]) . "\n";
exit(0);
