#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/health.py - the samples kept for the Overview's graphs (2026-11-14): one point per
read, [time, load, memory in use in per cent, connections in hardware], seven days of them, a failed
read a point with no load and no memory. The readings are made up; nothing here asks an appliance.

    python3 tools/test-health.py
"""
import importlib.util
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'health', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'health.py'))
hl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hl)

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


READ = '1234.5 4800.1\n3.20 2.90 2.70 2/180 4242\nMemTotal: 3000000 kB\nMemAvailable: 1500000 kB\n4\n'
answer = dict(hl.parse(READ), at=1000)
check('a read: load and memory', (answer['load'][0], answer['mem_total_kb']), (3.2, 3000000))
check('a sample: time, load, memory in use, connections', hl.sample(answer, 42), [1000, 3.2, 50.0, 42])
check('a failed read: a gap, not a zero', hl.sample({'at': 1300, 'error': 'silent'}, None), [1300, None, None, None])

with tempfile.TemporaryDirectory() as d:
    hl.HISTORY = os.path.join(d, 'health-history.json')
    check('nothing kept yet', hl.history(), [])
    hl.KEEP = 3
    for t in (100, 200, 300, 400):
        hl.keep([t, 1.0, 10.0, t])
    check('the oldest let go past the limit', [p[0] for p in hl.history()], [200, 300, 400])
    hl.keep([350, 9.0, 9.0, 9])
    check('a sample older than the last (a clock set back) starts again from it', [p[0] for p in hl.history()],
          [200, 300, 350])
    with open(hl.HISTORY, 'w', encoding='utf-8') as f:
        f.write('not json')
    check('a broken file read as nothing kept', hl.history(), [])

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
