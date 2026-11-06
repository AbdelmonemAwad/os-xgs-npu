#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The order of the shutdown's hooks, and four small hooks' gates, away from the appliance.

  - stop: both watchdogs - the coprocessor's watch (feeder.py) and the host's timer (watchdog.sh) -
    stopped after the kernel follower's rebuild, the longest stop hook, and before 80-freebsd takes the
    network; until 2026-11-06 they were stopped first, and the rebuild ran with neither. And an install
    takes away their hooks by the old names, which would stop them first still.
  - 65-xgs-report: the reset's record moved to the boot's time as it reads now (reset_reason.py
    follow) before the reporter is started.
  - 71-xgs-npu-clock: five minutes of tries that all failed said once, at their end.
  - 74-xgs-defaults: the appliance's own settings set again where their record is missing - the first
    boot after a factory reset - and nothing done where it is there.

rc.syshook runs a level's hooks in the order of their names. The hooks themselves run in copies whose
paths point at stand-ins, with /bin/sh, so on Linux; the order is read anywhere.

    python3 tools/test-hooks.py
"""
import os
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
HOOKS = os.path.join(ROOT, 'src', 'etc', 'rc.syshook.d')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'


class StopOrder(unittest.TestCase):

    def hooks(self):
        return sorted(os.listdir(os.path.join(HOOKS, 'stop')))

    def test_the_watchdogs_stop_after_the_rebuild_and_before_the_network(self):
        names = self.hooks()
        feeder = [n for n in names if n.endswith('-xgs-feeder')]
        watchdog = [n for n in names if n.endswith('-xgs-watchdog')]
        self.assertEqual((len(feeder), len(watchdog)), (1, 1), names)
        self.assertIn('20-octep', names)
        # after this plugin's follower and os-kernelfollow's (stop/21-kernelfollow, in its own repository)
        for longest in ('20-octep', '21-kernelfollow'):
            self.assertGreater(feeder[0], longest)
            self.assertGreater(watchdog[0], longest)
        # the coprocessor's watch first, then the host's timer: a coprocessor reset after the watch has
        # gone still has the host's timer to reset a host it froze
        self.assertLess(feeder[0], watchdog[0])
        self.assertLess(watchdog[0], '80-freebsd')

    def test_an_install_takes_the_old_names_away(self):
        with open(os.path.join(ROOT, 'install', 'install.sh'), encoding='utf-8') as f:
            text = f.read()
        for old in ('05-xgs-watchdog', '07-xgs-feeder'):
            self.assertIn('rm -f "${PREFIX}/etc/rc.syshook.d/stop/%s"' % old, text)
            self.assertNotIn('src/etc/rc.syshook.d/stop/%s"' % old, text)


@unittest.skipUnless(POSIX, 'the hooks run under /bin/sh')
class Hooks(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.calls = os.path.join(self.dir, 'calls')
        open(self.calls, 'w').close()

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body='exit 0'):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\necho "%s $*" >> "%s"\n%s\n' % (name, self.calls, body))
        os.chmod(path, 0o755)
        return path

    def hook(self, level, name, stubs):
        with open(os.path.join(HOOKS, level, name), encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        path = os.path.join(self.dir, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        done = subprocess.run(['/bin/sh', path], capture_output=True, text=True, errors='replace', timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)

    def called(self, wait_for=None):
        deadline = time.time() + 20
        while True:
            with open(self.calls) as f:
                calls = f.read().splitlines()
            if wait_for is None or any(c.startswith(wait_for) for c in calls) or time.time() > deadline:
                return calls
            time.sleep(0.1)

    def test_the_report_hook_moves_the_reset_record_first(self):
        self.hook('start', '65-xgs-report', {
            '/usr/local/opnsense/scripts/xgs/reset_reason.py': self.stub('reset_reason'),
            '/usr/local/opnsense/scripts/xgs/reporter.py': self.stub('reporter'),
            '/usr/sbin/daemon': self.stub('daemon')})
        calls = self.called()
        self.assertEqual(calls[0], 'reset_reason follow')
        self.assertTrue(calls[1].startswith('daemon -f ') and calls[1].endswith(' boot'), calls)

    def test_the_clock_hook_says_once_that_five_minutes_of_tries_failed(self):
        self.hook('start', '71-xgs-npu-clock', {
            '/usr/local/opnsense/scripts/xgs/npu-clock.sh': self.stub('npu-clock', 'exit 1'),
            'sleep 15': '"%s" 15' % self.stub('sleep'),
            '/usr/bin/logger': self.stub('logger')})
        calls = self.called(wait_for='logger ')
        self.assertEqual(len([c for c in calls if c.startswith('npu-clock')]), 20)
        said = [c for c in calls if c.startswith('logger ')]
        self.assertEqual(len(said), 1, calls)
        self.assertIn('not set in the boot\'s five minutes of tries', said[0])

    def test_the_clock_hook_says_nothing_once_the_clock_is_set(self):
        self.hook('start', '71-xgs-npu-clock', {
            '/usr/local/opnsense/scripts/xgs/npu-clock.sh': self.stub('npu-clock', 'exit 0'),
            'sleep 15': '"%s" 15' % self.stub('sleep'),
            '/usr/bin/logger': self.stub('logger')})
        time.sleep(1)
        calls = self.called()
        self.assertEqual([c for c in calls if c.startswith('npu-clock')], ['npu-clock '])
        self.assertEqual([c for c in calls if c.startswith('logger ')], [])

    def defaults(self, marked):
        mark = os.path.join(self.dir, 'os-xgs-npu-defaults')
        if marked:
            with open(mark, 'w') as f:
                f.write('thermal_hardware\n')
        script = os.path.join(self.dir, 'defaults.php')
        open(script, 'w').close()
        self.hook('start', '74-xgs-defaults', {
            '/usr/local/opnsense/scripts/xgs/defaults.php': script,
            '/conf/os-xgs-npu-defaults': mark,
            '/usr/local/bin/php': self.stub('php', 'echo "thermal_hardware: amdtemp"'),
            '/usr/bin/logger': self.stub('logger', 'cat > /dev/null')})
        return self.called()

    def test_the_defaults_are_set_again_where_their_record_is_missing(self):
        calls = self.defaults(marked=False)		# the two ends of one pipe: in either order
        self.assertEqual(sorted(c.split()[0] for c in calls), ['logger', 'php'])
        self.assertIn('php %s' % os.path.join(self.dir, 'defaults.php'), calls)
        self.assertIn('logger -t xgs-defaults', calls)

    def test_the_defaults_are_left_where_their_record_is(self):
        self.assertEqual(self.defaults(marked=True), [])


if __name__ == '__main__':
    unittest.main(verbosity=1)
