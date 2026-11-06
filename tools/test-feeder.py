#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The coprocessor's watchdog feeder as scripts/xgs/feeder.py reads it and puts it right (#768), with
the coprocessor's answers made up: npu-run.sh is replaced by a function that answers as the
coprocessor does and keeps what it was asked, and the system log and the state file by a list and a
temporary file. And npu-run.sh's own console session as feeder.py asks for it, in a copy whose paths
point at stand-ins.

    python3 tools/test-feeder.py
"""

import importlib.util
import json
import os
import subprocess
import tempfile
import threading
import time
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

    def __init__(self, uptime, pid, state, pstore='', panic='60 60', cont=True):
        self.uptime, self.pid, self.state, self.pstore = uptime, pid, state, pstore
        self.panic = panic		# its panic timeout before the look, and after it
        self.cont = cont		# whether the look's own SIGCONT takes (CONTINUE)
        self.asked = []

    def __call__(self, seconds, command):
        self.asked.append(command)
        if command.startswith(feeder.ASK):
            out = '%d\npid %s\n' % (self.uptime, self.pid or 'none')
            if self.pid:
                out += 'State:\t%s (whatever)\n' % self.state
            # the look's own SIGCONT, as its shell runs it: a stopped feeder, at any uptime
            if feeder.CONTINUE in command and self.pid and self.state in ('T', 't') and self.cont:
                out += 'continued %s\n' % self.pid
                self.state = 'S'
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
        feeder.LOOKED.update(proof=None, silent=None, ssh=None)
        feeder.NPU_EXIT.update(rc=None, link=False)

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        feeder.LOOKED.update(proof=None, silent=None, ssh=None)
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
        """In the look's own session (#1002): over the console a second one is a second login."""
        feeder.npu = Coprocessor(900, '2448', 'T')
        self.assertEqual(feeder.once(), 'continued')
        self.assertNotIn('kill -CONT 2448 && echo continued', feeder.npu.asked)
        self.assertEqual(feeder.npu.state, 'S')
        self.assertIn('was stopped: continued', self.said[0])
        self.assertEqual(self.kept()['last']['found'], 'stopped')

    def test_a_stopped_feeder_the_look_did_not_continue_is_asked_again(self):
        feeder.npu = Coprocessor(900, '2448', 'T', cont=False)
        self.assertEqual(feeder.once(), 'continued')
        self.assertIn('kill -CONT 2448 && echo continued', feeder.npu.asked)

    def test_a_stopped_feeder_is_continued_while_the_coprocessor_settles(self):
        """A SIGCONT is harmless at any uptime, and a feeder stopped in a boot's first two minutes is reset as
        surely as a later one (the review of the fix for #1002, A3)."""
        feeder.npu = Coprocessor(60, '2448', 'T')
        self.assertEqual(feeder.once(), 'continued')
        self.assertEqual(feeder.npu.state, 'S')
        self.assertIn('was stopped: continued', self.said[-1])
        self.assertNotIn('-ge %d' % feeder.SETTLE, feeder.ASK)

    def test_a_look_with_an_answer_is_the_watch_s_last_proof(self):
        feeder.npu = Coprocessor(900, '2448', 'S')
        feeder.once()
        self.assertIsNotNone(feeder.LOOKED['proof'])
        self.assertIsNone(feeder.LOOKED['silent'])

    def test_a_look_whose_console_is_silent_is_kept_for_the_watch(self):
        """npu-run.sh's 69 - its console asked twice, no prompt either time - after ssh did not answer:
        weighed by watch_userland."""
        def silent(seconds, command):
            feeder.NPU_EXIT['rc'] = feeder.NO_PROMPT
        feeder.npu = silent
        self.assertEqual(feeder.once(), 'unreachable')
        self.assertIsNotNone(feeder.LOOKED['silent'])

    def test_a_look_refused_the_console_or_cut_short_is_no_silence(self):
        for rc in (feeder.BUSY, 1, 124, None):
            def failed(seconds, command, rc=rc):
                feeder.NPU_EXIT['rc'] = rc
            feeder.npu = failed
            self.assertEqual(feeder.once(), 'unreachable')
            self.assertIsNone(feeder.LOOKED['silent'])

    def test_a_look_answered_over_the_link_s_ssh_is_kept_for_the_watch(self):
        """npu-run.sh says when the link's ssh carried a look (NPU_RUN_SAY_LINK): F6 is ssh working in the
        link's run, and then not, and what watch_userland says rests on it (a review of the watch,
        2026-11-04). A look over the console later does not take it back."""
        coprocessor = Coprocessor(900, '2448', 'S')

        def over_the_link(seconds, command):
            feeder.NPU_EXIT.update(rc=0, link=True)
            return coprocessor(seconds, command)
        feeder.npu = over_the_link
        feeder.once()
        self.assertIsNotNone(feeder.LOOKED['ssh'])
        self.assertEqual(feeder.LOOKED['ssh'], feeder.LOOKED['proof'])
        first = feeder.LOOKED['ssh']
        feeder.npu = Coprocessor(920, '2448', 'S')
        feeder.once()
        self.assertEqual(feeder.LOOKED['ssh'], first)
        self.assertGreaterEqual(feeder.LOOKED['proof'], first)

    def test_the_looks_and_the_watch_keep_at_once_without_losing_a_record(self):
        def many():
            for _ in range(100):
                feeder.keep('stopped', 'continued', '2448')
        both = [threading.Thread(target=many) for _ in range(2)]
        for thread in both:
            thread.start()
        for thread in both:
            thread.join()
        self.assertEqual(self.kept()['count'], 200)

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

    NAMES = ('say', 'answers_on_link', 'sysctl', 'console_answers', 'tell_driver', 'recovering',
             'want_console', 'release_console', 'retell_link', 'keep')

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
        self.console, self.asked, self.waits, self.twice = False, 0, [], []
        feeder.console_answers = self.ask_console
        self.told = 0
        feeder.tell_driver = self.tell
        self.recovery = False
        feeder.recovering = lambda: self.recovery
        self.retold, self.released = 0, 0
        feeder.retell_link = self.retell
        feeder.want_console = lambda: None
        feeder.release_console = self.release
        feeder.UPTIME.update(at=None, uptime=None)
        feeder.LOOKED.update(proof=None, silent=None, ssh=None)
        feeder.USERLAND, feeder.RETOLD = {'said': None}, {'at': None}
        feeder.QUIESCED['at'] = None
        feeder.CONSOLE_CONTINUED['pid'] = None

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        feeder.QUIESCED['at'] = None
        feeder.CONSOLE_CONTINUED['pid'] = None
        feeder.LOOKED.update(proof=None, silent=None, ssh=None)
        feeder.USERLAND, feeder.RETOLD = {'said': None}, {'at': None}
        self.tmp.cleanup()

    def ask_console(self, wait=5, twice=True):
        self.asked += 1
        self.waits.append(wait)
        self.twice.append(twice)
        return self.console.pop(0) if isinstance(self.console, list) else self.console

    def tell(self):
        self.told += 1
        return True

    def retell(self):
        self.retold += 1
        return True

    def release(self):
        self.released += 1

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

    def test_a_stop_inside_a_slow_look_is_decided_from_the_last_answer(self):
        """#1012, the review's F3b again: the stop came during a look of about a minute. The looks run in a
        thread of their own now, the polls go on and nothing is lent: the console is asked EARLY s after the
        last answer, asked once more and octep told at SILENT, whatever the look takes - before the reset at
        54-60 s."""
        link = self.run_until({'ok': None, 'acted': False}, range(0, 201, 2))
        self.link = False
        link = self.run_until(link, range(203, 230, 3))
        self.assertEqual((self.asked, self.told), (1, 0))
        self.assertEqual(self.waits, [feeder.URGENT_WAIT])
        self.assertTrue(feeder.holding(link, 228))
        self.run_until(link, [230])
        self.assertEqual((self.asked, self.told), (2, 1))
        self.assertIn('for 30 s nor on its console', self.said[-1])

    def test_a_stall_that_ends_before_silent_is_left_alone(self):
        """A Linux that stalls - its console silent at EARLY - and answers its link again before SILENT is not
        stopped, and the console's mark is taken back."""
        link = self.run_until(self.silent_since(0), [10, 20])
        self.assertEqual((self.asked, self.told), (1, 0))
        self.link = True
        link = self.run_until(link, [24])
        self.link = False
        self.run_until(link, [27, 30])
        self.assertEqual((self.asked, self.told, self.released), (1, 0, 1))

    def test_a_console_held_throughout_decides_nothing(self):
        """Held by one session for all of the silence - longer than any reader holds it - nothing is decided
        without the console: asked at each poll, the first time waiting URGENT_WAIT."""
        self.console = None
        link = self.run_until(self.silent_since(0), [10, 40, 45, 50, 60])
        self.assertEqual((self.asked, self.told), (5, 0))
        self.assertEqual(self.waits, [feeder.URGENT_WAIT, 5, 5, 5, 5])
        self.assertTrue(feeder.holding(link, 61))

    def test_a_link_found_down_alone_is_given_its_address_again(self):
        """A4 of the review of the fix for #1002: its console answered, and the link still silent RETELL s
        after its last answer is given its address again, once."""
        self.console = True
        link = self.run_until(self.silent_since(0), [10, 20, 40])
        self.assertEqual((self.asked, self.retold), (1, 0))
        self.run_until(link, [45, 48, 60])
        self.assertEqual(self.retold, 1)
        self.assertIn('given its address again', self.said[-1])

    def test_a_link_back_by_itself_is_not_told_its_address(self):
        self.console = True
        link = self.run_until(self.silent_since(0), [10, 20])
        self.link = True
        self.run_until(link, [30, 50])
        self.assertEqual(self.retold, 0)

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
        self.assertEqual((self.asked, self.told), (2, 1))
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
        self.assertIn('no answer either way from its console at 2 look(s) before', self.said[-1])

    def test_a_short_silence_is_asked_early_and_the_looks_held(self):
        """Silent EARLY s, the console asked; silent too, nothing decided before SILENT."""
        link = self.run_until(self.silent_since(0), [2, 10, 20, 28])
        self.assertEqual((self.told, self.asked), (0, 1))
        self.assertTrue(feeder.holding(link, 28))

    def test_a_silent_link_and_a_silent_console_tell_octep_once(self):
        link = self.run_until(self.silent_since(0), [10, 20, 30])
        self.assertEqual((self.asked, self.told), (2, 1))
        self.assertIn('octep told to stop touching it first', self.said[-1])
        self.assertIn('for 30 s nor on its console', self.said[-1])
        self.assertNotIn('held by another caller', self.said[-1])
        with open(feeder.STATE, encoding='utf-8') as f:
            self.assertEqual(json.load(f)['last']['found'], 'silent')
        # decided: the feeder is asked again, and the same silence tells nothing more
        self.assertFalse(feeder.holding(link, 31))
        self.run_until(link, [32, 40, 60])
        self.assertEqual((self.asked, self.told), (2, 1))

    def test_a_console_that_answers_means_the_link_alone_is_down(self):
        self.console = True
        link = self.run_until(self.silent_since(0), [30, 32])
        self.assertEqual((self.asked, self.told), (1, 0))
        self.assertIn('but its console does', self.said[-1])
        self.assertFalse(feeder.holding(link, 33))

    def test_a_console_that_answers_and_continued_a_stopped_feeder_says_so(self):
        """The second XGS 3300, 2026-11-03 (#1002): its link down and its console answering, its
        watchdog feeder stopped, and the looks held while the silence was weighed - the reset froze
        the host. The watch's own ask continues the feeder now."""
        def answers_and_continues(wait=5):
            feeder.CONSOLE_CONTINUED['pid'] = '1834'
            return True
        feeder.console_answers = answers_and_continues
        self.run_until(self.silent_since(0), [30, 32])
        self.assertIn('but its console does', self.said[-2])
        self.assertIn('watchdog feeder (pid 1834) was stopped: continued, over its console', self.said[-1])
        with open(feeder.STATE, encoding='utf-8') as f:
            self.assertEqual(json.load(f)['last']['found'], 'stopped')

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

    def test_a_console_found_silent_early_is_asked_once_more_at_silent(self):
        """Finding 5 of a review of the watch (2026-11-04): the console found silent at the ask after EARLY
        s, up to 20 s before, is asked once more at SILENT - one carriage return, a short wait - before
        octep is told. One found silent at SILENT or later is not asked again."""
        self.run_until(self.silent_since(0), [10, 20, 30])
        self.assertEqual((self.asked, self.told), (2, 1))
        self.assertEqual((self.waits, self.twice), ([feeder.URGENT_WAIT, 5], [True, False]))
        self.assertIn('for 30 s nor on its console, found silent at 10 s and again at 30 s, while its fast '
                      'path still does', self.said[-1])
        self.link = True
        link = self.run_until({'ok': None, 'acted': False}, range(1000, 1201, 2))
        self.link = False
        self.run_until(link, [1231])
        self.assertEqual((self.asked, self.told), (3, 2))
        self.assertEqual((self.waits[2:], self.twice[2:]), ([5], [True]))
        self.assertIn('for 31 s nor on its console, while its fast path still does', self.said[-1])

    def test_a_console_that_answers_when_asked_again_is_left_as_it_is(self):
        self.console = [False, True]
        link = self.run_until(self.silent_since(0), [10, 20, 30])
        self.assertEqual((self.asked, self.told, link['why']), (2, 0, 'alive'))
        self.assertIn('for 30 s, but its console does - asked again after it showed no prompt at 10 s: left '
                      'as it is', self.said[-1])

    def test_a_console_not_answered_when_asked_again_is_asked_at_the_next_poll(self):
        """F2 of a review of the watch (2026-11-06): the ask at SILENT that gave no answer either way let
        the first ask's answer stand, and octep was told on it alone. It is asked again at the next poll."""
        self.console = [False, None, None, False]
        self.run_until(self.silent_since(0), [10, 20, 30, 32, 34])
        self.assertEqual((self.asked, self.told), (4, 1))
        self.assertEqual(self.twice, [True, False, False, False])
        self.assertIn('found silent at 10 s and again at 34 s', self.said[-1])

    def test_a_console_that_answers_at_the_next_poll_is_left_as_it_is(self):
        self.console = [False, None, True]
        link = self.run_until(self.silent_since(0), [10, 30, 32])
        self.assertEqual((self.asked, self.told, link['why']), (3, 0, 'alive'))

    def test_r4_a_link_back_during_a_long_ask_is_not_quiesced(self):
        """R4 of a review of the watch (2026-11-04): the first ask, waiting URGENT_WAIT for another
        caller's session, took 35 s and found no prompt; the link had answered again meanwhile, and octep
        was told with no echo sent after the ask."""
        clock = {'now': 210.0}

        class FakeTime:
            monotonic = staticmethod(lambda: clock['now'])
            time = staticmethod(feeder.time.time)

        def slow_silent_ask(wait=5, twice=True):
            self.asked += 1
            self.waits.append(wait)
            clock['now'] += 35.0
            self.link = True		# the link answers again during the ask
            return False

        link = self.silent_since(200)
        feeder.console_answers = slow_silent_ask
        real = feeder.time
        feeder.time = FakeTime
        try:
            link = feeder.watch_link(link, 210.0)
        finally:
            feeder.time = real
        self.assertEqual((self.waits, self.told, self.released), ([feeder.URGENT_WAIT], 0, 1))
        self.assertEqual(link, {'ok': 245.0, 'up': 245.0, 'acted': False})

    def test_a_link_back_during_the_ask_at_silent_is_not_quiesced(self):
        def back_during_the_second(wait=5, twice=True):
            self.asked += 1
            if not twice:
                self.link = True	# the link answers again while the console is asked once more
            return False
        feeder.console_answers = back_during_the_second
        link = self.run_until(self.silent_since(0), [10, 20, 30])
        self.assertEqual((self.asked, self.told, self.released, link['acted']), (2, 0, 1, False))
        self.assertAlmostEqual(link['up'], 30, places=2)

    def test_a_record_that_cannot_be_kept_tells_octep_once(self):
        """Finding 10 of a review of the watch (2026-11-04): a state file that cannot be written - a full
        /var/db - took watch_link out before it returned its decision, and octep was told again at every
        poll."""
        def full(*args):
            raise OSError(28, 'No space left on device')
        feeder.keep = full
        link = self.run_until(self.silent_since(200), [231, 233, 235])
        self.assertEqual((self.told, link['why']), (1, 'told'))
        self.assertTrue(any('No space left on device' in line for line in self.said))

    def test_the_address_is_given_again_at_most_once_in_ten_minutes(self):
        """A link that flaps on a coprocessor long up has each new run steady at its first look, and had
        its address given again at each silence - NPUMGMT reconfigured and the console tried for up to five
        minutes each time (finding 11 of a review of the watch, 2026-11-04)."""
        self.console = True
        link = self.run_until(self.silent_since(200), [210, 245])
        self.assertEqual(self.retold, 1)
        for start, retold in ((300, 1), (900, 2)):
            self.link = True
            link = self.run_until(link, range(start, start + 121, 2))
            self.link = False
            link = self.run_until(link, [start + 130, start + 165, start + 167])
            self.assertEqual((link['why'], self.retold), ('alive', retold))

    def test_no_address_is_given_while_a_recovery_runs_or_the_driver_has_let_go(self):
        """A recovery gives the link its address itself, at its end (the same review)."""
        self.console = True
        link = self.run_until(self.silent_since(0), [10])
        self.recovery = True
        link = self.run_until(link, [45, 47])
        self.assertEqual(self.retold, 0)
        self.recovery = False
        self.values[feeder.GONE] = '1'
        link = self.run_until(link, [49])
        self.assertEqual(self.retold, 0)
        self.values[feeder.GONE] = '0'
        self.run_until(link, [51])
        self.assertEqual(self.retold, 1)

    def test_the_first_ask_outwaits_a_twenty_second_session(self):
        """A console session holds the console about its seconds and seven more, and portstats.py and
        os-fanctl's board curve ask for 20: the watch's first ask waits for all of one, which 25 s did not
        (finding 6 of a review of the watch, 2026-11-04)."""
        self.assertGreaterEqual(feeder.URGENT_WAIT, 20 + 7)
        self.run_until(self.silent_since(0), [10])
        self.assertEqual(self.waits, [feeder.URGENT_WAIT])


class SilentUserland(unittest.TestCase):
    """A look that finds the coprocessor's console without a Linux prompt to two carriage returns, its
    link's ssh not answering either, while the link answers its echoes (watch_userland): the review's F6
    - its userland stopped, its kernel answering every echo - said, and not acted on; and the looks that
    say nothing of the link's present run (a review of the watch, 2026-11-04)."""

    NAMES = SilentLink.NAMES
    setUp, tearDown = SilentLink.setUp, SilentLink.tearDown
    ask_console, tell = SilentLink.ask_console, SilentLink.tell
    retell, release = SilentLink.retell, SilentLink.release
    run_until, silent_since = SilentLink.run_until, SilentLink.silent_since

    def steady_link(self, start=0):
        return self.run_until({'ok': None, 'acted': False}, range(start, start + 201, 2))

    def both(self, link, times):
        """What run() does at each poll: watch_link, then watch_userland."""
        for t in times:
            link = feeder.watch_link(link, t)
            feeder.watch_userland(link, t)
        return link

    def userland(self):
        return [line for line in self.said if 'without a Linux prompt' in line]

    def test_a_silent_look_while_the_link_answers_is_said_not_acted_on(self):
        """Finding 2 of the review: one look's no prompt on a link that answers is also what a console that
        is alive gives - a login asking for a password, a shell in a continuation prompt - and octep told
        for nothing costs a recovery that cannot reboot the coprocessor, and so a restart."""
        link = self.steady_link()
        feeder.LOOKED.update(proof=100.0, silent=190.0)
        self.assertTrue(feeder.watch_userland(link, 201))
        self.assertEqual((self.asked, self.told), (0, 0))
        self.assertEqual(self.said, [
            'a look found the coprocessor\'s console without a Linux prompt and its management link\'s ssh '
            'not answering, while the link answers its echoes: not acted on; no look over ssh had an answer '
            'in the link\'s present run'])
        # said once for each run of answers, not at every poll or look
        feeder.LOOKED.update(silent=196.0)
        self.assertFalse(feeder.watch_userland(link, 203))
        self.assertEqual((len(self.userland()), self.told), (1, 0))

    def test_it_says_whether_a_look_over_ssh_had_an_answer_in_the_run(self):
        """F6 is ssh working and then not: what a decision on it would rest on, said with it."""
        link = self.steady_link()
        feeder.LOOKED.update(proof=170.0, ssh=170.0, silent=190.0)
        self.assertTrue(feeder.watch_userland(link, 201))
        self.assertIn('not acted on; a look over ssh had an answer earlier in the link\'s present run',
                      self.said[-1])
        # an answer over ssh from before the run arms nothing in it
        link = self.steady_link(1000)
        feeder.LOOKED.update(proof=1180.0, silent=1190.0)
        self.assertTrue(feeder.watch_userland(link, 1201))
        self.assertIn('not acted on; no look over ssh had an answer in the link\'s present run',
                      self.said[-1])
        self.assertEqual(self.told, 0)

    def test_a_look_with_an_answer_since_says_nothing(self):
        link = self.steady_link()
        feeder.LOOKED.update(proof=195.0, silent=190.0)
        self.assertFalse(feeder.watch_userland(link, 201))
        self.assertEqual((self.told, self.userland()), (0, []))

    def test_no_silent_look_says_nothing(self):
        link = self.steady_link()
        feeder.LOOKED.update(proof=100.0, silent=None)
        self.assertFalse(feeder.watch_userland(link, 201))

    def test_not_while_the_link_is_not_yet_steady(self):
        link = self.run_until({'ok': None, 'acted': False}, range(0, 61, 2))
        feeder.LOOKED.update(proof=10.0, silent=30.0)
        self.assertFalse(feeder.watch_userland(link, 61))
        self.assertEqual((self.told, self.userland()), (0, []))

    def test_a_silent_link_not_yet_decided_is_left_to_its_own_watch(self):
        link = self.run_until(self.silent_since(200), [205])
        feeder.LOOKED.update(proof=100.0, silent=190.0)
        self.assertFalse(feeder.watch_userland(link, 205))

    def test_nothing_after_the_link_alone_was_found_down(self):
        """Finding 1 of the review: after "the link alone is down" every look goes over the console for as
        long as the link stays down, and one look's no prompt, at any time after, told octep."""
        self.console = True
        link = self.run_until(self.silent_since(200), [210])
        self.assertEqual(link['why'], 'alive')
        feeder.LOOKED.update(proof=100.0, silent=220.0)
        self.assertFalse(feeder.watch_userland(link, 230))
        self.assertEqual((self.told, self.userland()), (0, []))

    def test_r5_a_look_an_hour_after_the_link_alone_was_found_down_tells_nothing(self):
        """R5 of the review: one look's no prompt among some 180, an hour after "the link alone is down" -
        a console left in a continuation prompt by a quote lost from an earlier look's own command line -
        told octep for a coprocessor whose console had answered."""
        self.console = True
        link = self.both(self.silent_since(200), [210])
        self.assertEqual(link['why'], 'alive')
        feeder.LOOKED.update(proof=3780.0, silent=3800.0)
        self.both(link, [3820])
        self.assertEqual((self.told, self.userland()), (0, []))

    def test_r1_a_look_from_before_a_quiesce_says_nothing_of_the_next_run(self):
        """R1 of the review: a look in flight at a real stop ends without a prompt; the link's own watch
        tells octep; the coprocessor is reset and taken up again, its link answers from 600 s, and no look
        has had an answer when the new run is steady - the old look told octep for the recovered
        coprocessor. A quiesce takes the look's answer away, and the run would not count it anyway."""
        link = self.silent_since(200)
        feeder.LOOKED.update(proof=150.0, silent=201.0)
        link = self.both(link, [210, 213, 216, 219, 222, 225, 228, 231])
        self.assertEqual(self.told, 1)
        self.assertIsNone(feeder.LOOKED['silent'])
        self.link = True
        self.both(link, range(600, 731, 2))
        self.assertEqual((self.told, self.userland()), (1, []))

    def test_a_look_begun_before_the_run_of_answers_says_nothing_of_it(self):
        """A look that began before the link's present run - in the silence before it - says nothing of
        it, as past_boot holds for the looks' uptime."""
        link = {'ok': 400.0, 'up': 380.0, 'acted': False}
        feeder.UPTIME.update(at=390.0, uptime=4 * 3600)		# long up: the run is steady at once
        feeder.LOOKED.update(proof=None, silent=370.0)
        self.assertFalse(feeder.watch_userland(link, 401))
        self.assertEqual((self.told, self.userland()), (0, []))

    def test_r2_a_boot_s_look_is_stale_when_the_run_turns_steady(self):
        """R2 of the review: after a boot the link answers from 0 s; a look at 20 s finds neither ssh nor a
        prompt - a coprocessor still starting - and the looks after it are refused or cut short. At 120 s
        the run is steady, and that look told octep for a coprocessor that never stopped."""
        link = self.both({'ok': None, 'acted': False}, range(0, 21, 2))
        feeder.LOOKED.update(proof=None, silent=20.0)
        self.both(link, range(22, 131, 2))
        self.assertEqual((self.told, self.userland()), (0, []))

    def test_r3_the_watch_s_own_answer_outranks_an_older_look(self):
        """R3 of the review: the link silent from 200 s, a look begun at 201 s found no prompt, and the
        watch's own ask at 210 s was answered - seconds later; the next poll told octep on the older look."""
        self.console = True
        link = self.silent_since(200)
        feeder.LOOKED.update(proof=150.0, silent=201.0)
        link = self.both(link, [210, 213])
        self.assertEqual(link.get('why'), 'alive')
        self.assertEqual((self.told, self.userland()), (0, []))
        self.assertGreaterEqual(feeder.LOOKED['proof'], 210)

    def test_the_same_checks_as_for_a_silent_link(self):
        link = self.steady_link()
        for name, value in ((feeder.QUIESCE, None), (feeder.GONE, '1'), (feeder.HEARTBEAT, 'dead')):
            values = dict(self.values)
            values[name] = value
            feeder.sysctl = values.get
            feeder.LOOKED.update(proof=100.0, silent=190.0)
            self.assertFalse(feeder.watch_userland(link, 201))
        feeder.sysctl = self.values.get
        self.recovery = True
        self.assertFalse(feeder.watch_userland(link, 201))
        self.assertEqual((self.told, self.userland()), (0, []))


class Looks(unittest.TestCase):
    """The looks in a thread of their own (#1012): one when due, none while the watch weighs a silence,
    and a coprocessor out of reach said once."""

    def setUp(self):
        self.real = {name: getattr(feeder, name) for name in ('say', 'once')}
        self.said, self.ran, self.result = [], 0, None
        feeder.say = self.said.append
        feeder.once = self.look
        feeder.WATCH['holding'] = False

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        feeder.WATCH['holding'] = False

    def look(self):
        self.ran += 1
        return self.result

    def test_a_look_when_due_and_the_next_every_seconds_after(self):
        due, unreachable = feeder.look_if_due(0.0, False, 100.0)
        self.assertEqual((self.ran, due), (1, 100.0 + feeder.EVERY))
        feeder.look_if_due(due, unreachable, 110.0)
        self.assertEqual(self.ran, 1)

    def test_none_while_the_watch_weighs_a_silence(self):
        feeder.WATCH['holding'] = True
        self.assertEqual(feeder.look_if_due(0.0, False, 100.0), (0.0, False))
        self.assertEqual(self.ran, 0)

    def test_out_of_reach_said_when_it_goes_and_when_it_comes_back(self):
        self.result = 'unreachable'
        due, unreachable = feeder.look_if_due(0.0, False, 100.0)
        due, unreachable = feeder.look_if_due(due, unreachable, 200.0)
        self.assertEqual(len(self.said), 1)
        self.result = None
        feeder.look_if_due(due, unreachable, 300.0)
        self.assertIn('answers again', self.said[-1])


class TheLoop(unittest.TestCase):
    """run() itself, on a short clock: the watch's polls go on while a look lasts - the looks in a thread of
    their own (#1012). Under the loop of 2026-11-03 a look stopped them for as long as it lasted (57 s
    measured), and its time was then lent to the link."""

    NAMES = ('signal', 'POLL', 'watch_beat', 'watch_link', 'watch_userland', 'once', 'release_console',
             'looker')

    class Signal:
        """signal, as run() uses it: the handler it sets is kept, not set."""
        SIGTERM = 15
        handlers = {}

        @classmethod
        def signal(cls, number, handler):
            cls.handlers[number] = handler

    def setUp(self):
        self.real = {name: getattr(feeder, name) for name in self.NAMES if hasattr(feeder, name)}
        self.Signal.handlers.clear()
        feeder.WATCH['holding'] = False

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        feeder.WATCH['holding'] = False

    def test_the_polls_go_on_while_a_look_lasts(self):
        """Counted, not timed (finding 9 of a review of the watch, 2026-11-04): the look lasts until ten
        polls have gone by during it, however slow the machine, and its thread ends with it - none outlives
        the test to call what tearDown put back."""
        polls, looks = [], []
        counted, ended = threading.Event(), threading.Event()

        def beat(value):
            # ten polls into the look the test has what it came for; and a bound for a look that never
            # came, so that a test that fails ends
            if looks and len([t for t in polls if t > looks[0]]) >= 10 or len(polls) >= 1000:
                counted.set()
                raise SystemExit
            return value

        def slow():
            looks.append(feeder.time.monotonic())
            counted.wait(30)
            looks.append(feeder.time.monotonic())

        def one_look():
            # looker() as it makes one look; the thread ends with it
            feeder.look_if_due(0.0, False)
            ended.set()

        feeder.signal, feeder.POLL = self.Signal, 0.01
        feeder.watch_beat = beat
        feeder.watch_link = lambda link, now=None: polls.append(feeder.time.monotonic()) or link
        feeder.watch_userland = lambda link, now=None: False
        feeder.once = slow
        feeder.looker = one_look
        feeder.release_console = lambda: None
        with self.assertRaises(SystemExit):
            feeder.run()
        self.assertTrue(ended.wait(30))
        self.assertEqual(len(looks), 2)		# one look: when it began, and when it ended
        self.assertGreaterEqual(len([t for t in polls if looks[0] < t < looks[1]]), 10)

    def test_a_watch_stopped_takes_its_console_mark_back(self):
        """SIGTERM while the watch asked the console left the mark behind, and npu-run.sh refused every
        other caller until the next start (finding 7 of a review of the watch, 2026-11-04)."""
        released = []

        def beat(value):
            raise SystemExit		# the loop's end: only the handler run() sets is wanted here

        feeder.signal = self.Signal
        feeder.watch_beat = beat
        feeder.looker = lambda: None
        feeder.release_console = lambda: released.append(1)
        with self.assertRaises(SystemExit):
            feeder.run()
        del released[:]			# run() takes back a mark left by an earlier watch at its start
        with self.assertRaises(SystemExit):
            self.Signal.handlers[self.Signal.SIGTERM](self.Signal.SIGTERM, None)
        self.assertEqual(released, [1])


class TheStop(unittest.TestCase):
    """What at_the_stop does once the driver takes the coprocessor as gone: SysRq over the console, then
    fastpath.py - but no keys to a coprocessor this feeder has itself said is about to be reset."""

    def setUp(self):
        self.real = {name: getattr(feeder, name) for name in ('say', 'subprocess', 'os', 'sysctl')}
        self.said, self.ran, self.started = [], [], []
        feeder.say = self.said.append
        # the stop octep finds from the heartbeat: its state is dead before the gone latch is set
        self.beat = 'dead'
        feeder.sysctl = lambda name: self.beat if name == feeder.HEARTBEAT else None
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

    def test_a_reset_already_under_way_sends_no_keys(self):
        """F1 of a review of the watch (2026-11-06): since #1030 octep takes the coprocessor as gone the
        moment BAR0's readiness register loses its magic - its reset under way, its console its boot
        firmware's - while its heartbeat is still alive (2026-11-04: gone 1, heartbeat alive). Only this
        feeder's own quiesce held the keys back, and npu-sysrq.sh's guard counts from a dead heartbeat.
        A heartbeat not dead sends none - and fastpath.py is started all the same."""
        for beat in ('alive', 'late', 'waiting', 'off', None):
            del self.ran[:], self.started[:]
            self.beat = beat
            feeder.at_the_stop()
            self.assertEqual(self.ran, [], beat)
            self.assertIn('keys sent now could reach its boot firmware', self.said[-1])
            self.assertEqual(len(self.started), 1)


class ConsoleAsk(unittest.TestCase):
    """The watch's ask of the console alone (#1002): one session that says the coprocessor answers and
    continues its watchdog feeder if it is stopped - npu-run.sh as it answers, faked."""

    def setUp(self):
        self.real = feeder.subprocess.run
        self.calls = []
        feeder.CONSOLE_CONTINUED['pid'] = '9999'	# left by an earlier ask: each ask starts afresh
        self.tmp = tempfile.TemporaryDirectory()
        self.urgent = feeder.URGENT
        feeder.URGENT = os.path.join(self.tmp.name, 'urgent')

    def tearDown(self):
        feeder.subprocess.run = self.real
        feeder.CONSOLE_CONTINUED['pid'] = None
        feeder.URGENT = self.urgent
        self.tmp.cleanup()

    def answer(self, returncode, stdout, stderr=''):
        def run(args, **kw):
            self.calls.append((args, kw))
            return type('Done', (), {'returncode': returncode, 'stdout': stdout, 'stderr': stderr})()
        feeder.subprocess.run = run

    def test_one_session_asks_and_continues(self):
        self.answer(0, 'alive\ncontinued 1834\n')
        self.assertIs(feeder.console_answers(), True)
        self.assertEqual(feeder.CONSOLE_CONTINUED['pid'], '1834')
        (args, kw), = self.calls
        self.assertEqual(args[:3], ['/bin/sh', feeder.NPU_RUN, '4'])
        self.assertTrue(args[3].startswith('echo alive;'))
        self.assertIn(feeder.CONTINUE, args[3])
        self.assertEqual(kw['env']['NPU_RUN_NO_LINK'], '1')

    def test_a_feeder_running_is_left_alone(self):
        self.answer(0, 'alive\n')
        self.assertIs(feeder.console_answers(), True)
        self.assertIsNone(feeder.CONSOLE_CONTINUED['pid'])

    def test_a_console_held_by_another_caller(self):
        self.answer(feeder.BUSY, '')
        self.assertIsNone(feeder.console_answers())
        self.assertIsNone(feeder.CONSOLE_CONTINUED['pid'])

    def test_a_silent_console(self):
        self.answer(feeder.NO_PROMPT, '')
        self.assertIs(feeder.console_answers(), False)

    def test_an_exit_other_than_no_prompt_is_no_answer_either_way(self):
        """F2 of a review of the watch (2026-11-06): npu-run.sh's exit 1 - refused, the host's UART not
        attached again, or its prompt shown and the answer lost to a dropped quote - was taken for a
        silent console, and a live Linux could be quiesced."""
        for rc, out in ((1, ''), (1, 'continued\n'), (0, ''), (0, 'alve\n')):
            self.answer(rc, out)
            self.assertIsNone(feeder.console_answers(), (rc, out))

    def test_an_ask_that_did_not_end_is_no_answer_either_way(self):
        def hangs(args, **kw):
            raise feeder.subprocess.TimeoutExpired(args, kw['timeout'])
        feeder.subprocess.run = hangs
        self.assertIsNone(feeder.console_answers())

    def test_an_answer_cut_before_its_end_marker_still_answers(self):
        """npu-run.sh exits 1 without the end marker, after printing what came before it: the shell did
        answer, and the feeder was continued - not a stop to quiesce for."""
        self.answer(1, 'alive\ncontinued 1834\n')
        self.assertIs(feeder.console_answers(), True)
        self.assertEqual(feeder.CONSOLE_CONTINUED['pid'], '1834')

    def test_a_continued_word_without_a_pid_names_none(self):
        self.answer(0, 'alive\ncontinued\n')
        self.assertIs(feeder.console_answers(), True)
        self.assertIsNone(feeder.CONSOLE_CONTINUED['pid'])
        self.answer(0, 'alive\ncontinued x9\n')
        feeder.console_answers()
        self.assertIsNone(feeder.CONSOLE_CONTINUED['pid'])

    def test_the_ask_goes_ahead_of_the_other_callers(self):
        """A1 of the review of the fix for #1002: URGENT marked for npu-run.sh's other callers, which stand
        aside while it is fresh; its own session waits the seconds it is given, and asks the console twice
        before it takes it as silent."""
        self.answer(0, 'alive\n')
        feeder.console_answers(25)
        (args, kw), = self.calls
        env = kw['env']
        self.assertEqual((env['NPU_RUN_URGENT'], env['NPU_RUN_WAIT'], env['NPU_RUN_TWICE']), ('1', '25', '1'))
        self.assertTrue(os.path.exists(feeder.URGENT))
        feeder.release_console()
        self.assertFalse(os.path.exists(feeder.URGENT))

    def test_alive_is_the_shell_s_line_not_a_word(self):
        self.answer(1, 'kernel: something still alive here\n')
        self.assertIsNone(feeder.console_answers())

    def test_the_ask_at_silent_reads_the_console_once(self):
        """The ask that confirms, at SILENT, a console found silent before: one carriage return - no
        NPU_RUN_TWICE - and a short wait, ahead of the other callers still (finding 5 of a review of the
        watch, 2026-11-04)."""
        self.answer(69, '')
        self.assertIs(feeder.console_answers(5, twice=False), False)
        (args, kw), = self.calls
        self.assertNotIn('NPU_RUN_TWICE', kw['env'])
        self.assertEqual((kw['env']['NPU_RUN_WAIT'], kw['env']['NPU_RUN_URGENT']), ('5', '1'))

    def test_npu_run_s_word_for_the_link_is_kept(self):
        """npu-run.sh says on stderr that the link's ssh took the command, before its exec, when asked to
        (NPU_RUN_SAY_LINK) - and only feeder.py asks: the other callers show what comes on stderr as their
        reason when nothing answers."""
        self.answer(0, '901\n', stderr='over the link\n')
        self.assertEqual(feeder.npu(8, 'true'), '901\n')
        self.assertEqual(feeder.NPU_EXIT, {'rc': 0, 'link': True})
        (args, kw), = self.calls
        self.assertEqual((kw['env']['NPU_RUN_SAY_LINK'], kw['env']['NPU_RUN_TWICE']), ('1', '1'))
        self.answer(feeder.NO_PROMPT, '', stderr='the coprocessor\'s console shows no Linux prompt; '
                                                 'nothing was sent\n')
        self.assertIsNone(feeder.npu(8, 'true'))
        self.assertEqual(feeder.NPU_EXIT, {'rc': feeder.NO_PROMPT, 'link': False})


class TheConsoleSession(unittest.TestCase):
    """npu-run.sh's console session as feeder.py asks for it, away from the appliance: a copy of the script
    whose paths point at stand-ins (as tools/test-mgmt-link.py runs mgmt-link.sh), the console one that
    answers what was typed. Its reads are made at once, after the keys of their turn, so that nothing here
    waits on a clock (a review of the watch, 2026-11-04)."""

    SCRIPT = os.path.join(XGS, 'npu-run.sh')

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        for sub in (('bin',), ('lib', 'xgs'), ('lib', 'npuctl')):
            os.makedirs(self.path(*sub))
        self.dev, self.kept = self.path('cuau2'), self.path('kept')
        self.pause, self.urgent = self.path('pause'), self.path('urgent')

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, *names):
        return os.path.join(self.dir, *names)

    @staticmethod
    def write(path, text, mode=0o755):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        os.chmod(path, mode)
        return path

    def kept_text(self):
        if not os.path.exists(self.kept):
            return ''
        with open(self.kept, encoding='utf-8') as f:
            return f.read()

    def run_script(self, first='', again='', interrupted='', ssh=1, mark=None, queued=False, **env):
        """npu-run.sh 4 'echo alive', the console printing first at its first read, and at a later one
        interrupted after a Ctrl-C and a carriage return, or again after a carriage return alone; the
        link's ssh exiting ssh; the watch's mark made mark seconds before (None: no mark), or while this
        caller queued for the lock (queued). What it did, and the keys it typed."""
        for name in ('reads', 'irq', 'kept', 'urgent'):
            if os.path.exists(self.path(name)):
                os.unlink(self.path(name))
        open(self.dev, 'w').close()
        if mark is not None:
            open(self.urgent, 'w').close()
            os.utime(self.urgent, (time.time() - mark, time.time() - mark))
        bin_ = self.path('bin')
        stubs = {
            '/sbin/sysctl': self.write(os.path.join(bin_, 'sysctl'),
                                       '#!/bin/sh\ncase "$2" in\ndev.octep.0.gone) echo 0 ;;\n'
                                       'dev.octep.0.sdp.hs_state) echo completed ;;\n*) exit 1 ;;\nesac\n'),
            '/usr/bin/lockf': self.write(os.path.join(bin_, 'lockf'),
                                         '#!/bin/sh\n' + ('touch "%s"\n' % self.urgent if queued else '') +
                                         'shift 4\nexec "$@"\n'),
            # stat -f %m <file>: its mtime, in seconds - as date -r gives it with GNU's tools and BSD's
            '/usr/bin/stat': self.write(os.path.join(bin_, 'stat'),
                                        '#!/bin/sh\n[ -e "$3" ] || exit 1\nexec date -r "$3" +%s\n'),
            '/bin/pgrep': self.write(os.path.join(bin_, 'pgrep'), '#!/bin/sh\nexit 1\n'),
            '/usr/bin/vmstat': self.write(os.path.join(bin_, 'vmstat'),
                                          '#!/bin/sh\nn=$(( $(cat "{0}" 2>/dev/null || echo 0) + 1 ))\n'
                                          'echo "$n" > "{0}"\n'
                                          'echo "irq4: uart2 $n 0"\n'.format(self.path('irq'))),
            '/usr/local/bin/python3': self.write(os.path.join(bin_, 'keep'),
                                                 '#!/bin/sh\ncat "$3" >> "%s"\n' % self.kept),
        }
        self.write(os.path.join(bin_, 'stty'), '#!/bin/sh\nexit 0\n')
        self.write(os.path.join(bin_, 'sleep'), '#!/bin/sh\nexit 0\n')
        # the console: what it prints when read, after the keys typed so far - a Ctrl-C and a carriage
        # return last, or not
        self.write(os.path.join(bin_, 'console'),
                   '#!/bin/sh\nn=$(( $(cat "{reads}" 2>/dev/null || echo 0) + 1 ))\necho "$n" > "{reads}"\n'
                   'case "$(od -An -tx1 "{dev}" | tr -d \' \\n\')" in\n'
                   '*030d) printf \'%b\' "$CONSOLE_INTERRUPTED" ;;\n'
                   '*) if [ "$n" = 1 ]; then printf \'%b\' "$CONSOLE_FIRST"; '
                   'else printf \'%b\' "$CONSOLE_AGAIN"; fi ;;\nesac\n'.format(reads=self.path('reads'),
                                                                               dev=self.dev))
        self.write(self.path('lib', 'xgs', 'board.sh'), 'echo "XGS3300 octeontx"\n', 0o644)
        self.write(self.path('lib', 'npuctl', 'mgmt-link.sh'),
                   '#!/bin/sh\n[ "$2" = true ] && exit %d\necho "over ssh: $2"\n' % ssh)
        with open(self.SCRIPT, encoding='utf-8') as f:
            text = f.read()
        swaps = [('LIB=/usr/local/opnsense/scripts', 'LIB=' + self.path('lib')),
                 ('DEV=/dev/cuau2', 'DEV=' + self.dev),
                 ('LOCK=/var/run/xgs-npu-console.lock', 'LOCK=' + self.path('lock')),
                 ('URGENT=/var/run/xgs-npu-console.urgent', 'URGENT=' + self.urgent),
                 ('TMP=/var/run/xgs-npu-run.$$', 'TMP=' + self.path('run.$$')),
                 ('/var/run/xgs-console-log.pause', self.pause),
                 ('UNIT=${DEV#/dev/cuau}', 'UNIT=2'),
                 ('[ -c "${DEV}" ]', '[ -e "${DEV}" ]'),
                 # read at once, after the keys of its turn (below), not by a cat beside them
                 ('/usr/bin/timeout $(( SECS + 6 )) cat "${DEV}" > "${TMP}" 2>/dev/null &', 'true &'),
                 ('> "${DEV}"', '>> "${DEV}"'),
                 ('printf \'\\r\' >> "${DEV}"; sleep 1',
                  'printf \'\\r\' >> "${DEV}"; sleep 1; console > "${TMP}"')]
        for real, fake in swaps + list(stubs.items()):
            self.assertIn(real, text)
            text = text.replace(real, fake)
        script = self.write(self.path('npu-run.sh'), text, 0o644)
        environ = {name: value for name, value in os.environ.items() if not name.startswith('NPU_RUN')}
        environ.update(PATH=bin_ + os.pathsep + os.environ.get('PATH', ''), NPU_RUN_TWICE='1',
                       CONSOLE_FIRST=first, CONSOLE_AGAIN=again, CONSOLE_INTERRUPTED=interrupted, **env)
        done = subprocess.run(['/bin/sh', script, '4', 'echo alive'], env=environ, capture_output=True,
                              text=True, timeout=60)
        with open(self.dev, 'rb') as f:
            return done, f.read()

    def test_asked_again_a_ctrl_c_goes_before_the_carriage_return(self):
        """A shell left in a continuation prompt - a quote of a command line lost on the way - answers a
        carriage return with its continuation prompt again, and a Ctrl-C with its prompt: a console that is
        alive, which a carriage return alone took for a stopped Linux (finding 2 of the review)."""
        done, keys = self.run_script(first='\r\n> ', again='\r\n> ', interrupted='^C\r\nroot@marvell:~# ')
        self.assertTrue(keys.startswith(b'\r\x03\r'), keys)
        self.assertIn(b'echo "---NPU""BEGIN---"; echo alive;', keys)
        self.assertNotEqual(done.returncode, feeder.NO_PROMPT)

    def test_both_reads_go_to_the_console_s_log(self):
        """The second read took TMP over, and the first - the evidence behind a no prompt, and so behind a
        quiesce - was lost to the console's log (finding 8 of the review; #819)."""
        done, keys = self.run_script(first='\r\nPassword: ', interrupted='^C\r\n')
        self.assertEqual((done.returncode, keys), (feeder.NO_PROMPT, b'\r\x03\r'))
        self.assertIn('no Linux prompt', done.stderr)
        kept = self.kept_text()
        self.assertIn('Password: ', kept)
        self.assertGreater(kept.find('^C'), kept.find('Password: '))

    def test_a_mark_from_the_future_is_no_mark(self):
        """With the clock stepped back, a mark left behind was fresh for as long as the clock took to catch
        up, and every caller was refused (finding 7 of the review)."""
        done, keys = self.run_script(first='\r\nroot@marvell:~# ', mark=-3600)
        self.assertNotEqual(done.returncode, feeder.BUSY)
        self.assertIn(b'echo "---NPU""BEGIN---"; echo alive;', keys)
        done, keys = self.run_script(first='\r\nroot@marvell:~# ', mark=10)
        self.assertEqual((done.returncode, keys), (feeder.BUSY, b''))
        self.assertIn('kept for the watch', done.stderr)

    def test_a_caller_refused_under_the_lock_lets_the_logger_back_in(self):
        """The mark made while this caller queued for the lock: refused once it has it, and the console's
        logger let back in - it stood aside until another session's end, or five minutes (finding 7 of the
        review)."""
        done, keys = self.run_script(first='\r\nroot@marvell:~# ', queued=True)
        self.assertEqual((done.returncode, keys), (feeder.BUSY, b''))
        self.assertIn('kept for the watch', done.stderr)
        self.assertFalse(os.path.exists(self.pause))

    def test_the_link_is_named_on_stderr_to_feeder_py_alone(self):
        done, keys = self.run_script(ssh=0, NPU_RUN_SAY_LINK='1')
        self.assertEqual((done.returncode, done.stdout, done.stderr), (0, 'over ssh: echo alive\n',
                                                                       'over the link\n'))
        done, keys = self.run_script(ssh=0)
        self.assertEqual((done.returncode, done.stderr, keys), (0, '', b''))


if __name__ == '__main__':
    unittest.main(verbosity=2)
