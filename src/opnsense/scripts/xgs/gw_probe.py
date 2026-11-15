#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Gateway quality alerts (plan 01, package 3.8; XGS: Settings, "Gateway jitter alert" and "TCP probe"):
an alert, never a decision. OPNsense's own gateway monitoring (dpinger, System: Gateways) keeps deciding
whether a gateway is up and which one carries the traffic; a second monitor taking gateways down as well
would have two sides deciding one state, and they would flap. So this only tells, through the alert files
os-netreport sends, of what that monitoring does not act on:

  JITTER     a monitored gateway whose round-trip time varies more than gw_jitter_alert milliseconds -
             dpinger's own standard deviation, the RTTd column of System: Gateways - and when it is
             back under; read from OPNsense's gateway_status.php
  TCP PROBE  gw_tcp_probe, "host:port": a TCP connection opened to it from the firewall, by its routing
             table, each minute; three failures in a row raise an alert, the first success after them
             another. Some paths answer ICMP and drop TCP, or the reverse.

    gw_probe.py         one look, from the plugin's cron job every minute; prints what it found

The state between looks is in /var/db/os-xgs-npu/gw-probe.json, so an alert is raised once, when a
reading crosses, and not each minute it stays over.
"""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

CONFIG = '/conf/config.xml'
STATUS = '/usr/local/opnsense/scripts/routes/gateway_status.php'
STATE = '/var/db/os-xgs-npu/gw-probe.json'
OUTBOX = '/var/db/os-xgs-npu/alerts'
FAILS = 3


def ms(text):
    """dpinger's "1.2 ms" (or a number) in milliseconds; None for "~", what an unmonitored gateway shows."""
    try:
        return float(str(text).replace('ms', '').strip())
    except ValueError:
        return None


def jitter_turns(state, readings, limit):
    """[(gateway, 'over' or 'back', value)] for the gateways whose jitter crossed limit since state, and
    the new state: {gateway: True while over}."""
    turns = []
    new = {}
    for name, value in sorted(readings.items()):
        if value is None:
            continue
        over = value > limit
        new[name] = over
        if over != bool(state.get(name, False)):
            turns.append((name, 'over' if over else 'back', value))
    return turns, new


def probe_turn(state, ok, fails_needed=FAILS):
    """The TCP probe's turn - 'down' at the fails_needed-th failure in a row, 'back' at the first success
    after one - or None, and the new state {"fails", "down"}."""
    fails = 0 if ok else int(state.get('fails', 0)) + 1
    down = bool(state.get('down', False))
    turn = None
    if not ok and not down and fails >= fails_needed:
        down, turn = True, 'down'
    elif ok and down:
        down, turn = False, 'back'
    return turn, {'fails': fails, 'down': down}


def target(text):
    """"host:port" or "[v6]:port" into (host, port); None for anything else."""
    text = (text or '').strip()
    if text.startswith('['):
        host, _, rest = text[1:].partition(']')
        port = rest[1:] if rest.startswith(':') else ''
    else:
        host, _, port = text.rpartition(':')
    if not host or not port.isdigit() or not 1 <= int(port) <= 65535:
        return None
    return host, int(port)


def connect_ms(host, port, timeout=3.0):
    started = time.monotonic()
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return (time.monotonic() - started) * 1000.0
    except OSError:
        return None


def setting(name):
    try:
        return (ET.parse(CONFIG).getroot().findtext('OPNsense/XGS/general/' + name) or '').strip()
    except (OSError, ET.ParseError):
        return ''


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=os.path.dirname(path), prefix='.gw-probe-')
    with os.fdopen(handle, 'w', encoding='utf-8') as stream:
        json.dump(obj, stream)
    os.replace(temporary, path)


def alert(kind, subject, text, facts):
    """Said in the system log, and written for os-netreport to send (the alert file health.py writes)."""
    at = int(time.time())
    try:
        subprocess.run(['/usr/bin/logger', '-t', 'xgs-gateway', '-p', 'daemon.warning', subject + ': ' + text],
                       stdin=subprocess.DEVNULL, capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass
    write_json(os.path.join(OUTBOX, '%s-%d-%d.json' % (kind, at, os.getpid())),
               {'source': 'os-xgs-npu', 'kind': kind, 'at': at, 'subject': subject, 'text': text, 'facts': facts})


def main():
    limit = setting('gw_jitter_alert')
    probe = target(setting('gw_tcp_probe'))
    if (not limit or limit == '0') and probe is None:
        return 0
    try:
        with open(STATE, encoding='utf-8') as f:
            state = json.load(f)
    except (OSError, ValueError):
        state = {}
    found = []
    if limit and limit != '0':
        try:
            status = json.loads(subprocess.run([STATUS], capture_output=True, text=True, timeout=20).stdout or '{}')
        except (OSError, ValueError, subprocess.SubprocessError):
            status = {}
        readings = {name: ms(g.get('stddev')) for name, g in status.items() if isinstance(g, dict)}
        turns, state['jitter'] = jitter_turns(state.get('jitter', {}), readings, float(limit))
        for name, turn, value in turns:
            alert('gateway-jitter', 'Gateway %s: jitter %s' % (name, 'over the limit' if turn == 'over' else 'back under it'),
                  'Its round-trip time varies by %.1f ms (the standard deviation dpinger measures), against the %s ms '
                  'XGS: Settings alerts at.' % (value, limit), {'gateway': name, 'jitter_ms': value, 'limit_ms': float(limit)})
        found.append('jitter: %d monitored, %d over' % (sum(1 for v in readings.values() if v is not None),
                                                         sum(1 for v in state['jitter'].values() if v)))
    if probe is not None:
        took = connect_ms(*probe)
        name = '%s:%d' % probe
        # a new target starts its own count: failures of the one before are not its own
        was = state.get('probe', {}) if state.get('probe', {}).get('target') == name else {}
        turn, state['probe'] = probe_turn(was, took is not None)
        state['probe']['target'] = name
        if turn is not None:
            alert('gateway-tcp', 'TCP probe to %s:%d %s' % (probe[0], probe[1], 'failing' if turn == 'down' else 'answering again'),
                  ('%d connections in a row failed, a minute apart' % FAILS) if turn == 'down' else
                  'A connection opened in %.0f ms.' % took, {'target': '%s:%d' % probe, 'connect_ms': took})
        found.append('probe: ' + ('%.0f ms' % took if took is not None else 'failed (%d in a row)' % state['probe']['fails']))
    write_json(STATE, state)
    print('; '.join(found))
    return 0


if __name__ == '__main__':
    sys.exit(main())
