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

    def __init__(self, uptime, pid, state, pstore=''):
        self.uptime, self.pid, self.state, self.pstore = uptime, pid, state, pstore
        self.asked = []

    def __call__(self, seconds, command):
        self.asked.append(command)
        if command == feeder.ASK:
            out = '%d\npid %s\n' % (self.uptime, self.pid or 'none')
            if self.pid:
                out += 'State:\t%s (whatever)\n' % self.state
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
        self.real = {name: getattr(feeder, name) for name in ('say', 'npu', 'console_keep', 'gone', 'at_the_stop')}
        self.tmp = tempfile.TemporaryDirectory()
        feeder.STATE = os.path.join(self.tmp.name, 'feeder.json')
        self.said = []
        feeder.say = self.said.append
        self.console = []
        feeder.console_keep = lambda text, who: self.console.append((who, text))

    def tearDown(self):
        for name, value in self.real.items():
            setattr(feeder, name, value)
        self.tmp.cleanup()

    def kept(self):
        with open(feeder.STATE, encoding='utf-8') as f:
            return json.load(f)

    def test_a_running_feeder_is_left_alone(self):
        feeder.npu = Coprocessor(900, '2448', 'S')
        self.assertIsNone(feeder.once())
        self.assertEqual(feeder.npu.asked, [feeder.ASK, feeder.PSTORE])
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
        self.assertEqual(feeder.npu.asked, [feeder.ASK])
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
        self.assertEqual(asked, [feeder.ASK])
        self.assertFalse(os.path.exists(feeder.STATE))

    def test_an_answer_cut_short_is_unreachable(self):
        feeder.npu = lambda seconds, command: '901\n'
        self.assertEqual(feeder.once(), 'unreachable')


if __name__ == '__main__':
    unittest.main(verbosity=2)
