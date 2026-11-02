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
import unittest

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


if __name__ == '__main__':
    unittest.main(verbosity=1)
