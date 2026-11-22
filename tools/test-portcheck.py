#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/portcheck.py's reading of the XGS 136's coprocessor ports, with what the coprocessor prints
made up: a line of its ports short of its interface - a field its shell printed empty - is said and the
others read, where until 2026-11-06 it raised IndexError, which the problem reporter took in silence and
ran the check again at its next look, a minute over the console every five minutes (the review of
2026-11-06, D10). Nothing here comes from an appliance.

    python3 tools/test-portcheck.py
"""
import importlib.util
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs')
sys.path.insert(0, SCRIPTS)
spec = importlib.util.spec_from_file_location('portcheck', os.path.join(SCRIPTS, 'portcheck.py'))
portcheck = importlib.util.module_from_spec(spec)
spec.loader.exec_module(portcheck)

WHOLE = 'soc 1 3 eth1 cp0/config-space/ethernet@0/eth1 de:ad:be:ef:00:01'
SHORT = 'soc 2 3'


class ShortLine(unittest.TestCase):

    def setUp(self):
        self.real = {name: getattr(portcheck, name) for name in ('run', 'host_macs', 'laggs', 'link')}
        self.said, self.reason = '', ''

        def run(argv, timeout=90, why=False):
            text = self.said if argv[:2] == ['/bin/sh', portcheck.NPU_RUN] else ''
            return (text, self.reason) if why else text
        portcheck.run = run
        portcheck.host_macs = lambda prefix: {}
        portcheck.laggs = lambda: {}
        portcheck.link = lambda dev: 'active'
        self.models = portcheck.models
        portcheck.models = type('models', (), {'ports': staticmethod(lambda assembly: [])})

    def tearDown(self):
        for name, value in self.real.items():
            setattr(portcheck, name, value)
        portcheck.models = self.models

    def test_a_short_line_is_said_and_the_rest_read(self):
        self.said = 'answered\ncount 2\n%s\n%s\nfinished\n' % (WHOLE, SHORT)
        found = portcheck.check_armada('201')
        self.assertIn("a line of the coprocessor's ports could not be read: %s" % SHORT, found['lines'])
        self.assertEqual([r['tag'] for r in found['ports'] if 'tag' in r], [1])

    def test_whole_lines_read_as_before(self):
        self.said = 'answered\ncount 1\n%s\nfinished\n' % WHOLE
        found = portcheck.check_armada('201')
        self.assertFalse([l for l in found['lines'] if 'could not be read' in l])
        self.assertEqual([r['tag'] for r in found['ports'] if 'tag' in r], [1])

    def test_an_answer_cut_short_is_not_read_as_the_list(self):
        """An XGS 87's console ran the read and not npu-run.sh's end marker (its support bundle, 2026-11-21):
        the switch link alone came, and a check that compared no front port said they agree."""
        self.said = 'answered\ncount 1\nsoc 0 2 eth0 cp0/config-space/ethernet@0/eth0 de:ad:be:ef:00:00\n'
        self.reason = "the coprocessor's console did not answer within 60s"
        found = portcheck.check_armada('201')
        self.assertEqual((found['verdict'], found['why'], found['ports']), ('unread', 'cut_short', []))
        self.assertEqual(found['lines'], ["the coprocessor answered, but its answer was cut short: "
                                          "the coprocessor's console did not answer within 60s"])

    def test_the_switch_link_alone_is_not_an_agreement(self):
        """An XGS 87's agent listed only its link to the switch (its support bundle, 2026-11-21): read whole,
        no front port was compared, and 0 of 0 "agreed" - which the untested-model report would keep as the
        table confirmed."""
        self.said = 'answered\ncount 1\nsoc 0 2 eth0 cp0/config-space/ethernet@0/eth0 de:ad:be:ef:00:00\nfinished\n'
        found = portcheck.check_armada('201')
        self.assertEqual((found['verdict'], found['why'], found['ports']), ('unread', 'no_front_ports', []))
        self.assertIn('no front port (num_of_ports 1)', found['lines'][0])


if __name__ == '__main__':
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
