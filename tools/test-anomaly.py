#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The parts of scripts/xgs/anomaly.py that decide what is unusual, on pf's own output.

    python3 tools/test-anomaly.py

An alert for nothing teaches its reader to ignore the next one, and one missed is a scan nobody saw;
both are decided here, from `pfctl -ss -vv` text and windows of numbers, without pf.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'anomaly', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'anomaly.py'))
an = importlib.util.module_from_spec(spec)
spec.loader.exec_module(an)

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


def state(sid, proto, src, dst, age, b0, b1, arrow='<-', nat=''):
    head = (f'all {proto} {dst}{nat} {arrow} {src}       ESTABLISHED:ESTABLISHED' if arrow == '<-'
            else f'all {proto} {src} {arrow} {dst}       ESTABLISHED:ESTABLISHED')
    return (head + '\n'
            f'   age 00:{age // 60:02d}:{age % 60:02d}, expires in 23:59:00, 10:10 pkts, {b0}:{b1} bytes, rule 7\n'
            f'   id: {sid} creatorid: 41397420\n')


# A connection routed through the firewall has a state on each interface: the "<-" one is read.
SS = (state('a1', 'tcp', '192.168.1.20:51515', '203.0.113.7:443', 30, 1000, 5000)
      + state('a2', 'tcp', '198.51.100.1:31001', '203.0.113.7:443', 30, 1000, 5000, arrow='->')
      + state('a3', 'tcp', '2001:db8:3::2[49812]', '2001:db8:2::2[443]', 12, 10, 20)
      + state('a4', 'tcp', '198.51.100.9:4444', '192.168.1.10:80', 5, 1, 2, nat=' (203.0.113.5:8080)'))
st = an.inbound_states(SS)
check('the inbound states only, each connection once', sorted(st), ['a1', 'a3', 'a4'])
check('the opener and where it went', (st['a1']['src'], st['a1']['dst'], st['a1']['dport']),
      ('192.168.1.20', '203.0.113.7', 443))
check('bytes from the opener and back, and the age', (st['a1']['bytes'], st['a1']['age']), ([1000, 5000], 30))
check('IPv6', (st['a3']['src'], st['a3']['dst'], st['a3']['dport']), ('2001:db8:3::2', '2001:db8:2::2', 443))
check('a translated destination is read as it came in on the wire', (st['a4']['dst'], st['a4']['dport']),
      ('203.0.113.5', 8080))

# A window: one connection that carried on, one that is new, one older than the window but unread
before = an.inbound_states(state('b1', 'tcp', '192.168.1.20:1000', '203.0.113.7:443', 100, 1000, 2000))
after = an.inbound_states(state('b1', 'tcp', '192.168.1.20:1000', '203.0.113.7:443', 150, 51000, 92000)
                          + state('b2', 'tcp', '192.168.1.20:1001', '203.0.113.8:443', 20, 300, 400)
                          + state('b3', 'tcp', '192.168.1.20:1002', '203.0.113.9:443', 3600, 7, 7))
f = an.window(before, after, 50)['192.168.1.20']
check('what moved in the window, and what the new connection carried',
      (f['sent'], f['received'], f['opened'], f['new_addresses']), (50000 + 300, 90000 + 400, 1, 1))

# A sweep made early in a window: its states are gone by the end, but a reading in between saw them
early = an.inbound_states(''.join(state('e%04x' % i, 'tcp', '192.168.1.68:%d' % (20000 + i), '10.0.0.%d:445' % (i + 1),
                                        12, 60, 0) for i in range(5)))
late = an.inbound_states(state('f001', 'tcp', '192.168.1.68:29999', '10.0.0.200:445', 2, 60, 0))
f = an.window({}, an.union([{}, early, late]), 50)
check('states seen by any reading count, as each was last seen', f['192.168.1.68']['new_addresses'], 6)

# A port scan: one host, 150 ports on one address, the states closed but still there (ids are hex, as pf's)
scan = ''.join(state('c%04x' % p, 'tcp', '192.168.1.66:%d' % (40000 + p), '192.168.1.10:%d' % p, 10, 60, 40)
               for p in range(1, 151))
f = an.window({}, an.inbound_states(scan), 50)
check('a port scan is seen', (f['192.168.1.66']['scan_ports'], f['192.168.1.66']['scan_dst']),
      (150, '192.168.1.10'))
alerts = an.judge(f, {}, 1000)
check('and reported at once, with no history needed', [(h, k) for h, k, _ in alerts], [('192.168.1.66', 'scan')])

# A sweep: 300 addresses
sweep = ''.join(state('d%04x' % i, 'tcp', '192.168.1.67:%d' % (30000 + i), '10.0.%d.%d:445' % (i // 250, i % 250 + 1),
                      10, 60, 0) for i in range(300))
alerts = an.judge(an.window({}, an.inbound_states(sweep), 50), {}, 1000)
check('a sweep is reported', [(h, k) for h, k, _ in alerts], [('192.168.1.67', 'sweep')])

# Volumes: a host that sends about 1 MB a window, then 2 GB
baseline = {}
quiet = {'192.168.1.30': {'sent': 1e6, 'received': 2e6, 'opened': 10, 'new_addresses': 2, 'scan_dst': None,
                          'scan_ports': 1}}
for i in range(3):
    alerts = an.judge(quiet, baseline, 1000 + i, warmup=3)
check('nothing while it learns', alerts, [])
loud = {'192.168.1.30': dict(quiet['192.168.1.30'], sent=2e9)}
alerts = an.judge(loud, dict((k, dict(v)) for k, v in baseline.items()), 2000, warmup=3)
check('2 GB where 1 MB is usual is reported', [(h, k) for h, k, _ in alerts], [('192.168.1.30', 'sent')])
check('in words', alerts[0][2], '192.168.1.30 sent 2.0 GB in a minute, where it usually sends 1.0 MB')
alerts = an.judge(loud, dict((k, dict(v)) for k, v in baseline.items()), 2000, warmup=10)
check('not before the host has history enough', alerts, [])
small = {'192.168.1.30': dict(quiet['192.168.1.30'], sent=100e6)}
alerts = an.judge(small, dict((k, dict(v)) for k, v in baseline.items()), 2000, warmup=3)
check('not below the floor, however unusual for the host', alerts, [])

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
