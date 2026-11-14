#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The coprocessor's own Linux - how long it has been up, its load, its memory - for the Overview.

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
"""

import json
import os
import subprocess
import sys
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
STATE = '/var/db/os-xgs-npu/health.json'
HISTORY = '/var/db/os-xgs-npu/health-history.json'
KEEP = 7 * 24 * 12                     # seven days of samples five minutes apart
FLOW_COUNTS = ('dev.octep.0.dp.flows', 'dev.npuep.0.flow.conns')
COMMAND = ('cat /proc/uptime /proc/loadavg; grep -E "^(MemTotal|MemAvailable):" /proc/meminfo; '
           'grep -c ^processor /proc/cpuinfo')


def parse(text):
    lines = [l.strip() for l in text.replace('\r', '').split('\n') if l.strip()]
    found = {}
    for line in lines:
        parts = line.split()
        if line.startswith('MemTotal:'):
            found['mem_total_kb'] = int(parts[1])
        elif line.startswith('MemAvailable:'):
            found['mem_available_kb'] = int(parts[1])
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
