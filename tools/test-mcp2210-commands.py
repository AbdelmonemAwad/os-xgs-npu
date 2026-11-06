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
    def load(self, serial='XYZAMDA0201-000312345678', fail_read=None):
        os.environ['XGS_PLANAR_SERIAL'] = serial
        self.addCleanup(os.environ.pop, 'XGS_PLANAR_SERIAL', None)
        tool = runpy.run_path(str(TOOL))
        sent = []
        # The bridge stood in for keeps the levels written to it (0x30) and gives them back (0x31);
        # fail_read is the number of the 0x31 read that goes unanswered, as the transport drops one.
        pins = {'value': 0x0001, 'reads': 0}

        def xfer(fh, tx):
            sent.append(tx[0])
            rx = bytearray(64)
            rx[0] = tx[0]
            if tx[0] == 0x20:
                rx[4:13] = bytes(9)            # every pin a GPIO
                rx[13], rx[14] = 0x01, 0x00
                rx[15], rx[16] = 0xd6, 0x01
            if tx[0] == 0x30:
                pins['value'] = tx[4] | (tx[5] << 8)
            if tx[0] == 0x31:
                pins['reads'] += 1
                if fail_read == pins['reads']:
                    raise IOError('no reply')
                rx[4], rx[5] = pins['value'] & 0xff, pins['value'] >> 8
            if tx[0] == 0x33:
                rx[4], rx[5] = 0xd6, 0x01
            return bytes(rx)
        self.pins = pins

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

    def run_cmd(self, *argv, serial='XYZAMDA0201-000312345678', fail_read=None):
        tool, main, sent = self.load(serial, fail_read)
        rc = main(['mcp2210.py'] + list(argv))
        return rc, sent, tool

    def test_a_read_lost_while_held_still_releases(self):
        # 2026-11-06: the read after the hold raised, the release was never written, and the
        # coprocessor stayed in reset for the whole boot. The second 0x31 is the one after the hold.
        rc, sent, tool = self.run_cmd('pulse', fail_read=2)
        self.assertEqual(sent.count(0x30), 2, sent)            # the hold and the release, both
        release = tool['NPU_VALUES'][201]['release']
        self.assertEqual(self.pins['value'] & tool['NPU_MASK'], release & tool['NPU_MASK'])
        self.assertEqual(rc, 0)

    def test_a_pulse_whose_lines_stay_held_fails(self):
        # the pulse is good only when the lines read back released
        tool, main, sent = self.load()
        write = main.__globals__['set_gpio_value']
        hold = tool['NPU_VALUES'][201]['hold']
        main.__globals__['set_gpio_value'] = lambda fh, value: write(fh, hold)    # stuck at hold
        self.assertEqual(main(['mcp2210.py', 'pulse']), 1)

    def test_boards_200_and_224_are_driven_only_when_asked(self):
        for serial in ('XYZAMDA0200-000412345678', 'XYZAMDA0224-000112345678'):
            with self.subTest(serial=serial):
                rc, sent, _ = self.run_cmd('pulse', serial=serial)
                self.assertEqual(rc, 2)
                self.assertEqual(sent, [])
                os.environ['XGS_NPUCTL_RND'] = '1'
                try:
                    rc, sent, _ = self.run_cmd('pulse', serial=serial)
                finally:
                    del os.environ['XGS_NPUCTL_RND']
                self.assertEqual(sent.count(0x30), 2)

    def test_board_208_is_driven(self):
        rc, sent, _ = self.run_cmd('pulse', serial='XYZAMDA0208-000112345678')
        self.assertEqual(rc, 0)
        self.assertEqual(sent.count(0x30), 2)

    def test_the_bridge_is_found_by_its_usb_ids(self):
        tool, main, _ = self.load()
        nodes = {'hidraw0': 'page=0x0001 usage=0x0006 bus=0x03 vendor=0x046d product=0xc31c version=0x0110',
                 'hidraw1': 'page=0x0000 usage=0x0000 bus=0x03 vendor=0x04d8 product=0x00de version=0x0002'}
        find = main.__globals__['find_bridge']
        g = find.__globals__
        listdir, run = g['os'].listdir, g['subprocess'].run
        self.addCleanup(setattr, g['os'], 'listdir', listdir)
        self.addCleanup(setattr, g['subprocess'], 'run', run)
        g['os'].listdir = lambda path: sorted(nodes) + ['null'] if path == '/dev' else listdir(path)

        class Done:
            def __init__(self, out):
                self.stdout = out

        g['subprocess'].run = lambda argv, **kw: Done(nodes.get('hidraw' + argv[2].split('.')[2], ''))
        self.assertEqual(find(), '/dev/hidraw1')
        nodes.pop('hidraw1')
        self.assertIsNone(find())

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
