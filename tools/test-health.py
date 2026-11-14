#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/health.py, away from the appliance: both things one collect does every five minutes.

- The samples kept for the Overview's graphs (2026-11-14): one point per read, [time, load, memory in
  use in per cent, connections in hardware], seven days of them, a failed read a point with no load
  and no memory.
- The memory error counters and their alert (hw-10): its EDAC line read, the alert raised at XGS:
  Settings' threshold and at an error not corrected, counted again from each alert and from each start
  of the coprocessor, said in the system log and written for os-netreport.

Every reading here is made up; nothing here asks an appliance.

    python3 tools/test-health.py
"""

import importlib.util
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'health.py')

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def load(tmp, setting=None):
    spec = importlib.util.spec_from_file_location('health_under_test', TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.STATE = os.path.join(tmp, 'health.json')
    module.HISTORY = os.path.join(tmp, 'health-history.json')
    module.ALERTED = os.path.join(tmp, 'ecc-alert.json')
    module.OUTBOX = os.path.join(tmp, 'alerts')
    module.CONFIG = os.path.join(tmp, 'config.xml')
    with open(module.CONFIG, 'w', encoding='utf-8') as handle:
        handle.write('<opnsense><OPNsense><XGS><general>%s</general></XGS></OPNsense></opnsense>' % (
            '' if setting is None else '<ecc_alert>%s</ecc_alert>' % setting))
    module.said = []
    module.log = lambda text: module.said.append(text)
    return module


def reading(at, uptime, ce, ue, counters=19):
    return {'at': at, 'uptime_s': uptime, 'edac': {'counters': counters, 'corrected': ce, 'uncorrected': ue}}


def outbox(m):
    return sorted(os.listdir(m.OUTBOX)) if os.path.isdir(m.OUTBOX) else []


TEXT = '\n'.join(['12345.67 40000.00', '3.20 3.10 3.00 5/200 1234', 'MemTotal:  3000000 kB',
                  'MemAvailable: 1500000 kB', '4', 'edac 19 7 0'])

# the graphs' history
with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp)
    answer = dict(m.parse(TEXT), at=1000)
    check('a read: load and memory', (answer['load'][0], answer['mem_total_kb']), (3.2, 3000000))
    check('a sample: time, load, memory in use, connections', m.sample(answer, 42), [1000, 3.2, 50.0, 42])
    check('a failed read: a gap, not a zero', m.sample({'at': 1300, 'error': 'silent'}, None),
          [1300, None, None, None])
    check('nothing kept yet', m.history(), [])
    m.KEEP = 3
    for t in (100, 200, 300, 400):
        m.keep([t, 1.0, 10.0, t])
    check('the oldest let go past the limit', [p[0] for p in m.history()], [200, 300, 400])
    m.keep([350, 9.0, 9.0, 9])
    check('a sample older than the last (a clock set back) starts again from it', [p[0] for p in m.history()],
          [200, 300, 350])
    with open(m.HISTORY, 'w', encoding='utf-8') as f:
        f.write('not json')
    check('a broken file read as nothing kept', m.history(), [])

# the memory error counters and their alert
with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp, None)
    found = m.parse(TEXT)
    check('the EDAC line read beside the rest', (found['edac'], found['cpus'], found['uptime_s']),
          ({'counters': 19, 'corrected': 7, 'uncorrected': 0}, 4, 12345))
    check('no counters registered (the ARMADA family): counted as none', m.parse('edac 0 0 0')['edac']['counters'], 0)
    check('the setting not saved yet: the default', m.threshold(), 10)

with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp, '5')
    check('below the threshold: no alert', m.ecc_alert(reading(100, 1000, 4, 0)), None)
    alert = m.ecc_alert(reading(200, 1100, 5, 0))
    check('at the threshold: an alert', (alert['kind'], alert['facts']['corrected']), ('coprocessor-ecc', 5))
    check('said in the system log', len(m.said), 1)
    check('and written for os-netreport', outbox(m), ['coprocessor-ecc-200.json'])
    with open(os.path.join(m.OUTBOX, 'coprocessor-ecc-200.json'), encoding='utf-8') as handle:
        check('with its source', json.load(handle)['source'], 'os-xgs-npu')
    check('counted again from the alert', m.ecc_alert(reading(300, 1200, 9, 0)), None)
    check('until the threshold again', m.ecc_alert(reading(400, 1300, 10, 0))['facts']['corrected'], 10)
    alert = m.ecc_alert(reading(500, 1400, 10, 1))
    check('an error not corrected: an alert whatever the threshold', 'not corrected' in alert['subject'], True)
    check('and only once', m.ecc_alert(reading(600, 1500, 10, 1)), None)
    check('the coprocessor started again: counted from zero, no alert below the threshold',
          m.ecc_alert(reading(700, 60, 2, 0)), None)
    check('and an alert at the threshold from zero', m.ecc_alert(reading(800, 160, 5, 0)) is not None, True)

with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp, '0')
    check('0: no alert for corrected errors', m.ecc_alert(reading(100, 1000, 500, 0)), None)
    check('but one for an error not corrected', m.ecc_alert(reading(200, 1100, 500, 1)) is not None, True)

with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp, '1')
    check('no counters on this model: nothing to alert on', m.ecc_alert(reading(100, 1000, 0, 0, counters=0)), None)
    check('nothing written', outbox(m), [])


# one collect does both: the reading kept, a graph sample kept, the alert raised
class Done:
    def __init__(self, stdout, returncode=0):
        self.stdout, self.stderr, self.returncode = stdout, '', returncode


def fake_run(argv, **kwargs):
    if argv[0] == '/sbin/sysctl':
        return Done('42\n')
    return Done(TEXT.replace('edac 19 7 0', 'edac 19 6 0'))


with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp, '5')
    real = m.subprocess.run
    m.subprocess.run = fake_run
    try:
        answer = m.collect()
    finally:
        m.subprocess.run = real
    check('collect: the reading kept for the page', m.show().get('edac'),
          {'counters': 19, 'corrected': 6, 'uncorrected': 0})
    check('collect: a graph sample kept with the connections in hardware', [p[1:] for p in m.history()],
          [[3.2, 50.0, 42]])
    check('collect: the alert raised and its time on the page', (outbox(m), answer.get('ecc_alert_at')),
          (['coprocessor-ecc-%d.json' % answer['at']], answer['at']))

print('all passed' if not failures else '%d failed' % failures)
sys.exit(1 if failures else 0)
