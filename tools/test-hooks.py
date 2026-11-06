#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The order of the shutdown's hooks, and five small hooks' gates, away from the appliance.

  - stop: both watchdogs - the coprocessor's watch (feeder.py) and the host's timer (watchdog.sh) -
    stopped after the kernel follower's rebuild, the longest stop hook, and before 80-freebsd takes the
    network; until 2026-11-06 they were stopped first, and the rebuild ran with neither. And an install
    takes away their hooks by the old names, which would stop them first still.
  - early/05-xgs-report: each boot written down as it begins, after OPNsense's 05-upgrade and before
    the coprocessor's reset and its driver's load, and one whose look at boot never ran kept for the
    reporter's next (the review of 2026-11-06, P18).
  - early, 31-xgs-mgmt-link: the ARMADA's management link assigned after 07-npuep, whose driver makes
    it, and after the first boot's assignment of the front ports (30-xgs-assign) - mgmt-link.sh early,
    whatever it answers, never the boot's end; and put on the ARMADA alone (the review's A4).
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


def link_hook():
    """The early hook that assigns the ARMADA's management link, by the end of its name."""
    names = [n for n in os.listdir(os.path.join(HOOKS, 'early')) if n.endswith('-xgs-mgmt-link')]
    return names[0] if len(names) == 1 else None


class EarlyOrder(unittest.TestCase):

    def test_the_boot_is_written_down_before_the_coprocessor_and_its_driver(self):
        """The problem reporter's record of the boot (P18 of the review of 2026-11-06) before the
        coprocessor's reset and its driver's load, and after OPNsense's 05-upgrade, which may reboot."""
        names = sorted(os.listdir(os.path.join(HOOKS, 'early')) + ['05-upgrade'])
        self.assertIn('05-xgs-report', names)
        for later in ('06-npuctl', '07-npuep', '08-octep'):
            self.assertLess(names.index('05-xgs-report'), names.index(later))
        self.assertGreater(names.index('05-xgs-report'), names.index('05-upgrade'))

    def test_the_link_is_assigned_once_the_driver_has_made_it(self):
        names = sorted(os.listdir(os.path.join(HOOKS, 'early')))
        self.assertIsNotNone(link_hook(), names)
        # after the hook whose kldload makes mvmgmt0, and after the first boot's front ports, which take
        # their places first; rc.bootup, which checks the assigned interfaces, comes after every early hook
        self.assertGreater(link_hook(), '07-npuep')
        self.assertGreater(link_hook(), [n for n in names if n.endswith('-xgs-assign')][0])

    def test_an_install_puts_it_on_the_armada_alone(self):
        self.assertIsNotNone(link_hook())
        with open(os.path.join(ROOT, 'install', 'install.sh'), encoding='utf-8') as f:
            text = f.read()
        armada = text.index('if [ "${OCTEONTX}" = 0 ]; then', text.index('echo "== boot hooks =="'))
        other = text.index('\nelse\n', armada)
        end = text.index('\nfi\n', other)
        path = 'etc/rc.syshook.d/early/%s"' % link_hook()
        self.assertIn('install -m 0755 "${SRC}/src/%s "${PREFIX}/%s' % (path, path), text[armada:other])
        self.assertIn('"${PREFIX}/%s' % path, text[other:end])
        self.assertNotIn('install ', text[other:end])
        with open(os.path.join(ROOT, 'install', 'uninstall.sh'), encoding='utf-8') as f:
            self.assertIn('rm -f "${PREFIX}/%s' % path, f.read())


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

    def test_the_early_report_hook_keeps_a_boot_no_look_took_away(self):
        """05-xgs-report: each boot's start written down, and one still there at the next boot - its
        look, which takes it away, never ran - kept beside the others like it for the reporter."""
        state = os.path.join(self.dir, 'reports')
        clock = os.path.join(self.dir, 'clock')

        def boot(when):
            with open(clock, 'w') as f:
                f.write('%d\n' % when)
            self.hook('early', '05-xgs-report', {
                '/var/db/os-xgs-npu/reports': state,
                '/sbin/sysctl': self.stub('sysctl', 'echo "{ sec = $(cat %s), usec = 0 } Sat Oct 17 '
                                                    '08:52:00 2026"' % clock)})

        def read(name):
            try:
                with open(os.path.join(state, name)) as f:
                    return f.read().split()
            except FileNotFoundError:
                return None

        boot(1000)
        self.assertEqual((read('boot-started'), read('boots-unreported')), (['1000'], None))
        boot(2000)						# the boot of 1000 went before its look
        self.assertEqual((read('boot-started'), read('boots-unreported')), (['2000'], ['1000']))
        os.unlink(os.path.join(state, 'boot-started'))	# the look of 2000 ran, and took its record
        os.unlink(os.path.join(state, 'boots-unreported'))
        boot(3000)
        self.assertEqual((read('boot-started'), read('boots-unreported')), (['3000'], None))
        boot(4000)
        boot(5000)
        self.assertEqual((read('boot-started'), read('boots-unreported')), (['5000'], ['3000', '4000']))

    def test_the_link_hook_asks_for_the_early_assignment_and_the_boot_goes_on(self):
        self.assertIsNotNone(link_hook())
        for answer in ('exit 0', 'exit 1'):
            self.hook('early', link_hook(), {
                '/usr/local/opnsense/scripts/npuctl/mgmt-link.sh': self.stub('mgmt-link', answer)})
            self.assertEqual(self.called(), ['mgmt-link early'], answer)
            open(self.calls, 'w').close()

    def test_the_link_hook_without_its_script_does_nothing(self):
        self.assertIsNotNone(link_hook())
        self.hook('early', link_hook(), {
            '/usr/local/opnsense/scripts/npuctl/mgmt-link.sh': os.path.join(self.dir, 'not-installed')})
        self.assertEqual(self.called(), [])

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
