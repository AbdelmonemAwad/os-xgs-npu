#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""bringup.sh's handshake away from the appliance: a coprocessor that an earlier load of the driver
handshook with is said to be so at once, and offered nothing - until 2026-11-06 it was waited out as a
handshake in flight, a minute a second at a time, and said not to answer; and an ordinary handshake is
still offered after the warm-up, the rings armed before it. sysctl, uname, logger and sleep are
stand-ins in a copy of the script, the driver's answers are made up, and the module and its stamp are
files in a temporary directory.

    python3 tools/test-bringup.py
"""
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'octep', 'bringup.sh')
KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'
EARLIER = 'not handshaken by this instance; register holds 0x0000000000000001 (a port is started)'
IDLE = 'idle (scratch 0x0000000000000000)'
DONE = 'completed (scratch 0x0000000000000000)'


class Handshake(unittest.TestCase):
    """The driver loaded and attached, its module stamped for the kernel that is running."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        with open(self.path('octep.ko'), 'w') as f:
            f.write('ELF')
        with open(self.path('octep.ko.kernel'), 'w') as f:
            f.write(KERNEL + '\n')

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def stub(self, name, body):
        path = self.path(name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def read(self, name):
        try:
            with open(self.path(name)) as f:
                return f.read()
        except FileNotFoundError:
            return ''

    def bring_up(self, state, answer=None):
        """bringup.sh with the handshake's state reading state, and answer once an offer is made. What it
        returned, and the writes and sleeps it made."""
        with open(self.path('hs'), 'w') as f:
            f.write(state + '\n')
        offered = ''
        if answer is not None:
            offered = 'dev.octep.0.sdp.handshake=1) echo "%s" > "%s" ;;' % (answer, self.path('hs'))
        # sysctl -n <name> reads; sysctl <name>=<value> ... writes, each kept in the order made
        sysctl = self.stub('sysctl', (
            'if [ "$1" = -n ]; then\n'
            '\tcase "$2" in\n'
            '\tdev.octep.0.sdp.hs_state) cat "{hs}" ;;\n'
            '\t*) exit 1 ;;\n'
            '\tesac\n'
            '\texit 0\n'
            'fi\n'
            'for w in "$@"; do\n'
            '\techo "$w" >> "{writes}"\n'
            '\tcase "$w" in\n'
            '\t{offered}\n'
            '\tesac\n'
            'done').format(hs=self.path('hs'), writes=self.path('writes'), offered=offered))
        os.makedirs(self.path('bin'))
        self.stub(os.path.join('bin', 'sleep'), 'echo "$1" >> "%s"' % self.path('sleeps'))
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for command, stand_in in (('/sbin/sysctl', sysctl),
                                  ('/usr/bin/uname', self.stub('uname', 'echo "%s"' % KERNEL)),
                                  ('/usr/bin/logger', self.stub('logger', 'exit 0'))):
            self.assertIn(command, text)
            text = text.replace(command, stand_in)
        script = self.path('bringup.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = {name: value for name, value in os.environ.items()
               if name not in ('CHECK', 'FORCE', 'HS_ATTEMPTS', 'COPROC_WARMUP', 'NWA_WAIT', 'RSIZE')}
        env.update(PATH=self.path('bin') + os.pathsep + os.environ.get('PATH', ''),
                   MODULE=self.path('octep.ko'), LOGFILE=self.path('log'), FASTPATH_WAIT='0',
                   KLDSTAT=self.stub('kldstat', 'exit 0'), KLDLOAD=self.stub('kldload', 'exit 1'))
        done = subprocess.run(['/bin/sh', script], env=env, capture_output=True, text=True, timeout=60)
        return done, self.read('writes').splitlines(), self.read('sleeps').splitlines()

    def test_a_coprocessor_handshaken_by_an_earlier_load_is_said_so_at_once(self):
        done, writes, sleeps = self.bring_up(EARLIER)
        log = self.read('log')
        self.assertEqual(done.returncode, 1)
        self.assertIn('the handshake register holds 0x0000000000000001 (a port is started), which this load '
                      'of the driver did not write: an earlier load handshook with the coprocessor', log)
        self.assertNotIn('not answering', log)
        self.assertEqual(sleeps, [], 'it waited for a handshake that was not coming')
        self.assertNotIn('dev.octep.0.sdp.handshake=1', writes)
        self.assertNotIn('dev.octep.0.dp.start=1', writes)

    def test_an_ordinary_handshake_is_offered_after_the_warm_up_and_taken(self):
        done, writes, sleeps = self.bring_up(IDLE, answer=DONE)
        log = self.read('log')
        self.assertIn('output rings armed', log)
        self.assertIn('handshake completed', log)
        self.assertEqual(writes.count('dev.octep.0.sdp.handshake=1'), 1)
        self.assertLess(writes.index('dev.octep.0.dp.start=1'), writes.index('dev.octep.0.sdp.handshake=1'))
        self.assertEqual(sleeps, ['20', '6'])
        # and on to the fast path, which no stand-in here answers
        self.assertEqual(done.returncode, 1)
        self.assertIn('the rpc facility never configured', log)


if __name__ == '__main__':
    unittest.main(verbosity=2)
