#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/subnets.py against the ifconfig and netstat outputs of the cases issue 911 met: two
interfaces in one subnet with the route on either, a narrower subnet inside a wider one, three in one
group, and the addresses that must not count - loopback, a /32 and a point-to-point link.
    python3 tools/test-subnets.py"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'opnsense',
                                'scripts', 'xgs'))
import subnets  # noqa: E402

NAMES = {'bridge0': {'key': 'lan', 'descr': 'LAN'}, 'igb0': {'key': 'opt16', 'descr': 'MGMT'},
         'oxp3': {'key': 'wan', 'descr': 'WAN'}, 'oxp0': {'key': 'opt1', 'descr': 'F1'}}
IFCONFIG = """lo0: flags=1008049<UP,LOOPBACK,RUNNING,MULTICAST,LOWER_UP> metric 0 mtu 16384
\tinet 127.0.0.1 netmask 0xff000000
igb0: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tinet 192.168.100.1 netmask 0xffffff00 broadcast 192.168.100.255
oxp3: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tinet 203.0.113.51 netmask 0xffffff00 broadcast 203.0.113.255
bridge0: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tinet 192.168.100.60 netmask 0xffffff00 broadcast 192.168.100.255
\tinet 198.51.100.1 netmask 0xffffffff broadcast 198.51.100.1
tun0: flags=8051<UP,POINTOPOINT,RUNNING,MULTICAST> metric 0 mtu 1500
\tinet 192.168.100.200 netmask 0xffffff00
"""
NETSTAT_MGMT = """Destination        Gateway            Flags         Netif Expire
default            203.0.113.1        UGS            oxp3
203.0.113.0/24     link#5             U              oxp3
192.168.100.0/24   link#1             U              igb0
192.168.100.1      link#2             UHS             lo0
"""
failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s' % ('ok  ' if ok else 'FAIL', name) + ('' if ok else ': got %r, want %r' % (got, want)))


found = subnets.addresses(IFCONFIG)
check('loopback, /32 and point-to-point left out', [d for d, _ in found], ['igb0', 'oxp3', 'bridge0'])

groups = subnets.overlaps(found, subnets.routes(NETSTAT_MGMT), NAMES)
check('one group', len(groups), 1)
members = {m['device']: m for m in groups[0]['members']}
check('the two in it', sorted(members), ['bridge0', 'igb0'])
check('the route on MGMT', members['igb0']['route'], 'igb0')
check('MGMT loses nothing', members['igb0']['lost'], [])
check('LAN loses its subnet to igb0', members['bridge0']['lost'], [{'network': '192.168.100.0/24', 'route': 'igb0'}])

# The same addresses after MGMT was applied again: the route moved to the bridge (issue 911).
moved = NETSTAT_MGMT.replace('192.168.100.0/24   link#1             U              igb0',
                             '192.168.100.0/24   link#24            U           bridge0')
groups = subnets.overlaps(found, subnets.routes(moved), NAMES)
members = {m['device']: m for m in groups[0]['members']}
check('after MGMT applied again, MGMT loses its subnet to bridge0', members['igb0']['lost'],
      [{'network': '192.168.100.0/24', 'route': 'bridge0'}])
lines = subnets.text_lines(groups)
check('the text names who answers nothing',
      any('MGMT (igb0) answers nothing from 192.168.100.0/24: its replies leave by bridge0' in l for l in lines), True)

# A /24 inside a /22 (the home network of issue 911): the narrower route wins for its addresses.
wide = [('igb0', subnets.ipaddress.ip_interface('10.0.0.4/22')),
        ('bridge0', subnets.ipaddress.ip_interface('10.0.0.1/24'))]
table = {'10.0.0.0/22': 'igb0', '10.0.0.0/24': 'bridge0'}
groups = subnets.overlaps(wide, table, NAMES)
members = {m['device']: m for m in groups[0]['members']}
check('/22 MGMT loses the /24 inside it to the bridge', members['igb0']['lost'],
      [{'network': '10.0.0.0/24', 'route': 'bridge0'}])
check('/24 LAN keeps its own', members['bridge0']['lost'], [])

# Three in one group, linked through the widest.
three = [('igb0', subnets.ipaddress.ip_interface('10.0.0.1/16')),
         ('bridge0', subnets.ipaddress.ip_interface('10.0.0.65/26')),
         ('oxp0', subnets.ipaddress.ip_interface('10.0.0.129/25'))]
groups = subnets.overlaps(three, {}, NAMES)
check('three overlapping subnets are one group', [len(g['members']) for g in groups], [3])

# Nothing to say.
check('no overlap, no group', subnets.overlaps(found[:2], subnets.routes(NETSTAT_MGMT), NAMES), [])
check('no overlap, no text', subnets.text_lines([]), [])

print('all passed' if failures == 0 else '%d failed' % failures)
sys.exit(1 if failures else 0)
