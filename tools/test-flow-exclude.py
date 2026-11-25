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

And the minute's `--if-changed` run while a firmware update replaces OPNsense's files (#1219): it
leaves quietly, writing nothing, while OPNsense's firmware lock is held or config.inc cannot be found,
where it once stopped in require_once with a PHP fatal the problem reporter sent; without an argument
a missing config.inc is still a failure. Each run is given an include path of its own, with no
config.inc or with a stand-in, and the lock is taken here as OPNsense's firmware launcher takes it.

    python3 tools/test-flow-exclude.py"""
import fcntl
import json
import os
import shutil
import subprocess
import sys
import tempfile

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

# The minute's run during a firmware update (#1219). OPNsense's lock, as scripts/firmware/config.sh
# names it; taken here without waiting, as launcher.sh takes it, and the file left as it was found.
LOCK = '/tmp/pkg_upgrade.progress'


def run(args, inc):
    """flow_exclude.php with the include path inc and nothing else to find config.inc in."""
    return subprocess.run(['php', '-d', 'include_path=' + inc, '-d', 'display_errors=stderr', '-d', 'log_errors=0',
                           SCRIPT] + args, capture_output=True, text=True, cwd=inc)


def holding_lock(args, inc):
    made = not os.path.exists(LOCK)
    fd = os.open(LOCK, os.O_RDONLY | os.O_CREAT, 0o644)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return run(args, inc)
    finally:
        os.close(fd)
        if made:
            os.unlink(LOCK)


with tempfile.TemporaryDirectory() as tmp:
    empty = os.path.join(tmp, 'empty')
    stub = os.path.join(tmp, 'stub')
    os.makedirs(empty)
    os.makedirs(stub)
    with open(os.path.join(stub, 'config.inc'), 'w') as f:
        f.write("<?php\n$config = ['OPNsense' => ['XGS' => ['general' => ['flow_exclude' => '192.0.2.1']]]];\n")

    out = run(['--if-changed'], empty)
    check('--if-changed, config.inc away: leaves quietly', (out.returncode, out.stdout, out.stderr), (0, '', ''))
    out = holding_lock(['--if-changed'], stub)
    check('--if-changed, the firmware lock held: leaves quietly', (out.returncode, out.stdout, out.stderr),
          (0, '', ''))
    out = run(['--if-changed'], stub)
    got = json.loads(out.stdout) if out.returncode == 0 and out.stdout else {}
    check('--if-changed, the lock free: goes on as before', got.get('entries'), ['192.0.2.1'])
    out = holding_lock([], stub)
    got = json.loads(out.stdout) if out.returncode == 0 and out.stdout else {}
    check('no argument, the lock held: does not wait', got.get('entries'), ['192.0.2.1'])
    out = run([], empty)
    check('no argument, config.inc away: still a failure', out.returncode != 0 and 'config.inc' in out.stderr, True)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
