#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/assign.php's choice of the MGMT device, case by case, with PHP and no OPNsense: what
the installer made `lan`, the host NIC by its name after a factory reset (issue 957), and nothing
taken that someone else has.

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

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
