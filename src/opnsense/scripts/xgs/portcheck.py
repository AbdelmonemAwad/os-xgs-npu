#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The driver's front ports held against what the coprocessor itself reports - on the appliance, for a
model this project has not been run on.

    portcheck.py            the check, one line a port, and a verdict
    portcheck.py --json     the same as JSON, for the problem reporter

WHY. Every model's ports are in scripts/xgs/models.py, restated from the vendor's board files; only
the XGS 136 and the XGS 3300 have been run. On any other model the first boot is where the table
meets the hardware, and this says whether they agree - so that a model's first owner, by reporting
it, confirms the table or shows where it is wrong. It changes nothing.

ARMADA. The coprocessor's network agent lists its own ports (/sys/kernel/nwa_ports_info): each one's
place, which is its tag, its kind - the link to the switch, or a front port of its own - and its
Linux interface, whose device-tree node names the CP and the MAC. The board file gives each label's
address, "0:N" with N the MAC's number across the two CPs. And npuep gives the agent each tag's
address, its own interface's: so the interface of the host whose address the coprocessor's port
carries is the one the driver bound to that tag. Board and driver agree when they name the same
label. Read on the XGS 136 before it was written down: every one agrees.

OCTEON TX. The network agent's discover answer (dev.octep.0.nwa.ports) lists every tag it has, with
a flag for a front port; the table's tags have to be among them, and every front tag the agent has
should be the table's or an expansion module's.

No address leaves this: the comparison is made here, and only labels, tags and interface names are
printed.
"""

import json
import re
import subprocess
import sys

try:
    import models
except ImportError:
    models = None

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
BOARD = '/usr/local/opnsense/scripts/xgs/board.sh'

# On the coprocessor: its own ports as the agent lists them, and each label's board-file address.
ARMADA_READ = r'''
cd /sys/kernel/nwa_ports_info || exit 1
n=$(cat num_of_ports); i=0
while [ $i -lt $n ]; do
  echo $i > curr_port
  set -- $(cat port_info)
  e=$6
  node=$(readlink /sys/class/net/$e/of_node)
  echo "soc $i $4 $e ${node##*devicetree/base/} $(cat /sys/class/net/$e/address)"
  i=$((i + 1))
done
p=0
while [ $p -lt 40 ]; do
  l=$(xgs-platform npu0.phy$p.label 2>/dev/null | cut -d= -f2)
  if [ -n "$l" ]; then
    e=$(xgs-platform npu0.phy$p.eth 2>/dev/null | cut -d= -f2)
    echo "board $l $(xgs-platform npu0.eth$e.port 2>/dev/null | cut -d= -f2)"
  fi
  p=$((p + 1))
done
'''


def run(argv, timeout=90):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout


def board():
    words = run(['/bin/sh', BOARD], timeout=10).split()
    return words + [''] * (3 - len(words))


def host_macs(prefix):
    """{mac: interface} for the driver's interfaces."""
    out = {}
    for block in re.split(r'\n(?=\S)', run(['/sbin/ifconfig', '-a'], timeout=10)):
        name = block.split(':', 1)[0]
        if not re.match(prefix + r'\d+$', name):
            continue
        m = re.search(r'\bether ([0-9a-f:]{17})', block)
        if m:
            out[m.group(1).lower()] = name
    return out


def link(dev):
    m = re.search(r'status: (.+)', run(['/sbin/ifconfig', dev], timeout=10))
    return m.group(1).strip() if m else 'unknown'


def label_of(name):
    """'Port9' from '9' or 'F1' - the board file writes RJ45 ports by number alone."""
    return 'Port' + name if name.isdigit() else name


def check_armada(assembly):
    table = {p['device']: p for p in models.ports(assembly)}
    by_label = {p['label']: p for p in table.values()}
    text = run(['/bin/sh', NPU_RUN, '60', ARMADA_READ], timeout=120)
    soc = [l.split() for l in text.splitlines() if l.startswith('soc ')]
    addrs = {}
    for l in text.splitlines():
        w = l.split()
        if w[:1] == ['board'] and len(w) >= 3:
            addrs[w[2]] = label_of(w[1])
    if not soc:
        return {'verdict': 'unread', 'lines': ['the coprocessor did not answer over the management link'], 'ports': []}
    macs = host_macs('npup')
    lines, agree, disagree, rows = [], 0, 0, []
    for w in soc:
        idx, kind, eth = w[1], w[2], w[3]
        node = w[4] if len(w) > 4 else ''
        mac = (w[5] if len(w) > 5 else '').lower()
        m = re.search(r'cp(\d)/config-space/ethernet@0/eth(\d)', node)
        addr = '0:%d' % (int(m.group(1)) * 3 + int(m.group(2))) if m else '?'
        where = ('CP%s eth%s' % (m.group(1), m.group(2))) if m else node
        if kind == '2':
            lines.append('coprocessor port %s (%s, %s = %s): the link to the switch' % (idx, eth, where, addr))
            rows.append({'tag': int(idx), 'eth': eth, 'mac': where, 'address': addr, 'switch_link': True})
            continue
        said = addrs.get(addr, '?')
        dev = macs.get(mac)
        put = table[dev]['label'] if dev in table else None
        ok = put is not None and put == said
        agree += ok
        disagree += not ok
        state = link(dev) if dev else 'no interface'
        lines.append('coprocessor port %s (%s, %s = %s): the board says %s; the driver bound %s there - %s; %s'
                     % (idx, eth, where, addr, said, ('%s (%s)' % (dev, put)) if dev else 'nothing',
                        'agrees' if ok else 'DISAGREES', state))
        rows.append({'tag': int(idx), 'eth': eth, 'mac': where, 'address': addr, 'board': said,
                     'driver': put, 'interface': dev, 'agrees': ok, 'link': state})
    for p in sorted(table.values(), key=lambda p: p['unit']):
        if p['tag'] & 0x8000:
            rows.append({'tag': p['tag'], 'label': p['label'], 'interface': p['device'], 'link': link(p['device'])})
    verdict = 'agrees' if disagree == 0 else 'disagrees'
    lines.append('%d of %d of the coprocessor\'s own front ports as the board says%s' % (
        agree, agree + disagree, '' if disagree == 0 else ' - the table is wrong for this model'))
    return {'verdict': verdict, 'lines': lines, 'ports': rows}


def check_octeontx(assembly):
    table = models.ports(assembly)
    text = run(['/sbin/sysctl', '-n', 'dev.octep.0.nwa.ports'], timeout=30)
    found = {}
    for l in text.splitlines():
        w = l.split()
        if len(w) >= 4 and w[0].isdigit() and w[1].startswith('0x'):
            found[int(w[1], 16)] = int(w[3], 16) & 1
    if not found:
        return {'verdict': 'unread', 'lines': ['the network agent\'s discover answer could not be read'], 'ports': []}
    lines, rows, bad = [], [], 0
    for p in table:
        has = p['tag'] in found and found[p['tag']]
        bad += not has
        state = link(p['device'])
        lines.append('%s %s (tag 0x%04x): %s; %s' % (p['device'], p['label'], p['tag'],
                     'the agent has it' if has else 'the agent HAS NO such front port', state))
        rows.append({'tag': p['tag'], 'label': p['label'], 'interface': p['device'], 'agent': bool(has), 'link': state})
    mine = {p['tag'] for p in table}
    extra = sorted(t for t, front in found.items() if front and t not in mine)
    if extra:
        lines.append('front tags the agent has and the table does not (an expansion module\'s, or the table '
                     'is short): ' + ' '.join('0x%04x' % t for t in extra))
    verdict = 'agrees' if bad == 0 else 'disagrees'
    lines.append('%d of %d of the table\'s ports are the agent\'s%s' % (
        len(table) - bad, len(table), '' if bad == 0 else ' - the table is wrong for this model'))
    return {'verdict': verdict, 'lines': lines, 'ports': rows, 'agent_front_tags_not_in_table': extra}


def check():
    if models is None:
        return {'verdict': 'unread', 'lines': ['models.py is missing'], 'ports': []}
    assembly, version = models.this()
    if assembly not in models.MODELS:
        return {'verdict': 'unknown', 'lines': ['not a model this project knows: %s' % assembly], 'ports': []}
    words = board()
    model, family, _, tested = models.MODELS[assembly]
    out = check_armada(assembly) if family == 'armada' else check_octeontx(assembly)
    out.update({'model': model, 'assembly': assembly, 'family': family, 'run_here': tested,
                'research_and_development': words[2] == 'rnd'})
    out['lines'].insert(0, '%s (%s), %s%s' % (model, assembly, 'ARMADA' if family == 'armada' else 'OCTEON TX',
                                              '' if tested else ': added from the vendor\'s files, untested'))
    return out


def main(argv):
    result = check()
    if '--json' in argv:
        print(json.dumps(result, indent=1))
    else:
        print('\n'.join(result['lines']))
    return 0 if result['verdict'] in ('agrees',) else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
