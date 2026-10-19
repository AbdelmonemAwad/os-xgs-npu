#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The parts of scripts/xgs/ja4_cut.py that decide which connections are cut, on Suricata's and pf's
own output.

    python3 tools/test-ja4-cut.py

A state wrongly matched is a connection cut for nothing, and one missed stays open with a listed
fingerprint; both are decided here, from eve.json lines and `pfctl -ss -vv` text, without Suricata or pf.
"""
import importlib.util
import os
import shutil
import sys
import tempfile
import time
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'ja4_cut', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'ja4_cut.py'))
jc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(jc)

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


CURL = 't13i2011h1_2b729b4bf6f3_36bf25f296df'
OTHER = 't13d1516h2_8daaf6152771_e5627efa2ab1'

for fp in (CURL, OTHER, 'q13d0312h3_55b375c5d22e_06cda9e17597', 't12d1209h1_d34a8e72043a_b39be8c56a14'):
    check(f'a JA4 fingerprint: {fp}', bool(jc.JA4.match(fp)), True)
for bad in ('t13i2011h1_2b729b4bf6f3', 'T13i2011h1_2b729b4bf6f3_36bf25f296df', 't13x2011h1_2b729b4bf6f3_36bf25f296df',
            't13i2011h1_2b729b4bf6f3_36bf25f296dz', '771,4865-4866,0-23,29-23,0'):
    check(f'not a JA4 fingerprint: {bad}', bool(jc.JA4.match(bad)), False)


def eve(ts, src, sport, dst, dport, fp=None, kind='tls', proto='TCP'):
    tls = '{"ja4":"%s"}' % fp if fp else '{"version":"TLS 1.3"}'
    return ('{"timestamp":"%s","flow_id":1,"in_iface":"oxp0","event_type":"%s","src_ip":"%s","src_port":%d,'
            '"dest_ip":"%s","dest_port":%d,"proto":"%s","tls":%s}\n' % (ts, kind, src, sport, dst, dport, proto, tls))


T0 = '2026-10-19T03:03:00.641496+0000'
t0 = jc.stamp(T0)
check("Suricata's timestamp read", t0,
      datetime(2026, 10, 19, 3, 3, 0, 641496, tzinfo=timezone.utc).timestamp())
check('a timestamp that is not one', jc.stamp('yesterday'), None)

# pfctl -ss -vv as an OPNsense appliance prints it: the connection 192.168.1.20:51515 -> 203.0.113.7:443
# has a state on each interface it crosses, the second translated; another connection to the same
# server; and one on the same ports made after the log line - a later connection, never cut for it.
SS = """all tcp 203.0.113.7:443 <- 192.168.1.20:51515       ESTABLISHED:ESTABLISHED
   [2628974272 + 65187] wscale 7  [590325212 + 65792] wscale 8
   age 00:00:57, expires in 24:00:00, 23:33 pkts, 3561:6023 bytes, rule 96
   id: 352ad56a00000000 creatorid: 41397420
   origif: oxp0
all tcp 198.51.100.1:31001 (192.168.1.20:51515) -> 203.0.113.7:443       ESTABLISHED:ESTABLISHED
   age 00:00:57, expires in 24:00:00, 120:118 pkts, 90123:1201 bytes, rule 51
   id: 452ad56a00000000 creatorid: 41397420
all tcp 203.0.113.7:443 <- 192.168.1.21:40000       ESTABLISHED:ESTABLISHED
   age 00:00:54, expires in 24:00:00, 10:10 pkts, 1000:1000 bytes, rule 96
   id: 552ad56a00000000 creatorid: 41397420
all tcp 2001:db8:2::2[443] <- 2001:db8:3::2[49812]       ESTABLISHED:ESTABLISHED
   age 01:00:12, expires in 24:00:00, 10:10 pkts, 1000:1000 bytes, rule 90
   id: 652ad56a00000000 creatorid: 41397420
all udp 203.0.113.7:443 <- 192.168.1.20:51515       MULTIPLE:MULTIPLE
   age 00:00:57, expires in 00:00:30, 4:4 pkts, 400:400 bytes, rule 96
   id: 752ad56a00000000 creatorid: 41397420
"""
st = jc.states(SS)
check('five states read', len(st), 5)
check('both ends of a state, with ports', st[0][2], {('203.0.113.7', 443), ('192.168.1.20', 51515)})
check('a translated state also carries the end it was translated from', st[1][2],
      {('198.51.100.1', 31001), ('192.168.1.20', 51515), ('203.0.113.7', 443)})
check('IPv6 ends, the port in brackets', st[3][2], {('2001:db8:2::2', 443), ('2001:db8:3::2', 49812)})
check("a state's age in seconds", (st[0][3], st[3][3]), (57, 3612))
check('the protocol', (st[0][1], st[4][1]), ('tcp', 'udp'))

# the connection's log line came 2 seconds after its states were made (the ClientHello follows the
# handshake); "now" is 55 seconds after the line
now = t0 + 55
ev = {'ja4': CURL, 'at': t0, 'proto': 'tcp', 'src': ('192.168.1.20', 51515), 'dst': ('203.0.113.7', 443)}
check("both of the connection's states, on each interface, and not the UDP one on the same ports",
      sorted(jc.to_cut([ev], st, now)), ['352ad56a00000000', '452ad56a00000000'])
later = dict(ev, at=t0 - 30)    # the line is older than these states: they are a later connection's
check('states made after the log line are a later connection on the same ports',
      sorted(jc.to_cut([later], st, now)), [])
ev6 = {'ja4': OTHER, 'at': t0 - 3559, 'proto': 'tcp', 'src': ('2001:db8:3::2', 49812), 'dst': ('2001:db8:2::2', 443)}
check('an IPv6 connection', sorted(jc.to_cut([ev6], st, now)), ['652ad56a00000000'])
check('no event, nothing cut', jc.to_cut([], st, now), {})

# A whole Watch against files of its own and a pf that answers from SS and records what it is asked
# to kill.
work = tempfile.mkdtemp()
try:
    jc.CONFIG = os.path.join(work, 'config.xml')
    jc.EVE = os.path.join(work, 'eve.json')
    jc.STATE_DIR = work
    jc.STATUS = os.path.join(work, 'status.json')
    logged, killed = [], []
    jc.log = logged.append

    class Done:
        def __init__(self, out='', rc=0):
            self.stdout, self.returncode = out, rc

    def fake_run(cmd, **kw):
        if cmd[1:] == ['-ss', '-vv']:
            return Done(SS)
        if cmd[1:3] == ['-k', 'id']:
            killed.append(cmd[4])
            return Done()
        return Done('', 1)
    jc.subprocess.run = fake_run
    real_time = time.time
    jc.time.time = lambda: now

    def config(listed, ids=True, tls=True, ja4=True):
        with open(jc.CONFIG, 'w', encoding='utf-8') as fh:
            fh.write('<opnsense><OPNsense><XGS><general><cut_ja4>%s</cut_ja4></general></XGS>'
                     '<IDS><general><enabled>%d</enabled><eveLog><tls><enable>%d</enable><custom>%s</custom></tls>'
                     '</eveLog></general></IDS></OPNsense></opnsense>'
                     % (listed, ids, tls, 'ja4' if ja4 else ''))
        t = os.stat(jc.CONFIG).st_mtime + 1
        os.utime(jc.CONFIG, (t, t))

    config(CURL + '\n' + 'not-one, ' + CURL)
    check('the listed fingerprints, once each, and what is not one',
          jc.settings()[0::2], ([CURL], ['not-one']))
    config(CURL, ids=False)
    check('Intrusion Detection off is said', jc.settings()[1], 'Intrusion Detection is off')
    config(CURL, ja4=False)
    check('a TLS log without JA4 is said', jc.settings()[1], "the Intrusion Detection's TLS log has no JA4")

    with open(jc.EVE, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(eve(T0, '192.168.1.20', 51515, '203.0.113.7', 443, CURL))
        fh.write(eve(T0, '192.168.1.21', 40000, '203.0.113.7', 443, OTHER))
        fh.write(eve(T0, '192.168.1.22', 40001, '203.0.113.7', 443))                 # no JA4
        fh.write(eve(T0, '192.168.1.23', 40002, '203.0.113.7', 443, CURL, kind='alert'))
    config('')
    w = jc.Watch()
    s = w.once()
    check('nothing listed: nothing read, nothing cut', (s['read'], s['cut'], killed), (0, 0, []))

    config(CURL)
    s = w.once()
    check('a fingerprint newly listed is looked for in the whole log, and its connection cut',
          (s['read'], s['matched'], sorted(killed)), (2, 1, ['352ad56a00000000', '452ad56a00000000']))
    check('the XGS log names the fingerprint and the connection', logged[-1],
          CURL + ': 192.168.1.20:51515 -> 203.0.113.7:443 cut (2 states)')
    check('the message', w.message(dict(s)), 'cut by TLS fingerprint: 1 fingerprint listed, 1 connection cut now, 1 in all')

    killed.clear()
    s = w.once()
    check('the next reading starts where the last stopped: nothing again', (s['read'], killed), (0, []))

    with open(jc.EVE, 'a', encoding='utf-8', newline='\n') as fh:
        fh.write(eve(T0, '192.168.1.21', 40000, '203.0.113.7', 443, CURL))
        fh.write(eve(T0, '192.168.1.24', 40003, '203.0.113.7', 443, CURL)[:60])     # still being written
    s = w.once()
    check('a line added since is read; a line not yet ended is left for the next reading',
          (s['read'], killed), (1, ['552ad56a00000000']))

    killed.clear()
    config(CURL + ',' + OTHER)
    s = w.once()
    check("a second fingerprint listed: its older line is read again, the first's are not",
          (s['matched'], killed), (1, ['552ad56a00000000']))

    killed.clear()
    os.remove(jc.EVE)
    with open(jc.EVE, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('x' * 10 + '\n')
        fh.write(eve(T0, '2001:db8:3::2', 49812, '2001:db8:2::2', 443, OTHER, proto='TCP'))
    w.status['offset'] = 10 ** 6    # a log rotated: smaller than where the last reading stopped
    s = w.once()
    check('a rotated log is read from its start', killed, ['652ad56a00000000'])
    check('an IPv6 connection in the XGS log', logged[-1],
          OTHER + ': [2001:db8:3::2]:49812 -> [2001:db8:2::2]:443 cut (1 state)')

    w.save()
    check('the cut connections counted by fingerprint', jc.load(jc.STATUS, {})['totals'], {CURL: 2, OTHER: 2})
    check('the fingerprints seen', [r['ja4'] for r in jc.seen()['seen']], [OTHER])
    jc.time.time = real_time
finally:
    shutil.rmtree(work, ignore_errors=True)

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
