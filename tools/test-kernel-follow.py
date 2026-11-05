#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""kernel-follow.sh away from the appliance, where the kernel's sources cannot be had: what it says of
the next boot with a module installed and with none (#818). logger and sysctl are stand-ins in a copy of
the script, uname answers the kernel the run follows, and every other path is a variable it has.

    python3 tools/test-kernel-follow.py
"""
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'install', 'kernel-follow.sh')
KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'


class NoSources(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        os.makedirs(os.path.join(self.dir, 'modules'))
        with open(os.path.join(self.dir, 'kernel'), 'w') as f:
            f.write('\0\0' + KERNEL + '\n\0')

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def follow(self):
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        text = (text.replace('/usr/bin/logger', self.stub('logger', 'exit 0'))
                .replace('/sbin/sysctl', self.stub('sysctl', 'exit 1')))
        script = os.path.join(self.dir, 'kernel-follow.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        d = self.dir
        env = dict(os.environ, OCTEP_FOLLOW_LOCKED='1', FORCE='1', NOFETCH='1',
                   MODDIR=os.path.join(d, 'modules'), KERNEL=os.path.join(d, 'kernel'),
                   KERNEL_OLD=os.path.join(d, 'kernel.old'), NEXTBOOT=os.path.join(d, 'nextboot'),
                   PENDINGDIR=os.path.join(d, 'pending'), PENDING_KERNEL=os.path.join(d, 'kernel.pending'),
                   PINNED=os.path.join(d, 'pinned'), SAID=os.path.join(d, 'said'),
                   FETCH=self.stub('fetch-sources.sh', 'echo "no sources for c962f76493b6 here, '
                                   'and NONET=1 forbids fetching them."; exit 1'),
                   UNAME=self.stub('uname', 'echo "%s"' % KERNEL))
        return subprocess.run(['/bin/sh', script], env=env, capture_output=True, text=True, timeout=60)

    def test_with_no_module_it_says_no_front_ports_and_arms_nothing(self):
        done = self.follow()
        said = done.stdout + done.stderr
        self.assertEqual(done.returncode, 1)
        self.assertIn('could not get kernel sources for ' + KERNEL, said)
        self.assertIn('octep.ko is installed, so the next boot brings no front ports up', said)
        self.assertNotIn('will refuse', said)
        self.assertNotIn('kernel.old', said)
        self.assertFalse(os.path.exists(os.path.join(self.dir, 'pinned')))

    def test_with_a_module_for_another_kernel_it_says_the_refusal(self):
        with open(os.path.join(self.dir, 'modules', 'octep.ko'), 'w') as f:
            f.write('ELF')
        with open(os.path.join(self.dir, 'modules', 'octep.ko.kernel'), 'w') as f:
            f.write('FreeBSD 15.1-RELEASE-p4 stable/26.7-n284159-abbbe67a45e7 SMP\n')
        done = self.follow()
        said = done.stdout + done.stderr
        self.assertEqual(done.returncode, 1)
        self.assertIn('the next boot will refuse the module and the front ports will not come up', said)
        self.assertIn('no usable /boot/kernel.old', said)


if __name__ == '__main__':
    unittest.main(verbosity=2)
