<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The front ports assigned by the names on the chassis - docs/octep/interface-assignment.md.
 *
 *   assign.php --plan     what would be done, port by port; writes nothing
 *   assign.php --check    exit 0 when there is something to do, 2 when the front ports were set up
 *                         already, 3 when there are no front ports yet, 1 on a board it does not know
 *   assign.php --apply    back config.xml up, assign, and say what was written
 *
 * THE LAYOUT. One front port is WAN, on DHCP - Port2 on the XGS 3300, Port1 on the XGS 136 - and
 * every other front port is an interface of its own, named as the chassis names it and with no
 * address, all of them members of one bridge, bridge0, which is the LAN:
 *
 *   XGS 3300  the host's own NIC is the installer's `lan`, named MGMT, and stays where it is; the
 *             bridge is a second interface named LAN on 10.0.0.1/24, with a DHCP range and the
 *             pass rules OPNsense gives a LAN.
 *   XGS 136   there is no host NIC, so the bridge IS `lan`, with the factory's 192.168.1.1/24, its
 *             DHCP range and its rules.
 *
 * THE NAMES. On the XGS 3300 they are read from the driver: octep names every front port in its
 * interface description from the port's tag - "XGS front port Port2". On the XGS 136 nothing is
 * printed beside the sockets, so they are numbered the way npuep numbers them: npupN is PortN for 1
 * to 12, and the two cages, npup13 and npup14, are F1 and F2.
 *
 * ONLY ON A BOX NOBODY HAS SET UP. If a single front port is assigned already, the box was set up
 * by hand and nothing here touches it - so installing or updating this project on an appliance in
 * service changes nothing. A WAN or LAN entry whose device exists is kept; one that names a device
 * this machine does not have - the factory's mismatch placeholders - is the one that is filled.
 */

require_once 'config.inc';
require_once 'util.inc';
require_once 'interfaces.inc';

$mode = $argv[1] ?? '--plan';
if (!in_array($mode, ['--plan', '--check', '--apply'])) {
    fwrite(STDERR, "usage: assign.php --plan | --check | --apply\n");
    exit(64);
}

$board = trim((string)shell_exec('/bin/sh /usr/local/opnsense/scripts/xgs/board.sh 2>/dev/null'));
switch (explode(' ', $board)[1] ?? '') {
    case 'octeontx':
        $prefix = 'oxp';
        $wan_label = 'Port2';
        $model = 'XGS 3300';
        // Used only when the driver gave a port no description: the bench box's map, port by port.
        $fallback = ['F1', 'F2', 'Port1', 'Port2', 'Port3', 'Port4', 'Port5', 'Port6', 'Port7',
                     'Port8', 'F3', 'F4'];
        break;
    case 'armada':
        $prefix = 'npup';
        $wan_label = 'Port1';
        $model = 'XGS 136';
        // npuep's own table: the unit is the port's number, and the cages come after the twelve.
        $fallback = [];
        for ($n = 1; $n <= 12; $n++) {
            $fallback[$n] = "Port{$n}";
        }
        $fallback[13] = 'F1';
        $fallback[14] = 'F2';
        break;
    default:
        fwrite(STDERR, "not a board this project knows: '{$board}'\n");
        exit(1);
}

/* The front ports present now, with the name each one carries. */
$ports = [];
foreach (preg_split('/\s+/', trim((string)shell_exec('/sbin/ifconfig -l'))) as $dev) {
    if (!preg_match('/^' . $prefix . '(\d+)$/', $dev, $m)) {
        continue;
    }
    $descr = trim((string)shell_exec('/sbin/ifconfig ' . escapeshellarg($dev) . ' | /usr/bin/sed -n "s/^[[:space:]]*description: //p"'));
    if (preg_match('/^XGS front port (\S+)$/', $descr, $d)) {
        $label = $d[1];
    } else {
        $label = $fallback[(int)$m[1]] ?? null;
    }
    if ($label !== null) {
        $ports[$dev] = $label;
    }
}
uksort($ports, 'strnatcmp');

$cfg = \OPNsense\Core\Config::getInstance();
$c = $cfg->object();
$assigned = [];
foreach ($c->interfaces->children() as $key => $node) {
    $assigned[(string)$node->if] = $key;
}
$taken = array_values(array_filter(array_keys($ports), function ($dev) use ($assigned) {
    return isset($assigned[$dev]);
}));
$wan_dev = array_search($wan_label, $ports, true);

$wan_ok = isset($c->interfaces->wan) && does_interface_exist((string)$c->interfaces->wan->if);
$lan_ok = isset($c->interfaces->lan) && does_interface_exist((string)$c->interfaces->lan->if);
$bridge_as_lan = !$lan_ok;

/* The plan, and what --check answers. */
if (!$ports) {
    $verdict = 3;
    $why = "no {$prefix} front ports yet";
} elseif ($taken) {
    $verdict = 2;
    $why = 'set up already: ' . implode(' ', array_map(function ($dev) use ($assigned, $ports) {
        return "{$ports[$dev]}={$assigned[$dev]}";
    }, $taken)) . ' - nothing is changed';
} else {
    $verdict = 0;
    $why = 'none of the ' . count($ports) . ' front ports is assigned';
}

if ($mode === '--check') {
    echo "{$model}: {$why}\n";
    exit($verdict);
}

echo "{$model}: {$why}\n";
printf("%-8s %-7s %s\n", 'device', 'name', 'to do');
foreach ($ports as $dev => $label) {
    if ($verdict !== 0) {
        $what = isset($assigned[$dev]) ? "kept ({$assigned[$dev]})" : 'left unassigned';
    } elseif ($dev === $wan_dev) {
        $what = $wan_ok ? 'member of bridge0 (a WAN exists)' : 'WAN, DHCP';
    } else {
        $what = 'member of bridge0';
    }
    printf("%-8s %-7s %s\n", $dev, $label, $what);
}
if ($verdict === 0) {
    if ($bridge_as_lan) {
        printf("%-8s %-7s %s\n", 'bridge0', 'LAN', '`lan` itself, keeping its address, DHCP and rules');
    } else {
        printf("%-8s %-7s %s\n", 'bridge0', 'LAN', 'a second LAN, 10.0.0.1/24, DHCP .100-.199, pass rules');
        printf("%-8s %-7s %s\n", (string)$c->interfaces->lan->if, 'MGMT', '`lan`, kept as it is');
    }
}
if ($mode === '--plan' || $verdict !== 0) {
    if ($mode === '--apply') {
        echo "nothing written\n";
    }
    exit($mode === '--apply' ? ($verdict === 2 ? 0 : 1) : 0);
}

/* --apply */
if (in_array('bridge0', preg_split('/\s+/', trim((string)shell_exec('/sbin/ifconfig -l'))), true) ||
    isset($assigned['bridge0'])) {
    echo "bridge0 exists already - nothing written\n";
    exit(1);
}
$backup = '/conf/config.xml.before-xgs-assign-' . date('Ymd-His');
copy('/conf/config.xml', $backup);
echo "config.xml backed up to {$backup}\n";

$next_opt = function () use ($c) {
    for ($n = 1; isset($c->interfaces->{"opt{$n}"}); $n++);
    return "opt{$n}";
};
$set = function ($node, $name, $value) {
    if (isset($node->$name)) {
        $node->$name = $value;
    } else {
        $node->addChild($name, $value);
    }
};

$members = [];
foreach ($ports as $dev => $label) {
    if ($dev === $wan_dev && !$wan_ok) {
        if (!isset($c->interfaces->wan)) {
            $w = $c->interfaces->addChild('wan');
            $set($w, 'ipaddr', 'dhcp');
            $set($w, 'ipaddrv6', 'dhcp6');
            $set($w, 'blockpriv', '1');
            $set($w, 'blockbogons', '1');
        }
        $w = $c->interfaces->wan;
        $set($w, 'if', $dev);
        $set($w, 'descr', 'WAN ' . $label);
        $set($w, 'enable', '1');
        echo "wan      {$dev} {$label}: WAN\n";
        continue;
    }
    $key = $next_opt();
    $o = $c->interfaces->addChild($key);
    $o->addChild('enable', '1');
    $o->addChild('if', $dev);
    $o->addChild('descr', $label);
    $members[] = $key;
    echo "{$key}    {$dev} {$label}: member of bridge0\n";
}

if ($bridge_as_lan) {
    if (!isset($c->interfaces->lan)) {
        $l = $c->interfaces->addChild('lan');
        $set($l, 'ipaddr', '192.168.1.1');
        $set($l, 'subnet', '24');
    }
    $lan_key = 'lan';
    $set($c->interfaces->lan, 'if', 'bridge0');
    $set($c->interfaces->lan, 'enable', '1');
    if ((string)$c->interfaces->lan->descr === '') {
        $set($c->interfaces->lan, 'descr', 'LAN');
    }
    echo "lan      bridge0 LAN: `lan` itself\n";
} else {
    if ((string)$c->interfaces->lan->descr === '') {
        $set($c->interfaces->lan, 'descr', 'MGMT');
    }
    $lan_key = $next_opt();
    $l = $c->interfaces->addChild($lan_key);
    $l->addChild('enable', '1');
    $l->addChild('if', 'bridge0');
    $l->addChild('descr', 'LAN');
    $l->addChild('ipaddr', '10.0.0.1');
    $l->addChild('subnet', '24');
    echo "{$lan_key}    bridge0 LAN: 10.0.0.1/24\n";
}

/* The bridge, and for a second LAN its DHCP range and pass rules, through their models. */
$bridge = new \OPNsense\Interfaces\Bridge();
$b = $bridge->bridged->Add();
$b->bridgeif = 'bridge0';
$b->members = implode(',', $members);
$b->descr = 'LAN';
$models = ['bridge' => $bridge];

if (!$bridge_as_lan) {
    $dns = new \OPNsense\Dnsmasq\Dnsmasq();
    $ifs = array_filter(explode(',', (string)$dns->interface));
    $ifs[] = $lan_key;
    $dns->interface = implode(',', array_unique($ifs));
    $r = $dns->dhcp_ranges->Add();
    $r->interface = $lan_key;
    $r->start_addr = '10.0.0.100';
    $r->end_addr = '10.0.0.199';
    $models['dnsmasq'] = $dns;

    $filter = new \OPNsense\Firewall\Filter();
    foreach (['inet' => 'Default allow LAN to any rule', 'inet6' => 'Default allow LAN IPv6 to any rule'] as $proto => $text) {
        $rule = $filter->rules->rule->Add();
        $rule->enabled = '1';
        $rule->action = 'pass';
        $rule->quick = '1';
        $rule->interface = $lan_key;
        $rule->direction = 'in';
        $rule->ipprotocol = $proto;
        $rule->protocol = 'any';
        $rule->source_net = $lan_key;
        $rule->destination_net = 'any';
        $rule->description = $text . ' (bridge0)';
    }
    $models['filter'] = $filter;
}

$bad = false;
foreach ($models as $name => $mdl) {
    foreach ($mdl->performValidation() as $msg) {
        echo "invalid {$name}: {$msg->getField()}: {$msg->getMessage()}\n";
        $bad = true;
    }
}
if ($bad) {
    echo "nothing written; config.xml is as it was\n";
    exit(1);
}
foreach ($models as $mdl) {
    $mdl->serializeToConfig();
}
$cfg->save();
echo 'written: WAN on ' . ($wan_ok ? 'the existing entry' : $wan_label) . ', bridge0 of ' . count($members) .
    " ports as LAN\n";
exit(0);
