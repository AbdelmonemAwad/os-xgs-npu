#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""
Interfaces whose IPv4 subnets overlap - for the Ports page, verify.sh and the support bundle (issue 911).

    subnets.py           JSON: {"overlaps": [...]}, each a group of addresses whose subnets overlap,
                         with the route the system keeps for each subnet
    subnets.py --text    the same as lines for verify.sh; nothing when there is no overlap
    subnets.py --mgmt    the host's own ports (MGMT) mixed with the coprocessor's - in their bridge or in a
                         subnet they use - as lines for verify.sh; nothing when MGMT is apart

WHY IT IS WORTH A WARNING. Two interfaces in one subnet share one route, and FreeBSD keeps it on one of
them: the one whose address was there first. The other's address is up and answers nothing from that
subnet - its replies leave by the interface that holds the route, cable or no cable. Which one holds it
changes whenever either is applied again: saving its page, or its link coming up (OPNsense applies an
interface again when its cable is plugged in). Measured on the XGS 3300 (issue 911): the LAN bridge,
with no port plugged in, given an address in MGMT's subnet; saving MGMT's page moved the route to the
bridge, and MGMT stopped answering ping and the web GUI. A DHCP address counts as much as a static one,
which is why the addresses are read from the system rather than from config.xml: OPNsense itself does
not check an overlap when one side is DHCP.

Read-only: ifconfig, netstat and config.xml.
"""
import ipaddress
import json
import re
import subprocess
import sys

try:
    import portmap
except ImportError:
    portmap = None

# The coprocessor's ports, by the drivers' names: OCTEON TX's oxp and ARMADA's npup.
COPROCESSOR = re.compile(r'^(oxp|npup)[0-9]+$')
# What is not a port of the host itself: the coprocessor's ports, its management links and the
# inspector's interface, and the virtual ones. MGMT on an XGS 3300 (igb0) is what is left.
NOT_HOST = re.compile(r'^(oxp|npup|oxips|mvmgmt|oxmgmt|bridge|vlan|lagg|lo|enc|pflog|pfsync|tun|tap|ovpn|'
                      r'wg|gif|gre|ipsec|stf|carp)')


def run(argv):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout if out.returncode == 0 else ''


def addresses(text=None):
    """[(device, ip_interface)] from `ifconfig -a inet`: every IPv4 address with a subnet - not loopback,
    not a /32 (a VIP or a host route), not a point-to-point link, whose far end is not a subnet."""
    text = run(['/sbin/ifconfig', '-a', 'inet']) if text is None else text
    found, dev, p2p = [], None, False
    for line in text.splitlines():
        head = re.match(r'^([a-z][a-z0-9_.]*): flags=[0-9a-f]+<([^>]*)>', line)
        if head:
            dev, p2p = head.group(1), 'POINTOPOINT' in head.group(2).split(',')
            continue
        m = re.match(r'^\s+inet (\d+\.\d+\.\d+\.\d+) netmask (0x[0-9a-f]+)', line)
        if not m or dev is None or dev.startswith('lo') or p2p:
            continue
        prefix = bin(int(m.group(2), 16)).count('1')
        if prefix >= 32:
            continue
        found.append((dev, ipaddress.ip_interface('%s/%d' % (m.group(1), prefix))))
    return found


def routes(text=None):
    """{network string: device} for the connected IPv4 routes, from `netstat -rn -4`."""
    text = run(['/usr/bin/netstat', '-rn', '-4']) if text is None else text
    out = {}
    for line in text.splitlines():
        cols = line.split()
        if len(cols) >= 4 and '/' in cols[0] and cols[1].startswith('link#') and 'U' in cols[2]:
            out[cols[0]] = cols[3]
    return out


def overlaps(found=None, table=None, names=None):
    """Groups of two or more addresses on different devices whose subnets overlap."""
    found = addresses() if found is None else found
    table = routes() if table is None else table
    if names is None:
        names = portmap.assignments() if portmap is not None else {}
    # Connected groups: an address that overlaps one already in a group joins it, and groups an
    # address links are merged, so three overlapping subnets come out as one group, not two.
    groups = []
    for i, (dev, addr) in enumerate(found):
        for other_dev, other in found[i + 1:]:
            if other_dev == dev or not addr.network.overlaps(other.network):
                continue
            pair = {(dev, addr), (other_dev, other)}
            touching = [g for g in groups if g & pair]
            for g in touching:
                groups.remove(g)
                pair |= g
            groups.append(pair)
    out = []
    for group in groups:
        members = []
        for dev, addr in sorted(group, key=lambda m: (m[1].network.prefixlen, m[0])):
            name = names.get(dev, {})
            members.append({
                'device': dev,
                'key': name.get('key', ''),
                'descr': name.get('descr', ''),
                'address': str(addr),
                'network': str(addr.network),
                # the device the system sends this subnet to: this one, another, or none
                'route': table.get(str(addr.network), ''),
            })
        # What each one cannot answer from: its own subnet when another holds the route, and any
        # narrower subnet inside it routed elsewhere - the narrower route wins for those addresses.
        for m in members:
            lost = []
            if m['route'] != m['device']:
                lost.append({'network': m['network'], 'route': m['route']})
            mine = ipaddress.ip_network(m['network'])
            for o in members:
                theirs = ipaddress.ip_network(o['network'])
                if (theirs.prefixlen > mine.prefixlen and theirs.subnet_of(mine)
                        and o['route'] and o['route'] != m['device']):
                    lost.append({'network': o['network'], 'route': o['route']})
            m['lost'] = lost
        out.append({'members': members})
    return out


def bridges(text=None):
    """{bridge: [member devices]} from `ifconfig -a`."""
    text = run(['/sbin/ifconfig', '-a']) if text is None else text
    out, dev = {}, None
    for line in text.splitlines():
        head = re.match(r'^([a-z][a-z0-9_.]*): flags=', line)
        if head:
            dev = head.group(1)
            continue
        m = re.match(r'^\s+member: (\S+)', line)
        if m and dev and dev.startswith('bridge'):
            out.setdefault(dev, []).append(m.group(1))
    return out


def mgmt_mixed(groups=None, members=None, names=None):
    """The host's own ports - MGMT on an XGS 3300 - in a bridge with the coprocessor's ports, or in a subnet
    one of them uses (the coprocessor's port itself, or a bridge it is in). MGMT reaches the host's CPU on
    its own; the coprocessor's ports reach it only through the coprocessor. Mixed, the host has two ways
    into one network and keeps one route for it, so one of them answers nothing (issue 911)."""
    members = bridges() if members is None else members
    groups = overlaps() if groups is None else groups
    if names is None:
        names = portmap.assignments() if portmap is not None else {}

    def name(dev):
        n = names.get(dev, {})
        return {'device': dev, 'key': n.get('key', ''), 'descr': n.get('descr', '')}

    def coprocessor_side(dev):
        return bool(COPROCESSOR.match(dev)) or any(COPROCESSOR.match(m) for m in members.get(dev, []))

    out = []
    for bridge in sorted(members):
        ports = members[bridge]
        if any(COPROCESSOR.match(m) for m in ports):
            for dev in ports:
                if not NOT_HOST.match(dev):
                    out.append(dict(name(dev), how='bridge', others=[name(bridge)]))
    for group in groups:
        devs = [m['device'] for m in group['members']]
        sides = [d for d in devs if coprocessor_side(d)]
        for dev in devs:
            if sides and not NOT_HOST.match(dev) and dev not in sides:
                out.append(dict(name(dev), how='subnet', others=[name(d) for d in sides]))
    return out


def mgmt_lines(mixed):
    lines = []
    for m in mixed:
        what = 'is in a bridge with ports of the coprocessor' if m['how'] == 'bridge' else \
            'shares a subnet with ports of the coprocessor'
        lines.append('%s %s: %s' % (describe(m), what, ', '.join(describe(o) for o in m['others'])))
    return lines


def describe(member):
    return '%s (%s)' % (member['descr'], member['device']) if member['descr'] else member['device']


def text_lines(groups):
    lines = []
    for group in groups:
        members = group['members']
        lines.append('%s share a subnet: %s' % (
            ' and '.join(describe(m) for m in members),
            ', '.join('%s %s' % (m['device'], m['address']) for m in members)))
        for network in sorted({m['network'] for m in members}):
            owner = next((m['route'] for m in members if m['network'] == network and m['route']), '')
            lines.append('  %s is routed by %s' % (network, owner or 'none of them'))
        for m in members:
            for lost in m['lost']:
                lines.append('  %s answers nothing from %s: its replies leave by %s' % (
                    describe(m), lost['network'], lost['route'] or 'no interface'))
    return lines


def main(argv):
    groups = overlaps()
    if '--text' in argv[1:]:
        for line in text_lines(groups):
            print(line)
        return 0
    if '--mgmt' in argv[1:]:
        for line in mgmt_lines(mgmt_mixed(groups)):
            print(line)
        return 0
    print(json.dumps({'status': 'ok', 'overlaps': groups, 'mgmt_mixed': mgmt_mixed(groups)}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
