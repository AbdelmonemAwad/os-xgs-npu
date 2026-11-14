#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The recommended settings behind XGS: Overview's card and its Apply recommended
(models/OPNsense/XGS/XGS.php, XGS::RECOMMENDED and XGS::differing; Api/SettingsController.php), with PHP
and no OPNsense: every recommended value is the model's own default in XGS.xml, so the card lists only
what a new install would have differently; a saved value that differs is listed with whether it waits
for a reboot, one with nothing saved is not; and the controller parses.

    python3 tools/test-recommended.py"""
import json
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MVC = os.path.join(ROOT, 'src', 'opnsense', 'mvc', 'app')
MODEL = os.path.join(MVC, 'models', 'OPNsense', 'XGS', 'XGS.php')
XML = os.path.join(MVC, 'models', 'OPNsense', 'XGS', 'XGS.xml')
CONTROLLER = os.path.join(MVC, 'controllers', 'OPNsense', 'XGS', 'Api', 'SettingsController.php')
HARNESS = r'''
namespace OPNsense\Base { class BaseModel {} }
namespace {
    include $argv[1];
    $c = json_decode($argv[2], true);
    echo json_encode($c === null ? \OPNsense\XGS\XGS::RECOMMENDED : \OPNsense\XGS\XGS::differing($c));
}
'''

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def php(arg):
    out = subprocess.run(['php', '-r', HARNESS, '--', MODEL, json.dumps(arg)], capture_output=True, text=True)
    if out.returncode != 0:
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:300]
    return json.loads(out.stdout)


if shutil.which('php') is None:
    print('FAIL php is not installed')
    sys.exit(1)

rec = php(None)
general = ET.parse(XML).getroot().find('items/general')
check('every recommended value is the model\'s default',
      {f: v['value'] for f, v in rec.items()},
      {f: (general.findtext(f + '/Default') or '').strip() for f in rec})
check('the ones that wait for a reboot', sorted(f for f, v in rec.items() if v['reboot']),
      ['inspect_then_hardware', 'ipsec_flows', 'ipsec_offload'])

check('a new install: nothing differs', php({f: '1' for f in rec}), [])
check('nothing saved: nothing differs', php({}), [])
check('an appliance from before: what is off is listed, in order, with its reboot',
      [(d['field'], d['now'], d['reboot']) for d in php({'offload': '0', 'ipsec_offload': '0', 'failclose': '0',
                                                         'flow_auto': '1'})],
      [('offload', '0', False), ('failclose', '0', False), ('ipsec_offload', '0', True)])

lint = subprocess.run(['php', '-l', CONTROLLER], capture_output=True, text=True)
check('SettingsController.php parses', lint.returncode, 0)

print('all passed' if not failures else '%d failed' % failures)
sys.exit(1 if failures else 0)
