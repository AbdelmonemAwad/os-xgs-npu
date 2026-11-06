#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""mgmt-link.sh coprocessor away from the appliance: how often it tells the coprocessor its address,
through what, and when it stops (#1010); and mgmt-link.sh trust, which gives the coprocessor this
host's key the same way. npu-run.sh, ping, ifconfig, logger, board.sh, sleep and ssh are stand-ins
that keep what they were asked, in a copy of the script whose paths point at them.

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

    def run_script(self, ping_from=None, busy=0, cmd='coprocessor'):
        """ping answers from its ping_from-th call (None: never); npu-run.sh refuses (75) its first
        `busy` calls."""
        ping = 'exit 1' if ping_from is None else '[ %s -ge %d ] && exit 0; exit 1' % (self.nth('ping'), ping_from)
        stubs = {'/usr/local/opnsense/scripts/xgs/board.sh': self.stub('board', 'echo "201 armada"'),
                 '/usr/bin/logger': self.stub('logger', 'exit 0'),
                 '/sbin/ifconfig': self.stub('ifconfig', 'exit 0'),
                 '/sbin/ping': self.stub('ping', ping)}
        npu = self.stub('npu-run', 'echo "npu-run-env NO_LINK=${NPU_RUN_NO_LINK:-} WAIT=${NPU_RUN_WAIT:-}" >> "%s"\n'
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
        # a key already made, so that trust has one to give
        key = os.path.join(self.dir, 'id_ed25519_npu')
        with open(key, 'w', newline='\n') as f:
            f.write('not a key\n')
        with open(key + '.pub', 'w', newline='\n') as f:
            f.write('ssh-ed25519 TESTKEYBODY xgs-host-to-npu\n')
        env = dict(os.environ, PATH=self.dir + os.pathsep + os.environ.get('PATH', ''),
                   MGMT_LINK_HELD='1', CONSOLE='/dev/null', NPU_RUN=npu, START_WAIT='300', KEY=key)
        done = subprocess.run(['/bin/sh', script, cmd], env=env, capture_output=True,
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
        self.assertIn('npu-run-env NO_LINK=1 WAIT=30', calls)
        self.assertIn('the coprocessor answers at 169.254.254.2', done.stdout)

    def test_the_address_waits_for_the_console_behind_a_look(self):
        """#1018: a tell refused after npu-run.sh's 5 s came back 15 s later, to the next look of
        feeder.py; it waits up to 30 s for the console's lock now."""
        done, calls = self.run_script(ping_from=2)
        waits = [c.split('WAIT=')[1] for c in calls if c.startswith('npu-run-env ')]
        self.assertEqual(waits, ['30'])

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

    def test_the_key_waits_for_the_console_behind_a_look(self):
        """trust runs when the link's ssh is refused, and every look of feeder.py goes over the console
        then as well: it waits for the console's lock as long as the address does, not npu-run.sh's
        5 s (A11)."""
        done, calls = self.run_script(cmd='trust')
        self.assertEqual(done.returncode, 0)
        tells = self.tells(calls)
        self.assertEqual(len(tells), 1)
        self.assertTrue(tells[0].startswith("npu-run 15 xgs-remount.sh rw; grep -qF 'TESTKEYBODY' "))
        self.assertIn('npu-run-env NO_LINK=1 WAIT=30', calls)
        self.assertIn('the coprocessor trusts', done.stdout)

    def test_the_key_is_asked_again_while_the_console_is_held(self):
        done, calls = self.run_script(cmd='trust', busy=2)
        self.assertEqual(done.returncode, 0)
        waits = [c.split('WAIT=')[1] for c in calls if c.startswith('npu-run-env ')]
        self.assertEqual(waits, ['30', '30', '30'])
        self.assertIn('the coprocessor trusts', done.stdout)

    def test_nothing_is_written_to_the_console_by_the_script_itself(self):
        with open(SCRIPT, encoding='utf-8') as f:
            self.assertNotIn('> "${CONSOLE}"', f.read())


if __name__ == '__main__':
    unittest.main(verbosity=1)
