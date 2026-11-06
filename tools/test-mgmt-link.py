#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""mgmt-link.sh coprocessor away from the appliance: how often it tells the coprocessor its address,
through what, and when it stops (#1010); and mgmt-link.sh trust, which gives the coprocessor this
host's key the same way. npu-run.sh, ping, ifconfig, logger, board.sh, sleep and ssh are stand-ins
that keep what they were asked, in a copy of the script whose paths point at them.

And mgmt-link.sh early, from the ARMADA's early boot hook (the review's A4): the link assigned before
OPNsense configures its interfaces where it is there and nothing carries it, and nothing done anywhere
else - nothing reconfigured or reloaded, nothing on the XGS 3300. mgmt-link.php is a stand-in that
keeps the assignment in a file, so that a second run is the boot after.

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


ASSIGN = '/usr/local/opnsense/scripts/npuctl/mgmt-link.php'
SHOW = 'php %s show mvmgmt0' % ASSIGN
WRITE = 'php %s assign 169.254.254.1 30 mvmgmt0' % ASSIGN


class Early(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.calls = os.path.join(self.dir, 'calls')
        open(self.calls, 'w').close()
        # what the stand-in configuration carries for the link: the line `show` prints, or no file
        self.config = os.path.join(self.dir, 'assigned')

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\necho "%s $*" >> "%s"\n%s\n' % (name, self.calls, body))
        os.chmod(path, 0o755)
        return path

    def run_early(self, board='201 armada', link=True, show=None, assign=None):
        """board: what board.sh prints; link: whether the driver made the link; show and assign: the
        stand-in mgmt-link.php's answer to each, in place of reading and writing its file."""
        show = show or '[ -f "%s" ] && cat "%s" || echo none' % (self.config, self.config)
        assign = assign or 'echo "opt15 NPUMGMT $3/$4" > "%s"; echo opt15' % self.config
        php = self.stub('php', 'case "$2" in\nshow) %s ;;\nassign) %s ;;\nesac' % (show, assign))
        stubs = {'/usr/local/opnsense/scripts/xgs/board.sh': self.stub('board', 'echo "%s"' % board),
                 '/usr/bin/logger': self.stub('logger', 'exit 0'),
                 '/sbin/ifconfig': self.stub('ifconfig', 'exit %d' % (0 if link else 1)),
                 '/usr/local/sbin/configctl': self.stub('configctl', 'exit 0'),
                 '/sbin/ping': self.stub('ping', 'exit 0')}
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        script = os.path.join(self.dir, 'mgmt-link.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = dict(os.environ, PHP=php, CONSOLE='/dev/null')
        done = subprocess.run(['/bin/sh', script, 'early'], env=env, capture_output=True, text=True,
                              timeout=60)
        with open(self.calls) as f:
            calls = f.read().splitlines()
        open(self.calls, 'w').close()
        return done, calls

    @staticmethod
    def of(calls, name):
        return [c for c in calls if c.split()[0] == name]

    def test_a_link_with_no_assignment_is_assigned_and_nothing_else_is_done(self):
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        # read, written once, and read again to say what was written
        self.assertEqual(self.of(calls, 'php'), [SHOW, WRITE, SHOW])
        # the link is only looked for: 07-npuep brought it up, and rc.bootup configures it and the rules
        self.assertEqual(self.of(calls, 'ifconfig'), ['ifconfig mvmgmt0'])
        self.assertEqual(self.of(calls, 'configctl'), [])
        self.assertEqual(self.of(calls, 'ping'), [])
        self.assertIn('mgmt-link: mvmgmt0 is interface opt15 (NPUMGMT), 169.254.254.1/30', done.stdout)

    def test_the_boot_after_reads_and_writes_nothing(self):
        self.run_early()
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [SHOW])
        self.assertEqual(done.stdout, '')

    def test_an_assignment_there_already_is_kept_as_it_is(self):
        """An owner's own key and address for it: kept, not made right - only install.sh's assign does that."""
        with open(self.config, 'w', newline='\n') as f:
            f.write('opt3 PCIE 10.0.0.1/30\n')
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [SHOW])
        self.assertEqual(self.of(calls, 'configctl') + self.of(calls, 'logger'), [])
        self.assertEqual(done.stdout, '')

    def test_a_boot_without_the_link_reads_and_writes_nothing(self):
        done, calls = self.run_early(link=False)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [])
        self.assertEqual(done.stdout, '')

    def test_the_xgs_3300_is_left_to_its_bootup_hook(self):
        for board in ('202 octeontx', '202 octeontx rnd'):
            done, calls = self.run_early(board=board)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertEqual(self.of(calls, 'php') + self.of(calls, 'ifconfig'), [], board)

    def test_a_board_this_project_does_not_know_gets_nothing(self):
        done, calls = self.run_early(board='none untested')
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [])

    def test_every_armada_board_is_assigned(self):
        done, calls = self.run_early(board='208 armada rnd')
        self.assertEqual(self.of(calls, 'php'), [SHOW, WRITE, SHOW])

    def test_an_answer_that_cannot_be_read_writes_nothing(self):
        for show in ('exit 1', 'echo "PHP Warning:  something"; echo none', 'echo'):
            done, calls = self.run_early(show=show)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertEqual(self.of(calls, 'php'), [SHOW], show)
            self.assertIn('the assignment of mvmgmt0 could not be read - nothing written', done.stdout)

    def test_an_assignment_that_was_not_written_is_said_and_the_boot_goes_on(self):
        done, calls = self.run_early(assign='echo "no free interface slot" >&2; exit 1')
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [SHOW, WRITE, SHOW])
        self.assertIn('the assignment of mvmgmt0 was not written', done.stdout)
        self.assertNotIn('is interface', done.stdout)
        self.assertEqual(self.of(calls, 'configctl'), [])


if __name__ == '__main__':
    unittest.main(verbosity=1)
