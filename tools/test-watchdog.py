#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/watchdog.sh, and install/install.sh arming the host's hardware watchdog with it, away
from the appliance.

  - start: the timer armed where the setting is on - as it is in a configuration whose page was never
    saved - its driver loaded and watchdogd started with -t 128 -s 1 and the script's own pidfile, and
    said; nothing armed where the setting says 0 (a watchdogd of the script's own stopped), or where no
    timer attached; once only - a second start, as an update's install makes, leaves the running
    watchdogd as it is; a timer another watchdogd holds - os-watchdog's, or one started by hand - left
    to it, none started beside it; a watchdogd that did not start said. Each exits 0.
  - status and stop name the other watchdogd that holds the timer, and stop leaves it running.
  - install.sh: watchdog.sh start once, after the script and both its hooks are in place and before
    every exit the run can end on after them; with descriptor 9 closed, which an update's lock is held
    on (scripts/xgs/update.py); its answer said under its own heading, and the install going on under
    its own set -e whatever the script answers - an error, nothing at all, or no script. And the real
    script through those lines twice, an install and then an update: the timer armed once, by a
    watchdogd that does not hold descriptor 9.

watchdog.sh runs in a copy whose kldstat, kldload, sysctl, pgrep, logger and watchdogd are stand-ins
in a temporary directory, its pidfile and config.xml moved there too; any other path it names outside
the few read-only tools it uses stops the test. The watchdogd stand-in leaves a sleep running for the
daemon and writes its process id where -I says; pgrep lists those still alive and the sleeps the test
starts for another watchdogd; all of them are killed at the end. install.sh's lines are cut from it
and run with PREFIX in that directory. /bin/sh, so on Linux.

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
INSTALL = os.path.join(ROOT, 'install', 'install.sh')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'
TIMER = 'AMD FCH Rev 41h+ Watchdog Timer'
HEADING = 'echo "== hardware watchdog =="'
STANDS_IN = ('/usr/sbin/watchdogd', '/sbin/kldstat', '/sbin/kldload', '/sbin/sysctl', '/usr/bin/logger',
             '/bin/pgrep')
MOVED = {'/var/run/xgs-watchdogd.pid', '/conf/config.xml'}
# What the copy may still run of the machine's own: the shell, reads, kill on the stand-ins' pids and rm
# on the moved pidfile.
LEFT = {'/bin/sh', '/bin/kill', '/bin/cat', '/bin/rm', '/usr/bin/sed', '/usr/bin/head', '/dev/null'}
PATHS = re.compile(r'(?<![\w.}$/-])(/(?:usr/)?s?bin/[\w.-]+|/(?:var|conf|dev|boot|etc)/[\w./-]*)')


def installer_lines():
    """install.sh's lines that arm the watchdog: its heading, and the paragraph it opens."""
    with open(INSTALL, encoding='utf-8') as handle:
        lines = handle.read().splitlines()
    start = lines.index(HEADING)
    end = lines.index('', start)
    return '\n'.join(lines[start:end]) + '\n'


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


class Installer(Bench):

    def setUp(self):
        super().setUp()
        self.prefix = self.path('prefix')
        self.script = os.path.join(self.prefix, 'opnsense', 'scripts', 'xgs', 'watchdog.sh')
        os.makedirs(os.path.dirname(self.script))

    def stand_in(self, body):
        """watchdog.sh as a stand-in that notes how it was called, and whether it was given descriptor 9."""
        with open(self.script, 'w', newline='\n') as handle:
            handle.write('#!/bin/sh\necho "watchdog.sh $*" >> "%s"\n'
                         'if (true >&9) 2>/dev/null; then echo "9 open" >> "%s"; else echo "9 closed" >> "%s"; fi\n'
                         '%s\n' % (self.calls, self.calls, self.calls, body))

    def install(self):
        """The install's lines, under the install's own set -e, with descriptor 9 open as an update has
        it, and a line after them that says the install went on."""
        text = 'set -e\nexec 9> "%s"\n%secho "the install goes on"\n' % (self.path('lock'), installer_lines())
        done = subprocess.run(['/bin/sh', '-c', text], env=dict(os.environ, PREFIX=self.prefix),
                              capture_output=True, text=True, errors='replace', timeout=60)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        said = done.stdout.splitlines()
        self.assertEqual((said[0], said[-1]), ('== hardware watchdog ==', 'the install goes on'), said)
        return said[1:-1]

    def test_watchdog_sh_start_once_with_nine_closed(self):
        self.stand_in('echo "armed: %s, 128 s, watchdogd 4242"' % TIMER)
        self.assertEqual(self.install(), ['   armed: %s, 128 s, watchdogd 4242' % TIMER])
        self.assertEqual(self.called(), ['watchdog.sh start', '9 closed'])

    def test_every_line_it_says_is_said(self):
        self.stand_in('echo "the hardware watchdog is switched off in the settings"\necho "not armed"')
        self.assertEqual(self.install(), ['   the hardware watchdog is switched off in the settings',
                                          '   not armed'])

    def test_an_error_is_said_and_the_install_goes_on(self):
        self.stand_in('echo "watchdog.sh: something went wrong" >&2\nexit 3')
        self.assertEqual(self.install(), ['   watchdog.sh: something went wrong',
                                          '   watchdog.sh start ended with status 3; the install goes on'])

    def test_no_answer_is_said(self):
        self.stand_in('exit 0')
        self.assertEqual(self.install(), ['   watchdog.sh start said nothing'])

    def test_no_script_at_all(self):
        said = self.install()
        self.assertEqual(len(said), 2, said)
        self.assertTrue(said[0].startswith('   ') and 'watchdog.sh' in said[0], said)
        self.assertRegex(said[1], r'^   watchdog\.sh start ended with status [1-9][0-9]*; the install goes on$')

    def test_an_install_then_an_update_arm_it_once(self):
        self.patched(self.script)
        said = self.install()
        pid = self.ours()
        self.assertEqual(said, ['   armed: %s, 128 s, watchdogd %d' % (TIMER, pid)])
        self.assertEqual(self.install(), ['   armed already, watchdogd %d' % pid])
        self.assertEqual(self.called('watchdogd'), ['watchdogd -t 128 -s 1 -I %s' % self.pidfile])
        self.assertTrue(self.alive(pid))
        self.assertNotIn('watchdogd holds 9', self.called())


class Order(unittest.TestCase):
    """Where install.sh arms it: once its pieces are in place, and on every way the run ends after them."""

    def setUp(self):
        with open(INSTALL, encoding='utf-8') as handle:
            self.lines = [line for line in handle.read().splitlines() if not line.lstrip().startswith('#')]

    def at(self, text):
        found = [i for i, line in enumerate(self.lines) if text in line]
        self.assertEqual(len(found), 1, text)
        return found[0]

    def test_once_its_pieces_are_in_place(self):
        arms = self.at('watchdog.sh" start')
        self.assertLess(self.at('for f in apply.php '), arms)
        self.assertIn(' watchdog.sh ', self.lines[self.at('for f in apply.php ')])
        self.assertLess(self.at('rc.syshook.d/start/08-xgs-watchdog" "${PREFIX}'), arms)
        self.assertLess(self.at('rc.syshook.d/stop/23-xgs-watchdog" "${PREFIX}'), arms)
        self.assertEqual(self.lines[arms - 2], HEADING)

    def test_before_every_exit_after_them(self):
        arms = self.at('watchdog.sh" start')
        hooks = self.at('rc.syshook.d/stop/23-xgs-watchdog" "${PREFIX}')
        exits = [i for i, line in enumerate(self.lines) if re.match(r'\s*exit\b', line) and i > hooks]
        self.assertTrue(exits)
        self.assertEqual([i for i in exits if i < arms], [])


if __name__ == '__main__':
    unittest.main(verbosity=1)
