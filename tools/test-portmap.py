#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/portmap.py - the VLANs on each front port for the Ports page (2026-11-14): a VLAN
subinterface found by its parent in ifconfig, its tag, and the bridge it is in, if any. The text is
made up as FreeBSD's ifconfig prints it; nothing here comes from an appliance.

    python3 tools/test-portmap.py
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs'))
spec = importlib.util.spec_from_file_location(
    'portmap', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'portmap.py'))
pm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pm)

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


IFCONFIG = """oxp0: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tdescription: LAN
\tether 00:1a:8c:00:00:01
\tmedia: Ethernet autoselect (1000baseT <full-duplex>)
\tstatus: active
oxp1: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tether 00:1a:8c:00:00:02
\tstatus: no carrier
vlan20: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tether 00:1a:8c:00:00:01
\tgroups: vlan
\tvlan: 20 vlanproto: 802.1q vlanpcp: 0 parent interface: oxp0
vlan5: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tvlan: 5 vlanproto: 802.1q vlanpcp: 0 parent interface: oxp0
vlan30: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tvlan: 30 vlanproto: 802.1q vlanpcp: 0 parent interface: igb0
bridge0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
\tmember: vlan5 flags=143<LEARNING,DISCOVER,AUTOEDGE,AUTOPTP>
\tmember: oxp1 flags=143<LEARNING,DISCOVER,AUTOEDGE,AUTOPTP>
"""

pm.run = lambda argv: IFCONFIG
ports = {p['device']: p for p in pm.interfaces('oxp')}
check('the front ports, and nothing else', sorted(ports), ['oxp0', 'oxp1'])
check('a port\'s VLANs by tag, each with its bridge', ports['oxp0']['vlans'],
      [{'device': 'vlan5', 'tag': 5, 'bridge': 'bridge0'}, {'device': 'vlan20', 'tag': 20, 'bridge': None}])
check('a port with none', ports['oxp1']['vlans'], [])
check('a port\'s own bridge', ports['oxp1']['bridge'], 'bridge0')
check('the limit the drivers keep', pm.VLAN_LIF_MAX, 64)

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
