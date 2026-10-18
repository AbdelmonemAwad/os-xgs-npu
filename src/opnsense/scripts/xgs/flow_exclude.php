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
 * Never fails the caller.
 */

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
if (isset($argv[1])) {
    $list = $argv[1];
} else {
    require_once('config.inc');
    $list = (string)($config['OPNsense']['XGS']['general']['flow_exclude'] ?? '');
}

$entries = [];
$refused = [];
$text = canonical($list, $entries, $refused);

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
}

echo json_encode([
    'applied' => $applied,
    'entries' => $entries,
    'refused' => $refused,
    'oid' => $oid,
    'canonical' => $text,
    'message' => $message,
]) . "\n";
exit(0);
