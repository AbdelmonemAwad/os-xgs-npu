#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""
Report the coprocessor's ports as JSON, for the Ports page: every front port the driver made, its
name on the metal, what OPNsense assigned it as, its link, its MTU and the bridge it belongs to;
the expansion module's ports, from the bring-up's ports.json; and what the switch chip's ports
were last set to, from switch-jumbo.sh's state file.

Read-only, like status.py: ifconfig, config.xml and two files the boot wrote. Nothing here talks
to the coprocessor - the switch reading is the one the boot hook made, with its time.
"""

import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

PORTS_JSON = '/var/db/os-xgs-npu/ports.json'
SWITCH_STATE = '/var/db/os-xgs-npu/switch-jumbo.state'


def run(argv):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout if out.returncode == 0 else ''


def family():
    parts = run(['/bin/sh', '/usr/local/opnsense/scripts/xgs/board.sh']).split()
    return parts[1] if len(parts) > 1 else None


def assignments():
    """Device -> (OPNsense key, description, address) from config.xml."""
    out = {}
    try:
        root = ET.parse('/conf/config.xml').getroot()
    except (OSError, ET.ParseError):
        return out
    ifs = root.find('interfaces')
    for node in list(ifs) if ifs is not None else []:
        dev = (node.findtext('if') or '').strip()
        if dev:
            addr = (node.findtext('ipaddr') or '').strip()
            sub = (node.findtext('subnet') or '').strip()
            out[dev] = {
                'key': node.tag,
                'descr': (node.findtext('descr') or node.tag).strip(),
                'address': (addr + '/' + sub) if addr and sub and addr[0].isdigit() else addr,
            }
    return out


def interfaces(prefix):
    """Every interface of the driver, with what ifconfig says of it."""
    text = run(['/sbin/ifconfig', '-a'])
    ports = []
    members = {}
    for block in re.split(r'\n(?=\S)', text):
        name = block.split(':', 1)[0]
        if name.startswith('bridge'):
            for m in re.findall(r'member: (\S+)', block):
                members[m] = name
        if not re.match(prefix + r'[0-9]+$', name):
            continue
        mtu = re.search(r'\bmtu (\d+)', block)
        status = re.search(r'status: (.+)', block)
        media = re.search(r'media: (.+)', block)
        descr = re.search(r'description: (.+)', block)
        ports.append({
            'device': name,
            'mtu': int(mtu.group(1)) if mtu else None,
            'status': status.group(1).strip() if status else None,
            'media': media.group(1).strip() if media else None,
            'description': descr.group(1).strip() if descr else '',
        })
    for p in ports:
        p['bridge'] = members.get(p['device'])
    ports.sort(key=lambda p: int(re.sub(r'\D', '', p['device']) or 0))
    return ports


def label(port, fam, index):
    """The name on the metal: the driver's own description on the XGS 3300, the npup number on the 136."""
    m = re.match(r'XGS front port (\S+)', port['description'])
    if m:
        return m.group(1)
    if fam == 'armada':
        n = int(re.sub(r'\D', '', port['device']))
        return {13: 'F1', 14: 'F2'}.get(n, 'Port%d' % n)
    return None


def main():
    fam = family()
    prefix = 'oxp' if fam == 'octeontx' else 'npup'
    assigned = assignments()
    ports = interfaces(prefix)
    for i, p in enumerate(ports):
        a = assigned.get(p['device'])
        p['label'] = label(p, fam, i) or (a['descr'] if a else p['device'])
        p['assigned'] = a
    module = None
    try:
        with open(PORTS_JSON, encoding='utf-8') as f:
            module = json.load(f)
    except (OSError, ValueError):
        pass
    switch = None
    try:
        with open(SWITCH_STATE, encoding='utf-8') as f:
            switch = f.read().strip()
    except OSError:
        pass
    print(json.dumps({'family': fam, 'ports': ports, 'module': module, 'switch': switch}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
