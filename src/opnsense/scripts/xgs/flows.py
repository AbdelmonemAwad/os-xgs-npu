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
    flows.py release N [R]
                       hand connection N (the table's first column) back to the host and keep it
                       there until pf lets its state go; print {"released", "message"}. R is the
                       revision the table printed beside N (its "rev"): given, the driver refuses N
                       if it has gone to another connection since, and changes nothing

Each driver keeps its table as text: dev.octep.0.dp.flow_table on the XGS 3300 (a connection over
three or four lines, addresses as the driver's own hex words) and dev.npuep.0.flow.flows on the XGS
136 (a connection on one line, addresses dotted). This reads whichever is there and gives both the
same rows, so the page draws one table for either family.

WHAT EACH ROW ALSO SAYS (2026-11-14):

    nhop      the next hop each direction leaves by: the driver keeps one per flow, not a route
              table. On the XGS 3300 dev.octep.0.dp.nhop_table names each one's MAC, MTU and VLAN,
              and the host's ARP and NDP tables give the address behind the MAC; the XGS 136's
              driver prints the index only.
    to_pf     whether what the coprocessor counts for a connection is added to its pf state
              (dev.octep.0.dp.hw_to_pf, dev.npuep.0.flow.hw_to_pf: Settings' count_to_pf); a row
              is "counted in pf" when it is on and the coprocessor has counted the connection.

LEFT HARDWARE. Neither driver keeps a record of a connection it let go, only counts of why
(dev.octep.0.dp.flow_reclaimed, flow_timeout, flow_released; dev.npuep.0.flow.auto_gone, released).
So each listing is kept (/var/db/os-xgs-npu/flows-seen.json), and a connection in the last one and
not in this one has left hardware between the two readings: it is kept in a list of the last 100
(flows-gone.json) with the two times and what pf says of it now - its state gone (closed, killed
or expired: a killed state leaves hardware within two seconds) or still there (the host carries
it: handed back, returned by hand, or let go for another reason). Not done when the listing is cut
short (the drivers print the first 4,096): a connection past it would read as gone.
"""

import json
import os
import re
import subprocess
import sys
import time

SEEN = '/var/db/os-xgs-npu/flows-seen.json'
GONE = '/var/db/os-xgs-npu/flows-gone.json'
GONE_KEEP = 100
NHOP_LINE = re.compile(r'^\s*(\d+)\s+rev\s+\d+\s+refs\s+\d+\s+iface\s+\d+\s+to\s+([0-9a-f:]{17})\s+from\s+\S+\s+'
                       r'mtu\s+(\d+)(?:\s+vlan\s+(\d+))?', re.I)
NEIGHBOUR = re.compile(r'\(([0-9a-f.:]+)\)\s+at\s+([0-9a-f:]{17})', re.I)

OCTEP_HEAD = re.compile(r'^\s*(\d+)\s+rev\s+(\d+)\s+proto\s+(\d+)')
OCTEP_DIR = re.compile(r'^\s*(orig|reply)\s+(0x[0-9a-f]{8}|\[[0-9a-f:.]+\]):(\d+)\s+->\s+(0x[0-9a-f]{8}|\[[0-9a-f:.]+\]):(\d+)(.*)$',
                       re.I)
NPUEP_ADDR = r'([\d.]+|\[[0-9a-f:.]+\])'
NPUEP_LINE = re.compile(r'^\s*(\d+)\s+rev\s+(\d+)\s+(\S+)(?:\s+bridged)?\s+(\d+)s\s+\|\s+orig\s+mflow\s+(\d+)\s+nhop\s+(\d+)\s+'
                        + NPUEP_ADDR + r':(\d+)\s+->\s+' + NPUEP_ADDR + r':(\d+)\s+\|\s+reply\s+mflow\s+(\d+)\s+'
                        r'nhop\s+(\d+)\s+' + NPUEP_ADDR + r':(\d+)\s+->\s+' + NPUEP_ADDR + r':(\d+)(.*)$', re.I)
# A connection a bridge carries (discovery 9): both drivers print the word on its first line, the XGS
# 3300's after the translation marks, the XGS 136's before the age.
BRIDGED = re.compile(r'\sbridged\s')
# A connection by a VLAN subinterface, its tag written by the coprocessor (discovery 10): the XGS 3300's
# driver ends a microflow's line with the words, the XGS 136's prints a line of its own under the first.
VLAN_WRITTEN = re.compile(r'\bvlan\s+written\b')
# What the coprocessor counted for a connection, since 2026-10-19: its statistics messages, read by
# the driver (octep_dp_cmsg_rx, npuflow_cmsg_rx) and printed under the connection's first line - the
# same line on both families.
OCTEP_COUNTED = re.compile(r'^\s*counted\s+orig\s+(\d+)\s+packets\s+(\d+)\s+bytes\s+reply\s+(\d+)\s+packets\s+'
                           r'(\d+)\s+bytes(?:\s+moved\s+(-?\d+)\s+s\s+ago)?')
PROTOCOLS = {1: 'ICMP', 6: 'TCP', 17: 'UDP', 50: 'ESP', 58: 'ICMPv6'}


def sysctl(name):
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, errors='replace', timeout=10)
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
                       'nat': '', 'age': '', 'port': '', 'state': '', 'ipsec': [], 'punts': 0,
                       'bridged': bool(BRIDGED.search(line)), 'vlan': False}
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
            nhop = re.search(r'\bnhop\s+(\d+)', rest)
            current.setdefault('nhop', [None, None])[0 if direction == 'orig' else 1] = \
                int(nhop.group(1)) if nhop else None
            if direction == 'orig':
                current.update(src=word_to_ip(src), sport=int(sport), dst=word_to_ip(dst), dport=int(dport))
                port = re.search(r'\bin\s+(\S+)', rest)
                current['port'] = port.group(1) if port else ''
                current['state'] = 'active' if re.search(r'\bactive\b', rest) else 'waiting'
                punts = re.search(r'punts\s+(\d+)', rest)
                current['punts'] = int(punts.group(1)) if punts else 0
            if VLAN_WRITTEN.search(rest):
                current['vlan'] = True
            if 'decrypted' in rest:
                current['ipsec'].append('in')
            continue
        if 'leaves encrypted' in line:
            current['ipsec'].append('out')
    return flows


def npuep(text):
    flows = []
    for line in text.splitlines():
        counted = OCTEP_COUNTED.match(line)
        if counted and flows:
            # the XGS 136's driver prints the XGS 3300's line, since it reads the same messages
            g = [int(x) for x in counted.groups()[:4]]
            flows[-1].update(packets=[g[0], g[2]], bytes=[g[1], g[3]])
            if counted.group(5) is not None and int(counted.group(5)) >= 0:
                flows[-1]['moved'] = int(counted.group(5))
            continue
        if flows and VLAN_WRITTEN.match(line.strip()):
            flows[-1]['vlan'] = True
            continue
        found = NPUEP_LINE.match(line)
        if not found:
            continue
        g = found.groups()
        nat = g[2] if g[2] != 'plain' else ''
        flows.append({'id': int(g[0]), 'rev': int(g[1]), 'protocol': '', 'nat': nat, 'age': int(g[3]),
                      'src': g[6].strip('[]'), 'sport': int(g[7]), 'dst': g[8].strip('[]'), 'dport': int(g[9]),
                      'port': '', 'state': 'being made' if 'being made' in g[16] else 'active',
                      'ipsec': [], 'punts': 0, 'bridged': bool(BRIDGED.search(line.split('|')[0] + ' ')),
                      'vlan': False, 'nhop': [int(g[5]), int(g[11])]})
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


def release(index, rev=''):
    """Write the index to the driver, then read back what it did: the driver answers in words, and a
    connection it no longer has is said so rather than failed.

    With the revision the page read beside the index, "<index> <rev>" is written, and the driver
    refuses an index that has gone to another connection since. Until 2026-11-06 the index went
    alone: a connection that closed between the page's reading and the click left its index to the
    next one made, and that one was handed back instead. Without a revision - an older page, or a
    call by hand - the index goes alone, as before, and a driver older than that day reads the index
    of either write and stops there. The revision is ASCII digits or nothing: a driver reads any other
    character as the end, and would go by the index alone without saying so."""
    answer = {'released': False, 'message': ''}
    if not index.isdigit() or (rev and not (rev.isascii() and rev.isdigit())):
        answer['message'] = 'not a connection number: ' + (index + ' ' + rev).strip()
        print(json.dumps(answer))
        return 0
    value = index + ' ' + rev if rev else index
    for oid in ('dev.octep.0.dp.flow_release', 'dev.npuep.0.flow.release'):
        if sysctl(oid) is None:
            continue
        try:
            done = subprocess.run(['/sbin/sysctl', oid + '=' + value], capture_output=True, text=True, errors='replace', timeout=30)
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


def run(argv):
    try:
        done = subprocess.run(argv, capture_output=True, text=True, errors='replace', timeout=20)
    except (OSError, subprocess.SubprocessError):
        return ''
    return done.stdout if done.returncode == 0 else ''


def next_hops(text):
    """The XGS 3300's dp.nhop_table, {index: {mac, mtu, vlan, address}}: the address is the host's
    neighbour with that MAC, from arp -an and ndp -an, when it has one."""
    found = {}
    for line in (text or '').splitlines():
        m = NHOP_LINE.match(line)
        if m:
            found[m.group(1)] = {'mac': m.group(2).lower(), 'mtu': int(m.group(3)),
                                 'vlan': int(m.group(4)) if m.group(4) else 0, 'address': ''}
    if found:
        by_mac = {}
        for line in run(['/usr/sbin/arp', '-an']).splitlines():
            m = NEIGHBOUR.search(line)
            if m:
                by_mac.setdefault(m.group(2).lower(), m.group(1))
        for line in run(['/usr/sbin/ndp', '-an']).splitlines():
            parts = line.split()
            if len(parts) > 1 and re.match(r'^[0-9a-f:]{17}$', parts[1], re.I) and not parts[0].startswith('fe80'):
                by_mac.setdefault(parts[1].lower(), parts[0])
        for nh in found.values():
            nh['address'] = by_mac.get(nh['mac'], '')
    return found


def flow_key(f):
    return '%s/%s' % (f.get('id'), f.get('rev'))


def pf_has(states, f):
    """Whether pf still has a state with this connection's two ends, from pfctl -ss: an IPv4 end
    as address:port, an IPv6 one as address[port], both on one line."""
    if ':' in str(f.get('src', '')):
        a, b = '%s[%s]' % (f['src'], f['sport']), '%s[%s]' % (f['dst'], f['dport'])
    else:
        a, b = '%s:%s' % (f.get('src'), f.get('sport')), '%s:%s' % (f.get('dst'), f.get('dport'))
    return any(a in line and b in line for line in states)


def load(path, default):
    try:
        with open(path, encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return default


def save(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + '.tmp', 'w', encoding='utf-8') as handle:
        json.dump(value, handle, separators=(',', ':'))
    os.replace(path + '.tmp', path)


def track(flows, complete, now=None):
    """The connections that left hardware since the last listing, added to the kept list; returns
    {'gone': [...], 'since': the last listing's time, 'complete': whether it could be told}."""
    now = int(now if now is not None else time.time())
    kept = load(GONE, [])
    kept = kept if isinstance(kept, list) else []
    last = load(SEEN, {})
    if not complete:
        # cut short: what is past the listing would read as gone; start again from the next whole one
        if os.path.exists(SEEN):
            os.unlink(SEEN)
        return {'gone': kept, 'since': None, 'complete': False}
    if isinstance(last, dict) and isinstance(last.get('flows'), dict):
        here = {flow_key(f) for f in flows}
        left = [f for k, f in last['flows'].items() if k not in here]
        if left:
            states = run(['/sbin/pfctl', '-ss']).splitlines()
            for f in left:
                kept.append(dict(f, after=last.get('at'), before=now,
                                 pf='state' if pf_has(states, f) else 'gone'))
            kept = kept[-GONE_KEEP:]
            save(GONE, kept)
    save(SEEN, {'at': now, 'flows': {flow_key(f): {k: f.get(k) for k in
                                                     ('id', 'rev', 'protocol', 'src', 'sport', 'dst', 'dport')}
                                     for f in flows}})
    return {'gone': kept, 'since': last.get('at') if isinstance(last, dict) else None, 'complete': True}


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'summary':
        return figures()
    if len(sys.argv) > 2 and sys.argv[1] == 'release':
        # no third argument, or an empty one: no revision
        return release(sys.argv[2].strip(), sys.argv[3].strip() if len(sys.argv) > 3 else '')
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
        answer['to_pf'] = (sysctl('dev.octep.0.dp.hw_to_pf') or '').strip() == '1'
        answer['nhops'] = next_hops(sysctl('dev.octep.0.dp.nhop_table'))
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
            answer['to_pf'] = (sysctl('dev.npuep.0.flow.hw_to_pf') or '').strip() == '1'
            answer['nhops'] = {}
    if answer['family']:
        listed = len(answer['flows'])
        answer['left'] = track(answer['flows'], listed >= answer['counters'].get('flows', listed))
    print(json.dumps(answer))
    return 0


if __name__ == '__main__':
    sys.exit(main())
