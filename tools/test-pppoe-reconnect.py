#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The scheduled PPPoE reconnect (scripts/xgs/pppoe_reconnect.php), with PHP and no OPNsense: the
interfaces it configures again - the enabled PPPoE ones, and nothing else - and the time of day the
setting holds, as the minute and hour of the cron job xgs_cron() gives; anything that is no time, none.

    python3 tools/test-pppoe-reconnect.py"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'pppoe_reconnect.php')
HARNESS = r'''
putenv('XGS_PPPOE_FUNCTIONS_ONLY=1');
include $argv[1];
$c = json_decode($argv[2], true);
echo json_encode(isset($c['at']) ? xgs_pppoe_reconnect_when($c['at']) : xgs_pppoe_interfaces($c['interfaces']));
'''

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def php(case):
    out = subprocess.run(['php', '-r', HARNESS, '--', SCRIPT, json.dumps(case)], capture_output=True, text=True)
    if out.returncode != 0:
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:300]
    return json.loads(out.stdout)


if shutil.which('php') is None:
    print('FAIL php is not installed, so the PPPoE reconnect cannot be checked')
    sys.exit(1)

INTERFACES = {
    'wan': {'if': 'pppoe0', 'ipaddr': 'pppoe', 'enable': '1'},
    'opt1': {'if': 'pppoe1', 'ipaddr': 'pppoe'},
    'opt2': {'if': 'oxp4', 'ipaddr': 'dhcp', 'enable': '1'},
    'lan': {'if': 'oxp5', 'ipaddr': '10.0.0.1', 'enable': '1'},
}
check('the enabled PPPoE interfaces, and no other', php({'interfaces': INTERFACES}), ['wan'])
check('none configured: none', php({'interfaces': {}}), [])
check('03:30 is minute 30 of hour 3', php({'at': '03:30'}), ['30', '3'])
check('00:00 is minute 0 of hour 0', php({'at': '00:00'}), ['0', '0'])
check('23:59 is minute 59 of hour 23', php({'at': ' 23:59 '}), ['59', '23'])
check('empty: no job', php({'at': ''}), None)
check('24:00: no job', php({'at': '24:00'}), None)
check('3:30, one hour digit: no job, as the page refuses it', php({'at': '3:30'}), None)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
