#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Cut the connections already open to or from an address a block list has just gained.

A block list in OPNsense is an alias - a pf table, filled from a URL, a feed or by hand and
refreshed on its own schedule - and a rule that blocks it blocks new connections only. A connection
opened before its address was listed keeps its pf state, and with offloading on its flow in the
coprocessor, for as long as it carries traffic. XGS: Settings names the lists to watch
(general.cut_lists); for each, this compares the table with what it held at the last run and kills,
by id, every pf state with either end in what was added. The coprocessor's flow goes with its state:
each driver takes a connection out of hardware once pf no longer holds it - the XGS 136 at its next
sweep, measured in docs/measurements/lists-cut-2026-10-19.md.

    lists_cut.py            run once for what config.xml names, print a JSON summary
                            (cron, every minute; and XGS: Settings's apply)
    lists_cut.py --status   print the last run's record

The first run for a list takes everything in it as new, so turning the watch on cuts what the list
already names. A state is killed by its id: `pfctl -k <host> -k <host>` kills nothing on this
firewall. Never fails its caller; what it could not do is in the summary and in the XGS log.
"""
import bisect
import ipaddress
import json
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

CONFIG = '/conf/config.xml'
STATE_DIR = '/var/db/os-xgs-npu/lists'
STATUS = os.path.join(STATE_DIR, 'status.json')
PFCTL = '/sbin/pfctl'
TAG = 'xgs-lists'


def log(message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, '-p', 'daemon.notice', message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def watched_lists(path=CONFIG):
    """The names of the enabled aliases XGS: Settings watches, in the order the page lists them."""
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return []
    want = [u for u in (root.findtext('OPNsense/XGS/general/cut_lists') or '').split(',') if u.strip()]
    names = {}
    for alias in root.findall('OPNsense/Firewall/Alias/aliases/alias'):
        if (alias.findtext('enabled') or '1') == '1' and alias.get('uuid'):
            names[alias.get('uuid')] = alias.findtext('name') or ''
    return [names[u] for u in want if names.get(u)]


def table_entries(name):
    """What pf's table holds now, as networks; None when there is no such table."""
    try:
        r = subprocess.run([PFCTL, '-t', name, '-T', 'show'], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0:
        return None
    out = set()
    for line in r.stdout.splitlines():
        item = line.strip()
        if not item or item.startswith('!'):     # a negated entry excludes, it does not list
            continue
        try:
            out.add(str(ipaddress.ip_network(item, strict=False)))
        except ValueError:
            continue
    return out


class Matcher:
    """Is an address inside any of a set of networks: each family's networks as sorted, merged
    integer ranges, so a state's addresses are looked up by bisection however long the list."""

    def __init__(self, networks):
        self.ranges = {4: [], 6: []}
        for n in networks:
            net = ipaddress.ip_network(n, strict=False)
            self.ranges[net.version].append((int(net.network_address), int(net.broadcast_address)))
        self.starts = {}
        for v, rs in self.ranges.items():
            rs.sort()
            merged = []
            for a, b in rs:
                if merged and a <= merged[-1][1] + 1:
                    merged[-1] = (merged[-1][0], max(merged[-1][1], b))
                else:
                    merged.append((a, b))
            self.ranges[v] = merged
            self.starts[v] = [a for a, _ in merged]

    def __contains__(self, addr):
        try:
            ip = ipaddress.ip_address(addr)
        except ValueError:
            return False
        rs = self.ranges[ip.version]
        i = bisect.bisect_right(self.starts[ip.version], int(ip)) - 1
        return i >= 0 and rs[i][0] <= int(ip) <= rs[i][1]


ADDR_V6 = re.compile(r'^([0-9A-Fa-f:.]+)\[\d+\]$')


def state_addresses(line):
    """Every address on the first line of a state in `pfctl -ss -vv`: both ends, and the translated
    one in parentheses. IPv4 is written address:port, IPv6 address[port]."""
    out = []
    for tok in line.split()[2:]:
        tok = tok.strip('()')
        m = ADDR_V6.match(tok)
        if m:
            cand = m.group(1)
        elif tok.count(':') == 1 and '.' in tok:
            cand = tok.split(':', 1)[0]
        else:
            cand = tok
        try:
            out.append(str(ipaddress.ip_address(cand)))
        except ValueError:
            continue
    return out


def states(text):
    """[(id, [addresses])] from `pfctl -ss -vv`."""
    out, addrs = [], None
    for line in text.splitlines():
        if line and not line[0].isspace():
            addrs = state_addresses(line)
            continue
        m = re.match(r'\s+id: ([0-9a-f]+)', line)
        if m and addrs is not None:
            out.append((m.group(1), addrs))
            addrs = None
    return out


def load(path, default):
    try:
        with open(path, encoding='utf-8') as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return default


def save(path, data):
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as fh:
        json.dump(data, fh, indent=1, sort_keys=True)
    os.replace(tmp, path)


def snapshot_path(name):
    return os.path.join(STATE_DIR, re.sub(r'[^A-Za-z0-9_.-]', '_', name) + '.json')


def run():
    os.makedirs(STATE_DIR, exist_ok=True)
    status = load(STATUS, {})
    lists = watched_lists()
    summary = {'lists': {}, 'cut': 0, 'run': int(time.time()), 'watched': lists}
    added, current = {}, {}
    for name in lists:
        now = table_entries(name)
        if now is None:
            summary['lists'][name] = {'error': 'no pf table by this name - is the alias loaded?'}
            continue
        before = load(snapshot_path(name), None)
        new = now if before is None else now - set(before)
        added[name], current[name] = new, now
        summary['lists'][name] = {'entries': len(now), 'added': len(new), 'cut': 0}

    if any(added.values()):
        try:
            text = subprocess.run([PFCTL, '-ss', '-vv'], capture_output=True, text=True, timeout=120).stdout
        except (OSError, subprocess.SubprocessError):
            text = ''
        all_states = states(text)
        killed = set()
        for name, new in added.items():
            if not new:
                continue
            match = Matcher(new)
            for sid, addrs in all_states:
                if sid in killed or not any(a in match for a in addrs):
                    continue
                r = subprocess.run([PFCTL, '-k', 'id', '-k', sid], capture_output=True, timeout=30)
                if r.returncode == 0:
                    killed.add(sid)
                    summary['lists'][name]['cut'] += 1
        summary['cut'] = len(killed)

    for name in added:
        # what was compared, not a second reading: an entry added since is new at the next run
        save(snapshot_path(name), sorted(current[name]))
        info = summary['lists'][name]
        if info.get('added') or info.get('cut'):
            log(f"{name}: {info['added']} new entr{'y' if info['added'] == 1 else 'ies'} of {info['entries']}, "
                f"{info['cut']} open connection{'' if info['cut'] == 1 else 's'} cut")

    totals = status.get('totals', {})
    for name, info in summary['lists'].items():
        totals[name] = totals.get(name, 0) + info.get('cut', 0)
    summary['totals'] = totals
    if not lists:
        summary['message'] = 'cut when listed: no list watched'
    else:
        parts = []
        for name in lists:
            info = summary['lists'].get(name, {})
            parts.append(f"{name} ({info['error']})" if 'error' in info else
                         f"{name} ({info['entries']} entries, {info['cut']} cut now, {totals.get(name, 0)} in all)")
        summary['message'] = 'cut when listed: ' + ', '.join(parts)
    save(STATUS, summary)
    return summary


def main(argv):
    if '--status' in argv:
        print(json.dumps(load(STATUS, {}), sort_keys=True))
        return 0
    try:
        print(json.dumps(run(), sort_keys=True))
    except Exception as e:      # never fail the caller: cron or the settings page
        log(f'stopped: {e}')
        print(json.dumps({'message': f'cut when listed: stopped - {e}', 'cut': 0}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
