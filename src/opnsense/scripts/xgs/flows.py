#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The connections the coprocessor forwards by itself, as the driver lists them - for the Flows page.

    flows.py           print {"family", "auto", "counters", "flows": [...]} as JSON
    flows.py summary   the same without the table: the figures alone, for the dashboard widget, which
                       asks every fifteen seconds and must not make the driver print a table of up to
                       two million connections each time
    flows.py release N hand connection N (the table's first column) back to the host and keep it
                       there until pf lets its state go; print {"released", "message"}

Each driver keeps its table as text: dev.octep.0.dp.flow_table on the XGS 3300 (a connection over
three or four lines, addresses as the driver's own hex words) and dev.npuep.0.flow.flows on the XGS
136 (a connection on one line, addresses dotted). This reads whichever is there and gives both the
same rows, so the page draws one table for either family.
"""

import json
import re
import subprocess
import sys

OCTEP_HEAD = re.compile(r'^\s*(\d+)\s+rev\s+(\d+)\s+proto\s+(\d+)')
OCTEP_DIR = re.compile(r'^\s*(orig|reply)\s+(0x[0-9a-f]{8}|\[[0-9a-f:.]+\]):(\d+)\s+->\s+(0x[0-9a-f]{8}|\[[0-9a-f:.]+\]):(\d+)(.*)$',
                       re.I)
NPUEP_ADDR = r'([\d.]+|\[[0-9a-f:.]+\])'
NPUEP_LINE = re.compile(r'^\s*(\d+)\s+rev\s+(\d+)\s+(\S+)\s+(\d+)s\s+\|\s+orig\s+mflow\s+(\d+)\s+nhop\s+(\d+)\s+'
                        + NPUEP_ADDR + r':(\d+)\s+->\s+' + NPUEP_ADDR + r':(\d+)\s+\|\s+reply\s+mflow\s+(\d+)\s+'
                        r'nhop\s+(\d+)\s+' + NPUEP_ADDR + r':(\d+)\s+->\s+' + NPUEP_ADDR + r':(\d+)(.*)$', re.I)
# What the coprocessor counted for a connection, on the XGS 3300 since 2026-10-19: its statistics
# messages, read by the driver (octep_dp_cmsg_rx) and printed under the connection's first line.
OCTEP_COUNTED = re.compile(r'^\s*counted\s+orig\s+(\d+)\s+packets\s+(\d+)\s+bytes\s+reply\s+(\d+)\s+packets\s+'
                           r'(\d+)\s+bytes(?:\s+moved\s+(-?\d+)\s+s\s+ago)?')
PROTOCOLS = {1: 'ICMP', 6: 'TCP', 17: 'UDP', 50: 'ESP', 58: 'ICMPv6'}


def sysctl(name):
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout if done.returncode == 0 else None


def word_to_ip(word):
    """The XGS 3300 driver prints an IPv4 address as the 32-bit word it holds, low byte first,
    and an IPv6 address in full inside brackets."""
    if word.startswith('['):
        return word[1:-1]
    raw = bytes.fromhex(word[2:])
    return '.'.join(str(b) for b in reversed(raw))


def octep(text):
    flows = []
    current = None
    for line in text.splitlines():
        head = OCTEP_HEAD.match(line)
        if head:
            current = {'id': int(head.group(1)), 'rev': int(head.group(2)),
                       'protocol': PROTOCOLS.get(int(head.group(3)), str(head.group(3))),
                       'nat': '', 'age': '', 'port': '', 'state': '', 'ipsec': [], 'punts': 0}
            flows.append(current)
            continue
        if current is None:
            continue
        counted = OCTEP_COUNTED.match(line)
        if counted:
            g = [int(x) for x in counted.groups()[:4]]
            current.update(packets=[g[0], g[2]], bytes=[g[1], g[3]])
            if counted.group(5) is not None and int(counted.group(5)) >= 0:
                current['moved'] = int(counted.group(5))
            continue
        found = OCTEP_DIR.match(line)
        if found:
            direction, src, sport, dst, dport, rest = found.groups()
            if direction == 'orig':
                current.update(src=word_to_ip(src), sport=int(sport), dst=word_to_ip(dst), dport=int(dport))
                port = re.search(r'\bin\s+(\S+)', rest)
                current['port'] = port.group(1) if port else ''
                current['state'] = 'active' if re.search(r'\bactive\b', rest) else 'waiting'
                punts = re.search(r'punts\s+(\d+)', rest)
                current['punts'] = int(punts.group(1)) if punts else 0
            if 'decrypted' in rest:
                current['ipsec'].append('in')
            continue
        if 'leaves encrypted' in line:
            current['ipsec'].append('out')
    return flows


def npuep(text):
    flows = []
    for line in text.splitlines():
        found = NPUEP_LINE.match(line)
        if not found:
            continue
        g = found.groups()
        nat = g[2] if g[2] != 'plain' else ''
        flows.append({'id': int(g[0]), 'rev': int(g[1]), 'protocol': '', 'nat': nat, 'age': int(g[3]),
                      'src': g[6].strip('[]'), 'sport': int(g[7]), 'dst': g[8].strip('[]'), 'dport': int(g[9]),
                      'port': '', 'state': 'being made' if 'being made' in g[16] else 'active',
                      'ipsec': [], 'punts': 0})
    return flows


# The figures the widget shows, under one name on both families: in hardware and the table's size,
# next hops, connections the host took back, revived in place (the XGS 3300's), and refused because
# a table was full.
SUMMARY = {
    'octeontx': ('dev.octep.0.dp.', {'in_hardware': 'flows', 'max': 'conn_max', 'next_hops': 'nhops',
                                      'taken_back': 'flow_reclaimed', 'revived': 'flow_revived',
                                      'refused_full': 'nhop_full'}),
    'armada': ('dev.npuep.0.flow.', {'in_hardware': 'conns', 'max': 'conn_max', 'next_hops': 'nhops',
                                     'made': 'auto_made', 'taken_back': 'auto_gone',
                                     'refused_full': 'flow_full'}),
}


def figures():
    answer = {'family': '', 'auto': None, 'summary': {}}
    for family, (root, names) in SUMMARY.items():
        auto = sysctl(root + 'auto')
        if auto is None:
            continue
        answer['family'] = family
        answer['auto'] = auto.strip() == '1'
        for key, name in names.items():
            value = (sysctl(root + name) or '').strip()
            if value.isdigit():
                answer['summary'][key] = int(value)
        break
    print(json.dumps(answer))
    return 0


def release(index):
    """Write the index to the driver, then read back what it did: the driver answers in words, and a
    connection it no longer has is said so rather than failed."""
    answer = {'released': False, 'message': ''}
    if not index.isdigit():
        answer['message'] = 'not a connection number: ' + index
        print(json.dumps(answer))
        return 0
    for oid in ('dev.octep.0.dp.flow_release', 'dev.npuep.0.flow.release'):
        if sysctl(oid) is None:
            continue
        try:
            done = subprocess.run(['/sbin/sysctl', oid + '=' + index], capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError) as e:
            answer['message'] = str(e)
            break
        said = (sysctl(oid) or '').strip()
        answer['message'] = said if done.returncode == 0 else (done.stderr.strip() or said)
        answer['released'] = done.returncode == 0 and said.startswith('connection ' + index + ' handed back')
        break
    else:
        answer['message'] = 'no driver that can hand a connection back is attached'
    print(json.dumps(answer))
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'summary':
        return figures()
    if len(sys.argv) > 2 and sys.argv[1] == 'release':
        return release(sys.argv[2].strip())
    answer = {'family': '', 'auto': None, 'counters': {}, 'flows': []}
    table = sysctl('dev.octep.0.dp.flow_table')
    if table is not None:
        answer['family'] = 'octeontx'
        answer['auto'] = (sysctl('dev.octep.0.dp.auto') or '').strip() == '1'
        for name in ('flows', 'flow_reclaimed', 'flow_revived', 'flow_timeout', 'nhop_full', 'conn_max', 'nhops',
                     'flow_v6_refused', 'flow_excluded', 'flow_released'):
            value = (sysctl('dev.octep.0.dp.' + name) or '').strip()
            if value.isdigit():
                answer['counters'][name] = int(value)
        answer['flows'] = octep(table)
    else:
        table = sysctl('dev.npuep.0.flow.flows')
        if table is not None:
            answer['family'] = 'armada'
            answer['auto'] = (sysctl('dev.npuep.0.flow.auto') or '').strip() == '1'
            summary = table.splitlines()[0] if table else ''
            for key, pattern in (('made', r'made (\d+)'), ('auto_made', r'\(auto (\d+)\)'),
                                 ('taken', r'taken (\d+)'), ('gone', r'gone (\d+)'),
                                 ('flow_full', r'flow-full (\d+)'), ('nhop_full', r'nhop-full (\d+)')):
                hit = re.search(pattern, summary)
                if hit:
                    answer['counters'][key] = int(hit.group(1))
            # The table's size and use, which the listing cannot give: it prints the first 4,096.
            for key, name in (('conn_max', 'conn_max'), ('flows', 'conns'), ('nhops', 'nhops'),
                              ('flow_v6_refused', 'v6_refused'), ('flow_excluded', 'excluded'),
                              ('flow_released', 'released')):
                value = (sysctl('dev.npuep.0.flow.' + name) or '').strip()
                if value.isdigit():
                    answer['counters'][key] = int(value)
            answer['flows'] = npuep(table)
    print(json.dumps(answer))
    return 0


if __name__ == '__main__':
    sys.exit(main())
