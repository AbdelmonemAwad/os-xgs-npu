#!/usr/local/bin/php
<?php
/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The OPNsense side of the management link to the ARMADA coprocessor (issue 374): mvmgmt0 as an
 * interface of this firewall, named NPUMGMT, with its fixed link-local address. Idempotent: an
 * assignment that already carries mvmgmt0 is kept and only its address and name are made right.
 *
 *   mgmt-link.php assign <address> <prefixlen>    prints the interface key, e.g. opt15
 *   mgmt-link.php show                            prints the key and address, or "none"
 *
 * Written through the same config.inc every page uses, so the change is in config.xml with a
 * revision line of its own and a backup beside it, like any change made in the GUI.
 */

require_once('config.inc');
require_once('util.inc');

$action = $argv[1] ?? 'show';
$interfaces = config_read_array('interfaces');

$found = null;
foreach ($interfaces as $key => $intf) {
    if (!empty($intf['if']) && $intf['if'] == 'mvmgmt0') {
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
    fwrite(STDERR, "usage: mgmt-link.php assign <address> <prefixlen> | show\n");
    exit(2);
}

$address = $argv[2] ?? '169.254.254.1';
$prefix = $argv[3] ?? '30';
if (!filter_var($address, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) || !ctype_digit((string)$prefix)) {
    fwrite(STDERR, "bad address or prefix length\n");
    exit(1);
}

if ($found === null) {
    /* The first free optN, the way the assignment page picks one. */
    for ($n = 1; $n < 256; $n++) {
        if (!isset($interfaces['opt' . $n])) {
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
$entry['if'] = 'mvmgmt0';
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

write_config(sprintf('os-xgs-npu: mvmgmt0 assigned as %s (NPUMGMT), %s/%s', $found, $address, $prefix));
echo $found . "\n";
exit(0);
