#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""mgmt-link.sh coprocessor away from the appliance: how often it tells the coprocessor its address,
through what, and when it stops (#1010). npu-run.sh, ping, ifconfig, logger, board.sh, sleep and ssh
are stand-ins that keep what they were asked, in a copy of the script whose paths point at them.

    python3 tools/test-mgmt-link.py
"""
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'npuctl', 'mgmt-link.sh')
ADDRESS = 'ip addr replace 169.254.254.2/30 dev mvmgmt0; ip link set mvmgmt0 up'


class Coprocessor(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.calls = os.path.join(self.dir, 'calls')
        open(self.calls, 'w').close()

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\necho "%s $*" >> "%s"\n%s\n' % (name, self.calls, body))
        os.chmod(path, 0o755)
        return path

    def nth(self, name):
        return '$(grep -c "^%s " "%s")' % (name, self.calls)

    def run_script(self, ping_from=None, busy=0):
        """ping answers from its ping_from-th call (None: never); npu-run.sh refuses (75) its first
        `busy` calls."""
        ping = 'exit 1' if ping_from is None else '[ %s -ge %d ] && exit 0; exit 1' % (self.nth('ping'), ping_from)
        stubs = {'/usr/local/opnsense/scripts/xgs/board.sh': self.stub('board', 'echo "201 armada"'),
                 '/usr/bin/logger': self.stub('logger', 'exit 0'),
                 '/sbin/ifconfig': self.stub('ifconfig', 'exit 0'),
                 '/sbin/ping': self.stub('ping', ping)}
        npu = self.stub('npu-run', 'echo "npu-run-env NO_LINK=${NPU_RUN_NO_LINK:-}" >> "%s"\n'
                                   '[ %s -le %d ] && exit 75; exit 0' % (self.calls, self.nth('npu-run'), busy))
        self.stub('sleep', 'exit 0')
        self.stub('ssh', 'exit 0')
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        script = os.path.join(self.dir, 'mgmt-link.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = dict(os.environ, PATH=self.dir + os.pathsep + os.environ.get('PATH', ''),
                   MGMT_LINK_HELD='1', CONSOLE='/dev/null', NPU_RUN=npu, START_WAIT='300')
        done = subprocess.run(['/bin/sh', script, 'coprocessor'], env=env, capture_output=True,
                              text=True, timeout=60)
        with open(self.calls) as f:
            return done, f.read().splitlines()

    @staticmethod
    def tells(calls):
        return [c for c in calls if c.startswith('npu-run ')]

    def test_a_link_that_answers_is_not_told(self):
        done, calls = self.run_script(ping_from=1)
        self.assertEqual(done.returncode, 0)
        self.assertEqual(self.tells(calls), [])
        self.assertIn('the coprocessor answers at 169.254.254.2', done.stdout)

    def test_the_address_goes_through_npu_run_never_over_the_link(self):
        done, calls = self.run_script(ping_from=2)
        self.assertEqual(self.tells(calls), ['npu-run 5 ' + ADDRESS])
        self.assertIn('npu-run-env NO_LINK=1', calls)
        self.assertIn('the coprocessor answers at 169.254.254.2', done.stdout)

    def test_a_console_held_by_another_caller_is_asked_again(self):
        done, calls = self.run_script(ping_from=5, busy=3)
        self.assertEqual(len(self.tells(calls)), 4)
        self.assertIn('answers at', done.stdout)

    def test_a_link_that_never_answers_is_told_for_about_five_minutes_then_left(self):
        done, calls = self.run_script(ping_from=None)
        self.assertEqual(done.returncode, 0)
        self.assertEqual(len(self.tells(calls)), 300 // 30 + 1)
        self.assertLessEqual(sum(int(c.split()[1]) for c in calls if c.startswith('sleep ')), 300)
        self.assertIn('does not answer at 169.254.254.2 after about 300 s', done.stdout)

    def test_nothing_is_written_to_the_console_by_the_script_itself(self):
        with open(SCRIPT, encoding='utf-8') as f:
            self.assertNotIn('> "${CONSOLE}"', f.read())


if __name__ == '__main__':
    unittest.main(verbosity=1)
