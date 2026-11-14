#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/ftw-status.sh, scripts/xgs/ftw-normal.sh and the start hook 10-xgs-ftw, away from the
appliance.

The coprocessor is a stand-in: npu-run.sh runs the command it is given with sh, its PATH holding an
xgs-ftw and an xgs-cpld that keep the XGS 3300's three relay registers in a file and answer as the
vendor's GPL tools do (xgs-bsp: tools/xgs-ftw.c, lib/xgs_ftw.c, tools/xgs-cpld.c). The CPLD behind them
is modelled on what the second XGS 3300 did on 2026-11-11 (docs/measurements/relay-3300-2026-11-11.md):
the watchdog's clear bit leaves the trigger latch while the relay holds the pair in bypass, -d releases
the pair and clears the latch, and bit 11 of 0x39 stays once the relay has switched on its own. And on
one guess the lab is to settle: an armed pair whose watchdog has expired goes back to bypass at once, so
-d alone does not release it - which is why the watchdog is turned off first.

  - ftw-status.sh writes nothing: only -s and one-argument xgs-cpld reads reach the stand-in
  - a pair in bypass is put back with -t disable and then -d, never armed, its arm bit kept; a latch
    alone on a pair not armed - what every host boot leaves (measured 2026-11-13) - is recorded and
    nothing is written; on an armed pair it is cleared and the pair not written; with nothing to do,
    nothing is written or said
  - at boot: nothing at all with xgs.ftw.boot=0 or on another board; a coprocessor that never answers
    is asked twenty times and that is said once; one that answers late, or loses an answer, still has
    its pair put back and said

In copies of nothing: the scripts run as they are, their paths given in the environment; /bin/sh, so on
Linux.

    python3 tools/test-ftw.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
XGS = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs')
STATUS = os.path.join(XGS, 'ftw-status.sh')
NORMAL = os.path.join(XGS, 'ftw-normal.sh')
HOOK = os.path.join(HERE, '..', 'src', 'etc', 'rc.syshook.d', 'start', '10-xgs-ftw')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'

# The registers as the second XGS 3300 read them (docs/measurements/relay-3300-2026-11-11.md).
FOUND = (0x0, 0x0, 0x2)				# unarmed, normal, watchdog off, a power fault latched
# After a host restart with the pair armed (2026-11-13): disarmed, bit 11 kept, not in bypass, and the
# boot's "power good lost: ATX" latched.
RESTARTED = (0x800, 0x0, 0x2)
ARMED_LATCHED = (0x801, 0x0, 0x2)		# armed, normal, watchdog off, a power fault latched
CLEAN = (0x0, 0x0, 0x0)
TRIPPED = (0x811, 0x80000006, 0x80000001)	# armed, its 3 s watchdog expired: bypass

# xgs-ftw as tools/xgs-ftw.c drives lib/xgs_ftw.c, over the CPLD model above.
FTW = r'''
import json, os, sys
path = os.environ['FAKE_CPLD']
with open(path) as f:
    s = json.load(f)
argv = sys.argv[1:]
s['calls'].append(['xgs-ftw'] + argv)

def save():
    with open(path, 'w') as f:
        json.dump(s, f)

def armed():
    return s['r39'] & 1

def expired():
    return (s['r3a'] >> 31) & 1 and not (s['r3a'] >> 29) & 1

def state():
    return (s['r39'] >> 4) & 3

def set_state(v):
    s['r39'] = (s['r39'] & ~0x30) | (v << 4)

if not s['pairs']:
    save()
    print('No FTW ports found', file=sys.stderr)
    sys.exit(1)
opt = {'a': 0, 'u': 0, 'e': 0, 'd': 0, 's': 0}
timer = None
i = 0
while i < len(argv):
    if argv[i] in ('-a', '-u', '-e', '-d', '-s'):
        opt[argv[i][1]] = 1
    elif argv[i] == '-t':
        i += 1
        timer = argv[i]
    else:
        save()
        sys.exit(1)
    i += 1
if not any(opt.values()) and timer is None:
    opt['s'] = 1
if opt['a']:
    s['r39'] |= 1
if opt['u']:
    s['r39'] &= ~1
if opt['d'] or opt['e']:
    s['r39'] = (s['r39'] & ~0xf00) | (0x100 if opt['d'] else 0x300) | (s['r39'] & 0x800)
    if opt['e'] or (armed() and (expired() or s['fault'])):
        set_state(1)
    else:
        set_state(0)
        s['r3b'] = 0
if timer is not None:
    if timer.startswith('dis'):
        s['r3a'] = 0x3e
        if state() == 0:
            s['r3b'] = 0
    elif timer.startswith('res'):
        s['r3a'] |= 1 << 30
    else:
        s['r3a'] = (int(timer, 0) * 2) | 1 << 31 | 1 << 29
        if state() == 0:
            s['r3b'] = 0
if opt['s']:
    mode = {0: 'disabled-bypass', 1: 'enabled-bypass', 3: 'unknown'}.get(state())
    if mode is None:
        save()
        print('Error: xgs_ftw_get_status: Device or resource busy', file=sys.stderr)
        sys.exit(1)
    print('Port %3s & %-3s: %-15s %-7s' % ('1', '2', mode, 'armed' if armed() else 'unarmed'))
    w = s['r3a']
    print('Watchdog timer: %s' % ('disabled' if not (w >> 31) & 1 else ('armed' if (w >> 29) & 1 else 'expired')))
    t = s['r3b']
    print('FTW trigger   : %s' % ('OK' if t == 0 else ('watchdog-timer-expired' if t & 1 else 'power-fault')))
save()
'''

# xgs-cpld <register> [value], as tools/xgs-cpld.c: one argument reads, two write.
CPLD = r'''
import json, os, sys
path = os.environ['FAKE_CPLD']
with open(path) as f:
    s = json.load(f)
args = sys.argv[1:]
s['calls'].append(['xgs-cpld'] + args)
if len(args) == 1:
    print('0x%8.8x' % s.get({0x39: 'r39', 0x3a: 'r3a', 0x3b: 'r3b'}.get(int(args[0], 0), ''), 0))
else:
    s['raw_writes'].append(args)
with open(path, 'w') as f:
    json.dump(s, f)
'''

# npu-run.sh <seconds> <command>, over a link that answers, or not (FAKE_MODE: ok, down, busyN - the
# first N sessions refused - lose: the first session that runs -d does it and loses its answer - or
# drop: every session that would run -d is lost before it runs).
RUN = r'''#!/bin/sh
echo "$2" >> "$FAKE_RUNS"
n=$(wc -l < "$FAKE_RUNS")
mode=$(cat "$FAKE_MODE")
case $mode in
down) echo "the coprocessor's console shows no Linux prompt; nothing was sent" >&2; exit 69 ;;
busy*) [ "$n" -le "${mode#busy}" ] && { echo "the console is busy" >&2; exit 75; } ;;
drop) case $2 in *" -d "*) exit 1 ;; esac ;;
lose)
	case $2 in
	*" -d "*)
		if [ ! -f "$FAKE_RUNS.lost" ]; then
			touch "$FAKE_RUNS.lost"
			PATH="$FAKE_BIN:$PATH" sh -c "$2" > /dev/null 2>&1
			exit 1
		fi
		;;
	esac
	;;
esac
PATH="$FAKE_BIN:$PATH" exec sh -c "$2"
'''


@unittest.skipUnless(POSIX, 'the scripts run under /bin/sh')
class Relay(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.bin = os.path.join(self.dir, 'bin')		# the coprocessor's PATH
        self.host = os.path.join(self.dir, 'host')		# this host's: a sleep that does not wait
        os.mkdir(self.bin)
        os.mkdir(self.host)
        self.cpld = os.path.join(self.dir, 'cpld.json')
        self.runs = os.path.join(self.dir, 'runs')
        self.mode = os.path.join(self.dir, 'mode')
        self.log = os.path.join(self.dir, 'log')
        self.board = os.path.join(self.dir, 'board')
        self.kenv = os.path.join(self.dir, 'kenv.value')
        self.state = os.path.join(self.dir, 'db', 'ftw-normal.state')
        open(self.runs, 'w').close()
        open(self.log, 'w').close()
        self.write(self.mode, 'ok\n')
        self.write(self.board, '202 octeontx\n')
        self.py(os.path.join(self.bin, 'xgs-ftw'), FTW)
        self.py(os.path.join(self.bin, 'xgs-cpld'), CPLD)
        for d in (self.bin, self.host):
            self.sh(os.path.join(d, 'sleep'), 'exit 0')
        self.npu_run = self.sh(os.path.join(self.dir, 'npu-run.sh'), RUN.split('\n', 1)[1])
        self.board_sh = self.sh(os.path.join(self.dir, 'board.sh'), 'cat "%s"' % self.board)
        self.logger = self.sh(os.path.join(self.dir, 'logger'), 'echo "$*" >> "%s"' % self.log)
        self.kenv_sh = self.sh(os.path.join(self.dir, 'kenv'),
                               '[ "$1" = -q ] && [ -f "%s" ] && cat "%s" || exit 1' % (self.kenv, self.kenv))
        self.registers(FOUND)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, path, text):
        with open(path, 'w', newline='\n') as f:
            f.write(text)

    def sh(self, path, body):
        self.write(path, '#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def py(self, path, body):
        self.write(path, '#!%s\n%s' % (sys.executable, body))
        os.chmod(path, 0o755)

    def registers(self, regs, pairs=True, fault=False):
        r39, r3a, r3b = regs
        with open(self.cpld, 'w') as f:
            json.dump({'r39': r39, 'r3a': r3a, 'r3b': r3b, 'pairs': pairs, 'fault': fault,
                       'calls': [], 'raw_writes': []}, f)

    def cpld_state(self):
        with open(self.cpld) as f:
            return json.load(f)

    def call(self, script, *args, **extra):
        env = dict(os.environ, PATH=self.host + ':' + os.environ.get('PATH', '/usr/bin:/bin'),
                   FAKE_CPLD=self.cpld, FAKE_RUNS=self.runs, FAKE_MODE=self.mode, FAKE_BIN=self.bin,
                   NPU_RUN=self.npu_run, BOARD_SH=self.board_sh, STATUS=STATUS, LOGGER=self.logger,
                   KENV=self.kenv_sh, STATE=self.state, GAP='0')
        env.update(extra)
        return subprocess.run(['/bin/sh', script] + list(args), capture_output=True, text=True,
                              errors='replace', timeout=120, env=env)

    def logged(self):
        with open(self.log) as f:
            return f.read().splitlines()

    def sessions(self):
        with open(self.runs) as f:
            return f.read().splitlines()

    def tool_calls(self):
        return [c[1:] for c in self.cpld_state()['calls'] if c[0] == 'xgs-ftw']

    def kv(self, out):
        return dict(line.split('=', 1) for line in out.splitlines() if '=' in line)

    def assert_never_armed(self):
        s = self.cpld_state()
        self.assertEqual(s['raw_writes'], [])
        for call in self.tool_calls():
            self.assertNotIn('-a', call)
            self.assertNotIn('-e', call)
            self.assertNotIn('-u', call)
            if '-t' in call:
                self.assertEqual(call[call.index('-t') + 1], 'disable', call)

    # ---------------------------------------------------------------- ftw-status.sh

    def test_the_status_is_read_and_nothing_is_written(self):
        done = self.call(STATUS)
        self.assertEqual(done.returncode, 0, done.stderr)
        lines = done.stdout.splitlines()
        self.assertIn('pair:           Port1 & Port2', lines)
        self.assertIn('armed:          no', lines)
        self.assertIn('mode:           normal', lines)
        self.assertIn('relay watchdog: off', lines)
        self.assertIn('trigger:        power good lost: ATX (latched, 0x3b 0x00000002)', lines)
        self.assertIn('registers:      0x39 0x00000000, 0x3a 0x00000000, 0x3b 0x00000002 (xgs-cpld, read only)',
                      lines)
        self.assertEqual(self.tool_calls(), [['-s']])
        self.assertEqual(self.cpld_state()['raw_writes'], [])
        self.assertEqual(len(self.sessions()), 1)

    def test_a_pair_in_bypass_reads_as_one(self):
        self.registers(TRIPPED)
        done = self.call(STATUS, '-k')
        self.assertEqual(done.returncode, 0, done.stderr)
        kv = self.kv(done.stdout)
        self.assertEqual({k: kv[k] for k in ('relay', 'pair', 'armed', 'mode', 'watchdog', 'watchdog_seconds',
                                             'trigger', 'cause', 'ok_signal', 'cpld_39', 'cpld_3a', 'cpld_3b')},
                         {'relay': '1', 'pair': 'Port1,Port2', 'armed': '1', 'mode': 'bypass',
                          'watchdog': 'expired', 'watchdog_seconds': '3.0', 'trigger': '1', 'cause': 'watchdog',
                          'ok_signal': 'low', 'cpld_39': '0x00000811', 'cpld_3a': '0x80000006',
                          'cpld_3b': '0x80000001'})
        self.assertEqual(kv['cause_text'], 'the relay watchdog expired')
        human = self.call(STATUS).stdout.splitlines()
        self.assertIn('mode:           bypass - Port1 and Port2 joined by the relay, past this firewall', human)
        self.assertIn('relay watchdog: expired, after 3.0 s', human)

    def test_each_line_of_the_latch_is_named(self):
        self.registers((0x0, 0x0, 0x1806 | 1 << 20))
        kv = self.kv(self.call(STATUS, '-k').stdout)
        self.assertEqual(kv['cause'], 'atx,atx-5v0,vddq-vtt,vdd-core,bit20')
        self.assertEqual(kv['cause_text'], 'power good lost: ATX, ATX 5 V, memory VDDQ/VTT, core, '
                                           'bit 20 (not named in the tool\'s source)')
        self.assertEqual(kv['ok_signal'], 'high')

    def test_no_relay_on_another_family(self):
        self.write(self.board, '201 armada\n')
        done = self.call(STATUS, '-k')
        self.assertEqual(done.returncode, 2)
        self.assertEqual(self.kv(done.stdout), {'relay': '0', 'reason': 'not the OCTEON TX family'})
        self.assertEqual(self.sessions(), [])

    def test_no_relay_without_the_tool_or_a_pair(self):
        os.unlink(os.path.join(self.bin, 'xgs-ftw'))
        done = self.call(STATUS)
        self.assertEqual((done.returncode, done.stdout.strip()),
                         (2, 'no fail-to-wire relay read: no xgs-ftw on the coprocessor'))
        self.py(os.path.join(self.bin, 'xgs-ftw'), FTW)
        self.registers(FOUND, pairs=False)
        done = self.call(STATUS)
        self.assertEqual((done.returncode, done.stdout.strip()),
                         (2, 'no fail-to-wire relay read: xgs-ftw finds no pair on this board'))

    def test_a_silent_coprocessor_is_an_error(self):
        self.write(self.mode, 'down\n')
        done = self.call(STATUS)
        self.assertEqual(done.returncode, 1)
        self.assertIn('npu-run.sh exit 69', done.stderr)
        self.assertEqual(done.stdout, '')

    # ---------------------------------------------------------------- ftw-normal.sh

    def test_a_pair_in_bypass_is_put_back_and_never_armed(self):
        self.registers(TRIPPED)
        done = self.call(NORMAL)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.tool_calls(), [['-s'], ['-t', 'disable'], ['-d'], ['-s']])
        self.assert_never_armed()
        s = self.cpld_state()
        self.assertEqual((s['r39'] & 1, (s['r39'] >> 4) & 3, s['r3a'] >> 31, s['r3b']), (1, 0, 0, 0))
        said = self.logged()
        self.assertEqual(len(said), 2, said)
        self.assertTrue(said[0].startswith('-t xgs-ftw -p daemon.warning relay pair Port1 & Port2 found in bypass '
                                           'now: in bypass, armed, relay watchdog expired after 3.0 s, trigger '
                                           'latched: the relay watchdog expired, the relay\'s OK output low'), said[0])
        self.assertIn('running xgs-ftw -t disable, then xgs-ftw -d', said[0])
        self.assertTrue(said[1].startswith('-t xgs-ftw -p daemon.notice relay pair Port1 & Port2 back to normal now '
                                           '(xgs-ftw -t disable, then xgs-ftw -d): normal, armed, relay watchdog '
                                           'off, no trigger latched'), said[1])
        with open(self.state) as f:
            self.assertIn('now: back to normal (xgs-ftw -t disable, then xgs-ftw -d)', f.read())

    def test_a_pair_that_does_not_come_back_is_said(self):
        self.registers(TRIPPED, fault=True)		# a power-good line still low: the relay stays
        done = self.call(NORMAL)
        self.assertEqual(done.returncode, 3, done.stdout + done.stderr)
        self.assert_never_armed()
        said = self.logged()
        self.assertTrue(said[-1].startswith('-t xgs-ftw -p daemon.warning relay pair Port1 & Port2 NOT back to '
                                            'normal now after xgs-ftw -t disable, then xgs-ftw -d: in bypass'),
                        said[-1])
        self.assertIn('Port1 & Port2 still joined by the relay, past this firewall', said[-1])

    def test_a_latch_alone_on_a_pair_not_armed_is_left_as_is(self):
        """Every host boot latches "power good lost: ATX" (0x3b 0x2), and a restart disarms an armed pair
        (0x39 0x800): normal, not armed, its watchdog not expired - the latch is recorded in the state
        file and said in one line, and nothing is written, by hand or at boot."""
        for regs in (FOUND, RESTARTED):
            for args, when in (((), 'now'), (('boot',), 'at boot')):
                self.registers(regs)
                self.write(self.log, '')
                done = self.call(NORMAL, *args)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                self.assertEqual(self.tool_calls(), [['-s']], (regs, args))
                s = self.cpld_state()
                self.assertEqual((s['r39'], s['r3a'], s['r3b'], s['raw_writes']), regs + ([],))
                said = self.logged()
                self.assertEqual(len(said), 1, said)
                self.assertTrue(said[0].startswith('-t xgs-ftw -p daemon.notice relay pair Port1 & Port2: '
                                                   'trigger latched at boot, left as is (not armed, nothing '
                                                   'written): normal, not armed, relay watchdog off, trigger '
                                                   'latched: power good lost: ATX'), said[0])
                with open(self.state) as f:
                    self.assertIn('%s: trigger latched at boot, left as is' % when, f.read())
                self.assertEqual(done.stdout != '', args == (), done.stdout)

    def test_a_latch_with_a_running_watchdog_on_a_pair_not_armed_is_left_as_is(self):
        self.registers((0x0, 0xa0000006, 0x2))
        done = self.call(NORMAL)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.tool_calls(), [['-s']])
        said = self.logged()
        self.assertEqual(len(said), 2, said)
        self.assertIn('latched at boot, left as is', said[0])
        self.assertIn('its watchdog is on and is left so', said[1])

    def test_a_latch_on_an_armed_pair_is_cleared_and_the_pair_not_written(self):
        self.registers(ARMED_LATCHED)
        done = self.call(NORMAL)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.tool_calls(), [['-s'], ['-t', 'disable'], ['-s']])
        self.assert_never_armed()
        s = self.cpld_state()
        self.assertEqual((s['r39'], s['r3b']), (0x801, 0))
        said = self.logged()
        self.assertEqual(len(said), 1, said)
        self.assertTrue(said[0].startswith('-t xgs-ftw -p daemon.notice relay pair Port1 & Port2: its trigger '
                                           'latch cleared now (xgs-ftw -t disable): normal, armed'), said[0])
        self.assertIn('it was normal, armed, relay watchdog off, trigger latched: power good lost: ATX', said[0])
        self.assertNotIn('left as is', said[0])

    def test_nothing_to_do_writes_and_says_nothing(self):
        self.registers(CLEAN)
        done = self.call(NORMAL)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.tool_calls(), [['-s']])
        self.assertEqual(self.logged(), [])
        with open(self.state) as f:
            self.assertIn('now: normal, nothing to do', f.read())

    def test_a_running_watchdog_is_left_alone_and_said(self):
        self.registers((0x0, 0xa0000006, 0x0))
        done = self.call(NORMAL)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.tool_calls(), [['-s']])
        said = self.logged()
        self.assertEqual(len(said), 1, said)
        self.assertIn('its watchdog is on and is left so', said[0])

    def test_by_hand_on_another_board_nothing_is_done(self):
        self.write(self.board, '202 octeontx rnd\n')
        done = self.call(NORMAL)
        self.assertEqual((done.returncode, done.stdout.strip()), (2, 'not the XGS 3300: nothing done'))
        self.assertEqual(self.sessions(), [])

    # ---------------------------------------------------------------- at boot

    def test_the_knob_at_zero_does_nothing_at_all(self):
        self.registers(TRIPPED)
        self.write(self.kenv, '0\n')
        done = self.call(NORMAL, 'boot')
        self.assertEqual(done.returncode, 0)
        self.assertEqual((self.sessions(), self.logged(), os.path.exists(self.state)), ([], [], False))
        self.write(self.kenv, '1\n')
        self.assertEqual(self.call(NORMAL, 'boot').returncode, 0)
        self.assertEqual((self.cpld_state()['r39'] >> 4) & 3, 0)

    def test_at_boot_another_board_is_left_alone(self):
        self.registers(TRIPPED)
        self.write(self.board, '202 octeontx rnd\n')
        done = self.call(NORMAL, 'boot')
        self.assertEqual((done.returncode, done.stdout), (2, ''))
        self.assertEqual((self.sessions(), self.logged()), ([], []))

    def test_a_coprocessor_that_never_answers_is_asked_twenty_times_and_said_once(self):
        self.write(self.mode, 'down\n')
        done = self.call(NORMAL, 'boot')
        self.assertEqual(done.returncode, 1)
        self.assertEqual(len(self.sessions()), 20)
        said = self.logged()
        self.assertEqual(len(said), 1, said)
        self.assertIn('not read in the boot\'s 20 tries', said[0])
        self.assertEqual(done.stdout, '')

    def test_at_boot_a_late_coprocessor_still_has_its_pair_put_back(self):
        self.registers(TRIPPED)
        self.write(self.mode, 'busy3\n')
        done = self.call(NORMAL, 'boot')
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assert_never_armed()
        said = self.logged()
        self.assertEqual(len(said), 2, said)
        self.assertIn('found in bypass at boot', said[0])
        self.assertIn('back to normal at boot', said[1])
        self.assertEqual((self.cpld_state()['r39'] >> 4) & 3, 0)
        self.assertEqual(done.stdout, '')

    def test_an_answer_lost_after_the_tool_ran_is_still_said(self):
        self.registers(TRIPPED)
        self.write(self.mode, 'lose\n')
        done = self.call(NORMAL, 'boot')
        self.assertEqual(done.returncode, 0, done.stderr)
        said = self.logged()
        self.assertEqual(len(said), 2, said)
        self.assertIn('found in bypass at boot', said[0])
        self.assertIn('back to normal at boot (xgs-ftw -t disable, then xgs-ftw -d, its answer lost)', said[1])
        self.assertEqual(self.tool_calls().count(['-d']), 1)

    def test_the_tool_is_run_in_three_tries_at_most(self):
        self.registers(TRIPPED)
        self.write(self.mode, 'drop\n')
        done = self.run_boot()
        self.assertEqual(done.returncode, 0, done.stderr)
        runs = self.sessions()
        self.assertEqual(len([r for r in runs if ' -d ' in r]), 3, runs)
        self.assertEqual(len(runs), 7, runs)			# four reads, three sessions lost
        said = self.logged()
        self.assertEqual(len(said), 2, said)
        self.assertIn('found in bypass at boot', said[0])
        self.assertIn('NOT back to normal at boot: xgs-ftw -t disable, then xgs-ftw -d was run three times', said[1])

    def test_a_state_the_tool_calls_unknown_is_not_called_bypass(self):
        self.registers((0x30, 0x0, 0x0))
        done = self.call(NORMAL)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.tool_calls(), [['-s'], ['-d'], ['-s']])
        said = self.logged()
        self.assertIn('found not normal now: in a state xgs-ftw calls unknown', said[0])
        self.assertIn('back to normal now (xgs-ftw -d)', said[1])

    def run_boot(self):
        return self.call(NORMAL, 'boot')

    def test_at_boot_a_normal_pair_is_quiet(self):
        self.registers(CLEAN)
        done = self.call(NORMAL, 'boot')
        self.assertEqual((done.returncode, done.stdout), (0, ''))
        self.assertEqual(self.logged(), [])
        with open(self.state) as f:
            self.assertIn('at boot: normal, nothing to do', f.read())

    # ---------------------------------------------------------------- the start hook

    def hook(self, script):
        with open(HOOK, encoding='utf-8') as f:
            text = f.read()
        daemon = self.sh(os.path.join(self.dir, 'daemon'), 'echo "daemon $*" >> "%s"' % self.log)
        text = text.replace('/usr/local/opnsense/scripts/xgs/ftw-normal.sh', script)
        text = text.replace('/usr/sbin/daemon', daemon)
        path = os.path.join(self.dir, '10-xgs-ftw')
        self.write(path, text)
        done = subprocess.run(['/bin/sh', path], capture_output=True, text=True, timeout=30)
        self.assertEqual(done.returncode, 0, done.stderr)
        return self.logged()

    def test_the_hook_starts_the_boot_run_detached(self):
        script = os.path.join(self.dir, 'ftw-normal.sh')
        self.write(script, '')
        self.assertEqual(self.hook(script), ['daemon -f /bin/sh %s boot' % script])

    def test_the_hook_without_its_script_does_nothing(self):
        self.assertEqual(self.hook(os.path.join(self.dir, 'not-installed')), [])


if __name__ == '__main__':
    unittest.main(verbosity=1)
