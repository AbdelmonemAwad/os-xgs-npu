#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/npu-clock.sh away from the appliance: this host's clock and the coprocessor's are numbers
in a file, `date` reads and formats them as FreeBSD's does, and the management link is a stand-in whose
every connection takes CONNECT seconds of this host's clock before the command runs on the far side.

Until 2026-11-06 the time given to the coprocessor was taken before the connection, so its clock was set
behind by the connection's length: past the two seconds allowed on a link three seconds long, said only
with -v, and set again at every run. Now the length of the first read is added, and a set that did not
hold is said in the system log - once, until it changes or the clock is right. In a copy of the script
whose paths point at the stand-ins; /bin/sh, so on Linux.

    python3 tools/test-npu-clock.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'npu-clock.sh')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'
START = 1793000000.25		# this host's clock when a run starts, a quarter of a second into a second

# FreeBSD's date, as the script uses it: -u +%s, and -u [-r seconds] '+format'.
DATE = r'''
import json, os, sys, time
with open(os.environ['FAKE_CLOCK']) as f:
    host = json.load(f)['host']
args = [a for a in sys.argv[1:] if a != '-u']
when = int(host)
if args and args[0] == '-r':
    when = int(args[1])
    args = args[2:]
fmt = args[0][1:] if args else '%c'
print(when if fmt == '%s' else time.strftime(fmt, time.gmtime(when)))
'''

# `mgmt-link.sh ssh <command>`: the connection takes CONNECT seconds, then the command runs there.
LINK = r'''
import calendar, json, os, re, sys, time
path = os.environ['FAKE_CLOCK']
with open(path) as f:
    clock = json.load(f)
cmd = sys.argv[2] if len(sys.argv) > 2 else ''
clock['host'] += clock['connect']
if clock['down']:
    sys.exit(255)
theirs = clock['host'] + clock['offset']
if cmd == 'date -u +%s':
    print(int(theirs))
else:
    m = re.match(r"date -u -s '([0-9-]+ [0-9:]+)' > /dev/null$", cmd)
    if m and not clock['refuse']:
        given = calendar.timegm(time.strptime(m.group(1), '%Y-%m-%d %H:%M:%S'))
        clock['offset'] = given - clock['host']
    clock['sets'] += 1
with open(path, 'w') as f:
    json.dump(clock, f)
'''


@unittest.skipUnless(POSIX, 'npu-clock.sh runs under /bin/sh')
class NpuClock(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.clock = os.path.join(self.dir, 'clock.json')
        self.log = os.path.join(self.dir, 'log')
        self.said = os.path.join(self.dir, 'xgs-npu-clock.failed')
        open(self.log, 'w').close()
        date = self.file('date.py', DATE)
        link = self.file('link.py', LINK)
        stubs = {
            '/usr/local/opnsense/scripts/npuctl/mgmt-link.sh':
                self.file('mgmt-link.sh', '#!/bin/sh\nexec "%s" "%s" "$@"\n' % (sys.executable, link), 0o755),
            '/bin/date': self.file('date', '#!/bin/sh\nexec "%s" "%s" "$@"\n' % (sys.executable, date), 0o755),
            '/usr/bin/timeout': self.file('timeout', '#!/bin/sh\nshift\nexec "$@"\n', 0o755),
            '/usr/bin/logger': self.file('logger', '#!/bin/sh\nshift 2\necho "$*" >> "%s"\n' % self.log, 0o755),
            '/var/run/xgs-npu-clock.failed': self.said,
        }
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        self.script = self.file('npu-clock.sh', text, 0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def file(self, name, body, mode=0o644):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write(body)
        os.chmod(path, mode)
        return path

    def run_script(self, offset, connect, refuse=False, down=False):
        with open(self.clock, 'w') as f:
            json.dump({'host': START, 'offset': offset, 'connect': connect, 'refuse': refuse, 'down': down,
                       'sets': 0}, f)
        done = subprocess.run(['/bin/sh', self.script, '-v'], capture_output=True, text=True, errors='replace',
                              timeout=60, env=dict(os.environ, FAKE_CLOCK=self.clock))
        with open(self.clock) as f, open(self.log) as g:
            return done, json.load(f), g.read().splitlines()

    def test_a_slow_link_sets_the_clock_right(self):
        """Three seconds a connection: the clock read 1970, and is set to within a second of this host's."""
        done, clock, log = self.run_script(offset=-START, connect=3)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(clock['sets'], 1)
        self.assertLessEqual(abs(clock['offset']), 1, clock)
        self.assertEqual(len(log), 1)
        self.assertIn("the coprocessor's clock set from this host's: it read 1970-01-01", log[0])

    def test_a_fast_link_as_before(self):
        done, clock, log = self.run_script(offset=-START, connect=0)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertLessEqual(abs(clock['offset']), 1, clock)

    def test_a_clock_that_is_right_is_left(self):
        done, clock, log = self.run_script(offset=1, connect=3)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((clock['sets'], log), (0, []))

    def test_a_set_that_does_not_hold_is_logged_once_until_it_holds(self):
        for _ in range(3):
            done, clock, log = self.run_script(offset=-START, connect=1, refuse=True)
            self.assertEqual(done.returncode, 1)
        self.assertEqual(len(log), 1, log)
        self.assertIn('the coprocessor did not take the time: it still reads 1970-01-01', log[0])
        done, clock, log = self.run_script(offset=-START, connect=1)		# it takes it now
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertFalse(os.path.exists(self.said))
        self.assertEqual(len(log), 2)
        done, clock, log = self.run_script(offset=-START, connect=1, refuse=True)	# and refuses again
        self.assertEqual(len(log), 3)

    def test_no_answer_is_said_with_v_only(self):
        done, clock, log = self.run_script(offset=0, connect=1, down=True)
        self.assertEqual(done.returncode, 1)
        self.assertIn('did not answer over the management link', done.stdout)
        self.assertEqual(log, [])


if __name__ == '__main__':
    unittest.main(verbosity=1)
