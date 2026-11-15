#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The gateway quality alerts (scripts/xgs/gw_probe.py), without OPNsense: dpinger's "1.2 ms" and "~" read,
a jitter alert raised once when a gateway crosses the limit and once when it is back under - not each
minute it stays over - the TCP probe's alert at the third failure in a row and at the first success after
them, and the probe's target as host:port or [IPv6]:port.

    python3 tools/test-gw-probe.py"""
import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location('gw_probe', os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'gw_probe.py'))
gw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gw)

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


check("dpinger's 1.2 ms", gw.ms('1.2 ms'), 1.2)
check('an unmonitored gateway\'s "~": nothing read', gw.ms('~'), None)

turns, state = gw.jitter_turns({}, {'WAN': 12.5, 'LAN': 0.4, 'OFF': None}, 10)
check('over the limit: one alert, the gateway under it none, the unmonitored one skipped',
      [turns, state], [[('WAN', 'over', 12.5)], {'LAN': False, 'WAN': True}])
turns, state = gw.jitter_turns(state, {'WAN': 15.0, 'LAN': 0.4}, 10)
check('still over a minute later: no second alert', turns, [])
turns, state = gw.jitter_turns(state, {'WAN': 3.0, 'LAN': 0.4}, 10)
check('back under: one alert', turns, [('WAN', 'back', 3.0)])

s = {}
seen = []
for ok in (False, False, False, False, True, True):
    turn, s = gw.probe_turn(s, ok)
    seen.append(turn)
check('the probe: an alert at the third failure in a row, none at the fourth, one at the first success',
      seen, [None, None, 'down', None, 'back', None])
turn, s = gw.probe_turn({'fails': 2, 'down': False}, True)
check('a success before the third failure: no alert, the count back to 0', [turn, s], [None, {'fails': 0, 'down': False}])

# One look as the cron job makes it (main), with the configuration, the connection and the alert stubbed and
# the state in a folder of its own: a target changed in the middle of a run of failures starts its own count,
# so the failures of the one before raise no alert for it.
import io
import json
import tempfile
import contextlib
tmp = tempfile.mkdtemp(prefix='gw-probe-test-')
gw.STATE = os.path.join(tmp, 'gw-probe.json')
raised = []
gw.alert = lambda kind, subject, text, facts: raised.append(subject)
gw.connect_ms = lambda host, port, timeout=3.0: None
settings = {'gw_jitter_alert': '0', 'gw_tcp_probe': 'one.example.net:22'}
gw.setting = lambda name: settings.get(name, '')
with contextlib.redirect_stdout(io.StringIO()):
    gw.main()
    gw.main()
    settings['gw_tcp_probe'] = 'one.example.net:9'
    gw.main()
    gw.main()
with open(gw.STATE, encoding='utf-8') as f:
    check('a new target starts its own count: two failures on :22, then two on :9 - no alert yet',
          [json.load(f)['probe'], raised], [{'fails': 2, 'down': False, 'target': 'one.example.net:9'}, []])
with contextlib.redirect_stdout(io.StringIO()):
    gw.main()
check('the third failure on the new target raises the alert', raised, ['TCP probe to one.example.net:9 failing'])
import shutil
shutil.rmtree(tmp, ignore_errors=True)

check('host:port', gw.target('one.example.net:443'), ('one.example.net', 443))
check('[IPv6]:port', gw.target('[2001:db8::1]:853'), ('2001:db8::1', 853))
check('no port: none', gw.target('one.example.net'), None)
check('port 0: none', gw.target('one.example.net:0'), None)
check('empty: none', gw.target(''), None)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
