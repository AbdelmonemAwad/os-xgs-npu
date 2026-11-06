#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""bridge-pathcost.sh away from the appliance: which path costs it sets, run after run. ifconfig is a
stand-in holding a made-up kernel's bridges - it prints them as FreeBSD's `ifconfig -a` does, and
takes `ifpathcost` as if_bridge does: ADMCOST set, and a cost above 65535 kept at 65535 on a port
speaking the old STP (bstp_set_path_cost). logger keeps what it is told.

    python3 tools/test-bridge-pathcost.py
"""
import json
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'npuctl', 'bridge-pathcost.sh')

# The member flags of net/if_bridgevar.h that ifconfig names, and ADMCOST, which it shows in the
# number alone.
IFCONFIG = r'''#!/usr/bin/env python3
import json
import os
import sys

ADMCOST = 0x400
NAMES = ((0x1, 'LEARNING'), (0x2, 'DISCOVER'), (0x4, 'STP'), (0x40, 'AUTOEDGE'), (0x80, 'PTP'),
         (0x100, 'AUTOPTP'))
UP = 'flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500'

with open(os.environ['KERNEL']) as f:
    kernel = json.load(f)
args = sys.argv[1:]
if args == ['-a']:
    for name, port in kernel['ports'].items():
        print('%s: %s' % (name, UP))
        print('\toptions=80000<LINKSTATE>')
        print('\tether 02:00:00:00:00:%02x' % port['n'])
        if port['media']:
            print('\tmedia: Ethernet autoselect (%s <full-duplex>)' % port['media'])
            print('\tstatus: active')
        else:
            print('\tmedia: Ethernet autoselect (none)')
            print('\tstatus: no carrier')
        print('\tnd6 options=29<PERFORMNUD,IFDISABLED,AUTO_LINKLOCAL>')
    for name, bridge in kernel['bridges'].items():
        print('%s: %s' % (name, UP))
        print('\toptions=0')
        print('\tether 02:00:00:00:00:%02x' % bridge['n'])
        print('\tid 02:00:00:00:00:%02x priority 32768 hellotime 2 fwddelay 15' % bridge['n'])
        print('\tmaxage 20 holdcnt 6 proto %s maxaddr 2000 timeout 1200' % bridge['proto'])
        print('\troot id 02:00:00:00:00:%02x priority 32768 ifcost 0 port 0' % bridge['n'])
        print('\tbridge flags=0<>')
        for m in bridge['members']:
            flags = 0x1 | 0x2 | 0x40 | 0x100 | (0x4 | 0x80 if m['stp'] else 0) | (ADMCOST if m['admcost'] else 0)
            print('\tmember: %s flags=%x<%s>' % (m['name'], flags, ','.join(n for b, n in NAMES if flags & b)))
            line = '\t        ifmaxaddr 0 port %d priority 128 path cost %d' % (m['port'], m['cost'])
            if m['stp']:
                print(line + ' proto ' + m['proto'])
                print('\t        role designated state forwarding')
            else:
                print(line)
        print('\tgroups: bridge')
        print('\tnd6 options=9<PERFORMNUD,IFDISABLED>')
    sys.exit(0)
if len(args) == 4 and args[1] == 'ifpathcost':
    with open(os.environ['CALLS'], 'a') as f:
        f.write('ifconfig %s\n' % ' '.join(args))
    cost = int(args[3])
    for m in kernel['bridges'].get(args[0], {}).get('members', []):
        if m['name'] == args[2] and 0 < cost <= 200000000:
            m['cost'] = 65535 if m['proto'] == 'stp' and cost > 65535 else cost
            m['admcost'] = True
            with open(os.environ['KERNEL'], 'w') as f:
                json.dump(kernel, f)
            sys.exit(0)
sys.exit(1)
'''

LOGGER = '#!/bin/sh\necho "logger $*" >> "${CALLS}"\n'


def member(name, cost, proto='rstp', stp=True, admcost=False):
    """A bridge member: proto is the protocol the port speaks, shown only while stp (in spanning tree)."""
    return {'name': name, 'cost': cost, 'proto': proto, 'stp': stp, 'admcost': admcost,
            'port': int(''.join(c for c in name if c.isdigit()) or 0) + 1}


class PathCost(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.kernel = os.path.join(self.dir, 'kernel.json')
        self.calls = os.path.join(self.dir, 'calls')
        self.costs = os.path.join(self.dir, 'costs')
        open(self.calls, 'w').close()
        for name, body in (('ifconfig', IFCONFIG), ('logger', LOGGER)):
            path = os.path.join(self.dir, name)
            with open(path, 'w', newline='\n') as f:
                f.write(body)
            os.chmod(path, 0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def machine(self, ports, bridges):
        """ports: name -> its medium as ifconfig names it, None for no carrier; bridges: name ->
        (protocol, [member, ...])."""
        kernel = {'ports': {}, 'bridges': {}}
        for i, (name, medium) in enumerate(ports.items()):
            kernel['ports'][name] = {'n': i + 1, 'media': medium}
        for i, (name, (proto, members)) in enumerate(bridges.items()):
            kernel['bridges'][name] = {'n': 0x10 + i, 'proto': proto, 'members': members}
        self.save(kernel)

    def load(self):
        with open(self.kernel) as f:
            return json.load(f)

    def save(self, kernel):
        with open(self.kernel, 'w') as f:
            json.dump(kernel, f)

    def change(self, name, **fields):
        """What the kernel or an owner does to a member between two runs."""
        kernel = self.load()
        for bridge in kernel['bridges'].values():
            for m in bridge['members']:
                if m['name'] == name:
                    m.update(fields)
        self.save(kernel)

    def medium(self, port, medium):
        kernel = self.load()
        kernel['ports'][port]['media'] = medium
        self.save(kernel)

    def run_script(self, *args):
        """One run, as cron makes it: what it set and what it said."""
        with open(self.calls) as f:
            before = len(f.read().splitlines())
        env = dict(os.environ, PATH=self.dir + os.pathsep + os.environ.get('PATH', ''),
                   KERNEL=self.kernel, CALLS=self.calls, COSTS=self.costs)
        done = subprocess.run(['sh', SCRIPT] + list(args), env=env, capture_output=True, text=True,
                              errors='replace', timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stderr, '')
        with open(self.calls) as f:
            calls = f.read().splitlines()[before:]
        return ([c[len('ifconfig '):] for c in calls if c.startswith('ifconfig ')],
                [c[len('logger '):] for c in calls if c.startswith('logger ')])

    def kept(self):
        if not os.path.exists(self.costs):
            return []
        with open(self.costs) as f:
            return f.read().splitlines()

    def test_a_gigabit_port_latched_at_10_mbit_is_set_once(self):
        self.machine({'npup1': '1000baseT'}, {'bridge0': ('rstp', [member('npup1', 2000000)])})
        sets, logs = self.run_script()
        self.assertEqual(sets, ['bridge0 ifpathcost npup1 20000'])
        self.assertEqual(logs, ['-t npuctl -p daemon.notice bridge0: npup1 path cost 2000000 -> 20000 (1000 Mb/s)'])
        self.assertEqual(self.run_script(), ([], []))

    def test_what_it_set_is_kept_one_line_a_member(self):
        self.machine({'npup1': '1000baseT', 'npup2': '1000baseT'},
                     {'bridge0': ('rstp', [member('npup1', 2000000), member('npup2', 2000000)])})
        self.run_script()
        self.assertEqual(self.kept(), ['bridge0 npup1 20000', 'bridge0 npup2 20000'])
        self.medium('npup1', '100baseTX')
        self.run_script()
        self.assertEqual(sorted(self.kept()), ['bridge0 npup1 200000', 'bridge0 npup2 20000'])

    def test_a_25g_port_is_set_from_its_medium_as_freebsd_names_it(self):
        """IFM_2500_T is 2500Base-T, with a capital B: until 2026-11-06 the two 2.5G ports matched
        nothing (A9)."""
        self.machine({'npup11': '2500Base-T', 'npup12': '2500Base-T'},
                     {'bridge0': ('rstp', [member('npup11', 2000000), member('npup12', 55)])})
        sets, _ = self.run_script()
        self.assertEqual(sets, ['bridge0 ifpathcost npup11 8000', 'bridge0 ifpathcost npup12 8000'])
        self.assertEqual(self.run_script(), ([], []))

    def test_a_port_speaking_the_old_stp_is_asked_once_for_what_it_keeps(self):
        """The old STP keeps 65535 of 200000: until 2026-11-06 it was asked for 200000 every minute (A8)."""
        self.machine({'npup3': '100baseTX', 'npup4': '100baseTX', 'npup5': '1000baseT'},
                     {'bridge0': ('stp', [member('npup3', 20000, proto='stp'),
                                          member('npup4', 65535, proto='stp'),
                                          member('npup5', 65535, proto='stp')])})
        sets, logs = self.run_script()
        self.assertEqual(sets, ['bridge0 ifpathcost npup3 65535', 'bridge0 ifpathcost npup5 20000'])
        self.assertIn('-t npuctl -p daemon.notice bridge0: npup3 path cost 20000 -> 65535 (100 Mb/s)', logs)
        for _ in range(3):
            self.assertEqual(self.run_script(), ([], []))

    def test_a_port_speaking_rstp_keeps_the_whole_figure(self):
        self.machine({'npup6': '100baseTX', 'npup7': '10baseT/UTP'},
                     {'bridge0': ('rstp', [member('npup6', 20000), member('npup7', 55)])})
        sets, _ = self.run_script()
        self.assertEqual(sets, ['bridge0 ifpathcost npup6 200000', 'bridge0 ifpathcost npup7 2000000'])
        self.assertEqual(self.run_script(), ([], []))

    def test_a_member_outside_spanning_tree_is_asked_once_whatever_it_speaks(self):
        """No protocol is shown for it, and the old STP may be what it speaks underneath: at 65535 after
        one ask, it keeps no more."""
        self.machine({'npup8': '100baseTX', 'npup9': '100baseTX', 'npup10': '10baseT/UTP'},
                     {'bridge0': ('rstp', [member('npup8', 2000000, proto='stp', stp=False),
                                           member('npup9', 2000000, stp=False),
                                           member('npup10', 65535, proto='stp', stp=False)])})
        sets, _ = self.run_script()
        self.assertEqual(sets, ['bridge0 ifpathcost npup8 200000', 'bridge0 ifpathcost npup9 200000'])
        for _ in range(3):
            self.assertEqual(self.run_script(), ([], []))
        costs = [m['cost'] for m in self.load()['bridges']['bridge0']['members']]
        self.assertEqual(costs, [65535, 200000, 65535])

    def test_a_cost_the_owner_set_is_left_alone(self):
        """ADMCOST and not the cost this script set: the owner's (A13)."""
        self.machine({'npup1': '1000baseT', 'npup2': '1000baseT'},
                     {'bridge0': ('rstp', [member('npup1', 50000, admcost=True),
                                           member('npup2', 2000000)])})
        sets, _ = self.run_script()
        self.assertEqual(sets, ['bridge0 ifpathcost npup2 20000'])
        self.assertEqual(self.run_script(), ([], []))
        self.assertEqual(self.load()['bridges']['bridge0']['members'][0]['cost'], 50000)

    def test_a_cost_the_owner_sets_over_this_scripts_own_is_left_alone(self):
        self.machine({'npup1': '1000baseT'}, {'bridge0': ('rstp', [member('npup1', 2000000)])})
        self.assertEqual(self.run_script()[0], ['bridge0 ifpathcost npup1 20000'])
        self.change('npup1', cost=40000)
        self.assertEqual(self.run_script(), ([], []))
        self.medium('npup1', '100baseTX')
        self.assertEqual(self.run_script(), ([], []))

    def test_its_own_cost_follows_the_speed(self):
        self.machine({'npup1': '1000baseT'}, {'bridge0': ('rstp', [member('npup1', 2000000)])})
        self.assertEqual(self.run_script()[0], ['bridge0 ifpathcost npup1 20000'])
        self.medium('npup1', '100baseTX')
        sets, logs = self.run_script()
        self.assertEqual(sets, ['bridge0 ifpathcost npup1 200000'])
        self.assertEqual(logs, ['-t npuctl -p daemon.notice bridge0: npup1 path cost 20000 -> 200000 (100 Mb/s)'])
        self.assertEqual(self.run_script(), ([], []))

    def test_its_own_cost_cut_by_a_fall_back_to_the_old_stp_is_still_its_own(self):
        """bstp_set_port_proto cuts the cost to 65535 when the port falls back; once the port speaks
        RSTP again its cost is still this script's to set."""
        self.machine({'npup2': '100baseTX'}, {'bridge0': ('rstp', [member('npup2', 2000000)])})
        self.assertEqual(self.run_script()[0], ['bridge0 ifpathcost npup2 200000'])
        self.change('npup2', proto='stp', cost=65535)
        self.assertEqual(self.run_script(), ([], []))
        self.change('npup2', proto='rstp')
        self.assertEqual(self.run_script()[0], ['bridge0 ifpathcost npup2 200000'])
        self.assertEqual(self.run_script(), ([], []))

    def test_a_dark_port_and_another_nic_are_left_alone(self):
        self.machine({'npup2': None, 'igb0': '1000baseT'},
                     {'bridge0': ('rstp', [member('npup2', 55), member('igb0', 2000000)])})
        self.assertEqual(self.run_script(), ([], []))

    def test_a_member_named_does_its_own_bridge_only(self):
        self.machine({'npup1': '1000baseT', 'npup2': '1000baseT'},
                     {'bridge0': ('rstp', [member('npup1', 2000000)]),
                      'bridge1': ('rstp', [member('npup2', 2000000)])})
        self.assertEqual(self.run_script('npup9'), ([], []))
        self.assertEqual(self.run_script('npup2')[0], ['bridge1 ifpathcost npup2 20000'])
        self.assertEqual(self.run_script()[0], ['bridge0 ifpathcost npup1 20000'])


if __name__ == '__main__':
    unittest.main(verbosity=1)
