#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/watchdog.sh, away from the appliance.

  - start: the timer armed where the setting is on - as it is in a configuration whose page was never
    saved - its driver loaded and watchdogd started with -t 128 -s 1 and the script's own pidfile, and
    said; nothing armed where the setting says 0 (a watchdogd of the script's own stopped), or where no
    timer attached; once only - a second start leaves the running watchdogd as it is; a timer another
    watchdogd holds - os-watchdog's, or one started by hand - left to it, none started beside it; a
    watchdogd that did not start said. Each exits 0.
  - status and stop name the other watchdogd that holds the timer, and stop leaves it running.

watchdog.sh runs in a copy whose kldstat, kldload, sysctl, pgrep, logger and watchdogd are stand-ins
in a temporary directory, its pidfile and config.xml moved there too; any other path it names outside
the few read-only tools it uses stops the test. The watchdogd stand-in leaves a sleep running for the
daemon and writes its process id where -I says; pgrep lists those still alive and the sleeps the test
starts for another watchdogd; all of them are killed at the end. /bin/sh, so on Linux.

    python3 tools/test-watchdog.py
"""
import os
import re
import signal
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
SCRIPT = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'watchdog.sh')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'
TIMER = 'AMD FCH Rev 41h+ Watchdog Timer'
STANDS_IN = ('/usr/sbin/watchdogd', '/sbin/kldstat', '/sbin/kldload', '/sbin/sysctl', '/usr/bin/logger',
             '/bin/pgrep')
MOVED = {'/var/run/xgs-watchdogd.pid', '/conf/config.xml'}
# What the copy may still run of the machine's own: reads, the shell, and kill on the stand-ins' pids.
LEFT = {'/bin/sh', '/bin/kill', '/bin/cat', '/bin/rm', '/usr/bin/sed', '/usr/bin/head', '/dev/null'}
PATHS = re.compile(r'(?<![\w.}$/-])(/(?:usr/)?s?bin/[\w.-]+|/(?:var|conf|dev|boot|etc)/[\w./-]*)')


@unittest.skipUnless(POSIX, 'watchdog.sh runs under /bin/sh')
class Bench(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = os.path.realpath(self.tmp.name)
        self.calls = self.path('calls')
        self.pidfile = self.path('xgs-watchdogd.pid')
        self.others = []
        open(self.calls, 'w').close()
        self.config('<opnsense><system/></opnsense>')		# the page never saved
        self.timer(True)
        self.stub('kldstat', '[ -e "%s" ]' % self.path('loaded'))
        self.stub('kldload', ': > "%s"' % self.path('loaded'))
        self.stub('sysctl', '[ -e "%s" ] || { echo "sysctl: unknown oid" >&2; exit 1; }\necho "%s"'
                  % (self.path('timer'), TIMER))
        self.stub('logger', 'exit 0')
        self.stub('pgrep', 'found=1\nfor f in "%s" "%s"; do\n\t[ -f "$f" ] || continue\n'
                           '\twhile read -r p; do\n\t\tif kill -0 "$p" 2>/dev/null; then echo "$p"; found=0; fi\n'
                           '\tdone < "$f"\ndone\nexit $found' % (self.path('daemons'), self.path('others')))
        self.stub('watchdogd', '(true >&9) 2>/dev/null && echo "watchdogd holds 9" >> "%s"\n'
                               '[ -e "%s" ] && { echo "watchdogd: unable to initialize watchdog" >&2; exit 69; }\n'
                               'pidfile="%s"\nwhile [ $# -gt 0 ]; do\n\t[ "$1" = -I ] && { pidfile=$2; shift; }\n'
                               '\tshift\ndone\n/bin/sleep 300 < /dev/null > /dev/null 2>&1 9>&- &\n'
                               'echo $! > "$pidfile"\necho $! >> "%s"'
                  % (self.calls, self.path('fail'), self.path('default.pid'), self.path('daemons')))
        self.copy = self.patched(self.path('watchdog.sh'))

    def tearDown(self):
        for pid in self.started():
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        for other in self.others:
            other.kill()
            other.wait()
        self.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def stub(self, name, body):
        with open(self.path(name), 'w', newline='\n') as handle:
            handle.write('#!/bin/sh\necho "%s $*" >> "%s"\n%s\n' % (name, self.calls, body))
        os.chmod(self.path(name), 0o755)

    def patched(self, where):
        """A copy of watchdog.sh whose tools are the stand-ins and whose files are here."""
        with open(SCRIPT, encoding='utf-8') as handle:
            text = handle.read()
        code = '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('#'))
        self.assertEqual(sorted(set(PATHS.findall(code)) - set(STANDS_IN) - MOVED - LEFT), [],
                         'a path of the machine the copy would use as it is')
        for real in STANDS_IN:
            text = text.replace(real, self.path(os.path.basename(real)))
        text = text.replace('/var/run/xgs-watchdogd.pid', self.pidfile)
        text = text.replace('/conf/config.xml', self.path('config.xml'))
        with open(where, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write(text)
        os.chmod(where, 0o755)
        return where

    def config(self, text):
        with open(self.path('config.xml'), 'w', encoding='utf-8') as handle:
            handle.write(text)

    def setting(self, value):
        self.config('<opnsense><OPNsense><XGS version="1.3.0"><general><watchdog>%s</watchdog>'
                    '</general></XGS></OPNsense></opnsense>' % value)

    def timer(self, there):
        if there:
            open(self.path('timer'), 'w').close()
        elif os.path.exists(self.path('timer')):
            os.unlink(self.path('timer'))

    def called(self, name=None):
        with open(self.calls, encoding='utf-8') as handle:
            calls = handle.read().splitlines()
        return [c for c in calls if name is None or c.split()[0] == name]

    def started(self):
        try:
            with open(self.path('daemons'), encoding='utf-8') as handle:
                return [int(p) for p in handle.read().split()]
        except FileNotFoundError:
            return []

    def ours(self):
        with open(self.pidfile, encoding='utf-8') as handle:
            return int(handle.read().strip())

    @staticmethod
    def alive(pid):
        """Running: a child of this process that has ended is reaped here, and where /proc says so, one
        that has ended and waits for its parent to reap it counts as ended."""
        try:
            if os.waitpid(pid, os.WNOHANG) != (0, 0):
                return False
        except ChildProcessError:
            pass
        if os.path.isdir('/proc/self'):
            try:
                with open('/proc/%d/stat' % pid, encoding='utf-8') as handle:
                    return handle.read().rsplit(')', 1)[1].split()[0] != 'Z'
            except (OSError, IndexError):
                return False
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        return True

    def another(self):
        """A watchdogd this script did not start: a sleep pgrep names as one."""
        other = subprocess.Popen(['sleep', '300'])
        self.others.append(other)
        with open(self.path('others'), 'a', encoding='utf-8') as handle:
            handle.write('%d\n' % other.pid)
        return other.pid

    def run_script(self, *args):
        done = subprocess.run(['/bin/sh', self.copy] + list(args), capture_output=True, text=True,
                              errors='replace', timeout=60)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        return done.stdout.splitlines()


class Script(Bench):

    def test_a_configuration_never_saved_arms_it(self):
        said = self.run_script('start')
        pid = self.ours()
        self.assertEqual(said, ['armed: %s, 128 s, watchdogd %d' % (TIMER, pid)])
        self.assertEqual(self.called('kldload'), ['kldload -n amdsbwd'])
        self.assertEqual(self.called('watchdogd'), ['watchdogd -t 128 -s 1 -I %s' % self.pidfile])
        self.assertIn('logger -t xgs-watchdog %s' % said[0], self.called('logger'))
        self.assertTrue(self.alive(pid))

    def test_the_setting_on_arms_it(self):
        self.setting(1)
        self.assertEqual(self.run_script('start'), ['armed: %s, 128 s, watchdogd %d' % (TIMER, self.ours())])

    def test_a_second_start_leaves_the_running_one_as_it_is(self):
        self.run_script('start')
        pid = self.ours()
        self.assertEqual(self.run_script('start'), ['armed already, watchdogd %d' % pid])
        self.assertEqual(len(self.called('watchdogd')), 1)
        self.assertEqual(self.ours(), pid)
        self.assertTrue(self.alive(pid))

    def test_the_setting_off_arms_nothing(self):
        self.setting(0)
        self.assertEqual(self.run_script('start'),
                         ['the hardware watchdog is switched off in the settings', 'not armed'])
        self.assertEqual(self.called('watchdogd'), [])

    def test_the_setting_off_stops_its_own(self):
        self.run_script('start')
        pid = self.ours()
        self.setting(0)
        self.assertEqual(self.run_script('start'),
                         ['the hardware watchdog is switched off in the settings', 'disarmed'])
        self.assertFalse(self.alive(pid))
        self.assertFalse(os.path.exists(self.pidfile))

    def test_no_timer_arms_nothing(self):
        self.timer(False)
        self.assertEqual(self.run_script('start'), ['no hardware watchdog timer on this host; nothing armed'])
        self.assertEqual(self.called('kldload'), ['kldload -n amdsbwd'])
        self.assertEqual(self.called('watchdogd'), [])

    def test_a_timer_another_watchdogd_holds_is_left_to_it(self):
        other = self.another()
        said = self.run_script('start')
        self.assertEqual(said, ['the timer is held by another watchdogd, %d; left to it' % other])
        self.assertIn('logger -t xgs-watchdog %s' % said[0], self.called('logger'))
        self.assertEqual(self.called('watchdogd'), [])
        self.assertEqual(self.run_script('status'), ['held by another watchdogd, %d' % other])
        self.assertEqual(self.run_script('stop'),
                         ['not armed by this script; the timer is held by another watchdogd, %d' % other])
        self.assertTrue(self.alive(other))

    def test_switched_off_with_another_holding_it(self):
        other = self.another()
        self.setting(0)
        self.assertEqual(self.run_script('start'),
                         ['the hardware watchdog is switched off in the settings',
                          'not armed by this script; the timer is held by another watchdogd, %d' % other])
        self.assertTrue(self.alive(other))

    def test_its_own_watchdogd_is_not_another(self):
        self.run_script('start')
        pid = self.ours()
        self.assertIn(str(pid), subprocess.run([self.path('pgrep'), '-x', 'watchdogd'], capture_output=True,
                                               text=True).stdout.split())
        self.assertEqual(self.run_script('status'), ['armed: %s, 128 s, watchdogd %d' % (TIMER, pid)])
        self.assertEqual(self.run_script('start'), ['armed already, watchdogd %d' % pid])
        self.assertEqual(self.run_script('stop'), ['disarmed'])
        self.assertFalse(self.alive(pid))
        self.assertEqual(self.run_script('status'), ['not armed (on in the settings)'])

    def test_a_watchdogd_that_did_not_start_is_said(self):
        open(self.path('fail'), 'w').close()
        self.assertEqual(self.run_script('start'), ['watchdogd did not start; the timer is not armed'])
        self.assertEqual(len(self.called('watchdogd')), 1)


if __name__ == '__main__':
    unittest.main(verbosity=1)
