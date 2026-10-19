#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The parts of scripts/xgs/lists_cut.py that decide which connections are cut, on pf's own output.

    python3 tools/test-lists-cut.py

A state that is wrongly matched is a connection cut for nothing, and one that is missed stays open to
a listed address; both are decided here, from `pfctl -ss -vv` and `pfctl -T show` text, without pf.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'lists_cut', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'lists_cut.py'))
lc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lc)

# pfctl -ss -vv, as an OPNsense appliance prints it: IPv4 with a port after a colon, IPv6 with the
# port in brackets, a translated state with the address it was translated to in parentheses.
SS = """all tcp 10.0.0.1:5310 <- 192.168.1.119:55215       ESTABLISHED:ESTABLISHED
   [2628974272 + 65187] wscale 7  [590325212 + 65792] wscale 8
   age 00:00:57, expires in 24:00:00, 23:33 pkts, 3561:6023 bytes, rule 96
   id: 352ad56a00000000 creatorid: 41397420
   origif: npup8
all tcp 192.168.1.51:31001 (192.168.1.20:51515) -> 203.0.113.7:443       ESTABLISHED:ESTABLISHED
   age 00:01:10, expires in 24:00:00, 120:118 pkts, 90123:1201 bytes, rule 51
   id: 452ad56a00000000 creatorid: 41397420
all tcp 2001:db8:2::2[5201] <- 2001:db8:3::2[49812]       ESTABLISHED:ESTABLISHED
   age 00:00:12, expires in 24:00:00, 10:10 pkts, 1000:1000 bytes, rule 90
   id: 552ad56a00000000 creatorid: 41397420
all udp 192.168.1.20:53 <- 192.168.1.5:40000       SINGLE:MULTIPLE
   id: 652ad56a00000000 creatorid: 41397420
all icmp 192.168.1.1:3 <- 198.51.100.9:3       0:0
   id: 752ad56a00000000 creatorid: 41397420
"""

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


st = lc.states(SS)
check('five states read', len(st), 5)
check('IPv4 ends', st[0], ('352ad56a00000000', ['10.0.0.1', '192.168.1.119']))
check('a translated state carries the address it was translated to',
      st[1][1], ['192.168.1.51', '192.168.1.20', '203.0.113.7'])
check('IPv6 ends, the port in brackets', st[2][1], ['2001:db8:2::2', '2001:db8:3::2'])
check('a state line without "id" in it, and the status words, are not addresses', st[3][1], ['192.168.1.20', '192.168.1.5'])

m = lc.Matcher({'203.0.113.0/24', '2001:db8:1::/48', '198.51.100.9/32', '10.0.0.0/30', '10.0.0.4/30'})
check('an address inside a listed network', '203.0.113.7' in m, True)
check('one outside every listed network', '203.0.114.1' in m, False)
check('a single listed address', '198.51.100.9' in m, True)
check('its neighbour', '198.51.100.10' in m, False)
check('IPv6 inside', '2001:db8:1::5' in m, True)
check('IPv6 outside', '2001:db8:2::2' in m, False)
check('two adjoining networks merged: the first', '10.0.0.1' in m, True)
check('two adjoining networks merged: the second', '10.0.0.7' in m, True)
check('two adjoining networks merged: past them', '10.0.0.8' in m, False)
check('not an address at all', 'ESTABLISHED' in m, False)

listed = lc.Matcher({'203.0.113.0/24', '198.51.100.9'})
cut = [sid for sid, addrs in st if any(a in listed for a in addrs)]
check('the states a list naming 203.0.113.0/24 and 198.51.100.9 cuts', cut,
      ['452ad56a00000000', '752ad56a00000000'])

empty = lc.Matcher(set())
check('an empty list matches nothing', any(a in empty for _, addrs in st for a in addrs), False)

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
