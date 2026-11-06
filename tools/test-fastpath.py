#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
What src/opnsense/scripts/xgs/fastpath.py decides when the fast path stops answering its
heartbeat, away from the appliance: the decision is a function of the heartbeat, the setting and
the restarts already made, and is asked here for each case that matters. Nothing is restarted.

    python3 tools/test-fastpath.py
"""

import importlib.util
import os
import signal
import subprocess
import sys
import threading
import time
import unittest

try:
    import fcntl
except ImportError:		# not on a POSIX host: the recovery's own processes are not run there
    fcntl = None

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'fastpath.py')
spec = importlib.util.spec_from_file_location('fastpath', SCRIPT)
fastpath = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fastpath)

NOW = 1_800_000_000


def hb(state, silent_s):
    return {'state': state, 'since_answer_ms': silent_s * 1000, 'on': 1}


class Decide(unittest.TestCase):
    def test_zero_says_so_and_nothing_more(self):
        self.assertEqual(fastpath.decide(hb('dead', 3600), 0, [], NOW)[0], 'none')

    def test_no_heartbeat_on_this_host(self):
        self.assertEqual(fastpath.decide(None, 5, [], NOW)[0], 'none')

    def test_late_is_not_stopped(self):
        self.assertEqual(fastpath.decide(hb('late', 600), 2, [], NOW)[0], 'none')

    def test_alive_after_a_stop_is_left_alone(self):
        self.assertEqual(fastpath.decide(hb('alive', 3), 2, [], NOW)[0], 'none')

    def test_stopped_waits_for_the_minutes_the_setting_names(self):
        self.assertEqual(fastpath.decide(hb('dead', 119), 2, [], NOW)[0], 'none')
        action, why = fastpath.decide(hb('dead', 120), 2, [], NOW)
        self.assertEqual(action, 'restart')
        self.assertIn('120 s', why)

    def test_three_restarts_a_day_and_no_more(self):
        made = [NOW - 3000, NOW - 2000, NOW - 1000]
        self.assertEqual(fastpath.decide(hb('dead', 900), 5, made, NOW)[0], 'refuse')

    def test_restarts_older_than_a_day_do_not_count(self):
        made = [NOW - 90000, NOW - 89000, NOW - 1000]
        self.assertEqual(fastpath.decide(hb('dead', 900), 5, made, NOW)[0], 'restart')

    def test_only_the_offered_choices_are_read_from_the_setting(self):
        self.assertEqual(fastpath.CHOICES, (0, 1, 2, 5, 10))
        self.assertEqual(fastpath.DEFAULT, 1)


class Recover(unittest.TestCase):
    """1, the default since 2026-10-26 (#768): take the coprocessor up again, restart if that fails."""

    def test_alive_is_left_alone(self):
        self.assertEqual(fastpath.decide(hb('alive', 3), 1, [], NOW)[0], 'none')

    def test_late_is_not_stopped(self):
        self.assertEqual(fastpath.decide(hb('late', 20), 1, [], NOW)[0], 'none')

    def test_a_dead_fast_path_is_taken_up_again_at_once(self):
        self.assertEqual(fastpath.decide(hb('dead', 31), 1, [], NOW)[0], 'recover')

    def test_a_coprocessor_taken_as_gone_is_taken_up_again(self):
        gone = dict(hb('alive', 2), gone=True)
        self.assertEqual(fastpath.decide(gone, 1, [], NOW)[0], 'recover')

    def test_a_failed_recovery_is_restarted_for(self):
        self.assertEqual(fastpath.decide(hb('dead', 300), 1, [], NOW, [NOW - 200], True)[0], 'restart')

    def test_a_failed_recovery_past_three_restarts_is_left_up(self):
        made = [NOW - 3600, NOW - 7200, NOW - 10800]
        self.assertEqual(fastpath.decide(hb('dead', 300), 1, made, NOW, [NOW - 200], True)[0], 'refuse')

    def test_six_recoveries_a_day_then_a_restart(self):
        done = [NOW - 3600 * k for k in range(1, 7)]
        self.assertEqual(fastpath.decide(hb('dead', 40), 1, [], NOW, done)[0], 'restart')
        self.assertEqual(fastpath.decide(hb('dead', 40), 1, [], NOW, done[:5])[0], 'recover')

    def test_no_heartbeat_on_this_host(self):
        self.assertEqual(fastpath.decide(None, 1, [], NOW)[0], 'none')

    def test_a_configuration_without_the_setting_takes_the_default(self):
        import tempfile
        with tempfile.NamedTemporaryFile('w', suffix='.xml', delete=False) as f:
            f.write('<opnsense><OPNsense><XGS><general></general></XGS></OPNsense></opnsense>')
        try:
            fastpath.CONFIG = f.name
            self.assertEqual(fastpath.setting(), 1)
            with open(f.name, 'w') as g:
                g.write('<opnsense><OPNsense><XGS><general><fastpath_restart>0</fastpath_restart>'
                        '</general></XGS></OPNsense></opnsense>')
            self.assertEqual(fastpath.setting(), 0)
        finally:
            os.unlink(f.name)


class RecoverLines(unittest.TestCase):
    """What recover.sh said reaches the log whether or not it finished in time (#985): a stand-in
    for it, a shell script, says two lines and then exits or outlasts its limit."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.real = fastpath.RECOVER

    def tearDown(self):
        fastpath.RECOVER = self.real
        self.tmp.cleanup()

    def stand_in(self, last):
        path = os.path.join(self.tmp.name, 'recover.sh')
        with open(path, 'w') as f:
            f.write('#!/bin/sh\necho "21:54:00 taking the coprocessor up again in place"\n'
                    'echo "21:55:00 the coprocessor has booted again"\n%s\n' % last)
        os.chmod(path, 0o755)
        fastpath.RECOVER = path

    def test_a_recovery_that_finishes_says_its_lines(self):
        self.stand_in('exit 0')
        lines, ok = fastpath.run_recover(10)
        self.assertTrue(ok)
        self.assertEqual(len(lines), 2)

    def test_a_recovery_that_fails_says_its_lines(self):
        self.stand_in('exit 1')
        lines, ok = fastpath.run_recover(10)
        self.assertFalse(ok)
        self.assertEqual(len(lines), 2)

    def test_a_recovery_stopped_at_its_limit_keeps_what_it_said(self):
        self.stand_in('exec sleep 30')
        lines, ok = fastpath.run_recover(2)
        self.assertFalse(ok)
        self.assertEqual(lines[0], '21:54:00 taking the coprocessor up again in place')
        self.assertEqual(lines[1], '21:55:00 the coprocessor has booted again')
        self.assertIn('did not finish', lines[-1])
        self.assertEqual(len(lines), 3)

    def test_a_recovery_that_cannot_start_says_so(self):
        fastpath.RECOVER = os.path.join(self.tmp.name, 'none')
        lines, ok = fastpath.run_recover(2)
        self.assertFalse(ok)
        self.assertEqual(len(lines), 1)
        self.assertIn('did not finish', lines[0])

    def test_what_it_said_on_stderr_is_said_too(self):
        """The review's F13 (2026-11-06): recover.sh's stderr was read and thrown away - what kldunload,
        devctl or lockf said of why a step failed went nowhere. With its lines, in the order they came."""
        self.stand_in('echo "kldunload: can\'t unload file: Device busy" >&2\nexit 1')
        lines, ok = fastpath.run_recover(10)
        self.assertFalse(ok)
        self.assertEqual(lines, ['21:54:00 taking the coprocessor up again in place',
                                 '21:55:00 the coprocessor has booted again',
                                 'kldunload: can\'t unload file: Device busy'])


class RestartUnderWay(unittest.TestCase):
    """A restart asked for in this boot is not asked for again by the next minute's check (F5 of a review
    of the watch, 2026-11-06): a reboot's stop hooks can take minutes, and until then the same stop was
    restarted for twice, both counted against the day's three. Nothing is restarted here."""

    def setUp(self):
        import tempfile
        names = ('check_lock', 'recovering', 'heartbeat', 'setting', 'load_state', 'save_state',
                 'boot_time', 'decide', 'collect_report', 'log', 'subprocess')
        self.real = {name: getattr(fastpath, name) for name in names}
        self.lock = tempfile.NamedTemporaryFile(delete=False)
        self.state, self.started, self.boot = {}, [], 1000
        test = self

        class Sub:
            DEVNULL = None

            @staticmethod
            def Popen(args, **kw):
                test.started.append(args)

        def save(kept):
            self.state.clear()
            self.state.update(kept)
        fastpath.check_lock = lambda: os.open(self.lock.name, os.O_RDWR)
        fastpath.recovering = lambda: False
        fastpath.heartbeat = lambda: hb('dead', 600)
        fastpath.setting = lambda: 5
        fastpath.load_state = lambda: dict(self.state)
        fastpath.save_state = save
        fastpath.boot_time = lambda: self.boot
        fastpath.decide = lambda *args: ('restart', 'the fast path stopped')
        fastpath.collect_report = lambda: None
        fastpath.log = lambda *args: None
        fastpath.subprocess = Sub

    def tearDown(self):
        for name, value in self.real.items():
            setattr(fastpath, name, value)
        self.lock.close()
        os.unlink(self.lock.name)

    def test_the_next_minute_in_the_same_boot_asks_for_none(self):
        fastpath.check()
        fastpath.check()
        self.assertEqual((len(self.started), len(self.state['restarts'])), (1, 1))

    def test_a_new_boot_s_stop_is_restarted_for(self):
        fastpath.check()
        self.boot = 2000
        fastpath.check()
        self.assertEqual(len(self.started), 2)


class TheBootAcrossAClockStep(unittest.TestCase):
    """The review's F14 (2026-11-06): a boot was kern.boottime's seconds, which a step of the clock moves -
    by a second within one boot, measured on 2026-10-14 (offload.sh) - and the check after one took the
    same boot for another: a second restart asked for while the first was under way. A token in /var/run
    now, here in a temporary directory, kern.boottime made to move all the same. Nothing is restarted."""

    NAMES = ('check_lock', 'recovering', 'heartbeat', 'setting', 'load_state', 'save_state', 'decide',
             'collect_report', 'log', 'subprocess', 'sysctl')

    def setUp(self):
        import tempfile
        self.real = {name: getattr(fastpath, name) for name in self.NAMES}
        self.tmp = tempfile.TemporaryDirectory()
        self.lock = os.path.join(self.tmp.name, 'lock')
        open(self.lock, 'w').close()
        self.state, self.started, self.sec = {}, [], 1800000000
        test = self

        class Sub:
            DEVNULL = None

            @staticmethod
            def Popen(args, **kw):
                test.started.append(args)

        def save(kept):
            self.state.clear()
            self.state.update(kept)
        fastpath.check_lock = lambda: os.open(self.lock, os.O_RDWR)
        fastpath.recovering = lambda: False
        fastpath.heartbeat = lambda: hb('dead', 600)
        fastpath.setting = lambda: 5
        fastpath.load_state = lambda: dict(self.state)
        fastpath.save_state = save
        fastpath.decide = lambda *args: ('restart', 'the fast path stopped')
        fastpath.collect_report = lambda: None
        fastpath.log = lambda *args: None
        fastpath.subprocess = Sub
        fastpath.sysctl = lambda name: ('{ sec = %d, usec = 0 } Thu Jan 15 08:00:00 2027' % self.sec
                                        if name == 'kern.boottime' else None)
        fastpath.BOOT_TOKEN = os.path.join(self.tmp.name, 'xgs-fastpath-boot')

    def tearDown(self):
        for name, value in self.real.items():
            setattr(fastpath, name, value)
        self.tmp.cleanup()

    def test_a_clock_step_in_the_boot_asks_for_no_second_restart(self):
        fastpath.check()
        self.sec -= 1			# the clock stepped back a second
        fastpath.check()
        self.assertEqual((len(self.started), len(self.state['restarts'])), (1, 1))

    def test_a_new_boot_is_still_one(self):
        """/var/run is emptied at boot: a new token for the new boot, and its stop restarted for."""
        fastpath.check()
        for name in os.listdir(self.tmp.name):
            if name.startswith('xgs-'):
                os.unlink(os.path.join(self.tmp.name, name))
        self.sec += 600
        fastpath.check()
        self.assertEqual(len(self.started), 2)
        first = fastpath.boot_time()
        self.assertEqual(fastpath.boot_time(), first)


class ReportAroundTheRecovery(unittest.TestCase):
    """#1072, F6 (2026-11-06): with reporting automatic, the stop's report was collected
    and sent before the recovery, through the fast path that had stopped - each send failing after its
    relay's 20 s, up to 120 s more of the outage. It is found and queued first now, nothing sent - the
    driver's reload takes the heartbeat's counters it is read from - and sent behind a recovery that
    brought the fast path back; before a restart it is found and queued alone, for the next boot to send.
    Nothing is recovered or restarted here."""

    NAMES = ('check_lock', 'recovering', 'heartbeat', 'setting', 'load_state', 'save_state', 'boot_time',
             'log', 'run_recover', 'subprocess')

    def setUp(self):
        import tempfile
        self.real = {name: getattr(fastpath, name) for name in self.NAMES}
        self.lock = tempfile.NamedTemporaryFile(delete=False)
        self.state, self.events, self.ok = {}, [], True
        test = self

        class Sub:
            """subprocess as check() starts the reporter and the reboot: what ran, waited for or started in
            the background, and whether it may send (XGS_REPORT_NO_SEND not set)."""
            DEVNULL = subprocess.DEVNULL
            SubprocessError = subprocess.SubprocessError

            @staticmethod
            def run(args, **kw):
                sends = 'XGS_REPORT_NO_SEND' not in (kw.get('env') or os.environ)
                test.events.append(('waited for', os.path.basename(args[0]), sends, kw.get('timeout')))
                return subprocess.CompletedProcess(args, 0, b'', b'')

            @staticmethod
            def Popen(args, **kw):
                sends = 'XGS_REPORT_NO_SEND' not in (kw.get('env') or os.environ)
                test.events.append(('started', os.path.basename(args[0]), sends))

        def recover(seconds=None):
            test.events.append('recovery')
            return ['the fast path answers again'], test.ok

        def save(kept):
            self.state.clear()
            self.state.update(kept)
        fastpath.check_lock = lambda: os.open(self.lock.name, os.O_RDWR)
        fastpath.recovering = lambda: False
        fastpath.heartbeat = lambda: dict(hb('dead', 40), gone=True, deaths=1)
        fastpath.setting = lambda: 1
        fastpath.load_state = lambda: dict(self.state)
        fastpath.save_state = save
        fastpath.boot_time = lambda: 1000
        fastpath.log = lambda *args: None
        fastpath.run_recover = recover
        fastpath.subprocess = Sub

    def tearDown(self):
        for name, value in self.real.items():
            setattr(fastpath, name, value)
        self.lock.close()
        os.unlink(self.lock.name)

    def kinds(self):
        """What ran, without the time each run was given."""
        return [event[:3] if isinstance(event, tuple) else event for event in self.events]

    def test_found_before_the_recovery_and_sent_behind_it(self):
        fastpath.check()
        self.assertEqual(self.kinds(), [('waited for', 'reporter.py', False), 'recovery',
                                        ('started', 'reporter.py', True)])
        # the finding alone is waited for, and not as long as a send through the stopped fast path took
        self.assertEqual(self.events[0][3], fastpath.FIND_SECONDS)
        self.assertLess(fastpath.FIND_SECONDS, 120)

    def test_a_recovery_that_failed_restarts_with_the_report_found_and_not_sent(self):
        """Nothing passes the fast path that did not come back, and the restart does not wait for sends
        that can only fail: the report queued goes from the next boot (reporter.py boot, its cron)."""
        self.ok = False
        fastpath.check()
        self.assertEqual(self.kinds(), [('waited for', 'reporter.py', False), 'recovery',
                                        ('waited for', 'reporter.py', False), ('started', 'rc.reboot', True)])


# lockf(1) as FreeBSD's runs a command (usr.bin/lockf): the lock taken - at once with -t 0, within -t's
# seconds, whenever it is free without -t, or 75 and "already locked" - the command run as a child that
# does not hold it, waited for with stdin, stdout and stderr closed; the lock gone when this one ends.
LOCKF = r'''
import fcntl, os, signal, sys, time
args = sys.argv[1:]
wait = None
while args[0].startswith('-'):
    if args.pop(0) == '-t':
        wait = float(args.pop(0))
fd = os.open(args[0], os.O_RDWR | os.O_CREAT, 0o600)
end = time.monotonic() + (wait or 0)
while True:
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        break
    except OSError:
        if wait is not None and time.monotonic() >= end:
            sys.stderr.write('lockf: %s: already locked\n' % args[0])
            os._exit(75)
        time.sleep(0.01)
child = os.fork()
if child == 0:
    os.close(fd)
    os.execv(args[1], args[1:])
signal.signal(signal.SIGINT, signal.SIG_IGN)
signal.signal(signal.SIGQUIT, signal.SIG_IGN)
for n in (0, 1, 2):
    os.close(n)
_, status = os.waitpid(child, 0)
os._exit(os.WEXITSTATUS(status) if os.WIFEXITED(status) else 70)
'''


def running(pid):
    """Whether pid is a process that runs: there, and no zombie."""
    try:
        with open('/proc/%d/stat' % pid) as f:
            return f.read().rsplit(')', 1)[1].split()[0] != 'Z'
    except (OSError, IndexError):
        return False


@unittest.skipUnless(fcntl is not None and os.path.isdir('/proc/self'), 'reads /proc: Linux, as CI runs it')
class RecoveryEndedWhole(unittest.TestCase):
    """recover.sh past its cap (#1072, F7, 2026-11-06): subprocess.run's timeout killed
    lockf alone - recover.sh had become it - its lock went with it, and recover.sh carried on under no
    lock while the check went on. A stand-in for recover.sh runs itself again under a stand-in for
    FreeBSD's lockf(1), as recover.sh does, and waits in a long step; its lock is watched throughout."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        here = self.tmp.name
        self.lock = os.path.join(here, 'xgs-recover.lock')
        self.pidfiles = [os.path.join(here, 'shell.pid'), os.path.join(here, 'step.pid')]
        lockf = os.path.join(here, 'lockf.py')
        with open(lockf, 'w') as f:
            f.write(LOCKF)
        script = os.path.join(here, 'recover.sh')
        with open(script, 'w') as f:
            f.write('#!/bin/sh\n'
                    'if [ -z "${XGS_RECOVER_LOCKED:-}" ]; then\n'
                    '\tXGS_RECOVER_LOCKED=1 exec "%s" "%s" -k -t 0 "%s" /bin/sh "$0" "$@"\n'
                    'fi\n'
                    'echo $$ > "%s"\n'
                    'echo "21:54:00 taking the coprocessor up again in place"\n'
                    'sleep 60 &\n'
                    'echo $! > "%s"\n'
                    'wait\n' % (sys.executable, lockf, self.lock, self.pidfiles[0], self.pidfiles[1]))
        os.chmod(script, 0o755)
        self.real = fastpath.RECOVER
        fastpath.RECOVER = script

    def tearDown(self):
        fastpath.RECOVER = self.real
        for pid in self.pids():		# whatever the cap left running
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        self.tmp.cleanup()

    def pids(self):
        """recover.sh's shell and its step, once each has said its pid."""
        found = []
        for name in self.pidfiles:
            try:
                with open(name) as f:
                    found.append(int(f.read().strip()))
            except (OSError, ValueError):
                pass
        return found

    def lock_free(self):
        fd = os.open(self.lock, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            return False
        finally:
            os.close(fd)
        return True

    def test_the_whole_recovery_ends_at_its_cap_and_its_lock_last(self):
        free_while_running, done = [], threading.Event()

        def watch():
            # the lock read first: a process found running after it was free was running while it was
            while not done.is_set():
                pids = self.pids()
                if len(pids) == 2 and self.lock_free():
                    free_while_running.extend(pid for pid in pids if running(pid))
                time.sleep(0.002)

        watcher = threading.Thread(target=watch)
        watcher.start()
        try:
            lines, ok = fastpath.run_recover(2)
            time.sleep(0.5)		# what the cap left running would still be running now
        finally:
            done.set()
            watcher.join()
        self.assertFalse(ok)
        self.assertEqual(lines[0], '21:54:00 taking the coprocessor up again in place')
        self.assertIn('did not finish', lines[-1])
        self.assertEqual(len(self.pids()), 2)
        self.assertEqual([pid for pid in self.pids() if running(pid)], [])
        self.assertTrue(self.lock_free())
        self.assertEqual(free_while_running, [])


@unittest.skipUnless(fcntl is not None, 'flock: a POSIX host, as CI runs it')
class TheRecoveryLockProbed(unittest.TestCase):
    """The review's F15 (2026-11-06): recovering() - here and in feeder.py - takes recover.sh's lock for a
    moment to see whether a recovery runs, and recover.sh took it with lockf -t 0: a recovery begun in that
    moment gave up at once with 75, which check() counts as a recovery that failed, and restarts for.
    recover.sh itself, its lock and library in a temporary directory and its lockf the stand-in above,
    begun while the lock is held: with no board it knows, it says so and exits 1 - once it has run."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        here = self.tmp.name
        self.lock = os.path.join(here, 'xgs-recover.lock')
        lockf = os.path.join(here, 'lockf.py')
        with open(lockf, 'w') as f:
            f.write(LOCKF)
        with open(os.path.join(os.path.dirname(SCRIPT), 'recover.sh'), encoding='utf-8') as f:
            text = f.read()
        for real, fake in (('LOCK=/var/run/xgs-recover.lock', 'LOCK=' + self.lock),
                           ('LIB=/usr/local/opnsense/scripts', 'LIB=' + here),
                           ('exec lockf ', 'exec "%s" "%s" ' % (sys.executable, lockf))):
            self.assertIn(real, text)
            text = text.replace(real, fake)
        self.script = os.path.join(here, 'recover.sh')
        with open(self.script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)

    def tearDown(self):
        self.tmp.cleanup()

    def held_for(self, seconds):
        """recover.sh begun while something holds its lock, for seconds."""
        fd = os.open(self.lock, os.O_RDWR | os.O_CREAT, 0o600)
        fcntl.flock(fd, fcntl.LOCK_EX)
        holder = threading.Timer(seconds, os.close, (fd,))
        holder.start()
        try:
            return subprocess.run(['/bin/sh', self.script], capture_output=True, text=True, timeout=60)
        finally:
            holder.join()

    def test_a_probe_s_moment_does_not_turn_the_recovery_away(self):
        done = self.held_for(0.3)
        self.assertNotEqual(done.returncode, 75, done.stderr)
        self.assertIn('not an XGS appliance this project knows; nothing done', done.stdout)

    def test_a_recovery_that_runs_still_turns_a_second_away(self):
        """It holds the lock for minutes: a second recovery gives up all the same, seconds later."""
        done = self.held_for(6)
        self.assertEqual(done.returncode, 75)
        self.assertIn('already locked', done.stderr)
        self.assertEqual(done.stdout, '')


if __name__ == '__main__':
    unittest.main(verbosity=1)
