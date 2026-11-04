#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The coprocessor's watchdog feeder as scripts/xgs/feeder.py reads it and puts it right (#768), with
the coprocessor's answers made up: npu-run.sh is replaced by a function that answers as the
coprocessor does and keeps what it was asked, and the system log and the state file by a list and a
temporary file.

    python3 tools/test-feeder.py
"""

import importlib.util
import json
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
XGS = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


feeder = load('feeder', os.path.join(XGS, 'feeder.py'))


OOPS = ('=== /sys/fs/pstore/dmesg-ramoops-0\nOops#1 Part1\n'
        '<1>[  182.479799] Unable to handle kernel NULL pointer dereference at virtual address 00000001\n')


class Coprocessor:
    """Answers feeder.py's questions as a coprocessor with this uptime, this feeder and these crash
    records in its pstore would."""

    def __init__(self, uptime, pid, state, pstore='', panic='60 60'):
        self.uptime, self.pid, self.state, self.pstore = uptime, pid, state, pstore
        self.panic = panic		# its panic timeout before the look, and after it
        self.asked = []

    def __call__(self, seconds, command):
        self.asked.append(command)
        if command.startswith(feeder.ASK):
            out = '%d\npid %s\n' % (self.uptime, self.pid or 'none')
            if self.pid:
                out += 'State:\t%s (whatever)\n' % self.state
            if feeder.PANIC_ASK in command:
                out += 'panic %s\n' % self.panic
            return out
        if command == feeder.PSTORE:
            return self.pstore
        if command.startswith('kill -CONT'):
            return 'continued\n'
        if command.startswith('/etc/init.d/S15watchdog start'):
            return 'Starting watchdog...\n3121\n'
        return ''


class Feeder(unittest.TestCase):

    def setUp(self):
        self.real = {name: getattr(feeder, name)
                     for name in ('say', 'npu', 'console_keep', 'gone', 'at_the_stop', 'sysctl')}
        # an XGS 3300's host: octep is there (its gone latch reads 0)
        feeder.sysctl = lambda name: '0' if name == feeder.GONE else None
        self.tmp = tempfile.TemporaryDirectory()
        feeder.STATE = os.path.join(self.tmp.name, 'feeder.json')
        self.said = []
        feeder.say = self.said.append
        self.console = []
        feeder.console_keep = lambda text, who: self.console.append((who, text))
        feeder.PANIC_TOLD['boot'] = None
        feeder.UPTIME.update(at=None, uptime=None)

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        self.tmp.cleanup()

    def test_each_look_sets_the_panic_timeout(self):
        self.assertIn('echo %d > /proc/sys/kernel/panic' % feeder.PANIC_WAIT, feeder.PANIC_ASK)
        feeder.npu = Coprocessor(900, '2448', 'S')
        self.assertIsNone(feeder.once())
        self.assertIn(feeder.PANIC_ASK, feeder.npu.asked[0])
        self.assertEqual(self.said, [])

    def test_the_xgs_136_s_panic_timeout_is_left_alone(self):
        """Its link goes down with its coprocessor, and npuep lets go at once: no octep, no change."""
        feeder.sysctl = lambda name: None
        feeder.npu = Coprocessor(900, '2448', 'S', panic='1 1')
        self.assertIsNone(feeder.once())
        self.assertEqual(feeder.npu.asked[0], feeder.ASK)
        self.assertEqual(self.said, [])

    def test_a_panic_timeout_of_one_second_is_said_once_a_boot(self):
        """2026-11-02: the coprocessor's own 1 s froze the host in five panics of six."""
        feeder.npu = Coprocessor(900, '2448', 'S', panic='1 60')
        feeder.once()
        self.assertEqual(len(self.said), 1)
        self.assertIn('would have restarted 1 s after a panic', self.said[0])
        self.assertIn('waits 60 s now', self.said[0])
        # the same boot, looked at again: nothing more
        feeder.npu = Coprocessor(920, '2448', 'S', panic='1 60')
        feeder.once()
        self.assertEqual(len(self.said), 1)
        # a new boot: said again
        feeder.npu = Coprocessor(30, None, None, panic='1 60')
        feeder.once()
        self.assertEqual(len(self.said), 2)

    def test_the_panic_timeout_is_set_while_the_coprocessor_settles(self):
        feeder.npu = Coprocessor(40, None, None, panic='1 60')
        self.assertIsNone(feeder.once())
        self.assertIn('waits 60 s now', self.said[0])

    def test_a_panic_timeout_that_cannot_be_set_is_said_so(self):
        feeder.npu = Coprocessor(900, '2448', 'S', panic='1 1')
        feeder.once()
        self.assertIn('could not be told to wait 60 s', self.said[0])

    def kept(self):
        with open(feeder.STATE, encoding='utf-8') as f:
            return json.load(f)

    def test_a_running_feeder_is_left_alone(self):
        feeder.npu = Coprocessor(900, '2448', 'S')
        self.assertIsNone(feeder.once())
        self.assertEqual(feeder.npu.asked, [feeder.ASK + feeder.PANIC_ASK, feeder.PSTORE])
        self.assertEqual(self.said, [])
        self.assertEqual(self.console, [])

    def test_a_stopped_feeder_is_continued(self):
        feeder.npu = Coprocessor(900, '2448', 'T')
        self.assertEqual(feeder.once(), 'continued')
        self.assertIn('kill -CONT 2448 && echo continued', feeder.npu.asked)
        self.assertIn('was stopped: continued', self.said[0])
        self.assertEqual(self.kept()['last']['found'], 'stopped')

    def test_a_feeder_that_is_gone_is_started_by_the_init_script(self):
        feeder.npu = Coprocessor(900, None, None)
        self.assertEqual(feeder.once(), 'started again (pid 3121)')
        self.assertTrue(any(a.startswith('/etc/init.d/S15watchdog start') for a in feeder.npu.asked))
        self.assertIn('was not running: started again (pid 3121)', self.said[0])
        self.assertEqual(self.kept()['count'], 1)

    def test_a_crash_record_is_kept_once(self):
        feeder.npu = Coprocessor(900, '2448', 'S', OOPS)
        feeder.once()
        self.assertEqual(len(self.console), 1)
        self.assertIn('virtual address 00000001', self.console[0][1])
        self.assertIn('1 crash record(s)', self.said[0])
        # the same boot of the coprocessor: its pstore is not asked again
        feeder.npu = Coprocessor(920, '2448', 'S', OOPS)
        feeder.once()
        self.assertEqual(feeder.npu.asked, [feeder.ASK + feeder.PANIC_ASK])
        # a later boot that still holds the same record: asked, and not kept twice
        feeder.npu = Coprocessor(130, '2448', 'S', OOPS)
        feeder.once()
        self.assertIn(feeder.PSTORE, feeder.npu.asked)
        self.assertEqual(len(self.console), 1)

    def test_no_crash_record_keeps_nothing(self):
        feeder.npu = Coprocessor(900, '2448', 'S', '')
        feeder.once()
        self.assertEqual(self.console, [])
        self.assertIn('pstore_boot', self.kept())

    def test_the_stop_is_acted_on_when_the_coprocessor_is_taken_as_gone(self):
        stops = []
        feeder.at_the_stop = lambda: stops.append('stop')
        feeder.gone = lambda: '0'
        beat = feeder.watch_beat(feeder.watch_beat(None))
        feeder.gone = lambda: '1'
        beat = feeder.watch_beat(beat)
        beat = feeder.watch_beat(beat)
        self.assertEqual(stops, ['stop'])
        # taken up again, and gone again later: a second stop
        feeder.gone = lambda: '0'
        beat = feeder.watch_beat(beat)
        feeder.gone = lambda: '1'
        feeder.watch_beat(beat)
        self.assertEqual(stops, ['stop', 'stop'])

    def test_a_feeder_started_on_a_gone_coprocessor_does_not_act(self):
        stops = []
        feeder.at_the_stop = lambda: stops.append('stop')
        feeder.gone = lambda: '1'
        feeder.watch_beat(feeder.watch_beat(None))
        self.assertEqual(stops, [])

    def test_no_driver_keeps_the_last_value(self):
        feeder.gone = lambda: None
        self.assertEqual(feeder.watch_beat('0'), '0')

    def test_a_zombie_is_started_again(self):
        feeder.npu = Coprocessor(900, '2448', 'Z')
        self.assertEqual(feeder.once(), 'started again (pid 3121)')
        self.assertEqual(self.kept()['last']['found'], 'zombie')

    def test_a_coprocessor_just_booted_is_left_to_its_init(self):
        feeder.npu = Coprocessor(40, None, None)
        self.assertIsNone(feeder.once())
        self.assertEqual(len(feeder.npu.asked), 1)

    def test_a_coprocessor_that_does_not_answer_is_not_touched(self):
        asked = []
        feeder.npu = lambda seconds, command: asked.append(command)
        self.assertEqual(feeder.once(), 'unreachable')
        self.assertEqual(asked, [feeder.ASK + feeder.PANIC_ASK])
        self.assertFalse(os.path.exists(feeder.STATE))

    def test_an_answer_cut_short_is_unreachable(self):
        feeder.npu = lambda seconds, command: '901\n'
        self.assertEqual(feeder.once(), 'unreachable')
        self.assertIsNone(feeder.UPTIME['at'])

    def test_a_look_keeps_the_coprocessor_s_uptime_for_the_watch(self):
        feeder.npu = Coprocessor(900, '2448', 'S')
        feeder.once()
        self.assertEqual(feeder.UPTIME['uptime'], 900)
        self.assertIsNotNone(feeder.UPTIME['at'])


class SilentLink(unittest.TestCase):
    """A coprocessor whose Linux stops without a panic (#768): the management link and then the console
    silent while the fast path answers, and octep told before the coprocessor's watchdog resets it -
    and every case in which nothing is to be done."""

    NAMES = ('say', 'answers_on_link', 'sysctl', 'console_answers', 'tell_driver', 'recovering')

    def setUp(self):
        self.real = {name: getattr(feeder, name) for name in self.NAMES}
        self.tmp = tempfile.TemporaryDirectory()
        feeder.STATE = os.path.join(self.tmp.name, 'feeder.json')
        self.said = []
        feeder.say = self.said.append
        self.link = True
        feeder.answers_on_link = lambda: self.link
        self.values = {feeder.QUIESCE: '0', feeder.GONE: '0', feeder.HEARTBEAT: 'alive'}
        feeder.sysctl = self.values.get
        self.console, self.asked = False, 0
        feeder.console_answers = self.ask_console
        self.told = 0
        feeder.tell_driver = self.tell
        self.recovery = False
        feeder.recovering = lambda: self.recovery
        feeder.UPTIME.update(at=None, uptime=None)
        feeder.QUIESCED['at'] = None

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        feeder.QUIESCED['at'] = None
        self.tmp.cleanup()

    def ask_console(self):
        self.asked += 1
        return self.console.pop(0) if isinstance(self.console, list) else self.console

    def tell(self):
        self.told += 1
        return True

    def run_until(self, link, times):
        for t in times:
            link = feeder.watch_link(link, t)
        return link

    def silent_since(self, at):
        """The link answered steadily up to <at> - every two seconds for 200 s - and is silent after."""
        link = self.run_until({'ok': None, 'acted': False}, range(at - 200, at + 1, 2))
        self.link = False
        return link

    def test_a_link_that_answers_is_left_alone(self):
        link = self.run_until({'ok': None, 'acted': False}, [0, 2, 4])
        self.assertEqual(link, {'ok': 4, 'up': 0, 'acted': False})
        self.assertFalse(feeder.holding(link, 5))
        self.assertEqual((self.told, self.asked, self.said), (0, 0, []))

    def test_an_echo_or_two_lost_keeps_the_steady_run(self):
        times = [t for t in range(0, 201, 2) if t not in (100, 102)]
        link = self.run_until({'ok': None, 'acted': False}, times)
        self.assertEqual(link['up'], 0)

    def polls(self, link, answers):
        """watch_link at each (time, answered) in turn."""
        for t, answered in answers:
            self.link = answered
            link = feeder.watch_link(link, t)
        return link

    def test_one_echo_lost_with_its_wait_keeps_the_run_two_end_it(self):
        """What a run forgives: an echo lost with its one-second wait - six seconds between two answers - not
        two (nine)."""
        link = self.run_until({'ok': None, 'acted': False}, range(0, 99, 2))
        link = self.polls(link, [(101, False), (104, True)])
        self.assertEqual(link['up'], 0)
        link = self.polls(link, [(107, False), (110, False), (113, True)])
        self.assertEqual(link['up'], 113)

    def test_a_look_between_two_polls_is_not_a_gap(self):
        """A look between two polls - ten seconds and more when it falls back to the console - is the feeder's
        time, not a gap in the link's answers (review, 2026-11-03): run() lends it to the link."""
        link = self.run_until({'ok': None, 'acted': False}, range(0, 101, 2))
        link = dict(link, lent=10)
        link = self.run_until(link, range(112, 201, 2))
        self.assertEqual(link['up'], 0)

    def test_a_stop_after_slow_looks_is_told(self):
        """The review's case: every look takes ten seconds. Counted as gaps, each look ended the run, which never
        grew STEADY, and the stop went on to the reset unseen; lent, the run holds and octep is told."""
        link, t = {'ok': None, 'acted': False}, 0
        while t < 300:
            link = feeder.watch_link(link, t)
            if t % 20 == 0 and link.get('ok') is not None:
                link = dict(link, lent=link.get('lent', 0) + 10)
                t += 12
            else:
                t += 2
        self.link = False
        link = self.run_until(link, range(t, t + 41, 3))
        self.assertEqual((self.asked, self.told), (1, 1))

    def test_a_long_look_then_one_echo_lost_is_no_silence(self):
        """The review of the fix: after a look of half a minute, one echo lost is three seconds of silence, not
        thirty - the console is not asked, and the run goes on."""
        link = self.run_until({'ok': None, 'acted': False}, range(0, 201, 2))
        link = dict(link, lent=29)
        link = self.polls(link, [(232, False)])
        self.assertEqual((self.asked, self.told), (0, 0))
        link = self.polls(link, [(235, True)])
        self.assertEqual(link['up'], 0)

    def test_a_link_answering_one_echo_in_three_is_never_steady(self):
        """A link losing two echoes of three - as one can while OPNsense sets its interfaces up after a
        recovery - is no steady run, and its silence after is not acted on (review of the fix)."""
        link = self.polls({'ok': None, 'acted': False}, [(t, t % 9 == 0) for t in range(0, 240, 3)])
        link = self.polls(link, [(t, False) for t in range(240, 300, 3)])
        self.assertEqual((self.asked, self.told), (0, 0))

    def test_a_told_driver_marks_its_own_quiesce(self):
        """watch_link marks the quiesce it made, so that at_the_stop sends no keys after it."""
        self.run_until(self.silent_since(200), [231])
        self.assertEqual(self.told, 1)
        self.assertIsNotNone(feeder.QUIESCED['at'])

    def test_a_tell_that_failed_marks_no_quiesce(self):
        feeder.tell_driver = lambda: False
        self.run_until(self.silent_since(200), [231])
        self.assertIsNone(feeder.QUIESCED['at'])

    def test_a_link_not_yet_steady_is_not_acted_on(self):
        """The second XGS 3300, 2026-11-02: right after a recovery its link answered once and went quiet
        for half a minute while OPNsense set its interfaces up again - a healthy coprocessor."""
        link = self.run_until({'ok': None, 'acted': False}, [100])
        self.link = False
        link = self.run_until(link, [110, 120, 131, 140])
        self.assertEqual((self.asked, self.told, self.said), (0, 0, []))
        self.assertFalse(feeder.holding(link, 141))
        # and its coming back is not said either
        self.link = True
        feeder.watch_link(link, 150)
        self.assertEqual(self.said, [])

    def test_a_watch_started_on_a_coprocessor_long_up_is_steady_at_once(self):
        """The second XGS 3300, 2026-11-02 (#987): its coprocessor's Linux stopped 42 s after feeder.py
        was started - unseen then. A look at the start finds the coprocessor up for hours."""
        link = self.run_until({'ok': None, 'acted': False}, [0])
        feeder.UPTIME.update(at=1, uptime=4 * 3600)
        link = self.run_until(link, range(2, 43, 2))
        self.link = False
        self.run_until(link, [50, 60, 72])
        self.assertEqual((self.asked, self.told), (1, 1))
        self.assertIn('for 30 s nor on its console', self.said[-1])

    def test_a_coprocessor_just_booted_still_asks_for_the_steady_run(self):
        """Right after a recovery a look finds the coprocessor up for minutes, not past its boot: the
        link that answered once and went quiet is not acted on (#986)."""
        link = self.run_until({'ok': None, 'acted': False}, [100])
        feeder.UPTIME.update(at=101, uptime=160)
        self.link = False
        self.run_until(link, [110, 131, 140])
        self.assertEqual((self.asked, self.told, self.said), (0, 0, []))

    def test_a_look_from_before_the_run_says_nothing_of_it(self):
        """A look at the coprocessor before a recovery says nothing of the one after it: a link that
        has answered once since and gone quiet is not acted on (#986)."""
        feeder.UPTIME.update(at=1, uptime=4 * 3600)
        link = self.run_until(self.silent_since(200), [230])
        self.assertEqual(self.told, 1)
        self.link = True
        link = self.run_until(link, [400])
        self.link = False
        self.run_until(link, [410, 431, 440])
        self.assertEqual((self.asked, self.told), (1, 1))

    def test_a_running_recovery_is_left_alone(self):
        self.recovery = True
        link = self.run_until(self.silent_since(200), [230, 240])
        self.assertEqual((self.asked, self.told), (0, 0))
        self.assertFalse(feeder.holding(link, 241))

    def test_a_console_held_by_another_caller_is_asked_again(self):
        self.console = [None, None, False]
        link = self.run_until(self.silent_since(200), [230])
        self.assertEqual((self.asked, self.told), (1, 0))
        self.assertTrue(feeder.holding(link, 231))
        self.run_until(link, [232, 234])
        self.assertEqual((self.asked, self.told), (3, 1))
        self.assertIn('for 34 s nor on its console', self.said[-1])
        self.assertIn('held by another caller at 2 look(s) before', self.said[-1])

    def test_a_short_silence_is_waited_out_and_the_feeder_held(self):
        link = self.run_until(self.silent_since(0), [2, 10, 20, 28])
        self.assertEqual((self.told, self.asked), (0, 0))
        self.assertTrue(feeder.holding(link, 28))

    def test_a_silent_link_and_a_silent_console_tell_octep_once(self):
        link = self.run_until(self.silent_since(0), [10, 20, 30])
        self.assertEqual((self.asked, self.told), (1, 1))
        self.assertIn('octep told to stop touching it first', self.said[-1])
        self.assertIn('for 30 s nor on its console', self.said[-1])
        self.assertNotIn('held by another caller', self.said[-1])
        with open(feeder.STATE, encoding='utf-8') as f:
            self.assertEqual(json.load(f)['last']['found'], 'silent')
        # decided: the feeder is asked again, and the same silence tells nothing more
        self.assertFalse(feeder.holding(link, 31))
        self.run_until(link, [32, 40, 60])
        self.assertEqual((self.asked, self.told), (1, 1))

    def test_a_console_that_answers_means_the_link_alone_is_down(self):
        self.console = True
        link = self.run_until(self.silent_since(0), [30, 32])
        self.assertEqual((self.asked, self.told), (1, 0))
        self.assertIn('but its console does', self.said[-1])
        self.assertFalse(feeder.holding(link, 33))

    def test_no_quiesce_switch_is_the_xgs_136_and_nothing_is_asked(self):
        del self.values[feeder.QUIESCE]
        link = self.run_until(self.silent_since(0), [30, 40])
        self.assertEqual((self.asked, self.told), (0, 0))
        self.assertFalse(feeder.holding(link, 41))

    def test_a_fast_path_that_stopped_too_is_left_to_the_gone_latch(self):
        self.values[feeder.HEARTBEAT] = 'dead'
        self.run_until(self.silent_since(0), [30])
        self.assertEqual((self.asked, self.told), (0, 0))

    def test_a_driver_already_gone_is_not_told_again(self):
        self.values[feeder.GONE] = '1'
        self.run_until(self.silent_since(0), [30])
        self.assertEqual((self.asked, self.told), (0, 0))

    def test_a_link_that_never_answered_is_not_watched(self):
        self.link = False
        link = self.run_until({'ok': None, 'acted': False}, [0, 30, 60, 120])
        self.assertEqual((self.asked, self.told), (0, 0))
        self.assertFalse(feeder.holding(link, 121))

    def test_the_link_answering_again_starts_a_new_watch(self):
        link = self.run_until(self.silent_since(0), [30])
        self.link = True
        link = feeder.watch_link(link, 200)
        self.assertEqual(link, {'ok': 200, 'up': 200, 'acted': False})
        self.assertIn('answers on its management link again', self.said[-1])
        # a silence soon after is not acted on - the link has not been steady since
        self.link = False
        link = self.run_until(link, [231])
        self.assertEqual(self.told, 1)
        # one after a steady run again is
        self.link = True
        link = self.run_until(link, range(240, 371, 2))
        self.link = False
        self.run_until(link, [380, 401])
        self.assertEqual(self.told, 2)


class TheStop(unittest.TestCase):
    """What at_the_stop does once the driver takes the coprocessor as gone: SysRq over the console, then
    fastpath.py - but no keys to a coprocessor this feeder has itself said is about to be reset."""

    def setUp(self):
        self.real = {name: getattr(feeder, name) for name in ('say', 'subprocess', 'os')}
        self.said, self.ran, self.started = [], [], []
        feeder.say = self.said.append
        test = self

        class Sub:
            SubprocessError = feeder.subprocess.SubprocessError
            DEVNULL = feeder.subprocess.DEVNULL

            @staticmethod
            def run(args, **kw):
                test.ran.append(args)

            @staticmethod
            def Popen(args, **kw):
                test.started.append(args)

        class Os:
            R_OK = os.R_OK

            @staticmethod
            def access(path, mode):
                return True

        feeder.subprocess, feeder.os = Sub, Os
        feeder.QUIESCED['at'] = None

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        feeder.QUIESCED['at'] = None

    def test_a_stop_found_by_the_heartbeat_asks_the_kernel_its_state(self):
        feeder.at_the_stop()
        self.assertEqual([a[1] for a in self.ran], [feeder.SYSRQ])
        self.assertEqual(len(self.started), 1)

    def test_its_own_quiesce_sends_no_keys_to_the_console(self):
        """npu-sysrq.sh counts from a dead heartbeat; after a quiesce the heartbeat is alive and the reset
        is coming within the minute - keys then can stop the coprocessor's boot (review, 2026-11-03)."""
        feeder.QUIESCED['at'] = feeder.time.monotonic()
        feeder.at_the_stop()
        self.assertEqual(self.ran, [])
        self.assertIn('keys sent now could reach its boot firmware', self.said[-1])
        self.assertEqual(len(self.started), 1)

    def test_a_quiesce_long_past_does_not_hold_back_a_later_stop(self):
        feeder.QUIESCED['at'] = feeder.time.monotonic() - 3600
        feeder.at_the_stop()
        self.assertEqual([a[1] for a in self.ran], [feeder.SYSRQ])


if __name__ == '__main__':
    unittest.main(verbosity=2)
