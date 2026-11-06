#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/switch-jumbo.sh away from the appliance, over a management link that is a stand-in: the
switch's ten Port Control 2 registers are a file, and contrib/mvsw's answers come from it - a value, or
one of the tool's own error lines ("switch command register stayed busy"). What is written, what is
logged and what the Ports page's state file says. Until 2026-11-06 two error lines for a port were its
majority, and the shell stopped on the word: nothing logged, and the state left at an earlier boot's
"confirmed". In a copy of the script whose paths point at the stand-ins; /bin/sh, so on Linux.

    python3 tools/test-switch-jumbo.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'switch-jumbo.sh')
MD5 = '0123456789abcdef0123456789abcdef'
EARLIER = '2026-11-01 08:00 UTC ports 1-10 at 10,240 bytes, confirmed'

# The coprocessor's end of the link: `mgmt-link.sh ssh <command>`, answered from the registers' file.
LINK = r'''
import json, os, re, sys
path = os.environ['FAKE_SWITCH']
with open(path) as f:
    st = json.load(f)
cmd = sys.argv[2] if len(sys.argv) > 2 else ''
st['calls'].append(cmd)
if 'md5sum' in cmd:
    print('%s  /persistent/os-xgs-npu/mvsw' % st['md5'])
elif 'for p in' in cmd:
    for p in range(1, 11):
        for i in range(3):
            print(p, st['errors'].get(str(p)) or st['regs'][str(p)])
else:
    for port, value in re.findall(r' write (\d+) 8 (0x[0-9a-f]+);', cmd):
        st['writes'].append([int(port), value])
        if str(port) not in st['refuse']:
            st['regs'][port] = value
with open(path, 'w') as f:
    json.dump(st, f)
'''


class SwitchJumbo(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.log = os.path.join(self.dir, 'log')
        self.state = os.path.join(self.dir, 'switch-jumbo.state')
        open(self.log, 'w').close()

    def tearDown(self):
        self.tmp.cleanup()

    def file(self, rel, body, mode=0o755):
        path = os.path.join(self.dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', newline='\n') as f:
            f.write(body)
        os.chmod(path, mode)
        return path

    def run_script(self, regs, errors=None, refuse=(), check=False):
        switch = os.path.join(self.dir, 'switch.json')
        with open(switch, 'w') as f:
            json.dump({'regs': {str(p): v for p, v in regs.items()}, 'errors': errors or {}, 'md5': MD5,
                       'refuse': [str(p) for p in refuse], 'calls': [], 'writes': []}, f)
        link = self.file('link.py', LINK, 0o644)
        self.file('lib/xgs/board.sh', '#!/bin/sh\necho "300 octeontx"\n')
        self.file('lib/npuctl/mgmt-link.sh', '#!/bin/sh\n[ "$2" = true ] && exit 0\nexec "%s" "%s" "$@"\n'
                  % (sys.executable, link))
        php = self.file('php', '#!/bin/sh\necho 1\n')
        logger = self.file('logger', '#!/bin/sh\nshift 4\necho "$*" >> "%s"\n' % self.log)
        self.file('bin/md5', '#!/bin/sh\necho %s\n' % MD5)
        self.file('bin/sleep', '#!/bin/sh\nexit 0\n')
        mvsw = self.file('mvsw', 'not the tool\n', 0o644)
        with open(self.state, 'w') as f:
            f.write(EARLIER + '\n')
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in {'LIB=/usr/local/opnsense/scripts': 'LIB=' + os.path.join(self.dir, 'lib'),
                           '/var/db/os-xgs-npu/switch-jumbo.state': self.state,
                           '/var/run/': self.dir + '/run-',
                           '/usr/local/bin/php': php, '/usr/bin/logger': logger}.items():
            text = text.replace(real, fake)
        script = self.file('switch-jumbo.sh', text)
        env = dict(os.environ, PATH=os.path.join(self.dir, 'bin') + os.pathsep + os.environ.get('PATH', ''),
                   MVSW=mvsw, FAKE_SWITCH=switch)
        done = subprocess.run(['/bin/sh', script] + (['--check'] if check else []), env=env,
                              capture_output=True, text=True, errors='replace', timeout=60)
        with open(switch) as f, open(self.state) as g, open(self.log) as h:
            return done, json.load(f), g.read().strip(), h.read()

    def test_ports_at_2048_are_written_and_confirmed(self):
        done, switch, state, log = self.run_script({p: '0x1480' for p in range(1, 11)})
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(sorted(p for p, _ in switch['writes']), list(range(1, 11)))
        self.assertEqual({v for _, v in switch['writes']}, {'0x2480'})
        self.assertTrue(state.endswith('ports 1-10 at 10,240 bytes, confirmed'), state)
        self.assertNotEqual(state, EARLIER)

    def test_an_error_line_is_no_value(self):
        """Port 3 answers with the tool's error on every read: it is never written, the rest are, the
        log says what the tool said, and the state says port 3 is not confirmed."""
        regs = {p: '0x1480' for p in range(1, 11)}
        done, switch, state, log = self.run_script(regs, errors={'3': 'switch command register stayed busy'})
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertNotIn('Illegal number', done.stderr)
        self.assertNotIn(3, [p for p, _ in switch['writes']])
        self.assertEqual(sorted({p for p, _ in switch['writes']}), [1, 2, 4, 5, 6, 7, 8, 9, 10])
        self.assertIn('said "switch command register stayed busy"', log)
        self.assertTrue(state.endswith('not confirmed after three rounds on ports 3'), state)

    def test_a_tool_that_cannot_open_the_bus_leaves_the_state_said(self):
        errors = {str(p): 'cannot open /dev/mvmdio-uio' for p in range(1, 11)}
        done, switch, state, log = self.run_script({p: '0x1480' for p in range(1, 11)}, errors=errors)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(switch['writes'], [])
        self.assertIn('said "cannot open /dev/mvmdio-uio"', log)
        self.assertTrue(state.endswith('not confirmed after three rounds on ports 1 2 3 4 5 6 7 8 9 10'), state)

    def test_check_reads_past_an_error_line(self):
        regs = {p: '0x2480' for p in range(1, 11)}
        regs[5] = '0x1480'
        done, switch, state, log = self.run_script(regs, errors={'7': 'mdio read failed'}, check=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(switch['writes'], [])
        self.assertIn(' 5:0x1480 ', log)
        self.assertIn(' 7:? ', log)
        self.assertEqual(state, EARLIER)		# --check writes no state

    def test_a_port_that_refuses_the_write_is_named(self):
        done, switch, state, log = self.run_script({p: '0x1480' for p in range(1, 11)}, refuse=(9,))
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertTrue(state.endswith('not confirmed after three rounds on ports 9'), state)


if __name__ == '__main__':
    unittest.main(verbosity=1)
