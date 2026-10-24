#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The Captive Portal's logged-in clients as the coprocessor's live users, so its zones can run in
hardware - XGS: Settings, "Captive Portal clients in hardware" (general.cp_hardware).

The portal lets a client through by putting its address in a pf table, and takes it out again at a
logout, a timeout or a disconnect. Until this, every enabled zone was kept with the host
(flow_exclude.php): a connection the coprocessor forwards outlives the pf state the portal kills.
The fast path has its own answer to that, the live users (discovery 5, docs/measurements): a
connection made with a user is given back at once, every one of them, when the user is deleted. So
each session the portal has is given a user number here, its addresses are written to the driver
(dp.live_users on the XGS 3300, flow.live_users on the XGS 136), and a session that ends is gone
from the next table - which deletes its user and takes its connections out of hardware.

What the portal reads from traffic it reads from ipfw's counts, which see nothing the coprocessor
forwards: the idle timeout (a client in hardware would look idle) and the RADIUS accounting (it
would look silent). The driver counts what the coprocessor carried for each user; this writes those
counts to hw-counts.json, and cp_overlay.py's lines in the portal's accounting add them to ipfw's.
So a zone goes to hardware when the setting is on and either it needs no counts - no idle timeout,
no RADIUS server with accounting - or the overlay is on. Any other zone stays with the host, as
before, and zones.json says why, for XGS: Settings and for flow_exclude.php.

    cp_users.py             one pass: decide the zones, write the table, read the counts; print JSON
    cp_users.py --for 58    the same every 2 seconds for 58 seconds (cron, every minute)
    cp_users.py --status    print the last run's record

A session keeps its number while the portal has it; numbers are not given again until the 65,535
have gone round. Its counts only grow while the session lasts - the portal takes a smaller total for
a reset and counts it all again - so what was counted before the driver forgot it (a zone that went
back to the host, a driver loaded again) is kept here and added. Never fails its caller.
"""
import fcntl
import ipaddress
import json
import os
import sqlite3
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cp_overlay  # noqa: E402

CONFIG = '/conf/config.xml'
DB = '/var/captiveportal/captiveportal.sqlite'
STATE_DIR = '/var/db/os-xgs-npu/cp'
STATE = os.path.join(STATE_DIR, 'users.json')
ZONES = os.path.join(STATE_DIR, 'zones.json')
COUNTS = cp_overlay.COUNTS
STATUS = os.path.join(STATE_DIR, 'status.json')
LOCK = os.path.join(STATE_DIR, 'lock')
OIDS = ('dev.octep.0.dp.live_users', 'dev.npuep.0.flow.live_users')
PFCTL = '/sbin/pfctl'
EXCLUDE = '/usr/local/opnsense/scripts/xgs/flow_exclude.php'
TAG = 'xgs-cp'
EVERY = 2
LUID_LAST = 65535
KEYS = ('in_pkts', 'in_bytes', 'out_pkts', 'out_bytes')


def log(message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, '-p', 'daemon.notice', message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


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
    os.chmod(tmp, 0o644)
    os.replace(tmp, path)


def number(text):
    text = (text or '').strip()
    return int(text) if text.isdigit() else 0


def settings():
    """(the setting, [zone]): each enabled zone with what decides where it runs."""
    try:
        root = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return False, []
    on = (root.findtext('OPNsense/XGS/general/cp_hardware') or '0') == '1'
    # A RADIUS server sends the portal's accounting when it has an accounting port (Radius.php)
    accounting = {s.findtext('name'): s for s in root.findall('system/authserver')
                  if (s.findtext('type') or '') == 'radius' and (s.findtext('radius_acct_port') or '').strip()}
    zones = []
    for z in root.findall('OPNsense/captiveportal/zones/zone'):
        if (z.findtext('enabled') or '0') != '1':
            continue
        servers = [s for s in (z.findtext('authservers') or '').split(',') if s]
        zones.append({
            'zoneid': number(z.findtext('zoneid')),
            'name': (z.findtext('description') or '').strip() or 'zone ' + (z.findtext('zoneid') or '?'),
            'idletimeout': number(z.findtext('idletimeout')),
            'hardtimeout': number(z.findtext('hardtimeout')),
            'accounting': sorted(s for s in servers if s in accounting),
        })
    return on, zones


def place(on, zones, overlay):
    """Each zone's place, hardware or host; reason is what XGS: Settings words (off, no_counts,
    counted, plain), why the same in English for the log."""
    for z in zones:
        needs = []
        if z['idletimeout']:
            needs.append('an idle timeout')
        if z['accounting']:
            needs.append('RADIUS accounting (' + ', '.join(z['accounting']) + ')')
        z['needs_counts'] = bool(needs)
        if not on:
            z['where'], z['reason'], z['why'] = 'host', 'off', 'Captive Portal clients in hardware is off'
        elif needs and not overlay.get('on'):
            z['where'], z['reason'] = 'host', 'no_counts'
            z['why'] = (' and '.join(needs) + ' need the counts of what the coprocessor carries, and they cannot '
                        'reach the portal: ' + (overlay.get('why') or 'the overlay is not on'))
        elif needs:
            z['where'], z['reason'] = 'hardware', 'counted'
            z['why'] = ' and '.join(needs) + ': the coprocessor\'s counts are added to the portal\'s'
        else:
            z['where'], z['reason'], z['why'] = 'hardware', 'plain', ''
    return zones


def pf_table(zoneid):
    try:
        out = subprocess.run([PFCTL, '-t', '__captiveportal_zone_%d' % zoneid, '-T', 'show'],
                             capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return set()
    return {line.strip() for line in out.splitlines() if line.strip() and '/' not in line}


def sessions():
    """{(zoneid, sessionid): {'ip', 'ips'}} for every session the portal has now, read-only."""
    out = {}
    if not os.path.exists(DB):
        return out
    con = sqlite3.connect('file:' + DB + '?mode=ro', uri=True, timeout=5)
    try:
        for zoneid, sid, ip in con.execute('select zoneid, sessionid, ip_address from cp_clients where deleted = 0'):
            out[(int(zoneid), str(sid))] = {'ip': ip or '', 'ips': {ip} if ip else set()}
        for zoneid, sid, ip in con.execute('select zoneid, sessionid, ip_address from cp_client_ips'):
            if (int(zoneid), str(sid)) in out and ip:
                out[(int(zoneid), str(sid))]['ips'].add(ip)
    finally:
        con.close()
    return out


def token(address, luid):
    a = ipaddress.ip_address(address)
    if a.version == 4:
        return 'a4:%08x=%d' % (int(a), luid)
    return 'a6:%032x=%d' % (int(a), luid)


def driver():
    for oid in OIDS:
        try:
            done = subprocess.run(['/sbin/sysctl', '-n', oid], capture_output=True, text=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0:
            return oid, done.stdout
    return None, None


def read_table(text):
    """({token}, {luid: [in_pkts, in_bytes, out_pkts, out_bytes, at]}) from the driver's table."""
    tokens, counts = set(), {}
    for line in (text or '').splitlines()[1:]:
        words = line.split()
        if not words or '=' not in words[0]:
            continue
        tokens.add(words[0])
        if len(words) >= 9 and words[1] == 'in' and words[4] == 'out' and words[7] == 'at':
            try:
                counts[int(words[0].split('=')[1])] = [int(words[i]) for i in (2, 3, 5, 6, 8)]
            except ValueError:
                pass
    return tokens, counts


class Users:
    """The sessions' numbers and counts, kept in memory and saved when they change."""

    def __init__(self):
        self.state = load(STATE, {})
        self.state.setdefault('next', 1)
        self.state.setdefault('sessions', {})
        self.written = None
        self.config_mtime = None
        self.on, self.zones = False, []
        self.where = None

    def reload(self):
        try:
            m = os.stat(CONFIG).st_mtime
        except OSError:
            m = None
        if m != self.config_mtime:
            self.config_mtime = m
            self.on, self.zones = settings()

    def number(self, key):
        known = self.state['sessions'].get(key)
        if known:
            return known['luid']
        used = {s['luid'] for s in self.state['sessions'].values()}
        luid = self.state['next']
        for _ in range(LUID_LAST):
            if luid not in used:
                break
            luid = luid % LUID_LAST + 1
        self.state['next'] = luid % LUID_LAST + 1
        self.state['sessions'][key] = {'luid': luid, 'ip': '', 'base': [0, 0, 0, 0], 'last': [0, 0, 0, 0], 'at': 0}
        return luid

    def once(self):
        self.reload()
        try:
            overlay = cp_overlay.sync()
        except OSError as e:
            overlay = dict(cp_overlay.status(), error=str(e))
        if overlay.get('changed'):
            log('the Captive Portal\'s accounting overlay: ' + ('on' if overlay.get('on') else 'off')
                + (' - ' + overlay['why'] if overlay.get('why') else ''))
        zones = place(self.on, [dict(z) for z in self.zones], overlay)
        hw = {z['zoneid'] for z in zones if z['where'] == 'hardware'}
        summary = {'run': int(time.time()), 'zones_in_hardware': len(hw), 'listed': 0, 'sessions': 0}
        try:
            have = sessions()
        except sqlite3.Error as e:
            have = None
            summary['error'] = 'the portal\'s database could not be read: %s' % e
        oid, text = driver()
        if oid is None:
            summary['error'] = 'no driver with live users is attached'
        if have is None or oid is None:
            self.publish(zones, overlay, summary, hw)
            return summary
        tables = {}

        def table(zoneid):
            if zoneid not in tables:
                tables[zoneid] = pf_table(zoneid)
            return tables[zoneid]

        # A session the portal has ended keeps its counts in COUNTS while its address is still in
        # the zone's table: the portal takes the address out of pf and ipfw a cycle after it ends
        # the session, and in between it reads ipfw again - without the coprocessor's part it would
        # take the smaller total for a reset and count all of it a second time (measured: an 8 GB
        # stream reported as 33 GB in the RADIUS stop). Its user is gone from the driver at once.
        alive = {'%d:%s' % k for k in have}
        alive_ips = {ip for s in have.values() for ip in s['ips']}
        now = int(time.time())
        for key in list(self.state['sessions']):
            if key in alive:
                self.state['sessions'][key].pop('gone', None)
                continue
            s = self.state['sessions'][key]
            gone = s.setdefault('gone', now)
            if (s['ip'] in alive_ips or now - gone > 60 or
                    s['ip'] not in table(int(key.split(':')[0]))):
                del self.state['sessions'][key]
        tokens = []
        for zoneid in sorted(hw):
            allowed = table(zoneid)
            for (zid, sid), s in sorted(have.items()):
                if zid != zoneid:
                    continue
                ips = sorted(ip for ip in s['ips'] if ip in allowed)
                if not ips:
                    continue
                key = '%d:%s' % (zid, sid)
                luid = self.number(key)
                self.state['sessions'][key]['ip'] = s['ip'] if s['ip'] in ips else ips[0]
                for ip in ips:
                    try:
                        tokens.append(token(ip, luid))
                    except ValueError:
                        continue
                summary['sessions'] += 1
        tokens = tokens[:1024]
        summary['listed'] = len(tokens)
        held, counts = read_table(text)
        if set(tokens) != held:
            done = subprocess.run(['/sbin/sysctl', oid + '=' + ' '.join(tokens)], capture_output=True,
                                  text=True, timeout=60)
            if done.returncode != 0:
                summary['error'] = 'the driver refused the table: ' + (done.stderr.strip() or done.stdout.strip())
            _, text = driver()
            held, counts = read_table(text)
        self.count(counts)
        self.publish(zones, overlay, summary, hw)
        return summary

    def count(self, counts):
        """Each session's counts, never smaller than the last: what the driver forgot is kept."""
        out = {}
        for key, s in self.state['sessions'].items():
            now = counts.get(s['luid'])
            if now is None:
                s['base'] = [b + v for b, v in zip(s['base'], s['last'])]
                s['last'] = [0, 0, 0, 0]
            else:
                if any(n < v for n, v in zip(now[:4], s['last'])):
                    s['base'] = [b + v for b, v in zip(s['base'], s['last'])]
                s['last'] = now[:4]
                s['at'] = max(s['at'], now[4])
            total = [b + v for b, v in zip(s['base'], s['last'])]
            if s['ip'] and any(total):
                entry = out.setdefault(s['ip'], {k: 0 for k in KEYS})
                for k, v in zip(KEYS, total):
                    entry[k] += v
                entry['last_accessed'] = max(entry.get('last_accessed', 0), s['at'])
        if out != load(COUNTS, None):
            save(COUNTS, out)

    def publish(self, zones, overlay, summary, hw):
        for z in zones:
            z['clients'] = sum(1 for k, s in self.state['sessions'].items()
                               if k.split(':')[0] == str(z['zoneid']) and 'gone' not in s)
        save(ZONES, {'setting': self.on, 'overlay': overlay, 'zones': zones, 'run': summary['run']})
        save(STATE, self.state)
        if self.where != hw:
            # a zone that moved: flow_exclude.php keeps the host's zones with the host, read from ZONES
            if self.where is not None:
                log('zones in hardware now: ' + (', '.join(z['name'] for z in zones if z['where'] == 'hardware')
                                                 or 'none'))
            self.where = hw
            try:
                subprocess.run(['/usr/local/bin/php', EXCLUDE, '--if-changed'], capture_output=True, timeout=60)
            except (OSError, subprocess.SubprocessError):
                pass


def message(summary, users):
    if 'error' in summary:
        return 'Captive Portal clients in hardware: ' + summary['error']
    if not users.on:
        return 'Captive Portal clients in hardware: off'
    return ('Captive Portal clients in hardware: %d zone%s in hardware, %d client%s listed'
            % (summary['zones_in_hardware'], '' if summary['zones_in_hardware'] == 1 else 's',
               summary['sessions'], '' if summary['sessions'] == 1 else 's'))


def main(argv):
    if '--status' in argv:
        print(json.dumps(load(STATUS, {}), sort_keys=True))
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
        # A watching run waits for the one before it - one started by the page's apply ends in the
        # middle of a minute, and cron's next would otherwise find it running and leave the rest of
        # that minute unwatched; a single pass does not wait.
        deadline = time.monotonic() + seconds
        while True:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    # another run is watching: it takes a change within two seconds
                    print(json.dumps({'message': load(STATUS, {}).get(
                        'message', 'Captive Portal clients in hardware: watched every two seconds')}))
                    return 0
                time.sleep(1)
        users = Users()
        until = time.monotonic() + seconds
        while True:
            summary = users.once()
            if not users.on and seconds:
                break       # the table is empty now: the next minute's run looks again
            if time.monotonic() + EVERY > until:
                break
            time.sleep(EVERY)
        summary['message'] = message(summary, users)
        save(STATUS, summary)
        print(json.dumps(summary, sort_keys=True))
    except Exception as e:      # never fail the caller: cron or the settings page
        log('stopped: %s' % e)
        print(json.dumps({'message': 'Captive Portal clients in hardware: stopped - %s' % e}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
