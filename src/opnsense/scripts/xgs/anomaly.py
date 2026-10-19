#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Watch for unusual traffic, and say so - never block.

Every minute (cron) pf's state table is read at the start and at the end of a window, and for each
host that opened connections in it this works out what it did in that window: bytes it sent and
received, connections it opened, new addresses it reached, and the most ports it tried on any one of
them. Then two kinds of alert, each to the XGS log (tag xgs-anomaly) and to the status this keeps:

  - a pattern no host has a normal for: a PORT SCAN, one host trying SCAN_PORTS ports or more on one
    address within the window, and a SWEEP, one host reaching SWEEP_HOSTS new addresses or more;
  - a host far beyond ITS OWN normal: for bytes sent, bytes received and connections opened, each
    host keeps a moving mean and spread of its windows (exponential, ALPHA a window), and a window
    more than SPREAD spreads above the mean - and above a floor, so that a quiet host's first
    download is not news - is reported, once the host has WARMUP windows of history.

An inbound state is read once per connection - its "<-" state, on the interface it arrived on - so a
routed connection, which pf keeps a state for on each interface, is not counted twice. On the XGS
3300, with "Count hardware traffic in pf" on, a state holds what the coprocessor forwarded too; on
the XGS 136 a connection in hardware stops adding to its state, so the volumes there undercount and
the connection patterns are what is complete. XGS: Settings, "Watch for unusual traffic", turns this
on; it is off until then.

    anomaly.py --for 50     one window of 50 seconds (cron, every minute)
    anomaly.py --status     the last window, the hosts followed, the last alerts
    anomaly.py --for 10 --warmup 3 --dir /tmp/x     a test: shorter, with history of its own
"""
import ipaddress
import json
import math
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

CONFIG = '/conf/config.xml'
STATE_DIR = '/var/db/os-xgs-npu/anomaly'
STATUS = os.path.join(STATE_DIR, 'status.json')
BASELINE = os.path.join(STATE_DIR, 'baseline.json')
PFCTL = '/sbin/pfctl'
TAG = 'xgs-anomaly'

SCAN_PORTS = 100            # ports on one address within a window
SWEEP_HOSTS = 256           # new addresses within a window
ALPHA = 0.02                # a window's weight in a host's moving mean: about the last fifty
SPREAD = 6.0                # how many spreads above the mean is unusual
WARMUP = 60                 # windows a host must have been followed before its volumes are judged
FLOORS = {'sent': 200e6, 'received': 1e9, 'opened': 500}     # never unusual below these, a window
QUIET = 600                 # seconds before the same host is reported for the same thing again
FORGET = 7 * 86400          # a host not seen for this long is no longer followed
KEEP_ALERTS = 100

ENDPOINT_V6 = re.compile(r'^([0-9A-Fa-f:.]+)\[(\d+)\]$')
AGE = re.compile(r'\bage (\d+):(\d{2}):(\d{2})\b')
COUNTS = re.compile(r'(\d+):(\d+) pkts, (\d+):(\d+) bytes')


def log(message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, '-p', 'daemon.notice', message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def enabled():
    try:
        root = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return False
    return (root.findtext('OPNsense/XGS/general/anomaly_watch') or '0') == '1'


def endpoint(tok):
    """(address, port) from a `pfctl -ss` token: IPv4 address:port, IPv6 address[port]; or None."""
    tok = tok.strip('()')
    m = ENDPOINT_V6.match(tok)
    if m:
        addr, port = m.group(1), m.group(2)
    elif tok.count(':') == 1 and '.' in tok:
        addr, port = tok.split(':', 1)
    else:
        return None
    try:
        return str(ipaddress.ip_address(addr)), int(port)
    except ValueError:
        return None


def inbound_states(text):
    """{id: state} for the "<-" states of `pfctl -ss -vv`: each connection once, on the interface it
    arrived on. A state: proto, src (the opener), dst (as it arrived: the address in parentheses
    when pf translated it), age in seconds, and bytes [from the opener, back to it]."""
    out, cur = {}, None
    for line in text.splitlines():
        if line and not line[0].isspace():
            cur = None
            words = line.split()
            if '<-' not in words or len(words) < 5:
                continue
            i = words.index('<-')
            left = [e for e in (endpoint(t) for t in words[2:i]) if e]
            right = [e for e in (endpoint(t) for t in words[i + 1:]) if e]
            if not left or not right:
                continue
            # "dst (original dst) <- src": the original is what came in on the wire
            dst = left[1] if len(left) > 1 else left[0]
            cur = {'proto': words[1], 'src': right[0][0], 'dst': dst[0], 'dport': dst[1],
                   'age': None, 'bytes': [0, 0]}
            continue
        if cur is None:
            continue
        m = AGE.search(line)
        if m:
            cur['age'] = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
        m = COUNTS.search(line)
        if m:
            cur['bytes'] = [int(m.group(3)), int(m.group(4))]
        m = re.match(r'\s+id: ([0-9a-f]+)', line)
        if m:
            out[m.group(1)] = cur
            cur = None
    return out


def snapshot():
    try:
        r = subprocess.run([PFCTL, '-ss', '-vv'], capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.SubprocessError):
        return None
    return inbound_states(r.stdout) if r.returncode == 0 else None


def window(before, after, seconds):
    """{host: features} for the window between two snapshots."""
    hosts = {}
    for sid, st in after.items():
        h = hosts.setdefault(st['src'], {'sent': 0, 'received': 0, 'opened': 0, 'dsts': set(),
                                         'ports': {}})
        old = before.get(sid)
        if old is not None and old['src'] == st['src']:
            h['sent'] += max(0, st['bytes'][0] - old['bytes'][0])
            h['received'] += max(0, st['bytes'][1] - old['bytes'][1])
            continue
        if st['age'] is not None and st['age'] > seconds + 5:
            continue        # older than the window: there before it, under an id not read then
        h['sent'] += st['bytes'][0]
        h['received'] += st['bytes'][1]
        h['opened'] += 1
        h['dsts'].add(st['dst'])
        h['ports'].setdefault(st['dst'], set()).add((st['proto'], st['dport']))
    out = {}
    for host, h in hosts.items():
        scan_dst, scan_n = None, 0
        for dst, ports in h['ports'].items():
            if len(ports) > scan_n:
                scan_dst, scan_n = dst, len(ports)
        out[host] = {'sent': h['sent'], 'received': h['received'], 'opened': h['opened'],
                     'new_addresses': len(h['dsts']), 'scan_dst': scan_dst, 'scan_ports': scan_n}
    return out


def fmt_bytes(n):
    for unit in ('B', 'kB', 'MB', 'GB', 'TB'):
        if n < 1000 or unit == 'TB':
            return ('%d %s' % (n, unit)) if unit == 'B' else ('%.1f %s' % (n, unit))
        n /= 1000.0


def judge(features, baseline, now, warmup=WARMUP):
    """[(host, kind, message)] for one window, and the baselines moved by it."""
    alerts = []
    for host, f in sorted(features.items()):
        if f['scan_ports'] >= SCAN_PORTS:
            alerts.append((host, 'scan', '%s tried %d ports on %s within a minute - a port scan'
                           % (host, f['scan_ports'], f['scan_dst'])))
        if f['new_addresses'] >= SWEEP_HOSTS:
            alerts.append((host, 'sweep', '%s reached %d new addresses within a minute - a sweep'
                           % (host, f['new_addresses'])))
        b = baseline.setdefault(host, {'n': 0, 'seen': now})
        for k in ('sent', 'received', 'opened'):
            x = float(f[k])
            m, v = b.get(k, [0.0, 0.0])
            if b['n'] >= warmup and x > FLOORS[k] and x > m + SPREAD * math.sqrt(v):
                if k == 'opened':
                    text = '%s opened %d connections in a minute, where it usually opens %.0f' % (host, x, m)
                else:
                    verb = {'sent': ('sent', 'sends'), 'received': ('received', 'receives')}[k]
                    text = '%s %s %s in a minute, where it usually %s %s' % (
                        host, verb[0], fmt_bytes(x), verb[1], fmt_bytes(m))
                alerts.append((host, k, text))
            if b['n'] == 0:
                m, v = x, 0.0       # the first window is the mean to start from, not zero
            else:
                d = x - m
                m += ALPHA * d
                v = (1 - ALPHA) * (v + ALPHA * d * d)
            b[k] = [m, v]
        b['n'] += 1
        b['seen'] = now
    return alerts


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


def run(seconds, warmup=WARMUP):
    os.makedirs(STATE_DIR, exist_ok=True)
    status = load(STATUS, {})
    if not enabled():
        # written once when it goes off, not every minute while it stays off
        if status.get('last', {}).get('message') != 'watching for unusual traffic: off':
            status['last'] = {'run': int(time.time()), 'message': 'watching for unusual traffic: off'}
            save(STATUS, status)
        return status['last']
    before = snapshot()
    time.sleep(seconds)
    after = snapshot()
    now = int(time.time())
    if before is None or after is None:
        summary = {'run': now, 'message': 'watching for unusual traffic: pf states could not be read'}
        status['last'] = summary
        save(STATUS, status)
        return summary
    feats = window(before, after, seconds)
    baseline = load(BASELINE, {})
    alerts = judge(feats, baseline, now, warmup)
    for host in [h for h, b in baseline.items() if now - b.get('seen', now) > FORGET]:
        del baseline[host]
    quiet = status.get('quiet', {})
    sent = []
    for host, kind, message in alerts:
        key = host + ' ' + kind
        if now - quiet.get(key, 0) < QUIET:
            continue
        quiet[key] = now
        log(message)
        sent.append({'at': now, 'host': host, 'kind': kind, 'message': message})
    status['quiet'] = {k: t for k, t in quiet.items() if now - t < QUIET}
    status['alerts'] = (status.get('alerts', []) + sent)[-KEEP_ALERTS:]
    busiest = sorted(feats.items(), key=lambda kv: -(kv[1]['sent'] + kv[1]['received']))[:5]
    summary = {'run': now, 'window': seconds, 'hosts': len(feats), 'followed': len(baseline),
               'alerts': len(sent),
               'busiest': [{'host': h, 'sent': f['sent'], 'received': f['received'], 'opened': f['opened'],
                            'new_addresses': f['new_addresses'], 'scan_ports': f['scan_ports']} for h, f in busiest]}
    summary['message'] = ('watching for unusual traffic: %d host%s in the last window, %d followed, %d alert%s'
                          % (summary['hosts'], '' if summary['hosts'] == 1 else 's', summary['followed'],
                             summary['alerts'], '' if summary['alerts'] == 1 else 's'))
    status['last'] = summary
    save(BASELINE, baseline)
    save(STATUS, status)
    return summary


def main(argv):
    global STATE_DIR, STATUS, BASELINE
    if '--status' in argv:
        print(json.dumps(load(STATUS, {}), sort_keys=True))
        return 0
    seconds = 50
    warmup = WARMUP
    if '--for' in argv:
        try:
            seconds = max(5, min(55, int(argv[argv.index('--for') + 1])))
        except (IndexError, ValueError):
            pass
    if '--warmup' in argv:      # for a test: how many windows before volumes are judged
        try:
            warmup = max(0, int(argv[argv.index('--warmup') + 1]))
        except (IndexError, ValueError):
            pass
    if '--dir' in argv:         # for a test: history and status of its own, apart from the minute's
        try:
            STATE_DIR = argv[argv.index('--dir') + 1]
        except IndexError:
            pass
        STATUS = os.path.join(STATE_DIR, 'status.json')
        BASELINE = os.path.join(STATE_DIR, 'baseline.json')
    try:
        print(json.dumps(run(seconds, warmup), sort_keys=True))
    except Exception as e:      # never fail cron
        log(f'stopped: {e}')
        print(json.dumps({'message': f'watching for unusual traffic: stopped - {e}'}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
