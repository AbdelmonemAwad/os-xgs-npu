#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""
Interfaces whose IPv4 subnets overlap - for the Ports page, verify.sh and the support bundle (issue 911).

    subnets.py           JSON: {"overlaps": [...]}, each a group of addresses whose subnets overlap,
                         with the route the system keeps for each subnet
    subnets.py --text    the same as lines for verify.sh; nothing when there is no overlap

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
    print(json.dumps({'status': 'ok', 'overlaps': groups}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
