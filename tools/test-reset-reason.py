#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/reset_reason.py across a clock step: the chipset's record is a file standing in for
/dev/mem, kern.boottime and the kernel's environment are the test's, and the problem reporter's own
reset_reason() is asked for the record afterwards, as its boot run asks it.

ntpd's first step moves kern.boottime, often between the start hook that keeps the record (06) and the
reporter's (65). Until 2026-11-06 a step of more than five seconds there lost the reset's cause from the
report of a restart without a shutdown, and a second `boot` after one read the cleared register and kept
"no cause" over it. Now the boot that kept a record marks it in the kernel's environment, `boot` does
not read again where it finds the mark, and `follow` (65-xgs-report) moves the record to the boot's
time as it reads then. os.pread, so on Linux.

    python3 tools/test-reset-reason.py
"""
import importlib.util
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
XGS = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs')
BOOT = 1793000000
STEP = 3600
AMD = '  Origin="AuthenticAMD"  Id=0x810f11  Family=0x17  Model=0x11  Stepping=1\n'
KEPT_AT = '/var/db/os-xgs-npu/reset-reason.json'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@unittest.skipUnless(hasattr(os, 'pread'), 'os.pread: not on Windows')
class AcrossAClockStep(unittest.TestCase):

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='reset-reason-')
        self.mem = os.path.join(self.root, 'mem')
        self.kept = self.root + KEPT_AT
        dmesg = os.path.join(self.root, 'dmesg.boot')
        with open(dmesg, 'w', encoding='utf-8') as handle:
            handle.write(AMD)
        self.write_register(0x02080800)		# bit 25: the hardware watchdog expired
        os.environ.update({'XGS_RESET_MEM': self.mem, 'XGS_RESET_OFFSET': '16', 'XGS_RESET_DMESG': dmesg,
                           'XGS_RESET_STATE': self.kept})
        self.module = load('reset_reason', os.path.join(XGS, 'reset_reason.py'))
        self.clock = {'boot': BOOT}
        self.kenv = {}
        self.logged = []
        self.module.boot_time = lambda: self.clock['boot']
        self.module.log = self.logged.append
        self.module.kenv_get = lambda name: self.kenv.get(name)
        self.module.kenv_set = lambda name, value: self.kenv.__setitem__(name, str(value))
        os.environ.update({'XGS_REPORT_ROOT': self.root, 'XGS_REPORT_STATE': os.path.join(self.root, 'reports'),
                           'XGS_REPORT_CONFIG': '/conf/config.xml'})
        self.reporter = load('reporter', os.path.join(XGS, 'reporter.py'))

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def write_register(self, value):
        with open(self.mem, 'wb') as handle:
            handle.write(bytes(16) + value.to_bytes(4, 'little') + bytes(12))

    def run_command(self, command):
        sys.stdout = open(os.devnull, 'w')
        try:
            self.module.main(['reset_reason.py', command])
        finally:
            sys.stdout.close()
            sys.stdout = sys.__stdout__

    def kept_value(self):
        with open(self.kept, encoding='utf-8') as handle:
            return json.load(handle)['value']

    def test_the_reporter_finds_the_cause_after_a_step_between_the_hooks(self):
        self.run_command('boot')					# 06-xgs-reset-reason
        self.assertEqual(self.reporter.reset_reason(BOOT)['value'], '0x02080800')
        self.clock['boot'] = BOOT + STEP				# ntpd steps the clock an hour on
        self.assertIsNone(self.reporter.reset_reason(BOOT + STEP))
        self.run_command('follow')					# 65-xgs-report, before the reporter
        found = self.reporter.reset_reason(BOOT + STEP)
        self.assertIsNotNone(found)
        self.assertEqual(found['value'], '0x02080800')
        self.assertEqual([b['bit'] for b in found['bits']], [11, 19, 25])
        self.assertEqual(len(self.logged), 2, self.logged)		# the reset, and the step
        self.assertIn('the clock moved +3600 s', self.logged[1])

    def test_a_second_read_after_a_step_keeps_the_cause(self):
        self.run_command('boot')
        self.write_register(0)					# cleared by the first read, as the chipset does
        self.clock['boot'] = BOOT - STEP
        self.run_command('boot')
        self.assertEqual(self.kept_value(), '0x02080800')

    def test_follow_reads_nothing_and_moves_no_other_boots_record(self):
        self.run_command('boot')
        self.kenv.clear()						# the next boot: its kernel environment is new
        self.write_register(0x00080800)
        self.clock['boot'] = BOOT + 86400
        self.run_command('follow')
        with open(self.kept, encoding='utf-8') as handle:
            kept = json.load(handle)
        self.assertEqual((kept['boot'], kept['value']), (BOOT, '0x02080800'))
        self.assertIsNone(self.reporter.reset_reason(BOOT + 86400))
        self.run_command('boot')					# and that boot reads its own
        self.assertEqual(self.kept_value(), '0x00080800')
        self.assertEqual(self.reporter.reset_reason(BOOT + 86400)['value'], '0x00080800')

    def test_without_the_kernels_environment_it_is_as_before(self):
        self.module.kenv_get = lambda name: None
        self.module.kenv_set = lambda name, value: None
        self.run_command('boot')
        self.write_register(0)
        self.run_command('boot')					# the same boot, no step: read already
        self.assertEqual(self.kept_value(), '0x02080800')
        self.clock['boot'] = BOOT + STEP
        self.run_command('follow')					# no mark: nothing moved
        self.assertIsNone(self.reporter.reset_reason(BOOT + STEP))
        self.assertEqual(self.kept_value(), '0x02080800')


if __name__ == '__main__':
    unittest.main(verbosity=1)
