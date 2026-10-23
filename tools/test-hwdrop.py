#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/hwdrop.py - which connections in hardware a cut drops on the coprocessor, read from
each driver's own table as flows.py reads it, and that "Cut in hardware" off drops nothing. The
tables are made up; nothing here comes from an appliance.

    python3 tools/test-hwdrop.py
"""
import importlib.util
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs'))
spec = importlib.util.spec_from_file_location(
    'hwdrop', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'hwdrop.py'))
hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hd)

FAILED = []


def check(what, got, want):
    if got != want:
        FAILED.append(what)
        print('FAIL %s: got %r, want %r' % (what, got, want))
    else:
        print('ok   %s' % what)


def word(ip):
    """The XGS 3300's table writes an IPv4 address as its 32-bit word, low byte first."""
    return '0x' + bytes(reversed([int(x) for x in ip.split('.')])).hex()


OCTEP = ''.join(
    '%4d rev 1  proto 6  sweep asks pf with the orig tuple, list 1\n'
    '      orig  %s:%d -> %s:%d  slot 7 rev 1  active  in oxp2\n'
    '      reply %s:%d -> %s:%d  slot 8 rev 1  active  in oxp3\n'
    % (i, word(s), sp, word(d), dp, word(d), dp, word(s), sp)
    for i, s, sp, d, dp in ((2, '10.0.0.2', 50001, '198.51.100.7', 443),
                            (3, '10.0.0.2', 50002, '203.0.113.9', 443),
                            (4, '10.0.0.3', 50003, '198.51.100.7', 80)))
NPUEP = ''.join(
    '  %d rev 1 plain 12s | orig mflow 5 nhop 2 %s:%d -> %s:%d | reply mflow 6 nhop 1 %s:%d -> %s:%d  active\n'
    % (i, s, sp, d, dp, d, dp, s, sp)
    for i, s, sp, d, dp in ((5, '192.168.1.119', 51000, '198.51.100.7', 443),
                            (6, '192.168.1.120', 51001, '203.0.113.9', 22)))


class Driver:
    """The two sysctls hwdrop reads and writes, answered as each driver answers them."""

    def __init__(self, family):
        self.family, self.writes, self.last = family, [], ''

    def sysctl(self, name):
        if self.family == 'octeontx' and name == 'dev.octep.0.dp.flow_table':
            return OCTEP
        if self.family == 'armada' and name == 'dev.npuep.0.flow.flows':
            return 'made 2 (auto 2) taken 0 gone 0\n' + NPUEP
        return None

    def run(self, cmd, **kw):
        class Done:
            pass
        done = Done()
        done.returncode, done.stdout = 0, ''
        if '-n' in cmd:
            done.stdout = self.last
        else:
            oid, value = cmd[1].split('=', 1)
            self.writes.append((oid, value))
            idx = value.split()[0]
            self.last = 'connection %s: verdict 1 (drop) on the coprocessor\n' % idx
        return done


def use(driver):
    hd.flows.sysctl = driver.sysctl
    hd.subprocess.run = driver.run


cfg = tempfile.NamedTemporaryFile('w', suffix='.xml', delete=False)
cfg.write('<?xml version="1.0"?>\n<opnsense><OPNsense><XGS><general></general></XGS></OPNsense></opnsense>\n')
cfg.close()
off = tempfile.NamedTemporaryFile('w', suffix='.xml', delete=False)
off.write('<?xml version="1.0"?>\n<opnsense><OPNsense><XGS><general><cut_in_hardware>0</cut_in_hardware>'
          '</general></XGS></OPNsense></opnsense>\n')
off.close()

d = Driver('octeontx')
use(d)
check('XGS 3300: both connections to a listed address dropped',
      hd.drop_addresses({'198.51.100.7'}, cfg.name), 2)
check('XGS 3300: through dp.flow_verdict, by slot',
      d.writes, [('dev.octep.0.dp.flow_verdict', '2 1'), ('dev.octep.0.dp.flow_verdict', '4 1')])

d = Driver('octeontx')
use(d)
check('XGS 3300: a JA4 cut drops only the connection with both its ends',
      hd.drop_ends({('10.0.0.2', 50002), ('203.0.113.9', 443)}, cfg.name), 1)
check('XGS 3300: that one', d.writes, [('dev.octep.0.dp.flow_verdict', '3 1')])

d = Driver('armada')
use(d)
check('XGS 136: a listed address dropped through flow.verdict', hd.drop_addresses({'203.0.113.9'}, cfg.name), 1)
check('XGS 136: by its index', d.writes, [('dev.npuep.0.flow.verdict', '6 1')])

d = Driver('octeontx')
use(d)
check('"Cut in hardware" off: nothing dropped', hd.drop_addresses({'198.51.100.7'}, off.name), 0)
check('"Cut in hardware" off: nothing written', d.writes, [])
check('a missing config.xml is the default, on', hd.enabled('/nonexistent/config.xml'), True)

os.unlink(cfg.name)
os.unlink(off.name)
print('%d failed' % len(FAILED) if FAILED else 'all passed')
sys.exit(1 if FAILED else 0)
