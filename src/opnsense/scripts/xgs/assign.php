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
 * address, all of them members of one bridge, bridge0, which is `lan`:
 *
 *   LAN       bridge0, 192.168.1.1/24, with the factory LAN's DHCP range (.100-.199) and its
 *             rules - on every board, so a machine plugged into any front port finds the firewall
 *             at the same address.
 *   MGMT      the host's own NIC, where there is one (igb0 on the XGS 3300): the installer made it
 *             `lan`, and it moves to an interface of its own named MGMT on 192.168.100.1/24, with
 *             a DHCP range (.100-.199) and pass rules. The XGS 136 has none.
 *
 * So the address a box is installed through (192.168.1.1 on the host NIC) is not the address it
 * answers on there afterwards: the host NIC is 192.168.100.1, and 192.168.1.1 is on the front ports.
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
$lan_dev = isset($c->interfaces->lan) ? (string)$c->interfaces->lan->if : '';
// The host's own NIC: what the installer made `lan`, when it exists and is not a front port.
$mgmt_dev = ($lan_dev !== '' && !isset($ports[$lan_dev]) && does_interface_exist($lan_dev)) ? $lan_dev : null;

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
    printf("%-8s %-7s %s\n", 'bridge0', 'LAN', '`lan`, 192.168.1.1/24, DHCP .100-.199, the LAN rules');
    if ($mgmt_dev !== null) {
        printf("%-8s %-7s %s\n", $mgmt_dev, 'MGMT', 'out of `lan`: 192.168.100.1/24, DHCP .100-.199, pass rules');
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

/* The host's own NIC out of `lan`, into an interface of its own on 192.168.100.1/24. */
$mgmt_key = null;
if ($mgmt_dev !== null) {
    $mgmt_key = $next_opt();
    $m = $c->interfaces->addChild($mgmt_key);
    $m->addChild('enable', '1');
    $m->addChild('if', $mgmt_dev);
    $m->addChild('descr', 'MGMT');
    $m->addChild('ipaddr', '192.168.100.1');
    $m->addChild('subnet', '24');
    echo "{$mgmt_key}    {$mgmt_dev} MGMT: 192.168.100.1/24\n";
}

/* `lan` is the bridge, at 192.168.1.1/24, on every board. */
if (!isset($c->interfaces->lan)) {
    $c->interfaces->addChild('lan');
}
$set($c->interfaces->lan, 'if', 'bridge0');
$set($c->interfaces->lan, 'enable', '1');
$set($c->interfaces->lan, 'ipaddr', '192.168.1.1');
$set($c->interfaces->lan, 'subnet', '24');
if ((string)$c->interfaces->lan->descr === '' || (string)$c->interfaces->lan->descr === 'MGMT') {
    $set($c->interfaces->lan, 'descr', 'LAN');
}
echo "lan      bridge0 LAN: 192.168.1.1/24\n";

/* The bridge, the DHCP ranges and MGMT's pass rules, through their models. */
$bridge = new \OPNsense\Interfaces\Bridge();
$b = $bridge->bridged->Add();
$b->bridgeif = 'bridge0';
$b->members = implode(',', $members);
$b->descr = 'LAN';
$models = ['bridge' => $bridge];

// A range on each of the two networks, unless the factory's is there already (on `lan` it is).
$dns = new \OPNsense\Dnsmasq\Dnsmasq();
$starts = [];
foreach ($dns->dhcp_ranges->iterateItems() as $r) {
    $starts[(string)$r->interface][] = (string)$r->start_addr;
}
$want = ['lan' => '192.168.1'];
if ($mgmt_key !== null) {
    $want[$mgmt_key] = '192.168.100';
}
$ifs = array_filter(explode(',', (string)$dns->interface));
foreach ($want as $key => $net) {
    $ifs[] = $key;
    $have = array_filter($starts[$key] ?? [], function ($a) use ($net) {
        return strpos($a, $net . '.') === 0;
    });
    if (!$have) {
        $r = $dns->dhcp_ranges->Add();
        $r->interface = $key;
        $r->start_addr = $net . '.100';
        $r->end_addr = $net . '.199';
    }
}
$dns->interface = implode(',', array_unique($ifs));
$models['dnsmasq'] = $dns;

// `lan` keeps the factory's rules; MGMT is a new interface and is given the same two.
if ($mgmt_key !== null) {
    $filter = new \OPNsense\Firewall\Filter();
    foreach (['inet' => 'Default allow MGMT to any rule', 'inet6' => 'Default allow MGMT IPv6 to any rule'] as $proto => $text) {
        $rule = $filter->rules->rule->Add();
        $rule->enabled = '1';
        $rule->action = 'pass';
        $rule->quick = '1';
        $rule->interface = $mgmt_key;
        $rule->direction = 'in';
        $rule->ipprotocol = $proto;
        $rule->protocol = 'any';
        $rule->source_net = $mgmt_key;
        $rule->destination_net = 'any';
        $rule->description = $text;
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
    ' ports as LAN on 192.168.1.1' . ($mgmt_key !== null ? ", {$mgmt_dev} as MGMT on 192.168.100.1" : '') . "\n";
exit(0);
