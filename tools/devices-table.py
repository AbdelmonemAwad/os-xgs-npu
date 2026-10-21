#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Every XGS model as a table: the README's, under its families table, and the site's home page's.

The two families with a driver are read from scripts/xgs/models.py, the table the drivers, the bring-up
and the first boot's assignment use; the others are restated below from the vendor's board file for
each assembly (xgs-bsp/data), which is where models.py's rows come from too. Nothing is written twice:
this writes the README between its markers and website/data/devices.json, and --check fails when
either is not what it would write.

    python3 tools/devices-table.py           write both
    python3 tools/devices-table.py --check   say what is stale, exit 1 if anything is
"""

import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
README = os.path.join(ROOT, 'README.md')
DATA = os.path.join(ROOT, 'website', 'data', 'devices.json')
START, END = '<!-- devices: written by tools/devices-table.py -->', '<!-- /devices -->'

spec = importlib.util.spec_from_file_location('models', os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'models.py'))
models = importlib.util.module_from_spec(spec)
spec.loader.exec_module(models)

FAMILY = {'armada': 'ARMADA', 'octeontx': 'OCTEON TX'}
# The module bays of the boards with a driver, as their board files list them.
BAYS = {'AMDA0202': 'A'}

# The families without a driver, from the vendor's board files: model, assembly, coprocessor, the front
# ports by socket, the module bays.
OTHERS = [
    ('OCTEON TX2', [
        ('XGS 4300', 'AMDA0203-0001', 'CN9265', '8 RJ45, 4 SFP+', 'A, B'),
        ('XGS 4500', 'AMDA0203-0002', 'CN9265', '8 RJ45, 4 SFP+', 'A, B'),
        ('XGS 4310', 'AMDA0228', 'CN9670', '8 RJ45, 4 SFP28', 'A, B'),
        ('XGS 5500', 'AMDA0204-0001', 'CN9670', '8 RJ45, 8 SFP+', 'A-D'),
        ('XGS 6500', 'AMDA0204-0002', 'CN9670', '8 RJ45, 12 SFP+', 'A-D'),
        ('XGS 6580', 'AMDA0225-0001', 'CN9670', '4 RJ45, 6 SFP+, 4 SFP28', 'A-D'),
    ]),
    ('OCTEON TX2 98XX', [
        ('XGS 7500', 'AMDA0205-0001', 'CN9880', '8 RJ45, 12 SFP+, 2 QSFP28', 'A-D'),
        ('XGS 8500', 'AMDA0205-0001', 'CN9880', '8 RJ45, 12 SFP+, 2 QSFP28', 'A-D'),
        ('XGS 7580', 'AMDA0226-0001', 'CN9880', '6 RJ45, 4 SFP+, 8 SFP28, 2 QSFP28', 'A-D'),
    ]),
    ('no coprocessor in the board file', [
        ('XGS 88, 88w', 'AMDA0220', '-', '4 RJ45', '-'),
        ('XGS 108, 108w', 'AMDA0221', '-', '6 RJ45, 1 SFP', '-'),
        ('XGS 118, 118w', 'AMDA0222', '-', '9 RJ45, 1 SFP', '-'),
        ('XGS 128, 128w', 'AMDA0223', '-', '9 RJ45, 1 SFP', '-'),
    ]),
]


def sockets(ports):
    """'8 RJ45, 2 SFP, 2 SFP+' - the front ports by socket, in the order RJ45 first."""
    count = {}
    for p in ports:
        count[p['socket']] = count.get(p['socket'], 0) + 1
    order = ['RJ45', 'SFP', 'SFP+', 'SFP28', 'QSFP28']
    return ', '.join('%d %s' % (count[s], s) for s in order if s in count)


def driven():
    """The models the drivers know, one row per model, w models beside their wired twin."""
    rows = []
    seen = {}
    for a in sorted(models.MODELS, key=lambda a: (models.MODELS[a][1] != 'armada',
                                                  int(re.sub(r'\D', '', models.MODELS[a][0].split(' ')[1])), a)):
        model, family, _, tested = models.MODELS[a]
        ports = models.ports(a)
        row = {
            'family': FAMILY[family], 'model': model, 'assembly': a, 'chip': models.chip(a),
            'ports': '%d: %s' % (len(ports), sockets(ports)),
            'labels': ' '.join(p['label'] for p in ports),
            'bays': BAYS.get(a.split('-')[0], '-'),
            'run': tested,
        }
        # An XGS 87w is an XGS 87 with Wi-Fi on the host's side: one row, unless one was run here.
        twin = model[:-1] if model.endswith('w') else None
        if twin and twin in seen and not tested and not seen[twin]['run']:
            seen[twin]['model'] += ', ' + model.split(' ')[1]
            seen[twin]['assembly'] += ', ' + a.split('-')[1]
            continue
        seen[model] = row
        rows.append(row)
    return rows


def others():
    return [{'family': fam, 'model': m, 'assembly': a, 'chip': c, 'ports': p, 'bays': b}
            for fam, rows in OTHERS for m, a, c, p, b in rows]


def readme_block():
    out = [START, '',
           '### Every model', '',
           'The two families with a driver, model by model - the table the drivers and the first boot read '
           '([`models.py`](src/opnsense/scripts/xgs/models.py), [front-ports-by-model.md](docs/families/front-ports-by-model.md)). '
           '**Run here** is an appliance this project has been run on; every other model is '
           '**research and development: untested, because there is no such appliance here to test on** - '
           'it installs, and its status page says so.', '',
           '| family | model | board | coprocessor | front ports | the labels, in interface order | module bays | state |',
           '|---|---|---|---|---|---|:-:|---|']
    for r in driven():
        out.append('| %s | %s | `%s` | %s | %s | %s | %s | %s |' % (
            r['family'], ('**%s**' % r['model']) if r['run'] else r['model'], r['assembly'], r['chip'],
            r['ports'], r['labels'], r['bays'],
            '**run here**' if r['run'] else 'research and development, untested'))
    out += ['', 'The families without a driver, from the vendor\'s board files:', '',
            '| family | model | board | coprocessor | front ports | module bays |',
            '|---|---|---|---|---|:-:|']
    for r in others():
        out.append('| %s | %s | `%s` | %s | %s | %s |' % (r['family'], r['model'], r['assembly'], r['chip'], r['ports'], r['bays']))
    out += ['', END]
    return '\n'.join(out)


def write_or_check(check):
    stale = []
    text = open(README, encoding='utf-8').read()
    if START in text and END in text:
        new = text[:text.index(START)] + readme_block() + text[text.index(END) + len(END):]
    else:
        anchor = '\nWhat has run on which board, and why there are two drivers'
        if anchor not in text:
            sys.exit('README: neither the markers nor the line after the families table')
        new = text.replace(anchor, '\n' + readme_block() + '\n' + anchor, 1)
    if new != text:
        stale.append('README.md')
        if not check:
            open(README, 'w', encoding='utf-8', newline='\n').write(new)
    data = json.dumps({'driven': driven(), 'others': others()}, indent=1, ensure_ascii=False) + '\n'
    old = open(DATA, encoding='utf-8').read() if os.path.exists(DATA) else ''
    if data != old:
        stale.append('website/data/devices.json')
        if not check:
            os.makedirs(os.path.dirname(DATA), exist_ok=True)
            open(DATA, 'w', encoding='utf-8', newline='\n').write(data)
    return stale


def main(argv):
    check = '--check' in argv
    stale = write_or_check(check)
    if check and stale:
        print('not what tools/devices-table.py writes: ' + ', '.join(stale))
        return 1
    print(('written: ' + ', '.join(stale)) if stale and not check else 'the device tables are current')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
