#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The coprocessor's own Linux - how long it has been up, its load, its memory - for the Overview.

    health.py collect    ask the coprocessor now, keep the answer, print it
    health.py show       print what was asked last, without asking

WHY KEPT. The Overview is opened often and the coprocessor is reached through
scripts/xgs/npu-run.sh, which on the XGS 3300 is its console at 115200, one command at a time
for every plugin. So the plugin's cron asks every five minutes - one short command - and the page
shows the last answer with the time it was read. On the XGS 136 the management link answers in a
second, but the page reads the kept answer there too, so the two families behave the same.

WHAT IT SAYS. /proc/uptime: a coprocessor that restarted on its own is up for less time than the
host; /proc/loadavg on its cores, which the fast path keeps busy; MemTotal and MemAvailable.
Measured on the XGS 136 on 2026-10-20: four cores, 3.0 GB, 1.5 GB of it available, a load of 3.2.
"""

import json
import os
import subprocess
import sys
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
STATE = '/var/db/os-xgs-npu/health.json'
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


def collect():
    answer = {'at': int(time.time())}
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, '8', COMMAND], stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, timeout=100)
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
    else:
        print(json.dumps({'error': 'usage: health.py collect|show'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
