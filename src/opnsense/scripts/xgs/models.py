#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Which appliance this is, and the ports on the front of it: every model of the two coprocessor
families this project drives, by the assembly number the board carries.

    models.py                 this appliance: assembly, family, model, how many front ports
    models.py --ports         its front ports, one a line: unit, tag, interface, label, media
    models.py --json          the same as JSON, and the whole table with --all

WHICH MODEL. The assembly number is the part number the board was built as - AMDA0202-0004 is an
XGS 3300 - and it is read from the planar serial, which carries it whole. It is the model and
nothing else: the board REVISION (smbios.planar.version) is not. Two XGS 3300s in the lab report
revisions 17 and 09 for the same assembly, and a table keyed on the revision named one of them and
not the other. The system version (3300r1, 136r1) says the same as the assembly and is printed
beside it, as a check a reader can make.

THE PORTS. Restated from the vendor's board files (xgs-bsp/data, one per assembly), which give
each port the label on the metal, its socket, and where it attaches: behind the board's switch, at
a port number, or on a MAC of the coprocessor itself. A port behind the switch is tagged
0x8000 | port << 8. A port on the coprocessor's own MAC is tagged by its place among the
coprocessor's own ports - the link to the switch among them - in the order of their addresses,
from 0: the board file's "0:N" is the MAC's number across the coprocessor's two CPs (CP0's eth0 to
eth2 are 0 to 2, CP1's 3 to 5), the coprocessor's Linux names its ports in that order, and the
network agent tags them so. Read on the XGS 136 (2026-10-21): eth0 is CP0's eth0, the link to the
switch, tag 0; eth1 to eth4 are CP0's eth2 and CP1's eth0 to eth2 - Port9, Port12, Port10 and
Port11 - tags 1 to 4. The XGS 3300's agent lists a tag 0 too, with no front-port flag, and F1 and F2
after it. On the XGS 138 the link to the switch is CP1's eth1, the fourth by address, so F1 is tag
0 - from its device tree, not read on one. The table says for each model whether it has been run.

The interface number is the vendor's logical interface for the port, the one the coprocessor is
told the port belongs to. The unit is this host's interface number: on the ARMADA family npupN is
PortN and the cages follow the highest port; on the OCTEON TX family the coprocessor's own cages
come first and then the switch's ports in order, which is the order the XGS 3300 has always had.
"""

import json
import os
import re
import subprocess
import sys


def _switch(n):
    return 0x8000 | (n << 8)


# Where the coprocessor's link to the board's switch is, as a "0:N" address, on the boards that put
# it somewhere other than first. Its place counts in the tags of the ports beside it.
UPLINK = {'AMDA0201': 0, 'AMDA0224': 4}

# Each coprocessor port's path, from the vendor's device tree for the board (sdk-ext-sophos patches,
# dt1/dt2/dt2-138): the CP and MAC, and what is behind it. The switch's ports all share its link.
PATHS = {
    'AMDA0200': {'uplink': 'CP0 eth0, 10GBASE-KR to the 88E6193X switch'},
    'AMDA0208': {'uplink': 'CP0 eth0, 10GBASE-KR to the 88E6193X switch'},
    'AMDA0201': {'uplink': 'CP0 eth0, 10GBASE-KR to the 88E6193X switch',
                 'Port9': 'CP0 eth2, through the AQR412C', 'Port12': 'CP1 eth0, through the AQR412C',
                 'Port10': 'CP1 eth1, through the AQR412C', 'Port11': 'CP1 eth2, through the AQR412C'},
    'AMDA0224': {'uplink': 'CP1 eth1, 5GBASE-KR to the switch',
                 'F1': 'CP0 eth0, 10GBASE-KR to the SFP+ cage', 'Port5': 'CP0 eth2, 2.5G through an AQR PHY',
                 'F2': 'CP1 eth0, 10GBASE-KR to the SFP+ cage', 'Port6': 'CP1 eth2, 2.5G through an AQR PHY'},
}


# label, socket, where it attaches ('s', n: switch port n; 'm', n: the coprocessor's MAC n), interface
_XGS87 = [('Port1', 'RJ45', 's', 1, 0), ('Port2', 'RJ45', 's', 2, 1), ('Port3', 'RJ45', 's', 3, 2),
          ('Port4', 'RJ45', 's', 4, 3), ('F1', 'SFP', 's', 9, 4)]
_XGS107 = [('Port%d' % n, 'RJ45', 's', n, n - 1) for n in range(1, 9)] + [('F1', 'SFP', 's', 9, 8)]
_XGS136 = ([('Port%d' % n, 'RJ45', 's', n, n - 1) for n in range(1, 9)] +
           [('F2', 'SFP', 's', 10, 8), ('F1', 'SFP', 's', 9, 9),
            ('Port9', 'RJ45', 'm', 2, 10), ('Port12', 'RJ45', 'm', 3, 11),
            ('Port10', 'RJ45', 'm', 4, 12), ('Port11', 'RJ45', 'm', 5, 13)])
_XGS138 = ([('Port%d' % n, 'RJ45', 's', n, n - 1) for n in range(1, 5)] +
           [('F2', 'SFP+', 'm', 3, 7), ('F1', 'SFP+', 'm', 0, 6),
            ('Port5', 'RJ45', 'm', 2, 4), ('Port6', 'RJ45', 'm', 5, 5)])
_XGS2100 = [('Port%d' % n, 'RJ45', 's', n, n - 1) for n in range(1, 9)] + \
           [('F1', 'SFP', 's', 9, 8), ('F2', 'SFP', 's', 10, 9)]
_XGS3300 = ([('Port%d' % n, 'RJ45', 's', n, n - 1) for n in range(1, 9)] +
            [('F3', 'SFP', 's', 9, 8), ('F4', 'SFP', 's', 10, 9),
             ('F1', 'SFP+', 'm', 10, 10), ('F2', 'SFP+', 'm', 12, 11)])

# assembly -> (model, family, ports, run on hardware here)
MODELS = {
    'AMDA0200-0001': ('XGS 87', 'armada', _XGS87, False),
    'AMDA0200-0002': ('XGS 87w', 'armada', _XGS87, False),
    'AMDA0200-0003': ('XGS 107', 'armada', _XGS107, False),
    'AMDA0200-0004': ('XGS 107w', 'armada', _XGS107, False),
    'AMDA0208-0001': ('XGS 116', 'armada', _XGS107, False),
    'AMDA0208-0002': ('XGS 116w', 'armada', _XGS107, False),
    'AMDA0201-0001': ('XGS 126', 'armada', _XGS136, False),
    'AMDA0201-0002': ('XGS 126w', 'armada', _XGS136, False),
    'AMDA0201-0003': ('XGS 136', 'armada', _XGS136, True),
    'AMDA0201-0004': ('XGS 136w', 'armada', _XGS136, False),
    'AMDA0224-0001': ('XGS 138', 'armada', _XGS138, False),
    'AMDA0202-0001': ('XGS 2100', 'octeontx', _XGS2100, False),
    'AMDA0202-0002': ('XGS 2300', 'octeontx', _XGS2100, False),
    'AMDA0202-0003': ('XGS 3100', 'octeontx', _XGS3300, False),
    'AMDA0202-0004': ('XGS 3300', 'octeontx', _XGS3300, True),
}

# The coprocessor on each board, as its board file names it.
CHIP = {
    'AMDA0200': 'CN9130', 'AMDA0201': 'CN9131', 'AMDA0208': 'CN9131', 'AMDA0224': 'CN9131',
    'AMDA0202-0001': 'CN8360', 'AMDA0202-0002': 'CN8360', 'AMDA0202-0003': 'CN8365', 'AMDA0202-0004': 'CN8365',
}


def chip(assembly):
    return CHIP.get(assembly) or CHIP.get(assembly.split('-')[0])


# The front port each family gives the internet to when it sets an appliance up by itself.
WAN = {'octeontx': 'Port2', 'armada': 'Port1'}
PREFIX = {'octeontx': 'oxp', 'armada': 'npup'}


def ports(assembly):
    """The model's front ports as dicts, in this host's unit order."""
    model, family, table, _ = MODELS[assembly]
    out = []
    # The coprocessor's own ports by address, the link to the switch among them: first unless the
    # board puts it elsewhere (UPLINK).
    order = sorted([n for _, _, kind, n, _ in table if kind == 'm'] +
                   [UPLINK.get(assembly.split('-')[0], -1)])
    paths = PATHS.get(assembly.split('-')[0], {})
    for label, socket, kind, n, iface in table:
        if kind == 's':
            tag = _switch(n)
        else:
            tag = order.index(n)
        out.append({'label': label, 'socket': socket, 'tag': tag, 'iface': iface,
                    'attach': ('switch port %d' % n) if kind == 's' else ('coprocessor MAC %d' % n),
                    'path': paths.get(label) or (('switch port %d' % n) if kind == 's' else '')})
    if family == 'armada':
        top = max(int(p['label'][4:]) for p in out if p['label'].startswith('Port'))
        for p in out:
            p['unit'] = int(p['label'][4:]) if p['label'].startswith('Port') else top + int(p['label'][1:])
        out.sort(key=lambda p: p['unit'])
    else:
        # The coprocessor's own cages first, by tag, then the switch's ports by port number:
        # oxp0 F1, oxp1 F2, oxp2 Port1 ... on the XGS 3300, as it has always been.
        out.sort(key=lambda p: (p['tag'] & 0x8000, p['tag']))
        for i, p in enumerate(out):
            p['unit'] = i
    for p in out:
        p['device'] = PREFIX[family] + str(p['unit'])
    return out


def assembly_of(serial):
    m = re.search(r'(AMDA0*[0-9]+-[0-9]{4})', serial or '')
    if m is None:
        return None
    # The planar serial runs the unit's own number on after the assembly's four digits; the
    # assembly is those four and no more.
    a = m.group(1)
    return 'AMDA%04d-%s' % (int(a[4:].split('-')[0]), a.split('-')[1])


def kenv(name):
    try:
        out = subprocess.run(['/bin/kenv', '-q', name], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout.strip()


def this():
    """This appliance: its assembly, and the model's entry if the table has it."""
    serial = os.environ.get('XGS_PLANAR_SERIAL') or kenv('smbios.planar.serial')
    a = assembly_of(serial)
    version = os.environ.get('XGS_SYSTEM_VERSION') or kenv('smbios.system.version')
    return a, version


def describe(a, version=None):
    model, family, table, tested = MODELS[a]
    return {'assembly': a, 'model': model, 'family': family, 'system_version': version,
            'run_on_hardware': tested, 'wan': WAN[family], 'ports': ports(a)}


def main(argv):
    if '--all' in argv:
        print(json.dumps({a: describe(a) for a in sorted(MODELS)}, indent=1))
        return 0
    a, version = this()
    if a not in MODELS:
        sys.stderr.write('not a model this project knows: %s\n' % (a or 'no assembly number'))
        return 1
    d = describe(a, version)
    if '--json' in argv:
        print(json.dumps(d, indent=1))
    elif '--ports' in argv:
        for p in d['ports']:
            print('%d 0x%04x %d %s %s' % (p['unit'], p['tag'], p['iface'], p['label'], p['socket']))
    else:
        print('%s %s %s %d' % (a, d['family'], d['model'].replace(' ', '_'), len(d['ports'])))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
