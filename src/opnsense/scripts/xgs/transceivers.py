#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The modules in the front cages, and what their diagnostics read - for the Transceivers page.

    transceivers.py collect    read every cage on the coprocessor now, keep it, print it
    transceivers.py show       print what was read last, without asking the coprocessor
    transceivers.py apply      tell the driver what was read last, without asking the coprocessor

WHY THE COPROCESSOR. The cages' EEPROMs are on the coprocessor's I2C, not the host's - the host
driver has no SIOCGI2C to offer, on either family - and the vendor's own tools are there:
`xgs-sff-decode -l <label>` reads the identity page (A0h) and `xgs-sff -p <phy> -g` the
diagnostics (A2h). Measured on the XGS 3300, 2026-10-17: an Intel 10GBASE-SR module in A1 reads
34.2 C, 3.368 V, 6.406 mA bias, 0.620 mW out and 0.455 mW in.

THE MEDIUM. For the same reason the drivers cannot tell what a cage carries, and both used to call
every cage fibre (#533). So what is read here is also written into the driver - giu.module_media
on the XGS 136, dp.module_media on the XGS 3300 - and a cage's port then reports its module's
medium: 1000baseT for a copper module, 1000baseSX for a fibre one. It comes from the module's
compliance codes (SFF-8472 bytes 3, 6, 8 and 36), with two exceptions. A module with an RJ45
connector (byte 2) is copper whatever its codes say. And some copper 10GBASE-T modules claim
10GBASE-SR and an LC connector outright - the two in the second XGS 3300's F1 and F2 do - so a
part number with a "T" of its own ("SFP-10G-T", "10G-SFP+-T", "SFP-GE-T") is taken as copper too,
and the page says that this is where it came from.

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
# where each family's driver takes a cage's medium: "<interface> <subtype at 1G> <subtype at 10G>"
MEDIA_NODES = ('dev.npuep.0.giu.module_media', 'dev.octep.0.dp.module_media')
# the standards a module can name, as the Ethernet media subtypes net/if_media.h numbers them
SUBTYPE = {
    '1000BASE-SX': 11, '1000BASE-LX': 14, '1000BASE-CX': 15, '1000BASE-T': 16,
    '10GBASE-LR': 18, '10GBASE-SR': 19, '10GBASE-CR': 22, '10GBASE-LRM': 24, '10GBASE-T': 26,
    '10GBASE-ER': 46,
}

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
    """The cages this appliance has, from the port map: {label: interface}, F1, F2 ... and A1 ..."""
    try:
        found = json.loads(subprocess.run([PORTMAP], capture_output=True, text=True, timeout=30).stdout)
    except (OSError, ValueError, subprocess.SubprocessError):
        return {}
    return {p['label']: str(p.get('device') or '') for p in found.get('ports') or []
            if CAGE.match(str(p.get('label') or ''))}


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
    codes = {}
    for line in lines:
        text = line.strip()
        if 'is not present' in text:
            return module
        if 'is not an SFF port' in text:
            module['sff'] = False
            return module
        found = re.match(r'^\S+\s+phy(\d+)\s+byte\s+(\d+)\s+0x([0-9a-f]{2})\s*(.*)$', text, re.I)
        if found:
            module['present'] = True
            module['phy'] = int(found.group(1))
            byte, value = int(found.group(2)), int(found.group(3), 16)
            if byte == 2:
                codes[2] = value
            elif byte == 12:
                rate = re.search(r'(\d+)\s*MBd', found.group(4))
                module['rate_mbd'] = int(rate.group(1)) if rate else value * 100
            elif byte == 92:
                module['diagnostics'] = bool(value & 0x40)
            continue
        # the compliance codes come indented under their table, without the port in front
        found = re.match(r'^byte\s+(\d+)\s+0x([0-9a-f]{2})\b', text, re.I)
        if found and int(found.group(1)) in (3, 6, 8, 36):
            codes[int(found.group(1))] = int(found.group(2), 16)
            continue
        for key, name in (('vendor', 'Vendor Name:'), ('part', 'Vendor PN:'), ('serial', 'Vendor SN:')):
            if text.startswith(name):
                module[key] = text[len(name):].strip()
        compliance = re.match(r'^(?:\d+G\s+)?Ethernet Compliance\s+(\S+)', text)
        if compliance:
            module.setdefault('types', []).append(compliance.group(1))
    if module['present']:
        module['codes'] = {str(k): v for k, v in sorted(codes.items())}
        module['medium'] = medium(module)
    return module


def medium(module):
    """What the module carries, at a gigabit and at ten: {'1g', '10g', 'by'} - see THE MEDIUM."""
    codes = {int(k): v for k, v in (module.get('codes') or {}).items()}
    b2, b3, b6, b8, b36 = (codes.get(k, 0) for k in (2, 3, 6, 8, 36))
    one = ('1000BASE-T' if b6 & 0x08 else '1000BASE-SX' if b6 & 0x01 else
           '1000BASE-LX' if b6 & 0x02 else '1000BASE-CX' if b6 & 0x04 else None)
    ten = ('10GBASE-T' if b36 in (0x16, 0x1c) else '10GBASE-CR' if b8 & 0x04 else
           '10GBASE-SR' if b3 & 0x10 else '10GBASE-LR' if b3 & 0x20 else
           '10GBASE-LRM' if b3 & 0x40 else '10GBASE-ER' if b3 & 0x80 else None)
    by = 'codes'
    tokens = re.split(r'[-_ /]+', str(module.get('part') or '').upper())
    if b2 == 0x22:
        by = 'connector'
    elif 'T' in tokens and one != '1000BASE-T' and ten != '10GBASE-T':
        by = 'part number'
    if by != 'codes':
        one = '1000BASE-T'
        ten = '10GBASE-T' if (module.get('rate_mbd') or 0) >= 10000 else None
    return {'1g': one, '10g': ten, 'by': by}


def dbm(mw):
    return round(10 * math.log10(mw), 2) if mw and mw > 0 else None


def collect(tries=1):
    """Read the cages; with tries > 1, ask again every 20 s while the coprocessor cannot answer.

    That is for the boot hook: on the XGS 3300 the console is busy at boot with the other plugins'
    first reads, and one refusal there left every cage "unknown" until the cron came round
    (measured 2026-10-19)."""
    answer, answered = collect_once()
    while tries > 1 and not answered:
        tries -= 1
        time.sleep(20)
        answer, answered = collect_once()
    return answer


def collect_once():
    """One read of every cage: (what is kept, whether the coprocessor answered at all)."""
    devices = cages()
    labels = list(devices)
    answer = {'at': int(time.time()), 'modules': [], 'error': ''}
    if not labels:
        answer['error'] = 'no cages in the port map'
        return keep(answer), True

    # The runner waits out the seconds it is given on the XGS 3300's console, so they are what the
    # commands need and no more: eight cages decode in under ten (measured 2026-10-17, 16 s in all
    # with the login), three modules' diagnostics in under five.
    reply, error = coprocessor(max(8, len(labels) + 2), 'for l in %s; do echo "@@$l"; xgs-sff-decode -l $l 2>&1 | '
                                   'grep -E "byte (0|2|3|6|8|12|36|92) |Name:|PN:|SN:|Compliance |not present|not an SFF"; done'
                               % ' '.join(labels))
    if error and not reply.strip():
        answer['error'] = error
        return tell_driver(keep(answer, previous=True), devices), False
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
    return tell_driver(keep(answer), devices), True


def tell_driver(answer, devices=None):
    """Write each cage's medium into the driver, for what its port reports - see THE MEDIUM.

    A cage found empty, or holding a module whose codes name no standard here, is written 0 0:
    its port then says "unknown" rather than a medium nobody read. A driver from before #533 has
    no such node, and is left alone."""
    node = None
    for name in MEDIA_NODES:
        if subprocess.run(['/sbin/sysctl', '-N', name], capture_output=True).returncode == 0:
            node = name
            break
    if node is None:
        return answer
    if devices is None:
        devices = cages()
    for module in answer.get('modules') or []:
        device = devices.get(module.get('port'))
        if not device:
            continue
        if module.get('sff') is False:
            one, ten = '1000BASE-T', '10GBASE-T'    # a socket, not a cage: an RJ45 module's
        else:
            found = (module.get('medium') or {}) if module.get('present') else {}
            one, ten = found.get('1g'), found.get('10g')
        subprocess.run(['/sbin/sysctl', '-q', '%s=%s %d %d' % (node, device, SUBTYPE.get(one, 0), SUBTYPE.get(ten, 0))],
                       capture_output=True)
    return answer


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
        tries = int(argv[3]) if len(argv) > 3 and argv[2] == '--tries' and argv[3].isdigit() else 1
        print(json.dumps(collect(tries)))
    elif command == 'show':
        print(json.dumps(show()))
    elif command == 'apply':
        tell_driver(show())
    else:
        print(json.dumps({'error': 'usage: transceivers.py collect|show|apply'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
