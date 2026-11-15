#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The ready-made service aliases (scripts/xgs/service_aliases.php), with PHP and no OPNsense: each name
one Firewall: Aliases takes - letters, digits and "_", at most 32 - each port a port; and which of them a
save adds, given the names Firewall: Aliases has: those missing, and an alias of the same name in any case
kept as it is.

    python3 tools/test-service-aliases.py"""
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'service_aliases.php')
HARNESS = r'''
putenv('XGS_ALIASES_FUNCTIONS_ONLY=1');
include $argv[1];
$existing = json_decode($argv[2], true);
[$add, $kept] = xgs_service_aliases_missing($existing);
echo json_encode(['all' => xgs_service_aliases(), 'add' => array_keys($add), 'kept' => $kept]);
'''

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def run(existing):
    out = subprocess.run(['php', '-r', HARNESS, '--', SCRIPT, json.dumps(existing)], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.startswith('{'):
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:300]
    return json.loads(out.stdout)


if shutil.which('php') is None:
    print('FAIL php is not installed, so the service aliases cannot be checked')
    sys.exit(1)

r = run([])
if isinstance(r, dict):
    names = list(r['all'].keys())
    check('every name is one Firewall: Aliases takes', [n for n in names if not re.match(r'^[A-Za-z0-9_]{1,32}$', n)], [])
    check('every port is a port', [(n, p) for n, (ports, _) in r['all'].items() for p in ports
                                   if not (p.isdigit() and 1 <= int(p) <= 65535)], [])
    check('each has its ports and what they are', all(len(v) == 2 and v[0] and v[1] for v in r['all'].values()), True)
    check('XGS_WEB is 80 and 443', r['all']['XGS_WEB'][0], ['80', '443'])
    check('none there: all added', r['add'], names)
    r2 = run(['xgs_web', 'XGS_DNS', 'MyAlias'])
    check('those there, in any case, kept', [r2['kept'], 'XGS_WEB' in r2['add'], 'XGS_DNS' in r2['add']],
          [['XGS_WEB', 'XGS_DNS'], False, False])
    check('all there: nothing added', run(names)['add'], [])
else:
    check('the script runs', r, 'a result')

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
