#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Every XGS model: a short table in the README, under its families table, and the data the site's
pages are written from - its home page's table, the page that lists every model, and a page for
each model (website/devices.py).

The two families with a driver are read from scripts/xgs/models.py, the table the drivers, the bring-up
and the first boot's assignment use; what a page says beyond the ports is tools/device_facts.py's,
each fact with where it was read; the families without a driver are restated below from the vendor's
board file for each assembly (xgs-bsp/data), which is where models.py's rows come from too. Nothing is
written twice: this writes the README between its markers and website/data/devices.json, and --check
fails when either is not what it would write.

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
SITE = 'https://npuforge.org'
START, END = '<!-- devices: written by tools/devices-table.py -->', '<!-- /devices -->'

spec = importlib.util.spec_from_file_location('models', os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'models.py'))
models = importlib.util.module_from_spec(spec)
spec.loader.exec_module(models)
spec = importlib.util.spec_from_file_location('device_facts', os.path.join(HERE, 'device_facts.py'))
facts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(facts)

FAMILY = {'armada': 'ARMADA', 'octeontx': 'OCTEON TX'}
DRIVER = {'armada': 'npuep', 'octeontx': 'octep'}
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
    order = ['RJ45', 'SFP', 'SFP+', 'SFP28', 'QSFP', 'QSFP28']
    return ', '.join('%d %s' % (count[s], s) for s in order if s in count)


def slug(model):
    return model.lower().replace(' ', '-')


def page_ports(assembly):
    """The model's ports as models.py has them, each told its speed, what it is on and between."""
    board = assembly.split('-')[0]
    wiring = facts.BOARDS[board]
    speeds = dict(facts.SPEED.get(board, {}), **facts.SPEED.get(assembly, {}))
    out = []
    for p in models.ports(assembly):
        own = wiring['own'].get(p['label'])
        m = re.match(r'switch port (\d+)', p['attach'])
        out.append({
            'label': p['label'], 'socket': p['socket'], 'speed': speeds.get(p['label'], '1G'),
            'device': p['device'], 'tag': '0x%04x' % p['tag'],
            'switch_port': int(m.group(1)) if m else None,
            'mac': own[0] if own else None, 'phy': own[1] if own else None,
            'poe': p['label'] in facts.POE.get(board, ()), 'bypass': p['label'] in facts.BYPASS.get(board, ()),
        })
    return out


def pages():
    """A page a model: the wired model with its Wi-Fi twin, each of them with its own state."""
    groups = {}
    order = sorted(models.MODELS, key=lambda a: (models.MODELS[a][1] != 'armada',
                                                 int(re.sub(r'\D', '', models.MODELS[a][0].split(' ')[1])), a))
    for a in order:
        model = models.MODELS[a][0]
        groups.setdefault(model[:-1] if model.endswith('w') else model, []).append(a)
    out = []
    for base, assemblies in groups.items():
        first = next(a for a in assemblies if models.MODELS[a][0] == base)
        _, family, _, run = models.MODELS[first]
        board = first.split('-')[0]
        wiring = facts.BOARDS[board]
        ports = page_ports(first)
        side, layout, modelled_on = facts.PANELS[base]
        out.append({
            'slug': slug(base), 'model': base, 'run': run,
            'variants': [{'model': models.MODELS[a][0], 'assembly': a, 'run': models.MODELS[a][3]} for a in assemblies],
            'family': FAMILY[family], 'family_key': family, 'driver': DRIVER[family], 'wan': models.WAN[family],
            'board': board, 'platform': facts.PLATFORM[board],
            'chip': models.chip(first), 'chip_parts': facts.CHIP_PARTS.get(models.chip(first)),
            'switch': wiring['switch'], 'uplink': list(wiring['uplink']), 'slot': wiring.get('slot'),
            'count': len(ports), 'sockets': sockets(ports), 'ports': ports,
            'panel': {'side': side, 'layout': layout, 'modelled_on': modelled_on, 'modelled_on_slug': slug(modelled_on) if modelled_on else None},
            'host': facts.HOSTS.get(base), 'link': facts.LINK[family],
            'physical': facts.PHYSICAL.get(base),
            'vendor': [{'source': s, 'figures': f} for s, f in facts.VENDOR.get(base, [])],
            'photo': facts.PHOTOS.get(base),
            'module_bay': {'bays': facts.BAYS_FIT[base][0], 'modules': [slug(a) for a in facts.BAYS_FIT[base][1]]}
                          if base in facts.BAYS_FIT else None,
            'host_bay': {'source': facts.HOST_BAY[base][0], 'items': facts.HOST_BAY[base][1]} if base in facts.HOST_BAY else None,
        })
    return out


def modules():
    """Every module the vendor's board files describe: its ports as they are numbered in bay A, the
    appliances whose datasheet lists it, and how far this project has gone with it - fitted and run
    here, added untested (it fits a model the drivers know, whose coprocessor names a module's ports
    itself), or no driver yet (it fits only the larger families)."""
    driven = {m[0] for m in models.MODELS.values()}
    out = []
    for assembly, (name, vendor_name, kind, ports) in facts.MODULES.items():
        fits = [{'model': m, 'bays': bays, 'slug': slug(m) if m in driven else None}
                for m, (bays, listed) in facts.BAYS_FIT.items() if assembly in listed]
        run_on = facts.MODULES_RUN.get(assembly)
        state = 'run' if run_on else ('rnd' if any(f['slug'] for f in fits) else 'none')
        shown = [{'label': 'A%d' % p['n'], 'socket': p['socket'], 'speed': p['speed'], 'phy': p['phy'],
                  'poe': p['poe'], 'bypass': p['bypass']} for p in sorted(ports, key=lambda p: p['n'])]
        out.append({'slug': slug(assembly), 'assembly': assembly, 'name': name, 'vendor_name': vendor_name, 'kind': kind,
                    'ports': shown, 'count': len(shown), 'sockets': sockets(shown),
                    'phys': sorted({p['phy'] for p in shown if p['phy']}), 'fits': fits, 'run_on': run_on, 'state': state})
    return out


def others():
    return [{'family': fam, 'model': m, 'assembly': a, 'chip': c, 'ports': p, 'bays': b}
            for fam, rows in OTHERS for m, a, c, p, b in rows]


def state_cell(page):
    run = [v['model'] for v in page['variants'] if v['run']]
    rest = [v['model'].split(' ')[1] for v in page['variants'] if not v['run']]
    if not run:
        return 'added, untested'
    return '**run here**' + ((' (%s: added, untested)' % ', '.join(rest)) if rest else '')


def readme_block():
    out = [START, '',
           '### Every model', '',
           'The two families with a driver, model by model - the table the drivers and the first boot read '
           '([`models.py`](src/opnsense/scripts/xgs/models.py), [front-ports-by-model.md](docs/families/front-ports-by-model.md)). '
           '**Run here** is an appliance this project has been run on; every other model is **added, '
           'untested** - from the vendor\'s board files and device trees, research and development, because '
           'there is no such appliance here to test on. It installs, its status page says so, and its first '
           'boot checks its ports against what its coprocessor reports '
           '([portcheck.py](src/opnsense/scripts/xgs/portcheck.py)). **Each model has a page on the site** - '
           'its ports drawn where they sit, the board drawn, every port\'s path, the vendor\'s figures - '
           '[every model on npuforge.org](%s/devices/).' % SITE, '',
           '| model | family | board | coprocessor | front ports | state |',
           '|---|---|---|---|---|---|']
    for p in pages():
        names = ', '.join([('**%s**' % v['model']) if v['run'] else v['model'] for v in p['variants'][:1]] +
                          [('**%s**' % v['model'].split(' ')[1]) if v['run'] else v['model'].split(' ')[1] for v in p['variants'][1:]])
        boards = ', '.join([p['variants'][0]['assembly']] + [v['assembly'].split('-')[1] for v in p['variants'][1:]])
        out.append('| [%s](%s/devices/%s/) | %s | `%s` | %s | %d: %s | %s |' % (
            names, SITE, p['slug'], p['family'], boards, p['chip'],
            p['count'], p['sockets'], state_cell(p)))
    out += ['', '> [!NOTE]', '> **Appliances of these models will be bought to test on.** Until then, each model '
            'marked untested is confirmed or corrected by its first owner\'s report.',
            '', 'The families without a driver, from the vendor\'s board files:', '',
            '| family | model | board | coprocessor | front ports | module bays |',
            '|---|---|---|---|---|:-:|']
    for r in others():
        out.append('| %s | %s | `%s` | %s | %s | %s |' % (r['family'], r['model'], r['assembly'], r['chip'], r['ports'], r['bays']))
    out += ['', END]
    return '\n'.join(out)


def data():
    return {'pages': pages(), 'others': others(), 'modules': modules(), 'sources': facts.SOURCES, 'metrics': facts.METRICS}


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
    body = json.dumps(data(), indent=1, ensure_ascii=False) + '\n'
    old = open(DATA, encoding='utf-8').read() if os.path.exists(DATA) else ''
    if body != old:
        stale.append('website/data/devices.json')
        if not check:
            os.makedirs(os.path.dirname(DATA), exist_ok=True)
            open(DATA, 'w', encoding='utf-8', newline='\n').write(body)
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
