#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Which bridge commands mcp2210.py sends, with the bridge replaced by a recorder.

Command bytes are the MCP2210's: 0x20 get and 0x21 set chip settings, 0x30 set and 0x31 get the
pins' values, 0x32 set and 0x33 get the pins' direction, 0x61 read the NVRAM settings. 0x32 was
sent as a read until 2026-11-05 and set every pin to an output; nothing here may send it.
"""

import os
from pathlib import Path
import runpy
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
TOOL = Path(os.environ.get('MCP2210_TOOL', ROOT / 'src/opnsense/scripts/npuctl/mcp2210.py'))
WRITES = {0x21, 0x30, 0x32, 0x40, 0x42, 0x51, 0x60, 0x11}
READS = {0x10, 0x20, 0x22, 0x31, 0x33, 0x41, 0x50, 0x61, 0x76}


class Commands(unittest.TestCase):
    def load(self, serial='XYZAMDA0201-000312345678'):
        os.environ['XGS_PLANAR_SERIAL'] = serial
        self.addCleanup(os.environ.pop, 'XGS_PLANAR_SERIAL', None)
        tool = runpy.run_path(str(TOOL))
        sent = []

        def xfer(fh, tx):
            sent.append(tx[0])
            rx = bytearray(64)
            rx[0] = tx[0]
            if tx[0] == 0x20:
                rx[4:13] = bytes(9)            # every pin a GPIO
                rx[13], rx[14] = 0x01, 0x00
                rx[15], rx[16] = 0xd6, 0x01
            return bytes(rx)

        main = tool['main']
        main.__globals__['xfer'] = xfer
        clock = main.__globals__['time']
        self.addCleanup(setattr, clock, 'sleep', clock.sleep)
        clock.sleep = lambda s: None
        node = tempfile.NamedTemporaryFile(delete=False)
        node.close()
        self.addCleanup(os.unlink, node.name)
        main.__globals__['DEV'] = node.name
        return tool, main, sent

    def run_cmd(self, *argv, serial='XYZAMDA0201-000312345678'):
        tool, main, sent = self.load(serial)
        rc = main(['mcp2210.py'] + list(argv))
        return rc, sent, tool

    def test_no_command_sets_the_direction_as_a_read(self):
        for argv in (('status',), ('pulse',), ('release',), ('probe',), ('nvram',)):
            with self.subTest(argv=argv):
                rc, sent, _ = self.run_cmd(*argv)
                self.assertNotIn(0x32, sent)

    def test_the_live_reads_are_reads(self):
        rc, sent, _ = self.run_cmd('pulse')
        self.assertEqual(rc, 0)
        # the pulse: reads, a hold written, reads, a release written, reads
        self.assertTrue(set(sent) <= {0x20, 0x21, 0x30, 0x31, 0x33}, sent)
        self.assertGreaterEqual(sent.count(0x31), 4)
        self.assertEqual(sent.count(0x21), 2)
        self.assertEqual(sent.count(0x30), 2)

    def test_status_and_probe_only_read(self):
        for argv in (('status',), ('probe',), ('nvram',)):
            with self.subTest(argv=argv):
                rc, sent, _ = self.run_cmd(*argv)
                self.assertEqual(rc, 0)
                self.assertFalse(set(sent) & WRITES, sent)

    def test_the_probe_list_holds_reads_only(self):
        _, _, tool = self.run_cmd('status')
        self.assertFalse(set(tool['SAFE_PROBE']) & WRITES)
        self.assertTrue(set(tool['SAFE_PROBE']) <= READS)

    def test_a_board_without_values_drives_nothing(self):
        rc, sent, _ = self.run_cmd('pulse', serial='XYZAMDA0202-000412345678')
        self.assertEqual(rc, 2)
        self.assertEqual(sent, [])


if __name__ == '__main__':
    unittest.main()
