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

And the scripts that name a port by what it is on (the review of 2026-11-06, D5): portstats.py's
counters, coprocessor.py's chips and temperatures.sh's port chips take the model's ports from the
table - on an XGS 2100 or 2300 the switch's 9 and 10 are F1 and F2, and the coprocessor has no cages
of its own. Until then all three gave those two models the XGS 3300's panel.

And the site's model pages (the review's D15): what tools/device_facts.py says a port is on, as
tools/devices-table.py writes it for every assembly and as website/data/devices.json has it - a MAC
of the coprocessor and a PHY only for a port the table puts on the coprocessor's own MACs, the ARMADA
boards' MACs as the table numbers and names them - and every port the facts name, for a speed, PoE,
the bypass pair or a place on a panel, one the model has. Until 2026-11-06 the XGS 2100's and 2300's
pages put their F1 and F2, the switch's ports 9 and 10, on the XGS 3300's QLM5 and QLM6 behind its
88X5113 as well.

    python3 tools/test-models.py
"""

import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import types
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

    def test_board_sh_names_every_model(self):
        # board.sh: every model of the table, the family as its second word, and "rnd" - research and
        # development, not run here - on every model but the XGS 136 and the XGS 3300.
        import subprocess
        sh = os.path.join(XGS, 'board.sh')
        for a, (model, family, _, tested) in models.MODELS.items():
            out = subprocess.run(['sh', sh], capture_output=True, text=True,
                                 env=dict(os.environ, XGS_PLANAR_SERIAL='XYZ' + a + '12345678'))
            words = out.stdout.split()
            self.assertEqual(out.returncode, 0, a)
            self.assertEqual(words[1], family, a)
            self.assertEqual(len(words) == 3 and words[2] == 'rnd', not tested, '%s %s' % (a, model))
        out = subprocess.run(['sh', sh], capture_output=True, text=True,
                             env=dict(os.environ, XGS_PLANAR_SERIAL='XYZAMDA0203-000112345678'))
        self.assertEqual(out.returncode, 1)

    def test_assign_defaults_are_the_two(self):
        src = read('src', 'opnsense', 'scripts', 'xgs', 'assign.php')
        octep = re.search(r"\$fallback = \[('F1'.*?)\];", src, re.S).group(1)
        self.assertEqual(re.findall(r"'(\w+)'", octep), [p['label'] for p in models.ports('AMDA0202-0004')])
        self.assertIn("$wan_label = 'Port2'", src)
        self.assertIn("$wan_label = 'Port1'", src)
        self.assertEqual(models.WAN, {'octeontx': 'Port2', 'armada': 'Port1'})


XGS2100, XGS3300, UNKNOWN = 'AMDA0202-0001', 'AMDA0202-0004', 'AMDA0202-0009'


def serial(assembly):
    return 'XYZ' + assembly + '12345678'


class NamedByTheTable(unittest.TestCase):
    """portstats.py, coprocessor.py and temperatures.sh against the table, each run as on an
    appliance of the model the planar serial names (XGS_PLANAR_SERIAL), the coprocessor stood in for."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='models-test-')
        self.saved = os.environ.get('XGS_PLANAR_SERIAL')

    def tearDown(self):
        shutil.rmtree(self.tmp, True)
        if self.saved is None:
            os.environ.pop('XGS_PLANAR_SERIAL', None)
        else:
            os.environ['XGS_PLANAR_SERIAL'] = self.saved

    # portstats.py ---------------------------------------------------------------------------------

    def portstats(self, assembly):
        """portstats.py collect on this model: the switch's port N answers N good octets, and each BGX
        LMAC asked for 2 frames; the ports kept, and the command the coprocessor was given."""
        os.environ['XGS_PLANAR_SERIAL'] = serial(assembly)
        mod = load('portstats_under_test', os.path.join(XGS, 'portstats.py'))
        mod.STATE = os.path.join(self.tmp, 'portstats.json')
        mod.PORTS_JSON = os.path.join(self.tmp, 'ports.json')
        mod.family = lambda: 'octeontx'
        asked = []

        def run(argv, **kwargs):
            asked.append(argv[-1])
            lines = ['port=%d in_good_octets=%d' % (n, n) for n in range(1, 11)]
            m = re.search(r'for p in ([^;]*); do b=', argv[-1])
            for item in (m.group(1).split() if m else []):
                lines.append('bgx=%s in_frames=2' % item.split(':')[0])
            return types.SimpleNamespace(returncode=0, stdout='\n'.join(lines) + '\n', stderr='')

        mod.subprocess = types.SimpleNamespace(run=run, DEVNULL=subprocess.DEVNULL,
                                               SubprocessError=subprocess.SubprocessError)
        answer = mod.collect()
        return answer.get('ports') or {}, asked[0]

    def test_portstats_2100_cages_are_the_switch_f1_f2(self):
        ports, command = self.portstats(XGS2100)
        self.assertEqual(sorted(ports), sorted(['Port%d' % n for n in range(1, 9)] + ['F1', 'F2']))
        self.assertEqual((ports['F1'], ports['F2']), ({'in_good_octets': 9}, {'in_good_octets': 10}))
        self.assertNotIn('devmem', command, 'an XGS 2100 has no cages on the coprocessor to read')

    def test_portstats_3300_as_before(self):
        ports, command = self.portstats(XGS3300)
        self.assertEqual((ports['F3'], ports['F4']), ({'in_good_octets': 9}, {'in_good_octets': 10}))
        self.assertEqual((ports['F1'], ports['F2']), ({'in_frames': 2}, {'in_frames': 2}))
        self.assertIn('F1:%d F2:%d' % (0x87E0E0000000 + 2 * 0x1000000, 0x87E0E0000000 + 2 * 0x1000000 + 0x100000),
                      command)

    def test_portstats_unknown_model_gets_the_3300_panel(self):
        ports, _ = self.portstats(UNKNOWN)
        self.assertEqual(sorted(ports), sorted(['Port%d' % n for n in range(1, 9)] + ['F1', 'F2', 'F3', 'F4']))

    def test_portstats_every_switch_port_named_as_the_table(self):
        for a, (model, family, _, _) in models.MODELS.items():
            if family != 'octeontx':
                continue
            ports, _ = self.portstats(a)
            want = {p['label'] for p in models.ports(a) if p['attach'].startswith('switch port')}
            self.assertEqual({k for k, v in ports.items() if 'in_good_octets' in v}, want, '%s %s' % (a, model))

    # coprocessor.py -------------------------------------------------------------------------------

    def chips(self, assembly):
        mod = load('coprocessor_under_test', os.path.join(XGS, 'coprocessor.py'))
        mod.sysctl = lambda name: ''
        return {r['part']: (r['name'], r['where'], r['source']) for r in mod.chips(assembly, {})[2:]}

    def test_coprocessor_2100_chips_serve_its_ports(self):
        for a in (XGS2100, 'AMDA0202-0002'):
            rows = self.chips(a)
            self.assertEqual(rows['switch'], ('88E6193X', 'Port1-8, F1, F2', 'family'), a)
            # whether the board carries an 88X5113 has not been read; it serves no port of this model
            self.assertEqual(rows['phy'], ('88X5113', '', 'family'), a)

    def test_coprocessor_run_here_as_before(self):
        self.assertEqual(self.chips(XGS3300), {'switch': ('88E6193X', 'Port1-8, F3, F4', 'photographed'),
                                               'phy': ('88X5113', 'F1, F2', 'photographed'),
                                               'cpld': ('LCMXO3LF-2100C', '', 'photographed')})
        self.assertEqual(self.chips('AMDA0201-0003'), {'switch': ('88E6193X', 'Port1-8, F1, F2', 'photographed'),
                                                       'phy': ('AQR412C', 'Port9-12', 'photographed')})
        self.assertEqual(self.chips(UNKNOWN)['switch'], ('88E6193X', 'Port1-8, F3, F4', 'family'))

    # temperatures.sh ------------------------------------------------------------------------------

    def temperatures(self, assembly):
        """temperatures.sh -v on this model, in a copy whose paths point at stand-ins: sysctl(8) keeps
        what it is told, and the link answers each label the coprocessor's temperature tool is asked
        for. What the link was asked, and the temperatures written."""
        run = tempfile.mkdtemp(dir=self.tmp).replace('\\', '/')
        calls = run + '/calls'

        def stub(name, body):
            path = run + '/' + name
            with open(path, 'w', newline='\n') as f:
                f.write('#!/bin/sh\n%s\n' % body)
            os.chmod(path, 0o755)
            return path

        sysctl = stub('sysctl', 'echo "sysctl $*" >> "%s"\ncase "$1" in\n'
                                '-N) case "$2" in dev.octep.0.temperature.memory) exit 1 ;; dev.octep.0.*) exit 0 ;; esac; exit 1 ;;\n'
                                '-n) echo "{ sec = 1700000000, usec = 0 }"; exit 0 ;;\nesac\nexit 0' % calls)
        link = stub('mgmt-link', 'echo "link $*" >> "%s"\n'
                                 'for l in $(echo "$2" | sed -n "s/^for l in \\([^;]*\\);.*/\\1/p"); do\n'
                                 '\techo "$l phy temperatures 50000 51000"\ndone' % calls)
        sensors = stub('xgs3300-sensors', 'echo \'{"npu_c": 60.5, "inlet_c": 30.5}\'')
        logger = stub('logger', 'exit 0')
        with open(os.path.join(XGS, 'temperatures.sh'), encoding='utf-8') as f:
            text = f.read()
        for real, fake in (('/usr/local/opnsense/scripts/npuctl/mgmt-link.sh', link),
                           ('/usr/local/opnsense/scripts/xgs/board.sh', os.path.join(XGS, 'board.sh').replace('\\', '/')),
                           ('/usr/local/opnsense/scripts/xgs/models.py', os.path.join(XGS, 'models.py').replace('\\', '/')),
                           ('/usr/local/bin/python3', sys.executable.replace('\\', '/')),
                           ('/usr/local/sbin/xgs3300-sensors', sensors), ('/usr/bin/logger', logger),
                           ('/sbin/sysctl', sysctl), ('/var/db/os-xgs-npu', run)):
            text = text.replace(real, fake)
        script = run + '/temperatures.sh'
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        done = subprocess.run(['sh', script, '-v'], capture_output=True, text=True, errors='replace', timeout=60,
                              env=dict(os.environ, XGS_PLANAR_SERIAL=serial(assembly)))
        self.assertEqual(done.returncode, 0, done.stderr)
        with open(calls, encoding='utf-8') as f:
            lines = f.read().splitlines()
        asked = [re.search(r'for l in ([^;]*);', line).group(1).split() for line in lines
                 if line.startswith('link ssh for l in')]
        written = sorted(re.match(r'sysctl -q dev\.octep\.0\.temperature\.(\w+)=', line).group(1) for line in lines
                         if line.startswith('sysctl -q dev.octep.0.temperature.'))
        return asked, written

    def test_temperatures_2100_reads_no_sfp_phy(self):
        asked, written = self.temperatures(XGS2100)
        self.assertEqual(asked, [['1', 'A1']], 'an XGS 2100\'s F1 is the switch\'s, not an SFP+ PHY\'s')
        self.assertEqual(written, ['inlet', 'module_phy', 'npu', 'switch'])

    def test_temperatures_3300_as_before(self):
        for a in (XGS3300, UNKNOWN):
            asked, written = self.temperatures(a)
            self.assertEqual(asked, [['1', 'F1', 'A1']], a)
            self.assertEqual(written, ['inlet', 'module_phy', 'npu', 'sfp_phy', 'switch'], a)


facts = load('device_facts', os.path.join(HERE, 'device_facts.py'))
table = load('devices_table', os.path.join(HERE, 'devices-table.py'))
PANEL_PARTS = ('|', 'lcd', 'com', 'usb', 'mgmt', 'bay')


class TheSitesPages(unittest.TestCase):
    """tools/device_facts.py, as tools/devices-table.py writes it into website/data/devices.json for
    the site's model pages, against the table, every assembly of every board (the review of
    2026-11-06, D15): a port is behind the switch or on one of the coprocessor's own MACs as
    models.py puts it, and is given a MAC and a PHY only when it is on the coprocessor. Until then
    the XGS 2100's and 2300's F1 and F2, the switch's ports 9 and 10, were given the XGS 3300's QLM5
    and QLM6 and its 88X5113 too."""

    def made(self, make, *args):
        """What tools/devices-table.py makes, or why it would not."""
        try:
            return make(*args)
        except SystemExit as e:
            self.fail('devices-table.py stopped: %s' % e)

    def written(self, assembly):
        """The assembly's ports as tools/devices-table.py writes them."""
        return self.made(table.page_ports, assembly)

    def same(self, where, ports, assembly):
        """ports, as the site's table has them, against models.py's for the assembly."""
        want = models.ports(assembly)
        self.assertEqual([p['label'] for p in ports], [p['label'] for p in want], where)
        for got, p in zip(ports, want):
            what = '%s %s' % (where, p['label'])
            self.assertEqual((got['socket'], got['device'], got['tag']), (p['socket'], p['device'], '0x%04x' % p['tag']), what)
            m = re.match(r'switch port (\d+)$', p['attach'])
            if m:
                self.assertEqual((got['switch_port'], got['mac'], got['phy']), (int(m.group(1)), None, None),
                                 what + ': behind the switch, no MAC or PHY of the coprocessor')
            else:
                self.assertIsNone(got['switch_port'], what)
                self.assertTrue(got['mac'], what + ': on the coprocessor, its MAC named')

    def test_every_assembly_written_as_the_table(self):
        for a, (model, _, _, _) in models.MODELS.items():
            self.same('%s %s' % (a, model), self.written(a), a)

    def test_the_sites_file_as_the_table(self):
        with open(os.path.join(ROOT, 'website', 'data', 'devices.json'), encoding='utf-8') as f:
            pages = json.load(f)['pages']
        self.assertEqual(sorted(v['assembly'] for p in pages for v in p['variants']), sorted(models.MODELS))
        for page in pages:
            self.same('devices.json, %s' % page['model'], page['ports'], page['variants'][0]['assembly'])

    def test_site_2100_f1_f2_are_the_switch(self):
        for a in (XGS2100, 'AMDA0202-0002'):
            ports = {p['label']: p for p in self.written(a)}
            self.assertEqual([(ports[n]['switch_port'], ports[n]['mac'], ports[n]['phy']) for n in ('F1', 'F2')],
                             [(9, None, None), (10, None, None)], a)

    def test_site_3300_as_before(self):
        for a in ('AMDA0202-0003', XGS3300):
            ports = {p['label']: p for p in self.written(a)}
            self.assertEqual([(ports[n]['switch_port'], ports[n]['mac'], ports[n]['phy']) for n in ('F1', 'F2', 'F3', 'F4')],
                             [(None, 'QLM5', '88X5113'), (None, 'QLM6', '88X5113'), (9, None, None), (10, None, None)], a)

    def test_armada_macs_as_the_table_names_them(self):
        # The board file's "0:N", as models.py reads it: CP0's eth0 to eth2 are 0 to 2, CP1's 3 to 5.
        # Its paths name the MAC first and then what is between - "through" a PHY, or a cage direct.
        for page in self.made(table.pages):
            if page['family_key'] != 'armada':
                continue
            paths = models.PATHS[page['board']]
            self.assertTrue(paths['uplink'].startswith('%s, %s ' % tuple(page['uplink'])), page['model'])
            self.assertEqual(page['uplink'][0], 'CP%d eth%d' % divmod(models.UPLINK.get(page['board'], 0), 3), page['model'])
            named = re.search(r'to the (\S+) switch', paths['uplink'])
            if named:
                self.assertEqual(page['switch'], named.group(1), page['model'])
            for v in page['variants']:
                macs = {p['label']: int(p['attach'].split()[-1]) for p in models.ports(v['assembly'])
                        if p['attach'].startswith('coprocessor MAC')}
                for p in self.written(v['assembly']):
                    if p['label'] not in macs:
                        continue
                    what, path = '%s %s' % (v['model'], p['label']), paths[p['label']]
                    self.assertEqual(p['mac'], 'CP%d eth%d' % divmod(macs[p['label']], 3), what)
                    self.assertTrue(path.startswith(p['mac'] + ','), '%s: %s' % (what, path))
                    named = re.search(r'through the (\S+)', path)
                    if named:
                        self.assertEqual(p['phy'], named.group(1), what)
                    self.assertEqual(p['phy'] is not None, 'through' in path, '%s: %s' % (what, path))

    def test_every_port_the_facts_name_is_the_models(self):
        # A speed, PoE and the bypass pair are given by board or by assembly, and a panel by model:
        # each port they name is one of that model's, and its panel shows each of its ports once.
        for a, (model, _, _, _) in models.MODELS.items():
            board = a.split('-')[0]
            labels = [p['label'] for p in models.ports(a)]
            named = (set(facts.SPEED.get(board, {})) | set(facts.SPEED.get(a, {})) | set(facts.POE.get(board, ())) |
                     set(facts.BYPASS.get(board, ())))
            self.assertLessEqual(named, set(labels), '%s %s' % (a, model))
            shown, lights = [], []
            for tok in facts.PANELS[model[:-1] if model.endswith('w') else model][1].split():
                if tok.startswith('led:'):
                    lights += tok[4:].split('/')
                elif tok not in PANEL_PARTS:
                    shown += tok.split('/')
            self.assertEqual(sorted(shown), sorted(labels), '%s %s: its panel' % (a, model))
            self.assertLessEqual(set(lights), set(labels), '%s %s: its panel\'s lights' % (a, model))


if __name__ == '__main__':
    unittest.main(verbosity=1)
