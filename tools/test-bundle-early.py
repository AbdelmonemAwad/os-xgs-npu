#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/bundle.py's early-boot.txt (#1045): what the early boot hooks said on the console this
boot, read from dmesg -a with OPNsense's uptime stamps, the boot before left out, and a buffer that no
longer holds this boot's start said as such - not as hooks that said nothing.

    python3 tools/test-bundle-early.py"""
import importlib.util
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


if __name__ == '__main__':
    unittest.main()
