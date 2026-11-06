#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/flow_exclude.php's reading of a "Never offloaded" list, with PHP and no driver: what it
would write to the driver. A list longer than the 64 entries both drivers keep is everything kept with
the host - until 2026-11-06 it was refused whole, and the driver, which starts every boot with no
exclusion, offloaded the listed addresses with the rest.

Only where no driver is attached - CI, a workstation: on an appliance the script WRITES each list it is
given to the driver (run there on 2026-11-06, it left a test list on an XGS 136 until the configured
one was written back with a plain `flow_exclude.php`). So it refuses to run where one is.

    python3 tools/test-flow-exclude.py"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'flow_exclude.php')
EVERYTHING = 'a4:00000000/0 a6:' + '0' * 32 + '/0'

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def read(listing):
    out = subprocess.run(['php', SCRIPT, listing], capture_output=True, text=True)
    if out.returncode != 0:
        return {'php failed': (out.stderr or out.stdout).strip()[:200]}
    return json.loads(out.stdout)


if shutil.which('php') is None:
    print('FAIL php is not installed, so flow_exclude.php cannot be checked')
    sys.exit(1)
for oid in ('dev.octep.0.dp.flow_exclude', 'dev.npuep.0.flow.exclude'):
    if shutil.which('sysctl') and subprocess.run(['sysctl', '-n', oid], capture_output=True).returncode == 0:
        print('FAIL %s is here: this test would write its lists to the running driver' % oid)
        sys.exit(1)

addresses = ['198.51.100.%d' % i for i in range(1, 66)]

got = read(', '.join(addresses[:64]))
check('64 entries: every one kept', len(got.get('entries', [])), 64)
check('64 entries: written as they are', got.get('canonical', '').startswith('a4:c6336401/32 '), True)

got = read(', '.join(addresses))
check('65 entries: everything kept with the host', got.get('canonical'), EVERYTHING)
check('65 entries: and said why', 'more than the 64 entries the driver keeps - everything' in got.get('ipfw', []),
      True)

got = read('192.0.2.0/24, port 22, 2001:db8::/32, not-an-address')
check('a short list: read entry by entry', got.get('entries'), ['192.0.2.0/24', 'port 22', '2001:db8::/32'])
check('a short list: what cannot be read is named', got.get('refused'), ['not-an-address'])

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
