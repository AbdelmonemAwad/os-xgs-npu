#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Run the early hook's actual guards, stopping before any driver or bridge access.

Only the guard prefix is copied into a temporary harness. SMBIOS, PCI and logging
are stand-ins; no reset tool, kernel module command or device access is included.
"""

import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
BOARD = ROOT / 'src/opnsense/scripts/xgs/board.sh'
HOOK = ROOT / 'src/etc/rc.syshook.d/early/06-npuctl'
MODELS = runpy.run_path(str(BOARD.with_name('models.py')))['MODELS']

# The ARMADA boards whose reset values have run at boot: 201 (the XGS 136, and the XGS 126 is the
# same board) and 208 (the XGS 116 r1, by its owner, #1037). Boards 200 and 224 wait for a run (#1047).
RUN_AT_BOOT = ('AMDA0201', 'AMDA0208')


class ResetGuard(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        folder = Path(self.tmp.name)
        # Cut before the first hardware operation, rather than trusting mocks of
        # kldload or the pulse to prevent a future test from touching hardware.
        prefix, marker, _ = HOOK.read_text().partition('# hidraw(4)')
        self.assertTrue(marker, 'the safety boundary must still exist')
        prefix = '\n'.join(line for line in prefix.splitlines() if not line.lstrip().startswith('#'))
        self.assertNotIn('/sbin/kld', prefix)
        self.assertNotIn(' pulse ', prefix)
        pci = folder / 'pci.sh'
        pci.write_text('#!/bin/sh\nprintf "%s\\n" "${TEST_PCI:-}"\n')
        prefix = prefix.replace('/usr/sbin/pciconf -l', '/bin/sh "' + str(pci) + '"')
        # logger -t npuctl -p daemon.notice MESSAGE: the message is the fifth word
        logger = folder / 'logger.sh'
        logger.write_text('#!/bin/sh\nprintf "%s\\n" "$5" >> "${TEST_LOG:?}"\n')
        prefix = prefix.replace('/usr/bin/logger', '/bin/sh "' + str(logger) + '"')
        prefix = prefix.replace('/bin/kenv -q smbios.planar.serial', '/bin/false')
        self.harness = folder / 'guard.sh'
        self.harness.write_text(prefix + '\nprintf "guard-accepted\\n"\n')
        self.log = folder / 'log'

    def accepted(self, serial, pci=''):
        self.log.write_text('')
        result = subprocess.run(
            ['sh', str(self.harness)], capture_output=True, text=True,
            env=dict(os.environ, BOARDSH=str(BOARD), XGS_PLANAR_SERIAL=serial,
                     TEST_PCI=pci, TEST_LOG=str(self.log)), timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, '')
        self.assertIn(result.stdout, ('', 'guard-accepted\n'))
        return result.stdout == 'guard-accepted\n'

    def logged(self):
        return self.log.read_text()

    def test_every_known_assembly(self):
        # boards 201 and 208, any model on them; no other ARMADA board yet (#1047), no OCTEON TX one
        for assembly in MODELS:
            with self.subTest(assembly=assembly):
                self.assertEqual(self.accepted('XYZ' + assembly + '12345678'),
                                 assembly[:8] in RUN_AT_BOOT)
                if assembly[:8] in RUN_AT_BOOT:
                    self.assertEqual(MODELS[assembly][1], 'armada')

    def test_boards_200_and_224_wait_for_a_run(self):
        # the XGS 87, 87w, 107, 107w and 138: the vendor's values, run at no boot
        for assembly in ('AMDA0200-0001', 'AMDA0200-0002', 'AMDA0200-0003', 'AMDA0200-0004',
                         'AMDA0200-9999', 'AMDA0224-0001', 'AMDA0224-0002'):
            with self.subTest(assembly=assembly):
                self.assertFalse(self.accepted('XYZ' + assembly + '12345678'))
                self.assertIn('run at no boot yet', self.logged())
                self.assertIn('#1047', self.logged())
                self.assertNotIn('not an ARMADA board', self.logged())

    def test_the_board_number_decides_not_the_variant(self):
        # the reset lines are the board's, so any variant of boards 201 and 208 is admitted
        for assembly in ('AMDA0208-0000', 'AMDA0208-0002', 'AMDA0208-0003',
                         'AMDA0208-9999', 'AMDA0201-0001', 'AMDA0201-9999'):
            with self.subTest(assembly=assembly):
                self.assertTrue(self.accepted('XYZ' + assembly + '12345678'))
                self.assertEqual(self.logged(), '')
        for serial in ('AMDA0208-001', 'AMDA0208-000X', 'AMDA0208-0002AMDA0208-0002'):
            with self.subTest(serial=serial):
                self.assertTrue(self.accepted(serial))

    def test_octeontx_and_unknown_boards_stay_excluded(self):
        for assembly in ('AMDA0202-0004', 'AMDA0999-0001'):
            with self.subTest(assembly=assembly):
                self.assertFalse(self.accepted('XYZ' + assembly + '12345678'))
                self.assertIn('not an ARMADA board', self.logged())

    def test_a_serial_without_a_board_number_stays_excluded(self):
        for serial in ('', 'unknown', 'AMDA0208'):
            with self.subTest(serial=serial):
                self.assertFalse(self.accepted(serial))
                self.assertIn('not an ARMADA board', self.logged())

    def test_board_classification_is_unchanged(self):
        for variant in ('0001', '0002'):
            result = subprocess.run(
                ['sh', str(BOARD)], capture_output=True, text=True,
                env=dict(os.environ, XGS_PLANAR_SERIAL='XYZAMDA0208-' + variant + '12345678'),
                timeout=5)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, '208 armada rnd\n')

    def test_separate_octeontx_pci_exclusion(self):
        for assembly in ('AMDA0201-0003', 'AMDA0208-0001'):
            for pci in ('octep0 chip=0xa300177d', 'octep0 vendor=0x177d device=0xa300'):
                with self.subTest(assembly=assembly, pci=pci):
                    self.assertFalse(self.accepted('XYZ' + assembly + '12345678', pci))
                    self.assertIn('OCTEON TX', self.logged())


if __name__ == '__main__':
    unittest.main()
