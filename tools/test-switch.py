#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/switch.sh, the switch chip set by hand, against a made-up chip: what it may write and
what it has to put back.

  - READ BEFORE WRITE: every register it writes was read in the same run before - a port register
    directly, the policy pointer 0x30 through its pointer;
  - PUT BACK WITH COMPARE: off writes the kept value back and reads it again; a value that does not
    stick is said, the line kept and the exit 1;
  - PIRL HELD: the ingress rate limiter is read by show and never written, and pirl is no command;
  - VLAN TABLE HELD (the owner's decision D7): the table is walked by show and vlan show and never
    written, no port's TCAM mode is written, and vlan add / vlan del are refused before anything;
  - THREE READS THAT AGREE: a register whose reads never agree three times in four rounds stops the
    command before anything is written; one that flickers now and then is read again and used;
  - FRONT-PORT NAMES: an unknown name, a bare number, and a port on the coprocessor's own MAC are
    refused before the coprocessor is asked anything.

npu-run.sh is a stand-in that runs the command with /bin/sh; mvsw is a stand-in that keeps each register
in a file and acts as the chip's PIRL, policy-pointer and VLAN-table engines do; models.py is the real
one, told the model through XGS_PLANAR_SERIAL. Nothing here comes from an appliance.

    python3 tools/test-switch.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
SWITCH = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'switch.sh')
MODELS = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'models.py')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'

# The chip. Registers in files r.<dev>.<reg> (decimal names, values as 0x%04x); the policy pointers
# in pol.<port>.<ptr>, PIRL words in pirl.<port>.<res>.<reg>, VLAN entries in vtu.<vid> and STU entries
# in stu.<sid>. Every read and write goes to the log as "R dev reg value" / "W dev reg value". A file
# "flicker" holding N makes every Nth read answer 0x7bad; "stuck.<dev>.<reg>" makes writes to that
# register go nowhere.
FAKE_MVSW = r'''#!/bin/sh
D=${FAKE_SW:?}
[ "$1" = 0 ] && [ "$2" = 2 ] || { echo "usage"; exit 1; }
op=$3; dev=$(( $4 )); reg=$(( $5 ))
get() { cat "$D/$1" 2>/dev/null || echo 0x0000; }
set_() { printf '0x%04x\n' $(( $2 )) > "$D/$1"; }
case $op in
read)
	n=$(( $(cat "$D/nreads" 2>/dev/null || echo 0) + 1 )); echo $n > "$D/nreads"
	v=$(get r.$dev.$reg)
	f=$(cat "$D/flicker" 2>/dev/null || echo 0)
	[ "$f" -gt 0 ] && [ $(( n % f )) -eq 0 ] && v=0x7bad
	echo "R $dev $reg $v" >> "$D/log"
	echo $v
	;;
write)
	val=$(( $6 ))
	echo "W $dev $reg $(printf 0x%04x $val)" >> "$D/log"
	[ -f "$D/stuck.$dev.$reg" ] && exit 0
	if [ $dev -le 10 ] && [ $reg -eq 14 ]; then
		ptr=$(( (val >> 8) & 0x3f ))
		[ $(( val & 0x8000 )) -ne 0 ] && set_ pol.$dev.$ptr $(( val & 0xff ))
		set_ r.$dev.14 $(( (ptr << 8) | $(get pol.$dev.$ptr) ))
	elif [ $dev -eq 28 ] && [ $reg -eq 9 ]; then
		o=$(( (val >> 13) & 3 )); p=$(( (val >> 8) & 0x1f )); rs=$(( (val >> 5) & 7 )); k=$(( val & 0xf ))
		case $o in
		0) set_ r.28.10 $(get pirl.$p.$rs.$k) ;;
		3) set_ pirl.$p.$rs.$k $(get r.28.10) ;;
		esac
		set_ r.28.9 $(( val & 0x7fff ))
	elif [ $dev -eq 27 ] && [ $reg -eq 5 ]; then
		o=$(( (val >> 12) & 7 ))
		r6=$(( $(get r.27.6) )); vid=$(( (r6 & 0xfff) | ((r6 & 0x2000) ? 0x1000 : 0) ))
		case $o in
		3)
			if [ $(( r6 & 0x1000 )) -ne 0 ]; then
				echo "$(get r.27.7) $(get r.27.8) $(get r.27.2) $(get r.27.3)" > "$D/vtu.$vid"
			else
				rm -f "$D/vtu.$vid"
			fi ;;
		4)
			[ $vid -eq 8191 ] && vid=-1
			nx=$(ls "$D" | sed -n 's/^vtu\.//p' | sort -n | awk -v c=$vid '$1 > c { print; exit }')
			if [ -n "$nx" ]; then
				set_ r.27.6 $(( 0x1000 | (nx & 0xfff) | ((nx & 0x1000) ? 0x2000 : 0) ))
				set -- $(cat "$D/vtu.$nx")
				set_ r.27.7 $1; set_ r.27.8 $2; set_ r.27.2 $3; set_ r.27.3 $4
			else
				set_ r.27.6 0x2fff
			fi ;;
		5)
			sid=$(( $(get r.27.3) & 0x3f ))
			if [ $(( r6 & 0x1000 )) -ne 0 ]; then echo "$(get r.27.7) $(get r.27.8)" > "$D/stu.$sid"; else rm -f "$D/stu.$sid"; fi ;;
		6)
			sid=$(( $(get r.27.3) & 0x3f )); [ $sid -eq 63 ] && sid=-1
			nx=$(ls "$D" | sed -n 's/^stu\.//p' | sort -n | awk -v c=$sid '$1 > c { print; exit }')
			if [ -n "$nx" ]; then
				set_ r.27.6 0x1000; set_ r.27.3 $nx
				set -- $(cat "$D/stu.$nx"); set_ r.27.7 $1; set_ r.27.8 $2
			else
				set_ r.27.6 0x0000; set_ r.27.3 0x003f
			fi ;;
		esac
		set_ r.27.5 $(( val & 0x7fff ))
	elif [ $dev -eq 27 ] && [ $reg -eq 11 ]; then
		set_ r.27.11 $(( val & 0x7fff ))
	else
		set_ r.$dev.$reg $val
	fi
	;;
*) echo "unknown command"; exit 1 ;;
esac
'''

FAKE_NPU_RUN = r'''#!/bin/sh
echo call >> "$FAKE_SW/npu-calls"
[ -f "$FAKE_SW/npu-down" ] && { echo "the coprocessor did not answer" >&2; exit 1; }
exec /bin/sh -c "$2"
'''

SERIAL = {'3300': 'X AMDA0202-0004 Y', '136': 'X AMDA0201-0003 Y'}


def chip_defaults(d):
    """What the vendor's coprocessor leaves in the chip, as read on both appliances."""
    def put(name, v):
        with open(os.path.join(d, name), 'w') as f:
            f.write('0x%04x\n' % v)
    for p in range(11):
        put('r.%d.8' % p, 0x2480 if p else 0x2480)
        put('r.%d.9' % p, 0x0001)
        put('r.%d.10' % p, 0x8000)
        put('r.%d.13' % p, 0x0001 if p else 0x0000)
        put('r.%d.5' % p, 0x0000)
        put('r.%d.6' % p, 0x0001 if p else 0x07fe)
        put('pol.%d.48' % p, 0x1f)
        put('r.%d.14' % p, 0x301f)
    put('r.28.5', 0x041f)
    put('r.27.1', 0x0000)


@unittest.skipUnless(POSIX, 'switch.sh runs under /bin/sh')
class Chip(unittest.TestCase):
    MODEL = '3300'

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.dir = self.tmp.name
        self.sw = os.path.join(d, 'chip')
        lib = os.path.join(d, 'lib')
        for sub in (self.sw, os.path.join(lib, 'xgs'), os.path.join(d, 'run'), os.path.join(d, 'db')):
            os.makedirs(sub)
        shutil.copy(MODELS, os.path.join(lib, 'xgs', 'models.py'))
        self.script(os.path.join(lib, 'xgs', 'npu-run.sh'), FAKE_NPU_RUN)
        self.mvsw = os.path.join(d, 'mvsw')
        self.script(self.mvsw, FAKE_MVSW)
        chip_defaults(self.sw)
        self.state = os.path.join(d, 'db', 'switch.state')
        self.env = dict(os.environ, XGS_LIB=lib, XGS_PYTHON=sys.executable, XGS_SWITCH_MVSW=self.mvsw,
                        XGS_SWITCH_STATE=self.state, XGS_SWITCH_LOCKF='/nonexistent',
                        XGS_SWITCH_TMP=os.path.join(d, 'run'), FAKE_SW=self.sw,
                        XGS_PLANAR_SERIAL=SERIAL[self.MODEL], XGS_SYSTEM_VERSION='x')

    def tearDown(self):
        self.tmp.cleanup()

    @staticmethod
    def script(path, text):
        with open(path, 'w', newline='\n') as f:
            f.write(text)
        os.chmod(path, 0o755)

    def run_sw(self, *args):
        self.clear_log()
        p = subprocess.run(['/bin/sh', SWITCH] + list(args), env=self.env, capture_output=True, text=True,
                           timeout=600)
        self.out, self.err, self.rc = p.stdout, p.stderr, p.returncode
        self.check_read_before_write()
        return p.returncode

    def clear_log(self):
        for f in ('log', 'npu-calls', 'nreads'):
            try:
                os.remove(os.path.join(self.sw, f))
            except FileNotFoundError:
                pass

    def log(self):
        try:
            with open(os.path.join(self.sw, 'log')) as f:
                return [line.split() for line in f]
        except FileNotFoundError:
            return []

    def writes(self):
        return [(int(d), int(r), int(v, 16)) for op, d, r, v in self.log() if op == 'W']

    def changes(self):
        """The writes that change the chip: not a pointer set to read through, not a READ or GET_NEXT."""
        out = []
        for d, r, v in self.writes():
            if d <= 10 and r == 14 and not v & 0x8000:
                continue
            if (d, r) == (28, 9) and (v >> 13) & 3 == 0:
                continue
            if (d, r) == (27, 5) and (v >> 12) & 7 in (4, 6):
                continue
            if (d, r) in ((28, 10), (27, 6), (27, 7), (27, 8), (27, 2), (27, 3)):
                continue
            out.append((d, r, v))
        return out

    def reg(self, name):
        with open(os.path.join(self.sw, name)) as f:
            return int(f.read().strip(), 16)

    def set_reg(self, name, v):
        with open(os.path.join(self.sw, name), 'w') as f:
            f.write('0x%04x\n' % v)

    def npu_calls(self):
        try:
            with open(os.path.join(self.sw, 'npu-calls')) as f:
                return len(f.readlines())
        except FileNotFoundError:
            return 0

    def state_lines(self):
        try:
            with open(self.state) as f:
                return [line.strip() for line in f if line.strip() and not line.startswith('#')]
        except FileNotFoundError:
            return []

    # ---- the rule every run is held to ----
    ENGINE = {(28, 9), (28, 10), (27, 5), (27, 6), (27, 7), (27, 8), (27, 2), (27, 3), (27, 11), (27, 12)}

    def check_read_before_write(self):
        """Every register the run wrote was read before in it, through whatever reaches it."""
        read = set()
        pirl_read = set()
        pol_read = set()
        pending_pol = {}
        for op, d, r, v in self.log():
            d, r, v = int(d), int(r), int(v, 16)
            if op == 'R':
                read.add((d, r))
                if d <= 10 and r == 14 and d in pending_pol:
                    pol_read.add((d, pending_pol.pop(d)))
                continue
            if d <= 10 and r == 14:
                ptr = (v >> 8) & 0x3f
                if v & 0x8000:
                    self.assertIn((d, ptr), pol_read, 'policy pointer 0x%02x of port %d written unread' % (ptr, d))
                else:
                    pending_pol[d] = ptr
                continue
            if (d, r) == (28, 9):
                key = ((v >> 8) & 0x1f, (v >> 5) & 7, v & 0xf)
                op_ = (v >> 13) & 3
                if op_ == 0:
                    pirl_read.add(key)
                else:
                    self.fail('PIRL written (it is held): %s 0x%04x' % (key, v))
                continue
            if (d, r) == (27, 5):
                op_ = (v >> 12) & 7
                if op_ not in (4, 6):
                    self.fail('VLAN table or STU written (it is held): operation %d, 0x%04x' % (op_, v))
                continue
            if (d, r) == (27, 11):
                self.fail('ATU operation (the VLAN table is held): 0x%04x' % v)
            if d <= 10 and r == 13:
                self.fail('TCAM mode of port %d written (the VLAN table is held): 0x%04x' % (d, v))
            if (d, r) in self.ENGINE:
                continue
            self.assertIn((d, r), read, 'register 0x%02x of device %d written unread' % (r, d))

    # ---- front ports ----
    def test_unknown_ports_refused_before_anything(self):
        for args in (['limit', 'set', 'Port99', '100'], ['limit', 'set', '3', '100'],
                     ['limit', 'set', 'F1', '100'], ['mirror', 'on', 'Port3', 'Port12'],
                     ['show', 'eth0']):
            self.assertEqual(self.run_sw(*args), 64, args)
            self.assertEqual(self.npu_calls(), 0, args)
            self.assertEqual(self.writes(), [], args)
        self.assertEqual(self.state_lines(), [])

    def test_names_are_case_blind_and_map_to_switch_ports(self):
        self.assertEqual(self.run_sw('limit', 'set', 'port3', '100'), 0, self.err)
        self.assertEqual(self.reg('r.3.10'), 0x8005)
        self.assertEqual(self.run_sw('limit', 'set', 'F4', '100'), 0, self.err)
        self.assertEqual(self.reg('r.10.10'), 0x8005)

    # ---- egress limit ----
    def test_limit_set_off_puts_back_and_show_equal(self):
        self.assertEqual(self.run_sw('show'), 0, self.err)
        before = self.out
        self.assertEqual(self.changes(), [], 'show changed the chip')
        self.assertEqual(self.run_sw('limit', 'set', 'Port3', '100'), 0, self.err)
        self.assertEqual(self.reg('r.3.10'), 0x8005)
        self.assertIn('limit port=3 mbit=100 set=0x8005 found=0x8000', self.state_lines())
        self.assertEqual(self.run_sw('show'), 0)
        self.assertIn('Port3 (switch port 3): mirror none; egress limit about 96.0 Mbit/s', self.out)
        self.assertEqual(self.run_sw('limit', 'off', 'Port3'), 0, self.err)
        self.assertEqual(self.reg('r.3.10'), 0x8000)
        self.assertEqual(self.state_lines(), [])
        # read, written, read again
        ops = [(o, int(d), int(r)) for o, d, r, v in self.log() if (int(d), int(r)) == (3, 10)]
        self.assertEqual(ops[0][0], 'R')
        self.assertEqual(ops[-1][0], 'R')
        self.assertIn(('W', 3, 10), ops)
        self.assertEqual(self.run_sw('show'), 0)
        self.assertEqual(self.out, before)

    def test_show_gives_a_limit_with_another_edec_by_umsds_scale(self):
        # switch_qos.py's 900 Mbit/s: eDec 90, N 50
        self.set_reg('r.4.9', 0x005a)
        self.set_reg('r.4.10', 0x8032)
        self.assertEqual(self.run_sw('show', 'Port4'), 0)
        self.assertIn("egress limit 900000 kbit/s at layer 2 by UMSD's scale (eDec 90, N 50)", self.out)
        self.assertEqual(self.changes(), [])

    def test_limit_refused_when_someone_else_set_one(self):
        self.set_reg('r.4.10', 0x8010)
        self.assertEqual(self.run_sw('limit', 'set', 'Port4', '100'), 1)
        self.assertEqual(self.writes(), [])
        self.assertIn('not as this tool keeps it', self.err)

    def test_limit_above_480_refused(self):
        self.assertEqual(self.run_sw('limit', 'set', 'Port4', '600'), 64)
        self.assertEqual(self.npu_calls(), 0)

    def test_put_back_that_does_not_stick_is_said_and_kept(self):
        self.assertEqual(self.run_sw('limit', 'set', 'Port3', '50'), 0, self.err)
        open(os.path.join(self.sw, 'stuck.3.10'), 'w').close()
        self.assertEqual(self.run_sw('limit', 'off', 'Port3'), 1)
        self.assertIn('PUT BACK DIFFERS', self.err)
        self.assertTrue(any(line.startswith('limit port=3 ') for line in self.state_lines()))
        os.remove(os.path.join(self.sw, 'stuck.3.10'))
        self.assertEqual(self.run_sw('limit', 'off', 'Port3'), 0, self.err)
        self.assertEqual(self.reg('r.3.10'), 0x8000)

    # ---- three reads that agree ----
    def test_reads_that_never_agree_write_nothing(self):
        with open(os.path.join(self.sw, 'flicker'), 'w') as f:
            f.write('3\n')
        self.assertEqual(self.run_sw('limit', 'set', 'Port3', '100'), 1)
        self.assertIn('did not read the same three times', self.err)
        self.assertEqual(self.writes(), [])
        self.assertEqual(self.state_lines(), [])
        reads = [x for x in self.log() if x[0] == 'R' and (int(x[1]), int(x[2])) == (3, 10)]
        self.assertEqual(len(reads), 12, 'four rounds of three')

    def test_a_flicker_now_and_then_is_read_again(self):
        with open(os.path.join(self.sw, 'flicker'), 'w') as f:
            f.write('7\n')
        self.assertEqual(self.run_sw('limit', 'set', 'Port3', '100'), 0, self.err)
        self.assertEqual(self.reg('r.3.10'), 0x8005)
        self.assertIn('limit port=3 mbit=100 set=0x8005 found=0x8000', self.state_lines())
        self.assertEqual(self.run_sw('limit', 'off', 'Port3'), 0, self.err)
        self.assertEqual(self.reg('r.3.10'), 0x8000)

    # ---- PIRL: held, read only ----
    def test_pirl_is_no_command(self):
        for args in (['pirl', 'set', 'Port4', '100'], ['pirl', 'off', 'Port4']):
            self.assertEqual(self.run_sw(*args), 64, args)
            self.assertEqual(self.npu_calls(), 0, args)
            self.assertEqual(self.writes(), [], args)
        self.assertEqual(self.state_lines(), [])

    def test_show_reads_an_ingress_limit_set_elsewhere(self):
        # UMSD's words at 100 Mbit/s: BktIncrement 5, BRF 1 (item M9)
        self.set_reg('pirl.4.0.2', 0x8005)
        self.set_reg('pirl.4.0.3', 1)
        self.assertEqual(self.run_sw('show', 'Port4'), 0)
        self.assertIn('ingress limit about 100.0 Mbit/s (BktIncrement 5, BRF 1', self.out)
        self.assertEqual([w for w in self.writes() if w[:2] == (28, 10)], [])
        self.assertEqual(self.reg('pirl.4.0.2'), 0x8005)

    # ---- mirror ----
    def test_mirror_on_off(self):
        self.assertEqual(self.run_sw('show'), 0)
        before = self.out
        self.assertEqual(self.run_sw('mirror', 'on', 'Port3', 'Port4', 'both'), 0, self.err)
        self.assertEqual(self.reg('r.3.8'), 0x24b0)
        self.assertEqual(self.reg('r.28.5') & 0x1f, 4)
        self.assertEqual(self.reg('pol.3.48'), 4)
        self.assertEqual(self.run_sw('mirror', 'on', 'Port5', 'Port6'), 1)
        self.assertIn('off first', self.err)
        self.assertEqual(self.run_sw('show'), 0)
        self.assertIn('Port3 (switch port 3): mirror ingress to Port4, egress to Port4', self.out)
        # a jumbo setting changed meanwhile is not undone by the put-back
        self.set_reg('r.3.8', 0x14b0)
        self.assertEqual(self.run_sw('mirror', 'off'), 0, self.err)
        self.assertEqual(self.reg('r.3.8'), 0x1480)
        self.set_reg('r.3.8', 0x2480)
        self.assertEqual(self.reg('r.28.5'), 0x041f)
        self.assertEqual(self.reg('pol.3.48'), 0x1f)
        self.assertEqual(self.run_sw('show'), 0)
        self.assertEqual(self.out, before)

    def test_mirror_second_refused(self):
        self.assertEqual(self.run_sw('mirror', 'on', 'Port3', 'Port4', 'in'), 0, self.err)
        self.assertEqual(self.run_sw('mirror', 'on', 'Port5', 'Port6', 'out'), 1)
        self.assertEqual(self.reg('r.28.5'), 0x041f)
        self.assertEqual(self.run_sw('mirror', 'off'), 0, self.err)
        self.assertEqual(self.reg('r.3.8'), 0x2480)

    # ---- VLAN table: held, read only ----
    def test_vlan_add_del_refused(self):
        for args in (['vlan', 'add', '300', 'Port3', 'Port4', '--tcam-off'], ['vlan', 'add', '300', 'Port3'],
                     ['vlan', 'del', '300']):
            self.assertEqual(self.run_sw(*args), 64, args)
            self.assertIn('HELD', self.err, args)
            self.assertIn('usage:', self.err, args)
            self.assertEqual(self.npu_calls(), 0, args)
            self.assertEqual(self.log(), [], args)
        self.assertEqual(self.state_lines(), [])
        self.assertFalse(any(f.startswith(('vtu.', 'stu.')) for f in os.listdir(self.sw)))
        self.assertEqual(self.reg('r.3.13'), 0x0001)

    def test_vlan_show_reads_a_table_set_elsewhere(self):
        with open(os.path.join(self.sw, 'vtu.300'), 'w') as f:
            f.write('0xfebf 0x003f 0x012c 0x0000\n')
        self.assertEqual(self.run_sw('vlan', 'show'), 0, self.err)
        self.assertIn('VLAN table (VTU): 1 entry', self.out)
        self.assertIn('VID 300: Port3 tagged, Port4 tagged; FID 300; SID 0', self.out)
        self.assertIn('TCAM mode: Port1 48-byte', self.out)
        self.assertEqual(self.changes(), [], 'vlan show changed the chip')
        self.assertEqual(self.run_sw('show'), 0, self.err)
        self.assertIn('VID 300: Port3 tagged, Port4 tagged; FID 300; SID 0', self.out)
        self.assertEqual(self.changes(), [], 'show changed the chip')
        with open(os.path.join(self.sw, 'vtu.300')) as f:
            self.assertEqual(f.read(), '0xfebf 0x003f 0x012c 0x0000\n')

    # ---- after a coprocessor boot ----
    def test_reapply_after_a_boot(self):
        self.assertEqual(self.run_sw('limit', 'set', 'Port3', '100'), 0, self.err)
        self.assertEqual(self.run_sw('mirror', 'on', 'Port5', 'Port6', 'out'), 0, self.err)
        kept = self.state_lines()
        # the coprocessor boots: the chip as it leaves it
        for f in os.listdir(self.sw):
            if re.match(r'(r|pol|pirl|vtu|stu)\.', f):
                os.remove(os.path.join(self.sw, f))
        chip_defaults(self.sw)
        self.assertEqual(self.run_sw('reapply'), 0, self.err)
        self.assertEqual(self.reg('r.3.10'), 0x8005)
        self.assertEqual(self.reg('r.5.8'), 0x24a0)
        self.assertEqual(sorted(self.state_lines()), sorted(kept))
        # set already: a second reapply changes nothing
        self.assertEqual(self.run_sw('reapply'), 0, self.err)
        self.assertEqual(self.changes(), [])
        for args in (['limit', 'off', 'Port3'], ['mirror', 'off']):
            self.assertEqual(self.run_sw(*args), 0, (args, self.err))
        self.assertEqual(self.state_lines(), [])

    def test_coprocessor_not_answering(self):
        open(os.path.join(self.sw, 'npu-down'), 'w').close()
        self.assertEqual(self.run_sw('limit', 'set', 'Port3', '100'), 1)
        self.assertIn('did not answer', self.err)
        self.assertEqual(self.state_lines(), [])


class Chip136(Chip):
    """The XGS 136: its cages are switch ports 9 and 10, its Port9 to Port12 on the coprocessor's MACs."""
    MODEL = '136'

    def test_names_are_case_blind_and_map_to_switch_ports(self):
        self.assertEqual(self.run_sw('limit', 'set', 'F1', '100'), 0, self.err)
        self.assertEqual(self.reg('r.9.10'), 0x8005)
        self.assertEqual(self.run_sw('limit', 'set', 'Port9', '100'), 64)
        self.assertIn("coprocessor's own MAC", self.err)

    def test_unknown_ports_refused_before_anything(self):
        for args in (['limit', 'set', 'Port13', '100'], ['limit', 'set', 'Port10', '100'], ['show', '9']):
            self.assertEqual(self.run_sw(*args), 64, args)
            self.assertEqual(self.npu_calls(), 0, args)


if __name__ == '__main__':
    unittest.main()
