#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""install/uninstall.sh run whole in a made-up root, for what it leaves of the kernel follower's (the
review's U12): no module of either driver - the previous one and its stamp included, where until
2026-11-06 npuep.ko.prev stayed - and the fall-back to kernel.old taken back only where the follower
marked it as its own, never while os-kernelfollow holds a mark of its own, nor once nextboot.conf names
another kernel.

Every path the script names under /boot, /conf, /tmp, /usr/local and /var is moved under a temporary
directory, service and nextboot are stand-ins, id answers root and kldstat that nothing is loaded - and
rm and rmdir refuse any path outside that directory, so a path this does not move stops the run there
instead of taking anything away from the machine it runs on.

    python3 tools/test-uninstall.py
"""
import os
import re
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
UNINSTALL = os.path.join(HERE, '..', 'install', 'uninstall.sh')
MODULES = ('npuep.ko', 'npuep.ko.kernel', 'npuep.ko.prev', 'npuep.ko.prev.kernel',
           'octep.ko', 'octep.ko.kernel', 'octep.ko.prev', 'octep.ko.prev.kernel')
MOVED = re.compile(r'(?<![\w.}$/-])(/usr/sbin/service|/sbin/nextboot|/boot/|/conf/|/tmp/|/usr/local/|/var/)')


class Uninstall(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = os.path.realpath(self.tmp.name)
        self.root = os.path.join(self.dir, 'root')
        self.bin = os.path.join(self.dir, 'bin')
        os.makedirs(self.bin)
        for name in MODULES:
            self.write('boot/modules/' + name, 'a module')
        self.write('boot/nextboot.conf', 'nextboot_enable="YES"\nkernel="kernel.old"\nkernel_options=""\n')
        self.stub('id', 'echo 0')
        self.stub('kldstat', 'exit 1')
        self.stub('service', 'exit 0')
        self.stub('nextboot', "printf '%%s\\n' \"$*\" >> '%s'" % self.path('nextboot.args'))
        # rm and rmdir that take away only what is under the made-up root
        for name in ('rm', 'rmdir'):
            self.stub(name, 'for a in "$@"; do case "$a" in -*) ;; "%s"/*) ;; *) echo "refused: %s $a" >&2; '
                            'exit 1 ;; esac; done\nexec /bin/%s "$@"' % (self.root, name, name))

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def write(self, name, text=''):
        p = os.path.join(self.root, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w', encoding='utf-8') as f:
            f.write(text)

    def there(self, name):
        return os.path.exists(os.path.join(self.root, name))

    def stub(self, name, body):
        p = os.path.join(self.bin, name)
        with open(p, 'w', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(p, 0o755)

    def read(self, name):
        try:
            with open(self.path(name)) as f:
                return f.read()
        except FileNotFoundError:
            return ''

    def uninstall(self):
        with open(UNINSTALL, encoding='utf-8') as f:
            text = f.read()
        stand_in = {'/usr/sbin/service': os.path.join(self.bin, 'service'),
                    '/sbin/nextboot': os.path.join(self.bin, 'nextboot')}
        text = MOVED.sub(lambda m: stand_in.get(m.group(1), self.root + m.group(1)), text)
        script = self.path('uninstall.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = dict(os.environ, PATH=self.bin + os.pathsep + os.environ.get('PATH', ''),
                   PREFIX=os.path.join(self.root, 'usr', 'local'))
        done = subprocess.run(['/bin/sh', script], env=env, capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn('A loaded module stays loaded until the next reboot.', done.stdout, 'it stopped early')
        self.assertEqual([m for m in MODULES if self.there('boot/modules/' + m)], [],
                         'modules of the follower left behind')
        return done.stdout

    def test_the_fall_back_the_follower_armed_is_taken_back(self):
        self.write('var/run/octep-follow.armed')
        self.write('var/run/octep-follow.pinned')
        said = self.uninstall()
        self.assertEqual(self.read('nextboot.args').splitlines(), ['-D'])
        # (the paths in what it says are moved too)
        self.assertIn('kernel.old the kernel follower armed is taken back', said)
        self.assertFalse(self.there('var/run/octep-follow.armed'))
        self.assertFalse(self.there('var/run/octep-follow.pinned'))

    def test_one_os_kernelfollow_armed_too_stays(self):
        for mark in ('var/run/kernelfollow.armed', 'var/db/os-kernelfollow/pinned'):
            with self.subTest(mark=mark):
                self.write('var/run/octep-follow.armed')
                self.write(mark)
                said = self.uninstall()
                self.assertEqual(self.read('nextboot.args'), '')
                self.assertIn('kernel.old stays: os-kernelfollow armed it as well, for a module of its own', said)
                self.assertTrue(self.there(mark))
                self.assertFalse(self.there('var/run/octep-follow.armed'))
                os.unlink(os.path.join(self.root, mark))

    def test_a_try_that_armed_nothing_takes_nothing_back(self):
        # the first run of a boot marks its try, armed or not; only a fall-back it armed is its own
        self.write('var/run/octep-follow.pinned')
        said = self.uninstall()
        self.assertEqual(self.read('nextboot.args'), '')
        self.assertNotIn('the kernel follower armed', said)
        self.assertFalse(self.there('var/run/octep-follow.pinned'))

    def test_a_next_boot_set_since_for_another_kernel_is_left(self):
        self.write('var/run/octep-follow.armed')
        self.write('boot/nextboot.conf', 'nextboot_enable="YES"\nkernel="kernel.debug"\nkernel_options=""\n')
        said = self.uninstall()
        self.assertEqual(self.read('nextboot.args'), '')
        self.assertNotIn('taken back', said)


if __name__ == '__main__':
    unittest.main(verbosity=2)
