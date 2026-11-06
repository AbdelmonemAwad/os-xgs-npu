#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/assign.php's choice of the MGMT device, case by case, with PHP and no OPNsense: what
the installer made `lan`, the host NIC by its name after a factory reset (issue 957), and nothing
taken that someone else has. And where a configuration names a front port without assigning it - a
VLAN's parent, a LAGG's member, a PPP link's port - which makes the box one somebody set up.

    python3 tools/test-assign-mgmt.py"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSIGN = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'assign.php')
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

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
