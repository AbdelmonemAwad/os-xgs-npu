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
# "answered" first, and "no-table" with what /sys/kernel has of the agent when its list is not there,
# so that a coprocessor that answers without the list is not taken for one that did not answer (#657).
NWA = '/sys/kernel/nwa_ports_info'
ARMADA_READ = r'''
echo answered
if cd %s 2>/dev/null; then
  n=$(cat num_of_ports 2>/dev/null); echo "count ${n:-?}"; i=0
  while [ $i -lt ${n:-0} ]; do
    echo $i > curr_port
    set -- $(cat port_info)
    e=$6
    node=$(readlink /sys/class/net/$e/of_node)
    echo "soc $i $4 $e ${node##*devicetree/base/} $(cat /sys/class/net/$e/address)"
    i=$((i + 1))
  done
else
  echo "no-table $(cd /sys/kernel && ls -d *nwa* *port* 2>/dev/null | sort -u | tr '\n' ' ')"
fi
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


def run(argv, timeout=90, why=False):
    """What argv printed; with why, also the last line it gave as its reason on stderr - npu-run.sh
    says there why the coprocessor was not asked or did not answer."""
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return ('', 'no answer within %d seconds' % timeout) if why else ''
    except (OSError, subprocess.SubprocessError) as failure:
        return ('', str(failure)) if why else ''
    if not why:
        return out.stdout
    said = [l.strip() for l in out.stderr.splitlines() if l.strip()]
    return out.stdout, said[-1] if said else ''


def board():
    words = run(['/bin/sh', BOARD], timeout=10).split()
    return words + [''] * (3 - len(words))


def host_macs(prefix):
    """{mac: [interfaces]} for the driver's interfaces - a list, because two can carry one address
    (a LAGG gives its members its own, and a hand-set address can repeat one), and a port matched
    by an address two interfaces carry is not one this can name (#777)."""
    out = {}
    for block in re.split(r'\n(?=\S)', run(['/sbin/ifconfig', '-a'], timeout=10)):
        name = block.split(':', 1)[0]
        if not re.match(prefix + r'\d+$', name):
            continue
        m = re.search(r'\bether ([0-9a-f:]{17})', block)
        if m:
            out.setdefault(m.group(1).lower(), []).append(name)
    return out


def laggs():
    """{member: lagg} - a LAGG gives its members its own address, so two of them carrying one is
    what it does, not a fault; one set by hand on two interfaces is another matter (#777)."""
    out = {}
    for block in re.split(r'\n(?=\S)', run(['/sbin/ifconfig', '-a'], timeout=10)):
        name = block.split(':', 1)[0]
        if re.match(r'lagg\d+$', name):
            for member in re.findall(r'\blaggport: (\S+)', block):
                out[member] = name
    return out


def link(dev):
    m = re.search(r'status: (.+)', run(['/sbin/ifconfig', dev], timeout=10))
    return m.group(1).strip() if m else 'unknown'


def label_of(name):
    """'Port9' from '9' or 'F1' - the board file writes RJ45 ports by number alone."""
    return 'Port' + name if name.isdigit() else name


def check_armada(assembly):
    table = {p['device']: p for p in models.ports(assembly)}
    text, reason = run(['/bin/sh', NPU_RUN, '60', ARMADA_READ % NWA], timeout=120, why=True)
    said = text.splitlines()
    soc = [l.split() for l in said if l.startswith('soc ')]
    addrs = {}
    for l in said:
        w = l.split()
        if w[:1] == ['board'] and len(w) >= 3:
            addrs[w[2]] = label_of(w[1])
    if not soc:
        # until 2026-10-26 all three read "the coprocessor did not answer over the management link"
        missing = next((l for l in said if l.startswith('no-table')), None)
        count = next((l.split()[1] for l in said if l.startswith('count ') and len(l.split()) > 1), '?')
        if 'answered' not in said:
            why, line = 'no_answer', 'the coprocessor did not answer: %s' % (reason or 'it printed nothing')
        elif missing is not None:
            there = missing[len('no-table'):].strip()
            why, line = 'no_table', ('the coprocessor answered, but has no %s - its network agent\'s list of '
                                     'its ports; in /sys/kernel: %s' % (NWA, there or 'nothing of the agent'))
        else:
            why, line = 'empty_table', ('the coprocessor answered, and its network agent\'s list of its ports '
                                        'gave none (num_of_ports %s)' % count)
        return {'verdict': 'unread', 'why': why, 'lines': [line], 'ports': [],
                'board_labels': len(addrs)}
    macs = host_macs('npup')
    lines, agree, disagree, shared, rows = [], 0, 0, 0, []
    member = laggs()
    for devs in sorted(d for d in macs.values() if len(d) > 1):
        groups = {member.get(d) for d in devs}
        if len(groups) == 1 and None not in groups:
            lines.append('%s carry one address between them (members of %s, which gives them its own) - '
                         'a port matched by it is named by the tag the driver gives it' % (' and '.join(devs), groups.pop()))
        else:
            lines.append('%s carry one address between them (not the members of one LAGG - an address set by '
                         'hand on more than one?) - a port matched by it cannot be named' % ' and '.join(devs))
    # and the other side: the coprocessor's own ports, which take the address the driver gives each tag
    theirs = {}
    for w in soc:
        if len(w) > 5 and w[2] != '2':
            theirs.setdefault(w[5].lower(), []).append(w[1])
    for idxs in sorted(i for i in theirs.values() if len(i) > 1):
        lines.append('the coprocessor\'s ports %s carry one address between them' % ' and '.join(idxs))
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
        devs = macs.get(mac, [])
        how = ''
        # A LAGG's members carry its address, and the coprocessor's ports take it from them: the
        # member whose tag in the driver's table is this port's is the one bound there (#823)
        tagged = [d for d in devs if member.get(d) and d in table and table[d]['tag'] == int(idx)]
        if len(devs) > 1 and len(tagged) == 1:
            devs, how = tagged, ', by its tag (the address is %s\'s)' % member[tagged[0]]
        if len(devs) > 1:
            shared += 1
            lines.append('coprocessor port %s (%s, %s = %s): the board says %s; its address is carried by %s'
                         ' - which of them is bound there cannot be told'
                         % (idx, eth, where, addr, said, ' and '.join(devs)))
            rows.append({'tag': int(idx), 'eth': eth, 'mac': where, 'address': addr, 'board': said,
                         'driver': None, 'interface': None, 'shared': devs, 'agrees': None})
            continue
        dev = devs[0] if devs else None
        put = table[dev]['label'] if dev in table else None
        ok = put is not None and put == said
        agree += ok
        disagree += not ok
        state = link(dev) if dev else 'no interface'
        lines.append('coprocessor port %s (%s, %s = %s): the board says %s; the driver bound %s there%s - %s; %s'
                     % (idx, eth, where, addr, said, ('%s (%s)' % (dev, put)) if dev else 'nothing', how,
                        'agrees' if ok else 'DISAGREES', state))
        rows.append({'tag': int(idx), 'eth': eth, 'mac': where, 'address': addr, 'board': said,
                     'driver': put, 'interface': dev, 'agrees': ok, 'link': state, 'by_tag': bool(how)})
    for p in sorted(table.values(), key=lambda p: p['unit']):
        if p['tag'] & 0x8000:
            rows.append({'tag': p['tag'], 'label': p['label'], 'interface': p['device'], 'link': link(p['device'])})
    verdict = 'disagrees' if disagree else ('unclear' if shared else 'agrees')
    tagged = len([r for r in rows if r.get('by_tag')])
    lines.append('%d of %d of the coprocessor\'s own front ports as the board says%s%s' % (
        agree, agree + disagree + shared, ' - the table is wrong for this model' if disagree else
        (' - %d matched by an address two interfaces carry, not judged' % shared if shared else ''),
        ' (%d of them named by the tag the driver gives them, their address a LAGG\'s)' % tagged if tagged else ''))
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
