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

try:
    import models
except ImportError:
    models = None

PORTS_JSON = '/var/db/os-xgs-npu/ports.json'
SWITCH_STATE = '/var/db/os-xgs-npu/switch-jumbo.state'
LABELS = '/var/db/os-xgs-npu/labels'
# The XGS 3300's panel ports in the order bringup.sh creates them, oxp0 first: what a model the
# table does not have is given.
OCTEP_ORDER = ['F1', 'F2', 'Port1', 'Port2', 'Port3', 'Port4', 'Port5', 'Port6', 'Port7', 'Port8', 'F3', 'F4']


def run(argv):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout if out.returncode == 0 else ''


def model_ports():
    """This appliance's model and its panel, unit -> label, from models.py; None if it is not there."""
    if models is None:
        return None, {}
    try:
        a, _ = models.this()
        if a in models.MODELS:
            return models.MODELS[a][0], {p['unit']: p['label'] for p in models.ports(a)}
    except (OSError, ValueError, KeyError):
        pass
    return None, {}


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
        # the address the interface answers to now: its own from the network agent (issue 730), or
        # one set on its OPNsense page
        ether = re.search(r'\n\s+ether (\S+)', block)
        ports.append({
            'device': name,
            'mac': ether.group(1).lower() if ether else None,
            'mtu': int(mtu.group(1)) if mtu else None,
            'status': status.group(1).strip() if status else None,
            'media': media.group(1).strip() if media else None,
            'description': descr.group(1).strip() if descr else '',
        })
    for p in ports:
        p['bridge'] = members.get(p['device'])
    ports.sort(key=lambda p: int(re.sub(r'\D', '', p['device']) or 0))
    return ports


def saved_labels():
    """Device -> name, as the XGS 3300's driver named each interface at boot (bringup.sh)."""
    out = {}
    try:
        with open(LABELS, encoding='utf-8') as f:
            for line in f:
                parts = line.split(None, 1)
                if len(parts) == 2:
                    out[parts[0]] = parts[1].strip()
    except OSError:
        pass
    return out


def label(port, fam, saved, module, panel):
    """
    The name on the metal. On the OCTEON TX family the driver writes it in the interface's
    description, which OPNsense replaces once the port is assigned, so it is taken from what the
    boot saved, then from the description, then from the model's panel in the order bringup.sh
    creates the interfaces in - the rule assign.php uses too - and the expansion module's ports
    after it, as ports.json lists them. On the ARMADA family the unit is the number: npupN is PortN
    and the cages follow the highest port, npup13 and npup14 on the XGS 136.
    """
    n = int(re.sub(r'\D', '', port['device']) or 0)
    if fam == 'armada':
        return panel.get(n) or {13: 'F1', 14: 'F2'}.get(n, 'Port%d' % n)
    if port['device'] in saved:
        return saved[port['device']]
    m = re.match(r'XGS front port (\S+)$', port['description'])
    if m:
        return m.group(1)
    order = [panel[u] for u in sorted(panel)] if panel else OCTEP_ORDER
    if n < len(order):
        return order[n]
    extra = (module or {}).get('extra') or []
    if n - len(order) < len(extra):
        return extra[n - len(order)].get('name')
    return None


def main():
    fam = family()
    prefix = 'oxp' if fam == 'octeontx' else 'npup'
    module = None
    try:
        with open(PORTS_JSON, encoding='utf-8') as f:
            module = json.load(f)
    except (OSError, ValueError):
        pass
    assigned = assignments()
    saved = saved_labels()
    model, panel = model_ports()
    ports = interfaces(prefix)
    for p in ports:
        p['label'] = label(p, fam, saved, module, panel) or p['device']
        p['assigned'] = assigned.get(p['device'])
    switch = None
    try:
        with open(SWITCH_STATE, encoding='utf-8') as f:
            switch = f.read().strip()
    except OSError:
        pass
    print(json.dumps({'family': fam, 'model': model, 'ports': ports, 'module': module, 'switch': switch}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
