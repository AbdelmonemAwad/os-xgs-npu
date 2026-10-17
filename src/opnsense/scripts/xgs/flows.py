#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The connections the coprocessor forwards by itself, as the driver lists them - for the Flows page.

    flows.py      print {"family", "auto", "counters", "flows": [...]} as JSON

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
OCTEP_DIR = re.compile(r'^\s*(orig|reply)\s+0x([0-9a-f]{8}):(\d+)\s+->\s+0x([0-9a-f]{8}):(\d+)(.*)$', re.I)
NPUEP_LINE = re.compile(r'^\s*(\d+)\s+rev\s+(\d+)\s+(\S+)\s+(\d+)s\s+\|\s+orig\s+mflow\s+(\d+)\s+nhop\s+(\d+)\s+'
                        r'([\d.]+):(\d+)\s+->\s+([\d.]+):(\d+)\s+\|\s+reply\s+mflow\s+(\d+)\s+nhop\s+(\d+)\s+'
                        r'([\d.]+):(\d+)\s+->\s+([\d.]+):(\d+)(.*)$')
PROTOCOLS = {1: 'ICMP', 6: 'TCP', 17: 'UDP', 50: 'ESP', 58: 'ICMPv6'}


def sysctl(name):
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout if done.returncode == 0 else None


def word_to_ip(word):
    """The XGS 3300 driver prints an IPv4 address as the 32-bit word it holds, low byte first."""
    raw = bytes.fromhex(word)
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
                      'src': g[6], 'sport': int(g[7]), 'dst': g[8], 'dport': int(g[9]),
                      'port': '', 'state': 'being made' if 'being made' in g[16] else 'active',
                      'ipsec': [], 'punts': 0})
    return flows


def main():
    answer = {'family': '', 'auto': None, 'counters': {}, 'flows': []}
    table = sysctl('dev.octep.0.dp.flow_table')
    if table is not None:
        answer['family'] = 'octeontx'
        answer['auto'] = (sysctl('dev.octep.0.dp.auto') or '').strip() == '1'
        for name in ('flows', 'flow_reclaimed', 'flow_revived', 'flow_timeout', 'nhop_full'):
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
            answer['flows'] = npuep(table)
    print(json.dumps(answer))
    return 0


if __name__ == '__main__':
    sys.exit(main())
