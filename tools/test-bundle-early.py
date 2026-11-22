#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/bundle.py's early-boot.txt (#1045): what the early boot hooks said on the console this
boot, read from dmesg -a with OPNsense's uptime stamps, the boot before left out, and a buffer that no
longer holds this boot's start said as such - not as hooks that said nothing. And its sysctl-driver.txt,
the running family's tree and loader tunables, and its coprocessor-ecc-alert.json where no alert was
ever written.

    python3 tools/test-bundle-early.py"""
import importlib.util
import json
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XGS = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs')
sys.path.insert(0, XGS)
spec = importlib.util.spec_from_file_location('bundle', os.path.join(XGS, 'bundle.py'))
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)

BANNER = 'Copyright (c) 1992-2026 The FreeBSD Project.\n'


class EarlyBoot(unittest.TestCase):

    def run_with(self, dmesg_a, files=None):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        names = []
        for i, text in enumerate(files or ()):
            path = os.path.join(tmp.name, 'f%d.log' % i)
            with open(path, 'w') as f:
                f.write(text)
            names.append(path)
        saved = (bundle.command, bundle.EARLY_FILES)
        bundle.command = lambda argv, timeout=60: dmesg_a if argv == ['/sbin/dmesg', '-a'] else ''
        bundle.EARLY_FILES = tuple(names)
        try:
            return bundle.early_boot()
        finally:
            bundle.command, bundle.EARLY_FILES = saved

    def test_stamped_lines_of_this_boot_alone(self):
        out = self.run_with('[9] npuep: the boot before\n---<<BOOT>>---\n' + BANNER +
                            "[3] >>> Invoking early script 'npuctl'\n"
                            '[3] npuctl: board \'200 armada rnd\': left as power-on left it (#1047)\n'
                            '[8] npuep: no npuep.ko, nothing to load\n'
                            'mgmt-link: mvmgmt0 is interface opt15 (NPUMGMT)\n'
                            '[9] npuep0: <ARMADA endpoint> on pci1\n')
        self.assertIn("[3] npuctl: board '200 armada rnd'", out)
        self.assertIn('[8] npuep: no npuep.ko', out)
        self.assertIn('mgmt-link: mvmgmt0 is interface opt15', out)
        self.assertNotIn('the boot before', out)
        self.assertNotIn('Invoking', out)
        self.assertNotIn('npuep0:', out)

    def test_hooks_that_said_nothing(self):
        out = self.run_with('---<<BOOT>>---\n' + BANNER + '[1] VT(vga): resolution 640x480\n')
        self.assertIn('[none in this boot]', out)

    def test_a_buffer_past_this_boot_s_start(self):
        out = self.run_with('[90000] em0: link state changed to UP\n')
        self.assertIn('[the message buffer no longer holds the start of this boot]', out)

    def test_the_hooks_files_last_lines(self):
        out = self.run_with('---<<BOOT>>---\n' + BANNER, files=['2026-11-21T12:00:00 octep up\n', ''])
        self.assertIn('2026-11-21T12:00:00 octep up', out)
        self.assertIn('[empty]', out)


class DriverTree(unittest.TestCase):
    """sysctl-driver.txt: the running family's sysctl tree, and its loader tunables from kenv - they are
    no sysctl node, and an XGS 87's bundle had three "unknown oid" lines in their place (2026-11-21).
    coprocessor-ecc-alert.json: no alert ever written is said so, not a cat's error."""

    KENV = ('hw.npuep.giu.queues="2"\nhw.octep.rpc_platform_test="0"\nhw.npuep_other="1"\n'
            'kern.hz="1000"\nhw.npuep.netmap="1"\n')

    def driver_tree(self, board):
        asked = []

        def command(argv, timeout=60):
            asked.append(argv)
            return {'/bin/sh': board, '/sbin/sysctl': 'dev.npuep.0.%desc: ARMADA endpoint\n',
                    '/bin/kenv': self.KENV}[argv[0]]
        saved = bundle.command
        bundle.command = command
        try:
            return bundle.driver_tree(), [a for a in asked if a[0] == '/sbin/sysctl']
        finally:
            bundle.command = saved

    def test_an_armada_asks_for_its_own_tree_and_lists_its_tunables(self):
        out, sysctl = self.driver_tree('201 armada\n')
        self.assertEqual(sysctl, [['/sbin/sysctl', '-e', 'dev.npuep']])
        self.assertIn('[loader tunables, from kenv]\nhw.npuep.giu.queues="2"\nhw.npuep.netmap="1"\n', out)
        for other in ('hw.octep', 'hw.npuep_other', 'kern.hz'):
            self.assertNotIn(other, out)

    def test_an_octeon_tx_asks_for_its_own(self):
        out, sysctl = self.driver_tree('202 octeontx\n')
        self.assertEqual(sysctl, [['/sbin/sysctl', '-e', 'dev.octep']])
        self.assertIn('hw.octep.rpc_platform_test="0"', out)
        self.assertNotIn('hw.npuep', out)

    def test_a_board_not_known_asks_for_both(self):
        out, sysctl = self.driver_tree('none untested\n')
        self.assertEqual(sysctl, [['/sbin/sysctl', '-e', 'dev.octep', 'dev.npuep']])
        self.assertIn('hw.octep.rpc_platform_test', out)
        self.assertIn('hw.npuep.netmap', out)

    def test_no_tunable_set_is_said(self):
        self.KENV = 'kern.hz="1000"\n'
        out, _ = self.driver_tree('201 armada\n')
        self.assertTrue(out.endswith('[loader tunables, from kenv]\n[none set]\n'), out)

    def test_no_memory_alert_ever_written(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        saved = bundle.ECC_ALERT
        bundle.ECC_ALERT = os.path.join(tmp.name, 'ecc-alert.json')
        try:
            self.assertEqual(bundle.ecc_alert(), '[no memory alert recorded]\n')
            with open(bundle.ECC_ALERT, 'w') as f:
                f.write('{"at": 1, "errors": 2}')
            self.assertEqual(json.loads(bundle.ecc_alert()), {'at': 1, 'errors': 2})
        finally:
            bundle.ECC_ALERT = saved


if __name__ == '__main__':
    unittest.main()
