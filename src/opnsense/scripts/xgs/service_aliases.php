<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * Ready-made service aliases (plan 01, package 3.6; XGS: Settings, "Ready-made service aliases"): port
 * aliases for the services a rule names most, added to Firewall: Aliases for the owner to use in rules -
 * the ports alone, since a port alias carries no protocol: the rule says TCP, UDP or TCP/UDP, which
 * OPNsense's rules already offer together.
 *
 *   service_aliases.php     add those missing, by name; print {"added", "kept", "refused", "message"}
 *
 * Only added, never changed nor taken away: an alias of the same name, whoever made it, is kept as it
 * is, and turning the setting off leaves them, since a rule may use them. Run by apply.php when the
 * setting is on.
 */

/* name => [ports, what they are] */
function xgs_service_aliases(): array
{
    return [
        'XGS_WEB' => [['80', '443'], 'web: HTTP and HTTPS'],
        'XGS_WEB_ALT' => [['8080', '8443'], 'web on the alternative ports'],
        'XGS_DNS' => [['53', '853'], 'DNS, and DNS over TLS'],
        'XGS_MAIL' => [['25', '110', '143', '465', '587', '993', '995'], 'mail: SMTP, submission, POP3 and IMAP, plain and over TLS'],
        'XGS_SSH' => [['22'], 'SSH'],
        'XGS_RDP' => [['3389'], 'Remote Desktop'],
        'XGS_NTP' => [['123'], 'NTP'],
        'XGS_FTP' => [['20', '21'], 'FTP'],
        'XGS_SMB' => [['139', '445'], 'Windows file sharing'],
        'XGS_LDAP' => [['389', '636'], 'LDAP, plain and over TLS'],
        'XGS_SNMP' => [['161', '162'], 'SNMP and its traps'],
        'XGS_SYSLOG' => [['514', '6514'], 'syslog, and syslog over TLS'],
        'XGS_VPN' => [['500', '4500', '1194', '51820'], 'VPN: IPsec (IKE and NAT-T), OpenVPN and WireGuard'],
    ];
}

/* Which of them to add, given the names Firewall: Aliases has (any case): [to add, kept]. */
function xgs_service_aliases_missing(array $existing): array
{
    $have = array_map('strtolower', $existing);
    $add = [];
    $kept = [];
    foreach (xgs_service_aliases() as $name => $what) {
        if (in_array(strtolower($name), $have, true)) {
            $kept[] = $name;
        } else {
            $add[$name] = $what;
        }
    }
    return [$add, $kept];
}

// The tests include this file for the functions above alone.
if (getenv('XGS_ALIASES_FUNCTIONS_ONLY') === '1') {
    return;
}

require_once('script/load_phalcon.php');

$mdl = new \OPNsense\Firewall\Alias();
$names = [];
foreach ($mdl->aliases->alias->iterateItems() as $item) {
    $names[] = (string)$item->name;
}
[$add, $kept] = xgs_service_aliases_missing($names);
$added = [];
$refused = [];
foreach ($add as $name => [$ports, $what]) {
    $node = $mdl->aliases->alias->Add();
    $node->enabled = '1';
    $node->name = $name;
    $node->type = 'port';
    $node->content = implode("\n", $ports);
    $node->description = 'XGS: ready-made service - ' . $what;
    $added[] = $name;
}
$message = '';
if (!empty($added)) {
    foreach ($mdl->performValidation() as $msg) {
        $refused[] = $msg->getField() . ': ' . $msg->getMessage();
    }
    if (empty($refused)) {
        $mdl->serializeToConfig();
        \OPNsense\Core\Config::getInstance()->save();
        (new \OPNsense\Core\Backend())->configdRun('filter refresh_aliases');
    } else {
        $added = [];
        $message = 'Firewall: Aliases refused them';
    }
}
echo json_encode(['added' => $added, 'kept' => $kept, 'refused' => $refused, 'message' => $message]) . "\n";
