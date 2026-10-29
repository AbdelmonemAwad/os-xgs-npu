#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Power over Ethernet on the front ports that carry it - for the Ports page.

    poe.py show                 read the PoE controller now and print it
    poe.py apply [--tries N]    switch each PoE port off or on, and set its limit, as XGS: Ports has
                                it; print what reads back
    poe.py cycle <label>        switch one port's power off and, three seconds later, on again

WHERE IT IS. On the XGS 136 the PoE controller behind labels 11 and 12 (U40, a Microchip PD69104 by
its marking) is on the coprocessor's I2C, not the host's: the host's SMBus answers at the memory's
SPD addresses and nowhere else (probed 2026-10-20). The vendor's tool on the coprocessor reads and
drives it, `xgs-poe`, so this runs that tool there through scripts/xgs/npu-run.sh, the runner every
plugin shares - over the management link when it is up, else the coprocessor's console.

WHAT IT CAN DO, measured on the XGS 136 on 2026-10-20 with nothing plugged into either port: a
port's power switched off - the controller stops looking for a device, and its detection reads
"Unknown" - and on again, detection back to "Open Circuit". And, measured on 2026-10-29 with a device
on each port, a port's limit: AT (30 W) or AF (15.4 W), the two values the tool's help names for a
controller without 1 W steps; a number, tried on 2026-10-20, is refused. The tool answers "Device or
resource busy" now and then on a port its controller is refusing, so a limit that does not read back
is set once more. Its other settings answer "Operation not supported" on this module: a port's
priority and the module's guard band. Its RESET is refused on a port with no powered device, so a
power cycle here is off, three seconds, on, which does not depend on what is plugged in.

WHAT IS KEPT. Which ports the owner switched off, and which ports the owner held to 15.4 W:
OPNsense/XGS/general/poe_off and poe_af in config.xml, each a list of labels, written by the Ports
page (Api/PoeController.php). The controller's ports start switched on, at AT, by the tool's own
help, so after a boot the start hook (rc.syshook.d/start/66-xgs-poe) switches off again a port the
owner switched off and sets AF again where the owner chose it, asking every 20 s while the
coprocessor comes up. A port is switched on here only when it reads switched off - not delivering
power and its detection "Unknown" - so a device that is powered is never cycled by an apply. A limit
is the exception: set either way, it makes the controller look for the device again, and a device
on the port goes without power for a second or two (measured 2026-10-29), so the page asks first.
"""

import json
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
BOARD = '/usr/local/opnsense/scripts/xgs/board.sh'
CONFIG = '/conf/config.xml'
LABEL = re.compile(r'^[0-9]{1,2}$')
# the tool's two tables, each a header of columns ending in '|' and rows right-aligned under them
MODULE_KEYS = ('id', 'status', 'ports', 'budget_w')
PORT_KEYS = ('label', 'enabled', 'good', 'detection', 'class', 'priority', 'limit_w', 'power_w',
             'voltage_v', 'current_ma')


def log(message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', 'xgs-poe', '-p', 'daemon.notice', message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def family():
    try:
        out = subprocess.run(['/bin/sh', BOARD], capture_output=True, text=True, timeout=30).stdout.split()
    except (OSError, subprocess.SubprocessError):
        return ''
    return out[1] if len(out) > 1 else ''


def coprocessor(seconds, command):
    """One command on the coprocessor, through the runner every plugin shares. (stdout, error)."""
    try:
        # no input: the runner's ssh would read whatever this was started with
        done = subprocess.run(['/bin/sh', NPU_RUN, str(seconds), command], stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, timeout=seconds + 90)
    except (OSError, subprocess.SubprocessError) as failure:
        return '', str(failure)
    if done.returncode == 75:
        return '', 'the coprocessor\'s console is busy with another command'
    if done.returncode != 0:
        return done.stdout, (done.stderr.strip() or 'exit %d' % done.returncode)
    return done.stdout, ''


def columns(header):
    """The column ends of a header line: each column runs up to and including its '|'."""
    ends, at = [], 0
    for part in header.rstrip().split('|')[:-1]:
        at += len(part) + 1
        ends.append(at)
    return ends


def cut(line, ends, keys):
    row, start = {}, 0
    for key, end in zip(keys, ends):
        row[key] = line[start:end].strip()
        start = end
    return row


def parse(text):
    """xgs-poe -e: {'modules': [...], 'ports': [...]}, or None when it printed neither table."""
    modules, ports, ends, keys = [], [], None, None
    for line in text.replace('\r', '').split('\n'):
        if line.startswith('Module ID|'):
            ends, keys = columns(line), MODULE_KEYS
        elif line.startswith('Port Label|'):
            ends, keys = columns(line), PORT_KEYS
        elif ends and line.strip() and not line.strip().startswith('|'):
            row = cut(line, ends, keys)
            if keys is MODULE_KEYS:
                row['ports'] = sorted(row['ports'].split(), key=lambda p: int(p) if p.isdigit() else 0)
                modules.append(row)
            elif LABEL.match(row['label']):
                ports.append(row)
    if not modules and not ports:
        return None
    return {'modules': modules, 'ports': sorted(ports, key=lambda p: int(p['label']))}


def switched_off(port):
    """A port reads switched off when it delivers nothing and its controller is not detecting."""
    return port.get('enabled', '').lower() != 'yes' and port.get('detection', '').lower().startswith('unknown')


def kept(field):
    """The labels in one of the owner's PoE lists in config.xml: poe_off, the ports switched off, or
    poe_af, the ports held to 15.4 W."""
    try:
        text = ET.parse(CONFIG).getroot().findtext('OPNsense/XGS/general/' + field) or ''
    except (OSError, ET.ParseError):
        return []
    return [p for p in re.split(r'[\s,]+', text) if LABEL.match(p)]


def kept_off():
    """The labels the owner switched off, from config.xml."""
    return kept('poe_off')


def limit_wanted(label, af):
    """The limit a port should read: AF where the owner held it to 15.4 W, AT, how it starts, else."""
    return 'AF' if label in af else 'AT'


def read(seconds=8):
    answer = {'present': False, 'at': int(time.time()), 'modules': [], 'ports': [], 'error': ''}
    fam = family()
    if fam == 'octeontx':
        answer['reason'] = 'this model has no PoE'
        return answer
    if fam != 'armada':
        answer['reason'] = 'not an XGS appliance this project knows'
        return answer
    out, error = coprocessor(seconds, 'xgs-poe -e')
    found = parse(out)
    if found is None:
        answer['error'] = error or (out.strip().split('\n')[0] if out.strip() else 'xgs-poe printed nothing')
        return answer
    off = kept_off()
    af = kept('poe_af')
    for port in found['ports']:
        port['kept_off'] = port['label'] in off
        port['kept_af'] = port['label'] in af
        port['switched_off'] = switched_off(port)
    answer.update(found)
    answer['present'] = bool(found['ports'])
    answer['error'] = error
    return answer


def limit_steps(state, af):
    """The tool's commands that set a port's limit where it does not read what the owner chose."""
    steps = []
    for port in state.get('ports', []):
        now = port.get('limit_w', '').upper()
        want = limit_wanted(port['label'], af)
        if now in ('AT', 'AF') and now != want:
            steps.append('xgs-poe -p %s -a %s' % (port['label'], want))
    return steps


def apply(tries=1):
    """Each PoE port switched, and its limit set, as the configuration has it; what reads back
    afterwards."""
    for attempt in range(max(1, tries)):
        state = read()
        if state['present'] or state.get('reason'):
            break
        if attempt + 1 < tries:
            time.sleep(20)
    if not state['present']:
        return state
    off = set(kept_off())
    af = set(kept('poe_af'))
    # the limit first, so that a port switched on comes up with the one the owner chose
    steps = limit_steps(state, af)
    for port in state['ports']:
        if port['label'] in off and not port['switched_off']:
            steps.append('xgs-poe -p %s -o OFF' % port['label'])
        elif port['label'] not in off and port['switched_off']:
            steps.append('xgs-poe -p %s -o ON' % port['label'])
    if not steps:
        state['changed'] = []
        return state
    out, error = coprocessor(8 + 2 * len(steps), '; '.join(steps))
    # a limit set either way makes the controller look for the device again, and it is powered
    # again a second or two later (measured 2026-10-29): read once it has been, not in between
    if any(' -a ' in s for s in steps):
        time.sleep(3)
    after = read()
    # a limit the tool answered "busy" to does not read back: set once more, a few seconds on
    again = limit_steps(after, af)
    if again:
        time.sleep(5)
        out, retry_error = coprocessor(8 + 2 * len(again), '; '.join(again))
        error = error or retry_error
        after = read()
    after['changed'] = [s.split()[2] + ' ' + s.split()[4].lower() for s in steps]
    for change in after['changed']:
        label, to = change.split()
        port = next((p for p in after.get('ports', []) if p['label'] == label), {})
        if to in ('at', 'af'):
            log('port %s limited to %s, as XGS: Ports has it; it reads %s' % (label, to.upper(), port.get('limit_w', 'nothing')))
        else:
            log('port %s switched %s, as XGS: Ports has it; it reads %s' % (label, to, port.get('detection', 'nothing')))
    after['error'] = error or after['error']
    return after


def cycle(label):
    state = read()
    if not state['present']:
        return state
    port = next((p for p in state['ports'] if p['label'] == label), None)
    if port is None:
        return dict(state, error='port %s carries no PoE' % label)
    if port['kept_off']:
        return dict(state, error='port %s is switched off; switch it on instead' % label)
    out, error = coprocessor(12, 'xgs-poe -p %s -o OFF; sleep 3; xgs-poe -p %s -o ON' % (label, label))
    after = read()
    after['cycled'] = label
    log('port %s switched off and on again from XGS: Ports' % label)
    after['error'] = error or after['error']
    return after


def main(argv):
    command = argv[1] if len(argv) > 1 else 'show'
    if command == 'show':
        print(json.dumps(read()))
    elif command == 'apply':
        tries = int(argv[3]) if len(argv) > 3 and argv[2] == '--tries' and argv[3].isdigit() else 1
        print(json.dumps(apply(tries)))
    elif command == 'cycle' and len(argv) > 2 and LABEL.match(argv[2]):
        print(json.dumps(cycle(argv[2])))
    else:
        print(json.dumps({'error': 'usage: poe.py show|apply [--tries N]|cycle <label>'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
