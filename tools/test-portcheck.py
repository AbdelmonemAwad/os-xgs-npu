#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/portcheck.py's reading of the XGS 136's coprocessor ports, with what the coprocessor prints
made up: a line of its ports short of its interface - a field its shell printed empty - is said and the
others read, where until 2026-11-06 it raised IndexError, which the problem reporter took in silence and
ran the check again at its next look, a minute over the console every five minutes (the review of
2026-11-06, D10). And an answer with the coprocessor's link to the switch alone in it: a list that could
not be read on a model with front ports on the coprocessor's own MACs, and the whole answer on one whose
front ports are all behind the switch (#880, #1177). Nothing here comes from an appliance.

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
# the agent's whole answer, with its link to the switch alone in it
SWITCH_ONLY = 'answered\ncount 1\nsoc 0 2 eth0 cp0/config-space/ethernet@0/eth0 de:ad:be:ef:00:00\nfinished\n'


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
        self.table = []		# the model's ports as models.ports() gives them: none unless a test gives some
        portcheck.models = type('models', (), {'ports': staticmethod(lambda assembly: list(self.table))})

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
        table confirmed. On a model with front ports on the coprocessor's own MACs - two of the XGS 136's
        here - that answer is a list without them, which the check could not read."""
        self.table = [{'device': 'npup1', 'label': 'Port1', 'tag': 0x8100, 'unit': 1},
                      {'device': 'npup9', 'label': 'Port9', 'tag': 1, 'unit': 9}]
        self.said = SWITCH_ONLY
        found = portcheck.check_armada('201')
        self.assertEqual((found['verdict'], found['why'], found['ports']), ('unread', 'no_front_ports', []))
        self.assertIn('no front port (num_of_ports 1)', found['lines'][0])
        # and with no table at all, as before
        self.table = []
        found = portcheck.check_armada('201')
        self.assertEqual((found['verdict'], found['why']), ('unread', 'no_front_ports'))

    def test_the_switch_link_alone_is_the_whole_answer_where_every_port_is_behind_the_switch(self):
        """An XGS 107's and an XGS 107w's every boot filed that answer as a coprocessor that did not answer
        (#880, #1177): their front ports are all behind the switch, and the agent has nothing else to list.
        The coprocessor answered; there is nothing of its own to compare, and nothing is said to agree."""
        self.table = [{'device': 'npup%d' % n, 'label': 'Port%d' % n, 'tag': 0x8000 | n << 8, 'unit': n}
                      for n in range(1, 9)] + [{'device': 'npup9', 'label': 'F1', 'tag': 0x8900, 'unit': 9}]
        self.said = SWITCH_ONLY
        found = portcheck.check_armada('200')
        self.assertEqual(found['verdict'], 'switch_only')
        self.assertNotIn('why', found)
        self.assertEqual(found['ports'], [{'tag': 0, 'eth': 'eth0', 'mac': 'CP0 eth0', 'address': '0:0',
                                           'switch_link': True}])
        self.assertEqual(found['lines'][0], 'coprocessor port 0 (eth0, CP0 eth0 = 0:0): the link to the switch')
        self.assertIn('(num_of_ports 1): every front port of this model is behind the switch', found['lines'][-1])
        # the same model's coprocessor not answering is still that, and an answer cut short too
        self.said, self.reason = '', "the coprocessor's console shows no Linux prompt; nothing was sent"
        found = portcheck.check_armada('200')
        self.assertEqual((found['verdict'], found['why']), ('unread', 'no_answer'))
        self.said = SWITCH_ONLY.replace('finished\n', '')
        found = portcheck.check_armada('200')
        self.assertEqual((found['verdict'], found['why']), ('unread', 'cut_short'))

    def test_which_models_have_every_front_port_behind_the_switch(self):
        """From the table itself: the boards AMDA0200 and AMDA0208 put every front port behind the switch;
        every other ARMADA board has some on the coprocessor's own MACs."""
        behind = sorted(a for a, (_, family, _, _) in self.models.MODELS.items()
                        if family == 'armada' and portcheck.behind_the_switch(
                            {p['device']: p for p in self.models.ports(a)}))
        self.assertEqual(behind, ['AMDA0200-0001', 'AMDA0200-0002', 'AMDA0200-0003', 'AMDA0200-0004',
                                  'AMDA0208-0001', 'AMDA0208-0002'])
        self.assertFalse(portcheck.behind_the_switch({}))


if __name__ == '__main__':
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
