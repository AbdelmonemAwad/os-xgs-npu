#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Cut the TLS connections whose client presents a listed JA4 fingerprint.

JA4 is a fingerprint of a TLS ClientHello - the versions, ciphers, extensions and protocols a client
offers - so it names the software that opened a connection rather than the address it went to: a
program that changes its server every hour keeps the ClientHello it was built with. OPNsense's
Intrusion Detection (Suricata) writes one for every TLS connection it sees once its TLS log carries
the JA4 field (Services: Intrusion Detection: Administration, advanced mode: "Enable eve TLS logging",
and "ja4" in "Eve TLS custom logging"); IDS mode, which only listens, is enough. XGS: Settings names
the fingerprints to cut (general.cut_ja4): for each connection the log shows with one of them, every
pf state of that connection is killed by id, and the coprocessor lets go of it with its state.

The ClientHello is a connection's first data, and the flow maker puts a connection into hardware
after it, so the log has the fingerprint of a connection however fast it then runs - measured in
docs/measurements/ja4-cut-2026-10-19.md.

    ja4_cut.py              read what the log gained since the last run, cut, print a JSON summary
                            (XGS: Settings's apply)
    ja4_cut.py --for 58     the same every 2 seconds for 58 seconds (cron, every minute), so a
                            connection is cut within seconds of its ClientHello
    ja4_cut.py --status     print the last run's record
    ja4_cut.py --seen       the fingerprints in the log now, the most frequent first, each with the
                            last connection that presented it - to find the one to list

A fingerprint newly listed is looked for in the whole log since it was last rotated, so listing one
cuts what is already open with it. A state is cut only when pf made it no later than the log line
that names its connection, so a later connection that happens to reuse the same ports is never taken
for it. Never fails its caller; what it could not do is in the summary and in the XGS log.
"""
import fcntl
import ipaddress
import json
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime

CONFIG = '/conf/config.xml'
EVE = '/var/log/suricata/eve.json'
STATE_DIR = '/var/db/os-xgs-npu/ja4'
STATUS = os.path.join(STATE_DIR, 'status.json')
LOCK = os.path.join(STATE_DIR, 'lock')
PFCTL = '/sbin/pfctl'
TAG = 'xgs-ja4'
EVERY = 2           # seconds between two readings of the log in --for
SLACK = 5           # seconds a state may be younger than its log line: Suricata stamps the packet

# protocol, TLS version, SNI or not, cipher and extension counts, the ALPN's first and last character;
# then the two truncated hashes - https://github.com/FoxIO-LLC/ja4
JA4 = re.compile(r'^[tqd][0-9a-z]{2}[di][0-9]{4}[0-9A-Za-z]{2}_[0-9a-f]{12}_[0-9a-f]{12}$')


def log(message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, '-p', 'daemon.notice', message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def settings():
    """(the fingerprints listed, what is wrong with the IDS's TLS log for this, entries not read)."""
    try:
        root = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return [], 'config.xml could not be read', []
    listed, bad = [], []
    for item in re.split(r'[\s,]+', root.findtext('OPNsense/XGS/general/cut_ja4') or ''):
        if not item:
            continue
        if JA4.match(item):
            if item not in listed:
                listed.append(item)
        else:
            bad.append(item)
    ids = root.find('OPNsense/IDS/general')
    problem = None
    if ids is None or (ids.findtext('enabled') or '0') != '1':
        problem = 'Intrusion Detection is off'
    elif (ids.findtext('eveLog/tls/enable') or '0') != '1':
        problem = 'the Intrusion Detection does not log TLS'
    elif 'ja4' not in (ids.findtext('eveLog/tls/custom') or '').split(','):
        problem = 'the Intrusion Detection\'s TLS log has no JA4'
    return listed, problem, bad


def stamp(text):
    """Suricata's timestamp, 2026-10-19T03:03:00.641496+0000, as seconds since the epoch."""
    try:
        return datetime.strptime(text, '%Y-%m-%dT%H:%M:%S.%f%z').timestamp()
    except (TypeError, ValueError):
        return None


def tls_events(fh):
    """(offset of the next whole line, [event]) for every TLS line with a JA4 from fh's position;
    a last line not yet ended is left for the next reading."""
    out = []
    pos = fh.tell()
    for raw in iter(fh.readline, b''):
        if not raw.endswith(b'\n'):
            break
        pos += len(raw)
        if b'"event_type":"tls"' not in raw or b'"ja4"' not in raw:
            continue
        try:
            ev = json.loads(raw)
            fp = ev['tls']['ja4']
            out.append({
                'ja4': fp, 'at': stamp(ev.get('timestamp')), 'proto': str(ev.get('proto', '')).lower(),
                'src': (str(ipaddress.ip_address(ev['src_ip'])), int(ev['src_port'])),
                'dst': (str(ipaddress.ip_address(ev['dest_ip'])), int(ev['dest_port'])),
                'offset': pos - len(raw),
            })
        except (ValueError, KeyError, TypeError):
            continue
    return pos, out


ENDPOINT_V6 = re.compile(r'^([0-9A-Fa-f:.]+)\[(\d+)\]$')
AGE = re.compile(r'\bage (\d+):(\d{2}):(\d{2})\b')


def endpoint(tok):
    """(address, port) from a `pfctl -ss` token: IPv4 address:port, IPv6 address[port]."""
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


def states(text):
    """[(id, protocol, {(address, port)}, age in seconds or None)] from `pfctl -ss -vv`: both ends,
    and the one a translated state was translated from."""
    out, cur = [], None
    for line in text.splitlines():
        if line and not line[0].isspace():
            words = line.split()
            ends = {e for e in (endpoint(t) for t in words[2:]) if e}
            cur = {'proto': words[1] if len(words) > 1 else '', 'ends': ends, 'age': None}
            continue
        if cur is None:
            continue
        m = AGE.search(line)
        if m:
            cur['age'] = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
        m = re.match(r'\s+id: ([0-9a-f]+)', line)
        if m:
            out.append((m.group(1), cur['proto'], cur['ends'], cur['age']))
            cur = None
    return out


def to_cut(events, all_states, now):
    """{state id: event}: the states of the connections the events name, made no later than the event."""
    out = {}
    for sid, proto, ends, age in all_states:
        for ev in events:
            if ev['proto'] and proto != ev['proto']:
                continue
            if ev['src'] not in ends or ev['dst'] not in ends:
                continue
            if age is not None and ev['at'] is not None and now - age > ev['at'] + SLACK:
                continue    # made after the line: a later connection on the same ports
            out[sid] = ev
            break
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


class Watch:
    """One reading of the log at a time, with where the last one stopped kept in memory and saved
    when something was cut and at the end - not every two seconds."""

    def __init__(self):
        self.status = load(STATUS, {})
        self.config_mtime = None
        self.listed, self.problem, self.bad = [], None, []

    def reload(self):
        try:
            m = os.stat(CONFIG).st_mtime
        except OSError:
            m = None
        if m != self.config_mtime:
            self.config_mtime = m
            self.listed, self.problem, self.bad = settings()

    def once(self):
        self.reload()
        st = self.status
        listed = self.listed
        before = set(st.get('listed', []))
        summary = {'run': int(time.time()), 'listed': len(listed), 'read': 0, 'matched': 0, 'cut': 0}
        if not listed:
            st['listed'] = []
            return summary
        fresh = [f for f in listed if f not in before]
        try:
            info = os.stat(EVE)
        except OSError:
            summary['error'] = 'no Suricata log at ' + EVE
            return summary
        same_file = st.get('inode') == info.st_ino and st.get('offset', 0) <= info.st_size
        known = st.get('offset', 0) if same_file else 0
        start = 0 if fresh else known
        if start >= info.st_size:
            st['listed'] = listed
            return summary
        with open(EVE, 'rb') as fh:
            fh.seek(start)
            end, events = tls_events(fh)
        summary['read'] = len(events)
        want = set(listed)
        matched = [e for e in events if e['ja4'] in want and (e['ja4'] in fresh or e['offset'] >= known)]
        summary['matched'] = len(matched)
        st.update({'inode': info.st_ino, 'offset': end, 'listed': listed})
        if not matched:
            return summary
        try:
            text = subprocess.run([PFCTL, '-ss', '-vv'], capture_output=True, text=True, timeout=120).stdout
        except (OSError, subprocess.SubprocessError):
            summary['error'] = 'pfctl -ss could not be read'
            log(f"{len(matched)} connection{'' if len(matched) == 1 else 's'} with a listed fingerprint "
                "not cut: pfctl -ss could not be read")
            return summary
        cuts = to_cut(matched, states(text), time.time())
        per_conn = {}
        totals = st.setdefault('totals', {})
        for sid, ev in cuts.items():
            r = subprocess.run([PFCTL, '-k', 'id', '-k', sid], capture_output=True, timeout=30)
            if r.returncode != 0:
                continue
            summary['cut'] += 1
            key = (ev['ja4'], ev['src'], ev['dst'])
            per_conn[key] = per_conn.get(key, 0) + 1
        for (fp, src, dst), n in per_conn.items():
            totals[fp] = totals.get(fp, 0) + 1
            log(f"{fp}: {fmt(src)} -> {fmt(dst)} cut ({n} state{'' if n == 1 else 's'})")
        summary['connections'] = len(per_conn)
        if per_conn:
            self.save()
        return summary

    def message(self, summary):
        if 'error' in summary:
            text = summary['error']
        elif not self.listed:
            text = 'no fingerprint listed'
        else:
            n = len(self.listed)
            text = (f"{n} fingerprint{'' if n == 1 else 's'} listed, "
                    f"{summary.get('connections', 0)} connection{'' if summary.get('connections', 0) == 1 else 's'} cut now, "
                    f"{sum(self.status.get('totals', {}).get(f, 0) for f in self.listed)} in all")
        if self.listed and self.problem:
            text += ' - ' + self.problem
        if self.bad:
            text += ' - not a JA4 fingerprint: ' + ', '.join(self.bad[:3])
        return 'cut by TLS fingerprint: ' + text

    def save(self):
        save(STATUS, self.status)


def fmt(end):
    addr, port = end
    return f'[{addr}]:{port}' if ':' in addr else f'{addr}:{port}'


def seen(limit=50):
    """The fingerprints in the log now, the most frequent first."""
    counts, last = {}, {}
    try:
        with open(EVE, 'rb') as fh:
            _, events = tls_events(fh)
    except OSError:
        return {'message': 'no Suricata log at ' + EVE, 'seen': []}
    for e in events:
        counts[e['ja4']] = counts.get(e['ja4'], 0) + 1
        last[e['ja4']] = e
    rows = sorted(counts, key=lambda f: (-counts[f], f))[:limit]
    return {'seen': [{'ja4': f, 'connections': counts[f], 'last': f"{fmt(last[f]['src'])} -> {fmt(last[f]['dst'])}"}
                     for f in rows]}


def main(argv):
    if '--status' in argv:
        print(json.dumps(load(STATUS, {}), sort_keys=True))
        return 0
    if '--seen' in argv:
        print(json.dumps(seen(), indent=1))
        return 0
    seconds = 0
    if '--for' in argv:
        try:
            seconds = max(0, int(argv[argv.index('--for') + 1]))
        except (IndexError, ValueError):
            seconds = 0
    try:
        os.makedirs(STATE_DIR, exist_ok=True)
        lock = open(LOCK, 'w')
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            # the minute's watcher is reading: it takes a changed list within two seconds
            last = load(STATUS, {}).get('last', {})
            print(json.dumps({'message': last.get('message', 'cut by TLS fingerprint: watched every two seconds'),
                              'cut': 0}))
            return 0
        w = Watch()
        until = time.monotonic() + seconds
        cut = conns = 0
        while True:
            summary = w.once()
            cut += summary['cut']
            conns += summary.get('connections', 0)
            if not w.listed and seconds:
                break       # nothing to watch: the next minute's run looks again
            if time.monotonic() + EVERY > until:
                break
            time.sleep(EVERY)
        summary['cut'], summary['connections'] = cut, conns
        summary['message'] = w.message(summary)
        w.status['last'] = summary
        w.save()
        print(json.dumps(summary, sort_keys=True))
    except Exception as e:      # never fail the caller: cron or the settings page
        log(f'stopped: {e}')
        print(json.dumps({'message': f'cut by TLS fingerprint: stopped - {e}', 'cut': 0}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
