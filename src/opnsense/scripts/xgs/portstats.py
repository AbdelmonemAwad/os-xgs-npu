#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The switch's own counters for each panel port behind it - errors and drops - for XGS: Ports.

    portstats.py collect    ask the coprocessor now, keep the answer with the one before, print it
    portstats.py show       print what was asked last, without asking

WHY THE SWITCH. Port1 to Port8 and two of the cages hang off the 88E6193X on both families, and a
frame the switch takes in with a bad checksum, too short, too long or dropped for want of room is
never handed on: neither the host's interface counters nor the coprocessor's see it. The switch's
RMON counters do, read on the coprocessor by contrib/mvsw ("mvsw 0 2 stats"), which reads every
counter twice because the network agent shares the MDIO bus.

WHICH PORT IS WHICH, from the vendor's platform data (docs/families/armada.md, octeon-tx.md): the
switch's ports 1 to 8 are Port1 to Port8 on both; its 9 and 10 are F1 and F2 on the XGS 136 and F3
and F4 on the XGS 3300. The other ports - the XGS 136's labels 9 to 12, the XGS 3300's F1 and F2 and
its expansion module - are not on the switch and have no row here.

WHY KEPT, as health.py: the XGS 3300's coprocessor is reached over its console, one command at a
time for every plugin, so the cron asks every five minutes and the page shows the last answer, with
what changed since the one before it.
"""

import json
import os
import subprocess
import sys
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
BOARD = '/usr/local/opnsense/scripts/xgs/board.sh'
STATE = '/var/db/os-xgs-npu/portstats.json'
COMMAND = '/persistent/os-xgs-npu/mvsw 0 2 stats 1 10'
CAGES = {'armada': {9: 'F1', 10: 'F2'}, 'octeontx': {9: 'F3', 10: 'F4'}}
# The counters that say something went wrong, as the page lists them first.
ERRORS = ('in_fcs_err', 'in_rx_err', 'in_undersize', 'in_fragments', 'in_oversize', 'in_jabber',
          'in_bad_octets', 'in_discards', 'out_fcs_err', 'collisions', 'late', 'excessive')


def family():
    try:
        out = subprocess.run(['/bin/sh', BOARD], stdin=subprocess.DEVNULL, capture_output=True,
                             text=True, timeout=10).stdout.split()
    except (OSError, subprocess.SubprocessError):
        return None
    return out[1] if len(out) > 1 else None


def label(port, fam):
    if port <= 8:
        return 'Port%d' % port
    return CAGES.get(fam, {}).get(port)


def parse(text, fam):
    """Lines of "port=N name=value ..." -> {label: {name: int or None}}; "?" was never agreed on."""
    ports = {}
    for line in text.replace('\r', '').split('\n'):
        fields = line.split()
        if not fields or not fields[0].startswith('port='):
            continue
        counters = {}
        for field in fields[1:]:
            name, _, value = field.partition('=')
            counters[name] = int(value) if value.isdigit() else None
        try:
            name = label(int(fields[0][5:]), fam)
        except ValueError:
            continue
        if name:
            ports[name] = counters
    return ports


def since(now, before):
    """What each counter moved by between two readings; a counter that went back was cleared."""
    out = {}
    for name, counters in now.items():
        old = before.get(name) or {}
        moved = {}
        for key, value in counters.items():
            if value is None or old.get(key) is None:
                continue
            moved[key] = value - old[key] if value >= old[key] else value
        out[name] = moved
    return out


def show():
    try:
        with open(STATE, encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def collect():
    fam = family()
    answer = {'at': int(time.time()), 'family': fam, 'errors': list(ERRORS)}
    try:
        for tries in range(3):
            done = subprocess.run(['/bin/sh', NPU_RUN, '20', COMMAND], stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=120)
            # 75: another plugin's command holds the XGS 3300's console; it is one at a time.
            if done.returncode != 75:
                break
            time.sleep(10)
        ports = parse(done.stdout, fam)
        if ports:
            answer['ports'] = ports
        elif 'usage' in done.stdout:
            # The tool on the coprocessor predates "stats"; switch-jumbo.sh replaces it at the next
            # boot with the one this host carries.
            answer['error'] = ('the switch tool on the coprocessor is older than this release; '
                               'it is replaced at the next boot')
        else:
            answer['error'] = (done.stderr.strip() or done.stdout.strip() or
                               'exit %d' % done.returncode)[:200]
    except (OSError, subprocess.SubprocessError) as failure:
        answer['error'] = str(failure)[:200]
    before = show()
    if answer.get('ports') and before.get('ports'):
        answer['before_at'] = before.get('read_at', before.get('at'))
        answer['since'] = since(answer['ports'], before['ports'])
    if 'ports' in answer or 'error' in answer:
        if 'ports' not in answer and before.get('ports'):
            # Keep the last good reading when this one failed; the error says why it is old.
            for key in ('ports', 'since', 'before_at'):
                if key in before:
                    answer[key] = before[key]
            answer['read_at'] = before.get('read_at', before.get('at'))
        else:
            answer['read_at'] = answer['at']
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        with open(STATE + '.tmp', 'w', encoding='utf-8') as handle:
            json.dump(answer, handle)
        os.replace(STATE + '.tmp', STATE)
    return answer


def main(argv):
    command = argv[1] if len(argv) > 1 else 'show'
    if command == 'collect':
        print(json.dumps(collect()))
    elif command == 'show':
        print(json.dumps(show()))
    else:
        print(json.dumps({'error': 'usage: portstats.py collect|show'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
