#!/usr/local/bin/php
<?php
/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The OPNsense side of the management link to the coprocessor (issue 374): this host's end of it as
 * an interface of this firewall, named NPUMGMT, with its fixed link-local address - mvmgmt0 on the
 * XGS 136, oxmgmt0 on the XGS 3300 (scripts/npuctl/mgmt-link.sh says which). Idempotent: an
 * assignment that already carries the link is kept and only its address and name are made right.
 *
 *   mgmt-link.php assign <address> <prefixlen> [link]    prints the interface key, e.g. opt15
 *   mgmt-link.php show [link]                            prints the key and address, or "none"
 *   mgmt-link.php state                                  prints what mgmt-link.sh last saw of NPUMGMT,
 *                                                        or "none" (#1072)
 *   mgmt-link.php mark <what>                            records it, in config.xml, when it changes
 *
 * Written through the same config.inc every page uses, so the change is in config.xml with a
 * revision line of its own and a backup beside it, like any change made in the GUI.
 */

require_once('config.inc');
require_once('util.inc');

$action = $argv[1] ?? 'show';

/* What mgmt-link.sh last saw of NPUMGMT (THE OWNER'S CHOICE KEPT there, #1072): in the XGS model's
 * general section, so it is part of config.xml - a factory reset or a restored configuration brings
 * its own answer, as a file beside it would not. Written only when it changes. */
if ($action == 'state' || $action == 'mark') {
    $general = $config['OPNsense']['XGS']['general'] ?? [];
    $now = is_array($general) && isset($general['npumgmt']) && is_string($general['npumgmt']) ?
        trim($general['npumgmt']) : '';
    if ($action == 'state') {
        echo ($now === '' ? 'none' : $now) . "\n";
        exit(0);
    }
    $what = trim(implode(' ', array_slice($argv, 2)));
    if (!preg_match('/^(assigned [a-z0-9_]+|removed [0-9: UTC-]+)$/', $what)) {
        fwrite(STDERR, "mark: assigned <key> or removed <when>\n");
        exit(2);
    }
    if ($what !== $now) {
        if (!isset($config['OPNsense']) || !is_array($config['OPNsense'])) {
            $config['OPNsense'] = [];
        }
        if (!isset($config['OPNsense']['XGS']) || !is_array($config['OPNsense']['XGS'])) {
            $config['OPNsense']['XGS'] = [];
        }
        if (!isset($config['OPNsense']['XGS']['general']) || !is_array($config['OPNsense']['XGS']['general'])) {
            $config['OPNsense']['XGS']['general'] = [];
        }
        $config['OPNsense']['XGS']['general']['npumgmt'] = $what;
        write_config('os-xgs-npu: NPUMGMT ' . $what);
    }
    exit(0);
}
$link = $action == 'assign' ? ($argv[4] ?? 'mvmgmt0') : ($argv[2] ?? 'mvmgmt0');
if (!in_array($link, ['mvmgmt0', 'oxmgmt0'], true)) {
    fwrite(STDERR, "not a management link: {$link}\n");
    exit(2);
}
$interfaces = config_read_array('interfaces');

$found = null;
foreach ($interfaces as $key => $intf) {
    if (!empty($intf['if']) && $intf['if'] == $link) {
        $found = $key;
        break;
    }
}

if ($action == 'show') {
    if ($found === null) {
        echo "none\n";
    } else {
        $intf = $interfaces[$found];
        echo sprintf("%s %s %s/%s\n", $found, $intf['descr'] ?? '', $intf['ipaddr'] ?? '', $intf['subnet'] ?? '');
    }
    exit(0);
}

if ($action != 'assign') {
    fwrite(STDERR, "usage: mgmt-link.php assign <address> <prefixlen> [link] | show [link]\n");
    exit(2);
}

$address = $argv[2] ?? '169.254.254.1';
$prefix = $argv[3] ?? '30';
if (!filter_var($address, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) || !ctype_digit((string)$prefix)) {
    fwrite(STDERR, "bad address or prefix length\n");
    exit(1);
}

if ($found === null) {
    /* The first free optN, the way the assignment page picks one - and named nowhere else in the
     * configuration. A key left behind by an interface removed by hand, still a bridge's member, a
     * group's or a rule's, would give the link that bridge, group or rule; until 2026-11-22 the first
     * key without an interface was taken, whatever still named it (an XGS 87's support bundle had
     * mvmgmt0 in a bridge with its WAN port; whether by this or by hand, the bundle does not say). */
    $rest = $config;
    unset($rest['interfaces']);
    $named = json_encode($rest, JSON_INVALID_UTF8_SUBSTITUTE) ?: '';
    for ($n = 1; $n < 256; $n++) {
        if (!isset($interfaces['opt' . $n]) && !preg_match('/(?<![A-Za-z0-9_])opt' . $n . '(?![A-Za-z0-9_])/', $named)) {
            $found = 'opt' . $n;
            break;
        }
    }
    if ($found === null) {
        fwrite(STDERR, "no free interface slot\n");
        exit(1);
    }
    $config['interfaces'][$found] = [];
}

$entry = &$config['interfaces'][$found];
$entry['if'] = $link;
$entry['descr'] = 'NPUMGMT';
$entry['enable'] = '1';
$entry['ipaddr'] = $address;
$entry['subnet'] = (string)$prefix;
foreach (['ipaddrv6', 'subnetv6', 'media', 'mediaopt', 'gateway', 'gatewayv6'] as $empty) {
    if (!isset($entry[$empty])) {
        $entry[$empty] = '';
    }
}
unset($entry);

write_config(sprintf('os-xgs-npu: %s assigned as %s (NPUMGMT), %s/%s', $link, $found, $address, $prefix));
echo $found . "\n";
exit(0);
