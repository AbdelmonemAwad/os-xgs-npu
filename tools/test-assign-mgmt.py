#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/assign.php's choice of the MGMT device, case by case, with PHP and no OPNsense: what
the installer made `lan`, the host NIC by its name after a factory reset (issue 957), and nothing
taken that someone else has. And where a configuration names a front port without assigning it - a
VLAN's parent, a LAGG's member, a PPP link's port - which makes the box one somebody set up.

And the layout each model is given, from scripts/xgs/models.py --json as assign.php reads it: the
port its table names as WAN, and every other front port in bridge0. The XGS 87 and 87w take Port2,
the socket their chassis prints WAN beside - until 2026-11-22 they took their family's Port1, the one
printed LAN - and the XGS 136 and the XGS 3300 are given what they always were.

    python3 tools/test-assign-mgmt.py"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSIGN = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'assign.php')
MODELS = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'models.py')
HARNESS = r'''
putenv('XGS_ASSIGN_FUNCTIONS_ONLY=1');
include $argv[1];
$c = json_decode($argv[2], true);
$exists = function ($dev) use ($c) { return in_array($dev, $c['exists'], true); };
echo json_encode(xgs_mgmt_device($c['prefix'], $c['lan'], array_fill_keys($c['ports'], 'Port'),
    $c['assigned'], $exists));
'''

WHERE = r'''
putenv('XGS_ASSIGN_FUNCTIONS_ONLY=1');
include $argv[1];
$c = json_decode($argv[2], true);
echo json_encode(xgs_where_named(simplexml_load_string($c['xml']), $c['devs'], $c['skip']));
'''

# The front ports as assign.php finds them on a model: each unit of the table there, named as the table
# names it - on the ARMADA family by the unit, on the OCTEON TX family by octep's description, which
# is the same name - and what the assignment makes of each.
LAYOUT = r'''
putenv('XGS_ASSIGN_FUNCTIONS_ONLY=1');
include $argv[1];
$c = json_decode($argv[2], true);
$t = xgs_model_table($c['known'], $c['family']);
if ($t === null) {
    echo json_encode(null);
    exit(0);
}
$ports = [];
foreach ($t['names'] as $unit => $label) {
    $ports[$c['prefix'] . $unit] = $label;
}
uksort($ports, 'strnatcmp');
$layout = xgs_layout($ports, $t['wan'], $c['wan_ok']);
$out = ['model' => $t['model'], 'wan' => [], 'bridge' => []];
foreach ($layout as $dev => $what) {
    $out[$what][] = $dev . ' ' . $ports[$dev];
}
echo json_encode($out);
'''

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def mgmt(prefix, lan, ports, assigned, exists):
    case = json.dumps({'prefix': prefix, 'lan': lan, 'ports': ports, 'assigned': assigned, 'exists': exists})
    out = subprocess.run(['php', '-r', HARNESS, '--', ASSIGN, case], capture_output=True, text=True)
    if out.returncode != 0:
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:200]
    return json.loads(out.stdout)


def where(xml, devs, skip=('/OPNsense/XGS',)):
    case = json.dumps({'xml': '<opnsense>%s</opnsense>' % xml, 'devs': list(devs), 'skip': list(skip)})
    out = subprocess.run(['php', '-r', WHERE, '--', ASSIGN, case], capture_output=True, text=True)
    if out.returncode != 0:
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:200]
    return json.loads(out.stdout) or {}


def known(assembly):
    """scripts/xgs/models.py --json on an appliance of this assembly, as assign.php reads it."""
    out = subprocess.run([sys.executable, MODELS, '--json'], capture_output=True, text=True,
                         env=dict(os.environ, XGS_PLANAR_SERIAL='XYZ' + assembly + '12345678'))
    return json.loads(out.stdout) if out.returncode == 0 else 'models.py failed: ' + out.stderr.strip()[:200]


def layout(assembly, family, prefix, wan_ok=False):
    case = json.dumps({'known': known(assembly), 'family': family, 'prefix': prefix, 'wan_ok': wan_ok})
    out = subprocess.run(['php', '-r', LAYOUT, '--', ASSIGN, case], capture_output=True, text=True)
    if out.returncode != 0:
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:200]
    return json.loads(out.stdout)


if shutil.which('php') is None:
    print('FAIL php is not installed, so assign.php cannot be checked')
    sys.exit(1)

OXP = ['oxp%d' % i for i in range(12)]
HOST = OXP + ['igb0']

check('a fresh install: `lan` is the host NIC the installer chose',
      mgmt('oxp', 'igb0', OXP, {'igb0': 'lan'}, HOST), 'igb0')
check('after a factory reset: `lan` names the placeholder, igb0 is free',
      mgmt('oxp', 'mismatch0', OXP, {'mismatch0': 'lan', 'mismatch1': 'wan'}, HOST), 'igb0')
check('after a factory reset, igb0 made WAN by hand: left alone',
      mgmt('oxp', 'mismatch0', OXP, {'igb0': 'wan'}, HOST), None)
check('after a factory reset on a board without igb0',
      mgmt('oxp', 'mismatch0', OXP, {}, OXP), None)
check('`lan` on a front port is no host NIC; igb0 free is',
      mgmt('oxp', 'oxp1', OXP, {'oxp1': 'lan'}, HOST), 'igb0')
check('the ARMADA family has no host NIC by name',
      mgmt('npup', 'mismatch0', ['npup%d' % i for i in range(1, 15)], {}, ['npup1', 'igb0']), None)
check('the ARMADA family: what the installer made `lan` still counts',
      mgmt('npup', 'ue0', ['npup1'], {'ue0': 'lan'}, ['npup1', 'ue0']), 'ue0')
check('no `lan` at all, igb0 free', mgmt('oxp', '', OXP, {}, HOST), 'igb0')

NPUP = ['npup1', 'npup2', 'npup3', 'npup4']
FACTORY = '<interfaces><wan><if>mismatch1</if></wan><lan><if>mismatch0</if></lan></interfaces>'
check('the factory configuration names no front port', where(FACTORY, NPUP), {})
check('a VLAN on a front port: its parent',
      where(FACTORY + '<vlans><vlan><if>npup1</if><tag>10</tag><vlanif>vlan0.10</vlanif></vlan></vlans>'
            '<interfaces><opt1><if>vlan0.10</if></opt1></interfaces>', NPUP),
      {'npup1': ['/vlans/vlan/if']})
check('a VLAN by its old name, assigned',
      where('<interfaces><opt1><if>npup2_vlan20</if></opt1></interfaces>', NPUP),
      {'npup2': ['/interfaces/opt1/if']})
check('a LAGG of two front ports',
      where('<laggs><lagg><members>npup1,npup3</members><laggif>lagg0</laggif></lagg></laggs>', NPUP),
      {'npup1': ['/laggs/lagg/members'], 'npup3': ['/laggs/lagg/members']})
check('a PPPoE link on a front port',
      where('<ppps><ppp><if>pppoe0</if><ports>npup1</ports><type>pppoe</type></ppp></ppps>', NPUP),
      {'npup1': ['/ppps/ppp/ports']})
check('a QinQ on a front port', where('<qinqs><qinqentry><if>npup4</if></qinqentry></qinqs>', NPUP),
      {'npup4': ['/qinqs/qinqentry/if']})
check('npup10 is not npup1', where('<vlans><vlan><if>npup10</if></vlan></vlans>', ['npup1']), {})
check('descriptions and this project\'s own settings are left out',
      where('<interfaces><opt1><descr>npup1 spare</descr></opt1></interfaces>'
            '<OPNsense><XGS><general><port>npup1</port></general></XGS></OPNsense>', NPUP), {})
check('an OCTEON TX VLAN by a dotted name', where('<interfaces><opt2><if>oxp0.20</if></opt2></interfaces>',
                                                  ['oxp0', 'oxp2']), {'oxp0': ['/interfaces/opt2/if']})
check('a WAN on PPPoE is a device the configuration makes',
      where('<interfaces><wan><if>pppoe0</if></wan></interfaces><ppps><ppp><if>pppoe0</if></ppp></ppps>',
            ['pppoe0'], ('/interfaces', '/OPNsense/XGS')), {'pppoe0': ['/ppps/ppp/if']})
check('a WAN on the factory placeholder is not',
      where(FACTORY, ['mismatch1'], ('/interfaces', '/OPNsense/XGS')), {})

XGS136 = ['npup2 Port2', 'npup3 Port3', 'npup4 Port4', 'npup5 Port5', 'npup6 Port6', 'npup7 Port7',
          'npup8 Port8', 'npup9 Port9', 'npup10 Port10', 'npup11 Port11', 'npup12 Port12', 'npup13 F1',
          'npup14 F2']
XGS3300 = ['oxp0 F1', 'oxp1 F2', 'oxp2 Port1', 'oxp4 Port3', 'oxp5 Port4', 'oxp6 Port5', 'oxp7 Port6',
           'oxp8 Port7', 'oxp9 Port8', 'oxp10 F3', 'oxp11 F4']
for assembly, model in (('AMDA0200-0001', 'XGS 87'), ('AMDA0200-0002', 'XGS 87w')):
    check('the %s: WAN on Port2, as its chassis prints it, and Port1, Port3, Port4 and F1 in bridge0' % model,
          layout(assembly, 'armada', 'npup'),
          {'model': model, 'wan': ['npup2 Port2'], 'bridge': ['npup1 Port1', 'npup3 Port3', 'npup4 Port4', 'npup5 F1']})
check('the XGS 136, as before: WAN on Port1, and the thirteen others in bridge0',
      layout('AMDA0201-0003', 'armada', 'npup'), {'model': 'XGS 136', 'wan': ['npup1 Port1'], 'bridge': XGS136})
check('the XGS 3300, as before: WAN on Port2, and the eleven others in bridge0',
      layout('AMDA0202-0004', 'octeontx', 'oxp'), {'model': 'XGS 3300', 'wan': ['oxp3 Port2'], 'bridge': XGS3300})
check('the XGS 87 with a WAN there already: every front port in bridge0',
      layout('AMDA0200-0001', 'armada', 'npup', wan_ok=True),
      {'model': 'XGS 87', 'wan': [], 'bridge': ['npup1 Port1', 'npup2 Port2', 'npup3 Port3', 'npup4 Port4', 'npup5 F1']})
check('a table of the other family is not this one\'s, and the defaults stay',
      layout('AMDA0200-0001', 'octeontx', 'oxp'), None)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
