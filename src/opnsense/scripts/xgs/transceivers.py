#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The modules in the front cages, and what their diagnostics read - for the Transceivers page.

    transceivers.py collect    read every cage on the coprocessor now, keep it, print it
    transceivers.py show       print what was read last, without asking the coprocessor

WHY THE COPROCESSOR. The cages' EEPROMs are on the coprocessor's I2C, not the host's - the host
driver has no SIOCGI2C to offer, on either family - and the vendor's own tools are there:
`xgs-sff-decode -l <label>` reads the identity page (A0h) and `xgs-sff -p <phy> -g` the
diagnostics (A2h). Measured on the XGS 3300, 2026-10-17: an Intel 10GBASE-SR module in A1 reads
34.2 C, 3.368 V, 6.406 mA bias, 0.620 mW out and 0.455 mW in.

The decode tool prints "Diagnostic Monitoring not supported" for every module, also for ones whose
byte 92 says they have it (0x68: bit 6, implemented; bit 5, internally calibrated). So byte 92 is
read here and decides, and the diagnostics are asked for only where bit 6 is set.

HOW OFTEN. Every five minutes, from the plugin's cron, and when the page asks for a refresh. On
the XGS 3300 the coprocessor is reached over its console, which one command at a time shares with
the other plugins (scripts/xgs/npu-run.sh holds the lock), so the whole read is two commands.
"""

import json
import math
import os
import re
import subprocess
import sys
import tempfile
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
PORTMAP = '/usr/local/opnsense/scripts/xgs/portmap.py'
STATE = '/var/db/os-xgs-npu/transceivers.json'

CAGE = re.compile(r'^(F|A)\d{1,2}$')
DOM = {
    'temp_c': re.compile(r'^Temperature\s+\(0x[0-9a-f]+\)\s+(-?[0-9.]+)\s*C', re.I),
    'vcc_v': re.compile(r'^Vcc\s+\(0x[0-9a-f]+\)\s+([0-9.]+)\s*V', re.I),
    'bias_ma': re.compile(r'^TX Bias\s+\(0x[0-9a-f]+\)\s+([0-9.]+)\s*mA', re.I),
    'tx_mw': re.compile(r'^TX Power\s+\(0x[0-9a-f]+\)\s+([0-9.]+)\s*mW', re.I),
    'rx_mw': re.compile(r'^RX Power\s+\(0x[0-9a-f]+\)\s+([0-9.]+)\s*mW', re.I),
}


def coprocessor(seconds, command):
    """One command on the coprocessor, through the runner every plugin shares. (stdout, ok)."""
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, str(seconds), command], capture_output=True,
                              text=True, timeout=seconds + 90)
    except (OSError, subprocess.SubprocessError) as failure:
        return '', str(failure)
    if done.returncode == 75:
        return '', 'the coprocessor\'s console is busy with another command'
    if done.returncode != 0:
        return done.stdout, (done.stderr.strip() or 'exit %d' % done.returncode)
    return done.stdout, ''


def cages():
    """The cage labels this appliance has, from the port map: F1, F2 ... and a module's A1 ..."""
    try:
        found = json.loads(subprocess.run([PORTMAP], capture_output=True, text=True, timeout=30).stdout)
    except (OSError, ValueError, subprocess.SubprocessError):
        return []
    return [p['label'] for p in found.get('ports') or [] if CAGE.match(str(p.get('label') or ''))]


def blocks(text, marker):
    """Split a reply into {key: [lines]} at lines '@@<key>'."""
    out = {}
    key = None
    for line in text.splitlines():
        line = line.rstrip()
        if line.startswith(marker):
            key = line[len(marker):].strip()
            out[key] = []
        elif key is not None and line.strip():
            out[key].append(line)
    return out


def identity(label, lines):
    module = {'port': label, 'present': False}
    for line in lines:
        text = line.strip()
        if 'is not present' in text:
            return module
        found = re.match(r'^\S+\s+phy(\d+)\s+byte\s+(\d+)\s+0x([0-9a-f]{2})\s*(.*)$', text, re.I)
        if found:
            module['present'] = True
            module['phy'] = int(found.group(1))
            byte, value = int(found.group(2)), int(found.group(3), 16)
            if byte == 12:
                rate = re.search(r'(\d+)\s*MBd', found.group(4))
                module['rate_mbd'] = int(rate.group(1)) if rate else value * 100
            elif byte == 92:
                module['diagnostics'] = bool(value & 0x40)
            continue
        for key, name in (('vendor', 'Vendor Name:'), ('part', 'Vendor PN:'), ('serial', 'Vendor SN:')):
            if text.startswith(name):
                module[key] = text[len(name):].strip()
        compliance = re.match(r'^(?:\d+G\s+)?Ethernet Compliance\s+(\S+)', text)
        if compliance:
            module.setdefault('types', []).append(compliance.group(1))
    return module


def dbm(mw):
    return round(10 * math.log10(mw), 2) if mw and mw > 0 else None


def collect():
    labels = cages()
    answer = {'at': int(time.time()), 'modules': [], 'error': ''}
    if not labels:
        answer['error'] = 'no cages in the port map'
        return keep(answer)

    # The runner waits out the seconds it is given on the XGS 3300's console, so they are what the
    # commands need and no more: eight cages decode in under ten (measured 2026-10-17, 16 s in all
    # with the login), three modules' diagnostics in under five.
    reply, error = coprocessor(max(8, len(labels) + 2), 'for l in %s; do echo "@@$l"; xgs-sff-decode -l $l 2>&1 | '
                                   'grep -E "byte (0|12|92) |Name:|PN:|SN:|Compliance |not present"; done'
                               % ' '.join(labels))
    if error and not reply.strip():
        answer['error'] = error
        return keep(answer, previous=True)
    found = blocks(reply, '@@')
    modules = [identity(label, found.get(label, [])) for label in labels]

    phys = [m['phy'] for m in modules if m.get('present') and m.get('diagnostics') and 'phy' in m]
    if phys:
        reply, error = coprocessor(max(5, 2 * len(phys)), 'for p in %s; do echo "@@$p"; xgs-sff -p $p -g 2>&1; done'
                                   % ' '.join(str(p) for p in phys))
        readings = blocks(reply, '@@')
        for module in modules:
            lines = readings.get(str(module.get('phy')), [])
            values = {}
            for line in lines:
                for key, pattern in DOM.items():
                    hit = pattern.match(line.strip())
                    if hit:
                        values[key] = float(hit.group(1))
            if values:
                values['tx_dbm'] = dbm(values.get('tx_mw'))
                values['rx_dbm'] = dbm(values.get('rx_mw'))
                module['dom'] = values
        if error:
            answer['error'] = error

    answer['modules'] = modules
    return keep(answer)


def keep(answer, previous=False):
    """Write the answer for the page; a failed read keeps the last good modules, marked."""
    if previous:
        old = show()
        if old.get('modules'):
            old['error'] = answer['error']
            old['failed_at'] = answer['at']
            answer = old
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=os.path.dirname(STATE), prefix='.transceivers-')
    with os.fdopen(handle, 'w') as stream:
        json.dump(answer, stream)
    os.chmod(temporary, 0o644)
    os.replace(temporary, STATE)
    return answer


def show():
    try:
        with open(STATE, 'r') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {'at': None, 'modules': [], 'error': 'not read yet'}


def main(argv):
    command = argv[1] if len(argv) > 1 else 'show'
    if command == 'collect':
        print(json.dumps(collect()))
    elif command == 'show':
        print(json.dumps(show()))
    else:
        print(json.dumps({'error': 'usage: transceivers.py collect|show'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
