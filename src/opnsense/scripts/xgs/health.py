#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The coprocessor's own Linux - how long it has been up, its load, its memory, its memory's error
counters - for the Overview.

    health.py collect    ask the coprocessor now, keep the answer, print it
    health.py show       print what was asked last, without asking
    health.py history    print the samples kept for the Overview's graphs, oldest first

WHY KEPT. The Overview is opened often and the coprocessor is reached through
scripts/xgs/npu-run.sh, which on the XGS 3300 is its console at 115200, one command at a time
for every plugin. So the plugin's cron asks every five minutes - one short command - and the page
shows the last answer with the time it was read. On the XGS 136 the management link answers in a
second, but the page reads the kept answer there too, so the two families behave the same.

WHAT IT SAYS. /proc/uptime: a coprocessor that restarted on its own is up for less time than the
host; /proc/loadavg on its cores, which the fast path keeps busy; MemTotal and MemAvailable.
Measured on the XGS 136 on 2026-10-20: four cores, 3.0 GB, 1.5 GB of it available, a load of 3.2.

THE GRAPHS (2026-11-14). Every collect also keeps a sample - the time, the one-minute load, the
memory in use in per cent, and the connections the coprocessor forwards by itself (the driver's
count, dev.octep.0.dp.flows or dev.npuep.0.flow.conns, read on the host) - in a history of seven
days at five minutes, for the Overview's graphs. A read that failed is a sample with no load and
no memory, so a gap shows as a gap. The same three figures go to Reporting: Health through
OPNsense's own collector (scripts/health/library/OPNsense/RRD/Stats/XGSCoprocessor.php).

ITS MEMORY'S ERRORS (hw-10, since 2026-11-14). The EDAC counters under /sys/devices/system/edac,
summed as scripts/xgs/coprocessor.py sums them: how many counters, the errors corrected and the ones
not corrected. The XGS 3300 registers nineteen - its memory controller's and its L2 caches' - and
the ARMADA family none, so on the XGS 136 the Overview says they are not read on this model. The
counters start again at zero when the coprocessor starts.

THE ALERT. XGS: Settings' ecc_alert is a number of corrected errors (0: no alert). When the
corrected errors counted since the last alert - or since the coprocessor started - reach it, and at
any error not corrected, an alert is said in the system log (xgs-coprocessor, warning) and written
for os-netreport to send: one JSON file in OUTBOX, {"source", "kind", "at", "subject", "text",
"facts"}, which os-netreport's alert layer takes and removes. Until os-netreport takes such files the
system log line is the alert; nothing here sends mail itself.
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
STATE = '/var/db/os-xgs-npu/health.json'
HISTORY = '/var/db/os-xgs-npu/health-history.json'
KEEP = 7 * 24 * 12                     # seven days of samples five minutes apart
FLOW_COUNTS = ('dev.octep.0.dp.flows', 'dev.npuep.0.flow.conns')
CONFIG = '/conf/config.xml'
# what the last alert counted from: {"base_ce", "ue", "uptime_s", "at"}
ALERTED = '/var/db/os-xgs-npu/ecc-alert.json'
# alerts written for os-netreport to send, a file each
OUTBOX = '/var/db/os-xgs-npu/alerts'
DEFAULT_ALERT = 10
COMMAND = ('cat /proc/uptime /proc/loadavg; grep -E "^(MemTotal|MemAvailable):" /proc/meminfo; '
           'grep -c ^processor /proc/cpuinfo; '
           'n=0; ce=0; ue=0; for f in $(find /sys/devices/system/edac -name ce_count 2>/dev/null); do n=$((n+1)); '
           'ce=$((ce+$(cat $f))); [ -f ${f%ce_count}ue_count ] && ue=$((ue+$(cat ${f%ce_count}ue_count))); done; '
           'echo "edac $n $ce $ue"')


def parse(text):
    lines = [l.strip() for l in text.replace('\r', '').split('\n') if l.strip()]
    found = {}
    for line in lines:
        parts = line.split()
        if line.startswith('MemTotal:'):
            found['mem_total_kb'] = int(parts[1])
        elif line.startswith('MemAvailable:'):
            found['mem_available_kb'] = int(parts[1])
        elif parts[0] == 'edac':
            if len(parts) == 4 and all(p.isdigit() for p in parts[1:]):
                found['edac'] = {'counters': int(parts[1]), 'corrected': int(parts[2]),
                                 'uncorrected': int(parts[3])}
        elif len(parts) == 2 and 'uptime_s' not in found and all(p.replace('.', '', 1).isdigit() for p in parts):
            found['uptime_s'] = int(float(parts[0]))
        elif len(parts) == 5 and '/' in parts[3]:
            found['load'] = [float(p) for p in parts[:3]]
        elif len(parts) == 1 and parts[0].isdigit():
            found['cpus'] = int(parts[0])
    return found


def flows_in_hardware():
    """The connections the coprocessor forwards now, as the driver counts them, or None."""
    for name in FLOW_COUNTS:
        try:
            done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True,
                                  errors='replace', timeout=10)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and done.stdout.strip().isdigit():
            return int(done.stdout.strip())
    return None


def sample(answer, flows):
    """One point of the history: [time, load, memory in use in per cent, flows]."""
    load = answer['load'][0] if answer.get('load') else None
    mem = None
    if answer.get('mem_total_kb') and 'mem_available_kb' in answer:
        mem = round(100.0 * (answer['mem_total_kb'] - answer['mem_available_kb']) / answer['mem_total_kb'], 1)
    return [answer['at'], load, mem, flows]


def history():
    try:
        with open(HISTORY, encoding='utf-8') as handle:
            kept = json.load(handle)
        return kept if isinstance(kept, list) else []
    except (OSError, ValueError):
        return []


def keep(point):
    kept = [p for p in history() if isinstance(p, list) and len(p) == 4 and p[0] < point[0]]
    kept = (kept + [point])[-KEEP:]
    os.makedirs(os.path.dirname(HISTORY), exist_ok=True)
    with open(HISTORY + '.tmp', 'w', encoding='utf-8') as handle:
        json.dump(kept, handle, separators=(',', ':'))
    os.replace(HISTORY + '.tmp', HISTORY)


def threshold():
    """XGS: Settings' ecc_alert: corrected errors that raise an alert, 0 for none."""
    try:
        value = ET.parse(CONFIG).getroot().findtext('OPNsense/XGS/general/ecc_alert')
    except (OSError, ET.ParseError):
        return DEFAULT_ALERT
    if value is None or not value.strip():
        return DEFAULT_ALERT
    try:
        return max(0, int(value))
    except ValueError:
        return DEFAULT_ALERT


def read_json(path, default):
    try:
        with open(path, encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return default


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=os.path.dirname(path), prefix='.health-')
    with os.fdopen(handle, 'w', encoding='utf-8') as stream:
        json.dump(obj, stream)
    os.replace(temporary, path)


def log(text):
    try:
        subprocess.run(['/usr/bin/logger', '-t', 'xgs-coprocessor', '-p', 'daemon.warning', text],
                       stdin=subprocess.DEVNULL, capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def ecc_alert(answer, limit=None):
    """The alert of THE ALERT above, for one reading: the alert written, or None. A coprocessor that
    started again (up for less time than at the last alert, or its counts below it) counts from zero."""
    edac = answer.get('edac')
    if not edac or not edac.get('counters'):
        return None
    limit = threshold() if limit is None else limit
    ce, ue = edac['corrected'], edac['uncorrected']
    last = read_json(ALERTED, {})
    restarted = ce < last.get('base_ce', 0) or ue < last.get('ue', 0) or \
        ('uptime_s' in answer and answer['uptime_s'] < last.get('uptime_s', 0))
    if restarted:
        last = {}
    base, seen_ue = last.get('base_ce', 0), last.get('ue', 0)
    reasons = []
    if ue > seen_ue:
        reasons.append('%d memory errors not corrected' % ue)
    if limit and ce - base >= limit:
        reasons.append('%d corrected memory errors, %d since the last alert (the alert is at %d)' % (ce, ce - base, limit))
    if not reasons:
        if restarted:
            write_json(ALERTED, {'base_ce': 0, 'ue': 0, 'uptime_s': answer.get('uptime_s', 0), 'at': answer['at']})
        return None
    subject = 'The coprocessor\'s memory: ' + '; '.join(reasons)
    alert = {'source': 'os-xgs-npu', 'kind': 'coprocessor-ecc', 'at': answer['at'], 'subject': subject,
             'text': subject + '. Read by its EDAC counters (%d of them) at %s; they start again at zero when '
                     'the coprocessor starts. See XGS: Overview.' % (
                         edac['counters'], time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(answer['at']))),
             'facts': {'corrected': ce, 'uncorrected': ue, 'counters': edac['counters'], 'threshold': limit}}
    log(subject)
    try:
        write_json(os.path.join(OUTBOX, 'coprocessor-ecc-%d.json' % answer['at']), alert)
    except OSError:
        pass
    write_json(ALERTED, {'base_ce': ce, 'ue': ue, 'uptime_s': answer.get('uptime_s', 0), 'at': answer['at']})
    return alert


def collect():
    answer = {'at': int(time.time())}
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, '8', COMMAND], stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, errors='replace', timeout=100)
        found = parse(done.stdout)
        if done.returncode != 0 and not found:
            answer['error'] = (done.stderr.strip() or 'exit %d' % done.returncode)[:200]
        answer.update(found)
    except (OSError, subprocess.SubprocessError, ValueError) as failure:
        answer['error'] = str(failure)[:200]
    if 'uptime_s' in answer or 'error' in answer:
        try:
            ecc_alert(answer)
        except OSError:
            pass
        # the last alert's time, on the page until the coprocessor starts again
        last = read_json(ALERTED, {}) if 'edac' in answer else {}
        if last.get('base_ce') or last.get('ue'):
            answer['ecc_alert_at'] = last.get('at')
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        with open(STATE + '.tmp', 'w', encoding='utf-8') as handle:
            json.dump(answer, handle)
        os.replace(STATE + '.tmp', STATE)
        keep(sample(answer, flows_in_hardware()))
    return answer


def show():
    try:
        with open(STATE, encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def main(argv):
    command = argv[1] if len(argv) > 1 else 'show'
    if command == 'collect':
        print(json.dumps(collect()))
    elif command == 'show':
        print(json.dumps(show()))
    elif command == 'history':
        print(json.dumps({'samples': history(), 'step': 300}))
    else:
        print(json.dumps({'error': 'usage: health.py collect|show|history'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
