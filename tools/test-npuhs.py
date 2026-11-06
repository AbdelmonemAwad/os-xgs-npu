#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/npuctl/npuhs.py's endpoint away from the appliance: found by the id its driver probes for,
wherever it sits on the bus, and refused - with nothing read - when it is not there. Until 2026-11-06
it was read at pci0:1:0:0 whatever sat there (A12). pciconf is a stand-in; nothing is mapped.

    python3 tools/test-npuhs.py
"""
import importlib.util
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
NPUHS = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'npuctl', 'npuhs.py')

# pciconf -l as FreeBSD prints it, the endpoint off bus 1 and another Marvell device at pci0:1:0:0 -
# its subdevice the endpoint's own number - and the older form, with chip=.
LISTING = ('hostb0@pci0:0:0:0:\tclass=0x060000 rev=0x11 hdr=0x00 vendor=0x8086 device=0x1980 '
           'subvendor=0x8086 subdevice=0x7270\n'
           'none1@pci0:1:0:0:\tclass=0x020000 rev=0x01 hdr=0x00 vendor=0x11ab device=0x4380 '
           'subvendor=0x11ab subdevice=0x7080\n'
           'npuep0@pci0:3:0:0:\tclass=0x020000 rev=0x00 hdr=0x00 vendor=0x11ab device=0x7080 '
           'subvendor=0x11ab subdevice=0x7080\n')
OLD = 'none0@pci0:3:0:0:\tclass=0x020000 card=0x708011ab chip=0x708011ab rev=0x00 hdr=0x00\n'
BARS = ('npuep0@pci0:3:0:0:\tclass=0x020000 rev=0x00 hdr=0x00 vendor=0x11ab device=0x7080 '
        'subvendor=0x11ab subdevice=0x7080\n'
        '    bar   [10] = type Memory, range 64, base 0xf6000000, size 1048576, enabled\n'
        '    bar   [18] = type Memory, range 64, base 0xf4000000, size 16777216, enabled\n'
        '    bar   [20] = type Memory, range 32, base 0xf5000000, size 16777216, enabled\n')

PCICONF = '''#!/bin/sh
echo "pciconf $*" >> "${CALLS}"
case "$1" in
-l) cat "${LISTING}" ;;
-lb) [ "$2" = pci0:3:0:0 ] && cat "${BARS}" ;;
esac
exit 0
'''


def load():
    spec = importlib.util.spec_from_file_location('npuhs_under_test', NPUHS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Endpoint(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.tmp.name
        self.calls = os.path.join(d, 'calls')
        self.listing = os.path.join(d, 'listing')
        bars = os.path.join(d, 'bars')
        for path, text in ((self.calls, ''), (bars, BARS)):
            with open(path, 'w', newline='\n') as f:
                f.write(text)
        stub = os.path.join(d, 'pciconf')
        with open(stub, 'w', newline='\n') as f:
            f.write(PCICONF)
        os.chmod(stub, 0o755)
        self.env = dict(os.environ)
        os.environ.update(PATH=d + os.pathsep + os.environ.get('PATH', ''), CALLS=self.calls,
                          LISTING=self.listing, BARS=bars)
        self.npuhs = load()

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.env)
        self.tmp.cleanup()

    def listed(self, text):
        with open(self.listing, 'w', newline='\n') as f:
            f.write(text)

    def asked(self):
        with open(self.calls) as f:
            return f.read().splitlines()

    def test_the_endpoint_is_found_by_its_id_off_bus_one(self):
        self.listed(LISTING)
        self.assertEqual(self.npuhs.bars()[0x18], (0xf4000000, 16777216))
        self.assertEqual(self.asked(), ['pciconf -l', 'pciconf -lb pci0:3:0:0'])

    def test_the_older_listing_is_read_too(self):
        self.listed(OLD)
        self.assertEqual(self.npuhs.bars()[0x18], (0xf4000000, 16777216))
        self.assertEqual(self.asked()[-1], 'pciconf -lb pci0:3:0:0')

    def test_no_endpoint_is_refused_before_anything_is_read(self):
        self.listed(LISTING.splitlines(True)[0] + LISTING.splitlines(True)[1])
        with self.assertRaises(SystemExit) as refused:
            self.npuhs.bars()
        self.assertIn('no endpoint 11ab:7080', str(refused.exception))
        self.assertEqual(self.asked(), ['pciconf -l'])


if __name__ == '__main__':
    unittest.main(verbosity=1)
