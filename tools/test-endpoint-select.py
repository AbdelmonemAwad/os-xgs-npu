#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""contrib/npuep/reload.sh and scripts/xgs/recover.sh find the XGS 136's endpoint in `pciconf -l` by the
id its driver probes for, 11ab:7080, in both forms pciconf has printed it - not by Marvell's vendor id
alone, which took the first Marvell device listed, whatever it was (the review of 2026-11-06, A14).

Each script's own selection line is taken out of it and run by /bin/sh with a pciconf that prints a
made-up listing. Nothing here comes from an appliance.

    python3 tools/test-endpoint-select.py
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = ('contrib/npuep/reload.sh', 'src/opnsense/scripts/xgs/recover.sh')
SELECTION = re.compile(r"SEL=\$\(pciconf -l .*?\}'\)", re.S)

# The endpoint in the older form (chip=) and the newer (vendor= device=), and another Marvell device.
OLDER = 'npuep0@pci0:1:0:0:\tclass=0x020000 card=0x00000000 chip=0x708011ab rev=0x00 hdr=0x00\n'
NEWER = ('npuep0@pci0:1:0:0:\tclass=0x020000 rev=0x00 hdr=0x00 vendor=0x11ab device=0x7080 '
         'subvendor=0x11ab subdevice=0x7080\n')
OTHER = ('none3@pci0:0:8:1:\tclass=0x0c0330 rev=0x00 hdr=0x00 vendor=0x11ab device=0x0100 '
         'subvendor=0x11ab subdevice=0x0100\n')


def selected(script, listing):
    """What the script's selection line puts in SEL, pciconf printing listing."""
    with open(os.path.join(ROOT, script), encoding='utf-8') as f:
        found = SELECTION.search(f.read())
    if not found:
        raise AssertionError('no selection line in %s' % script)
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, 'listing'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(listing)
        with open(os.path.join(d, 'pciconf'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\ncat "%s"\n' % os.path.join(d, 'listing'))
        os.chmod(os.path.join(d, 'pciconf'), 0o755)
        with open(os.path.join(d, 'run.sh'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\n%s\necho "${SEL}"\n' % found.group(0))
        env = dict(os.environ, PATH=d + os.pathsep + os.environ.get('PATH', '/usr/bin:/bin'))
        out = subprocess.run(['/bin/sh', os.path.join(d, 'run.sh')], env=env, capture_output=True,
                             text=True, check=True)
        return out.stdout.strip()


class Selection(unittest.TestCase):

    def test_the_endpoint_in_either_form(self):
        for script in SCRIPTS:
            for listing in (OLDER, NEWER):
                self.assertEqual(selected(script, listing), 'pci0:1:0:0', (script, listing))

    def test_another_marvell_device_listed_first_is_not_taken(self):
        """Until 2026-11-06 vendor=0x11ab alone matched it, and pci0:0:8:1 was taken."""
        for script in SCRIPTS:
            self.assertEqual(selected(script, OTHER + NEWER), 'pci0:1:0:0', script)

    def test_no_endpoint_is_nothing(self):
        for script in SCRIPTS:
            self.assertEqual(selected(script, OTHER), '', script)


if __name__ == '__main__':
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
