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
    def test_the_default_says_so_and_nothing_more(self):
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
        self.assertEqual(fastpath.CHOICES, (0, 2, 5, 10))


if __name__ == '__main__':
    unittest.main(verbosity=1)
