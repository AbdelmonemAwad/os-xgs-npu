#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The table of models, held against every other place a front port is named.

scripts/xgs/models.py has each model's ports; npuep.c has the ARMADA family's again, in C, because
the driver makes its interfaces before any script runs; and bringup.sh, portmap.py and assign.php
keep the XGS 3300's and the XGS 136's panels as what a model the table does not have is given. They
were written apart and have to say the same thing, so this reads each of them and compares.

    python3 tools/test-models.py
"""

import importlib.util
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
XGS = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


models = load('models', os.path.join(XGS, 'models.py'))


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding='utf-8') as f:
        return f.read()


def npuep_tables():
    """name -> [(label, tag, iface, unit)] and assembly -> (model, table name), out of npuep.c."""
    src = read('contrib', 'npuep', 'npuep.c')
    tables = {}
    for m in re.finditer(r'static const struct npuep_front_port (npuep_ports_\w+)\[\] = \{(.*?)\n\};', src, re.S):
        rows = re.findall(r'\{\s*"(\w+)",\s*(0x[0-9a-fA-F]+),\s*(\d+),\s*(\d+)\s*\}', m.group(2))
        tables[m.group(1)] = [(l, int(t, 16), int(i), int(u)) for l, t, i, u in rows]
    chosen = {a: (name, t) for a, name, t in
              re.findall(r'NPUEP_MODEL\("(AMDA\d{4}-\d{4})",\s*"([^"]+)",\s*(npuep_ports_\w+)\)', src)}
    return tables, chosen


class Models(unittest.TestCase):

    def test_every_port_once(self):
        for a, (model, family, _, _) in models.MODELS.items():
            ports = models.ports(a)
            for key in ('label', 'tag', 'iface', 'unit', 'device'):
                values = [p[key] for p in ports]
                self.assertEqual(len(values), len(set(values)), '%s %s: a %s twice' % (a, model, key))

    def test_the_two_run_here(self):
        # What every box of each family had before the table: unchanged.
        p3300 = [(p['unit'], p['tag'], p['iface'], p['label']) for p in models.ports('AMDA0202-0004')]
        self.assertEqual(p3300, [(0, 1, 10, 'F1'), (1, 2, 11, 'F2')] +
                         [(n + 1, 0x8000 | (n << 8), n - 1, 'Port%d' % n) for n in range(1, 9)] +
                         [(10, 0x8900, 8, 'F3'), (11, 0x8a00, 9, 'F4')])
        self.assertTrue(models.MODELS['AMDA0202-0004'][3])
        self.assertTrue(models.MODELS['AMDA0201-0003'][3])

    def test_assembly_from_a_serial(self):
        self.assertEqual(models.assembly_of('XYZ0AMDA0202-0004QQ12345678'), 'AMDA0202-0004')
        self.assertEqual(models.assembly_of('NEAAMDA0201-000322210169'), 'AMDA0201-0003')
        self.assertIsNone(models.assembly_of('no number here'))

    def test_npuep_has_the_armada_family(self):
        tables, chosen = npuep_tables()
        armada = {a for a, m in models.MODELS.items() if m[1] == 'armada'}
        self.assertEqual(set(chosen), armada)
        for a in sorted(armada):
            name, t = chosen[a]
            self.assertEqual(name, models.MODELS[a][0], a)
            want = [(('Port' + p['label']) if p['label'].startswith('F') else p['label'],
                     p['tag'], p['iface'], p['unit']) for p in models.ports(a)]
            self.assertEqual(tables[t], want, '%s: npuep.c %s against models.py' % (a, t))

    def test_bringup_fallback_is_the_3300(self):
        src = read('src', 'opnsense', 'scripts', 'octep', 'bringup.sh')
        fallback = src[src.index('the XGS 3300\'s panel"'):]
        self.assertIn('sc ${S}.dp.if_iface=10\n\tsc ${S}.dp.if_add=1', fallback)
        self.assertIn('sc ${S}.dp.if_iface=11\n\tsc ${S}.dp.if_add=2', fallback)
        self.assertIn('for p in 1 2 3 4 5 6 7 8 9 10; do', fallback)

    def test_portmap_fallback_is_the_3300(self):
        src = read('src', 'opnsense', 'scripts', 'xgs', 'portmap.py')
        order = re.search(r'OCTEP_ORDER = (\[.*?\])', src).group(1)
        self.assertEqual(eval(order), [p['label'] for p in models.ports('AMDA0202-0004')])

    def test_assign_defaults_are_the_two(self):
        src = read('src', 'opnsense', 'scripts', 'xgs', 'assign.php')
        octep = re.search(r"\$fallback = \[('F1'.*?)\];", src, re.S).group(1)
        self.assertEqual(re.findall(r"'(\w+)'", octep), [p['label'] for p in models.ports('AMDA0202-0004')])
        self.assertIn("$wan_label = 'Port2'", src)
        self.assertIn("$wan_label = 'Port1'", src)
        self.assertEqual(models.WAN, {'octeontx': 'Port2', 'armada': 'Port1'})


if __name__ == '__main__':
    unittest.main(verbosity=1)
