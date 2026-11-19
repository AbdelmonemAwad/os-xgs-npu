#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/switch_qos.py, the switch chip's queues for the plugins, in two parts.

The register words, as functions (any system):
  - THE TCAM ENTRY for a DSCP, a mask and a queue: the words measured on the XGS 3300 on 2026-11-11
    and 2026-11-17 for EF to queue 7, every other register of pages 0 and 2 zero, and the operation
    words of the load the lab made (flush 0xa003, page 2 0xb803 ... page 0 0xb003); what a read-back
    compares - the seven registers the lab read back after a load;
  - QUEUE CONTROL's scheduling words, the WEIGHT TABLE's pairs and length, PORT 0'S FLOW CONTROL's
    two words (0x9088 the pause off, 0x908c on as the coprocessor leaves it), and the EGRESS LIMIT's
    eDec and N for 100, 480, 900 and 1000 Mbit/s as UMSD's macro gives them, a rate between steps
    taken down, and 0x09's bits other than 6:0 kept;
  - the settings file's refusals: a third class, a weight sequence that leaves a queue out, a port
    that is not behind the switch; the census of the vendor's entries, and what counts as moved.

The runs, against a made-up chip (a POSIX shell): npu-run.sh is a stand-in that runs the command with
/bin/sh - and stops answering after a number of sessions, as a link that drops or a host stopped in
the middle of a run; mvsw a stand-in that keeps each register in a file and acts as the chip's TCAM,
queue-control, weight-table, flow-control and counter engines do; models.py is the real one, told the
model through XGS_PLANAR_SERIAL. Every run is held to: each register written read first in the same
run, port 0's own scheduling and limit never written, no flow control written but port 0's pointer 0x10
and that only 0x88 or 0x8c, no TCAM entry but 3 and 255 ever flushed or loaded. And:
  - apply sets what the settings file asks, keeps what it found, sets nothing twice, and clear puts it
    all back and reads it again - show the same before and after;
  - an entry or an egress limit that is not this tool's refuses the run before anything is written;
  - a set that does not read back puts back everything the run changed;
  - a run cut at each step - weights, a limit, an entry - leaves what was found kept, and the next run
    finishes from there, the cut run's write taken as this tool's when it landed;
  - clear removes the settings file first, never sets again what it has put back, and keeps in the
    state what it could not; a boot with no settings file only puts back;
  - a load that does not read back as written is flushed in its own session, and a run during which
    the vendor's entries change is not kept;
  - port 0's pause: with weights, turned off after them - 0x9088, read back 0x1088 - and only over
    the 0x8c the coprocessor leaves, any other value refusing the run before a write; put back first,
    0x908c, when the weights go; left as found by strict classes; kept in the state, a run cut before
    it finished by the next; a set of it that does not read back putting the whole run back, a
    put-back of it that does not kept for the next run;
  - check reads entries 3 and 255, port 0's mode and its pause again;
  - after a coprocessor boot, reapply sets it all again, port 0's pause last, and keeps the found
    values;
  - writes on the XGS 3300 alone: the XGS 3100 and the XGS 136 refused before the coprocessor is asked;
    on the XGS 136 show reads port registers alone, no TCAM operation; no switch tool on the
    coprocessor, said, and nothing read.

Nothing here comes from an appliance.

    python3 tools/test-switch-qos.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
SCRIPTS = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs')
TOOL = os.path.join(SCRIPTS, 'switch_qos.py')
MODELS = os.path.join(SCRIPTS, 'models.py')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'

sys.path.insert(0, SCRIPTS)
import switch_qos as sq  # noqa: E402

# The chip. Registers in files r.<dev>.<reg> (decimal, values 0x%04x). The pointer registers - Queue
# Control (port register 0x1c) and the weight table (Global2 0x1c) - keep what a write with bit 15 sets
# in p.<dev>.<pointer>, and the ports' flow control (port register 0x02) in fc.<dev>.<pointer>; TCAM
# entries in t.<entry>.<page>.<reg>, loaded and read through the data registers r.31.2 to r.31.27 by
# the operation register r.31.0; counters in ctr.<port>.<counter>.<bank>.
# Every read and write goes to the log. "flicker" N: every Nth read answers 0x7bad; "stuck.<dev>.<reg>":
# writes there go nowhere; "once.<dev>.<reg>": the first write with bit 15 there goes through, and the
# register is stuck after it; "fcstuck.<dev>.<pointer>": writes with bit 15 to that flow-control pointer
# go nowhere, the pointer still taken; "tstuck.<entry>": loads of that entry go nowhere; "tmix.<entry>":
# a load of that entry's page 0 lands with another word in octet 19's register, as the agent's
# operation between two of ours would leave it; "agent": every load also changes the vendor's entry 4,
# as the agent arming a port would.
FAKE_MVSW = r'''#!/bin/sh
D=${FAKE_SW:?}
[ "$1" = 0 ] && [ "$2" = 2 ] || { echo "usage"; exit 1; }
op=$3; dev=$(( $4 )); reg=$(( $5 ))
get() { cat "$D/$1" 2>/dev/null || echo ${2:-0x0000}; }
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
	[ $(( val & 0x8000 )) -ne 0 ] && [ -f "$D/once.$dev.$reg" ] && { rm -f "$D/once.$dev.$reg"; : > "$D/stuck.$dev.$reg"; }
	if { [ $dev -le 10 ] || [ $dev -eq 28 ]; } && [ $reg -eq 28 ]; then
		ptr=$(( (val >> 8) & 0x7f ))
		[ $(( val & 0x8000 )) -ne 0 ] && set_ p.$dev.$ptr $(( val & 0xff ))
		set_ r.$dev.28 $(( (ptr << 8) | $(get p.$dev.$ptr) ))
	elif [ $dev -eq 31 ] && [ $reg -eq 0 ]; then
		o=$(( (val >> 12) & 7 )); pg=$(( (val >> 10) & 3 )); e=$(( val & 0xff ))
		case $o in
		2) rm -f "$D"/t.$e.* ;;
		3) if [ ! -f "$D/tstuck.$e" ]; then
			for g in $(seq 2 27); do set_ t.$e.$pg.$g $(get r.31.$g); done
			[ $pg -eq 0 ] && [ -f "$D/tmix.$e" ] && set_ t.$e.0.25 0xfc00
			[ -f "$D/agent" ] && set_ t.4.0.3 0xff10
		   fi ;;
		4) nx=$(ls "$D" | sed -n 's/^t\.\([0-9]*\)\.0\.2$/\1/p' | sort -n | while read x; do
			[ "$x" -gt $e ] && [ "$(get t.$x.0.2)" != 0x00ff ] && { echo $x; break; }; done)
		   e=${nx:-255} ;;
		5) for g in $(seq 2 27); do d=0x0000; [ $pg -eq 0 ] && [ $g -eq 2 ] && d=0x00ff; set_ r.31.$g $(get t.$e.$pg.$g $d); done ;;
		esac
		set_ r.31.0 $(( (o << 12) | (pg << 10) | e ))
	elif [ $dev -eq 27 ] && [ $reg -eq 29 ]; then
		p=$(( ((val >> 5) & 0x1f) - 1 )); c=$(( val & 0x1f )); bk=$(( (val >> 10) & 1 ))
		v=$(cat "$D/ctr.$p.$c.$bk" 2>/dev/null || echo 0)
		set_ r.27.30 $(( (v >> 16) & 0xffff )); set_ r.27.31 $(( v & 0xffff ))
		set_ r.27.29 $(( val & 0x7fff ))
	elif [ $dev -le 10 ] && [ $reg -eq 2 ]; then
		ptr=$(( (val >> 8) & 0x7f ))
		[ $(( val & 0x8000 )) -ne 0 ] && [ ! -f "$D/fcstuck.$dev.$ptr" ] && set_ fc.$dev.$ptr $(( val & 0xff ))
		set_ r.$dev.2 $(( (ptr << 8) | $(get fc.$dev.$ptr) ))
	else
		set_ r.$dev.$reg $val
	fi
	;;
*) echo "unknown command"; exit 1 ;;
esac
'''

# npu-run.sh: each session counted; with a file "cut" holding N, sessions after the Nth do not answer
FAKE_NPU_RUN = r'''#!/bin/sh
echo call >> "$FAKE_SW/npu-calls"
n=$(wc -l < "$FAKE_SW/npu-calls")
[ -f "$FAKE_SW/npu-down" ] && { echo "the coprocessor did not answer" >&2; exit 1; }
[ -f "$FAKE_SW/cut" ] && [ $n -gt $(cat "$FAKE_SW/cut") ] && { echo "the link went down" >&2; exit 1; }
exec /bin/sh -c "$2"
'''

FAKE_LOGGER = r'''#!/bin/sh
while [ $# -gt 1 ]; do shift; done
echo "$1" >> "$FAKE_SW/syslog"
'''

SERIAL = {'3300': 'X AMDA0202-0004 Y', '3100': 'X AMDA0202-0003 Y', '136': 'X AMDA0201-0003 Y'}


def default_weights():
    """A sequence of UMSD's default weights, 33, 25, 17, 12, 6, 3, 2 and 1 for queues 7 to 0: 99 turns,
    the length the XGS 3300 reads (its own order was not read)."""
    counts = {7: 33, 6: 25, 5: 17, 4: 12, 3: 6, 2: 3, 1: 2, 0: 1}
    cur = dict.fromkeys(counts, 0)
    seq = []
    for _ in range(99):
        for q in counts:
            cur[q] += counts[q]
        best = max(counts, key=lambda q: (cur[q], q))
        cur[best] -= 99
        seq.append(best)
    return seq


DEFAULT_WEIGHTS = default_weights()


def chip_defaults(d):
    """What the vendor's coprocessor leaves in the chip, as read on the second XGS 3300 on 2026-11-17;
    the flow control as read there on 2026-11-19 - port 0's 0x8c and port 8's 0x08, given to every
    front port here."""
    def put(name, v):
        with open(os.path.join(d, name), 'w') as f:
            f.write('0x%04x\n' % v)
    for p in range(11):
        put('r.%d.4' % p, 0x007f if p else 0x017f)
        put('r.%d.8' % p, 0x2480 if p else 0x2080)
        put('r.%d.9' % p, 0x0001)
        put('r.%d.10' % p, 0x8000)
        put('r.%d.13' % p, 0x0001 if p else 0x0000)
        put('p.%d.0' % p, 0x00)
        put('fc.%d.16' % p, 0x08 if p else 0x8c)
    put('p.28.64', len(DEFAULT_WEIGHTS))
    put('p.28.65', 0x01)
    for i in range(0, len(DEFAULT_WEIGHTS), 2):
        a = DEFAULT_WEIGHTS[i]
        b = DEFAULT_WEIGHTS[i + 1] if i + 1 < len(DEFAULT_WEIGHTS) else 0
        put('p.28.%d' % (i // 2), a | (b << 4))
    # the vendor's entries around the two free ones: 0, 1, 2, 4 and its drop-all 254
    for e, k1 in ((0, 0xc000), (1, 0xc100), (2, 0xc600), (4, 0xc700), (254, 0xc000)):
        put('t.%d.0.2' % e, k1)
    put('t.1.0.3', 0x4500)
    put('t.2.0.3', 0xbb00)
    put('t.4.0.3', 0xff02)
    put('t.254.2.6', 0xc000)
    put('t.254.2.27', 0x4000)


class Words(unittest.TestCase):
    """The register words, as UMSD and the lab's measured writes have them."""

    def test_tcam_entry_for_ef_is_the_measured_one(self):
        page0, page2 = sq.tcam_words(46, 63, 7, 1)
        # lab-tcam0.sh's qpri action and lab-tp-p0.sh's LOAD3, measured on 2026-11-11 and 2026-11-17
        self.assertEqual({r: v for r, v in page0.items() if v},
                         {0x02: 0xc740, 0x03: 0xff01, 0x16: 0xff08, 0x17: 0xff00, 0x19: 0xfcb8})
        self.assertEqual({r: v for r, v in page2.items() if v}, {0x03: 0x00f0, 0x1b: 0x5000})
        self.assertEqual(sorted(page0), list(range(0x02, 0x1c)))
        self.assertEqual(sorted(page2), [2, 3, 4, 5, 6, 7, 8, 0x1b])

    def test_a_read_back_compares_what_the_lab_read_back(self):
        # LOAD3 read back page 0's 0x02 0x03 0x16 0x17 0x19 and page 2's 0x03 0x1b after its load
        self.assertEqual(sq.P0_CHECKED, (0x02, 0x03, 0x16, 0x17, 0x19))
        self.assertEqual(sq.P2_CHECKED, (0x03, 0x1b))
        self.assertEqual(sq.checked({'dscp': 46, 'mask': 63, 'queue': 7}, 3),
                         {'p0': {'2': 0xc740, '3': 0xff01, '22': 0xff08, '23': 0xff00, '25': 0xfcb8},
                          'p2': {'3': 0x00f0, '27': 0x5000}})
        self.assertIsNone(sq.checked(None, 3))
        self.assertEqual(sq.checked({'dscp': 34, 'mask': 63, 'queue': 6}, 255)['p2'], {'3': 0x00e0, '27': 0x6000})

    def test_tcam_entry_mask_queue_and_counter(self):
        # CS5 to EF, DSCP 40-47 under mask 0x38: TOS 0xa0 under 0xe0; queue 6, counter 2 (entry 255)
        page0, page2 = sq.tcam_words(40, 0x38, 6, 2)
        self.assertEqual(page0[0x19], 0xe0a0)
        self.assertEqual(page2[0x03], 0x00e0)
        self.assertEqual(page2[0x1b], 0x6000)
        # AF41 exactly; the bits of the DSCP outside its mask do not reach the key
        self.assertEqual(sq.tcam_words(34, 63, 5, 1)[0][0x19], 0xfc88)
        self.assertEqual(sq.tcam_words(47, 0x38, 5, 1)[0][0x19], 0xe0a0)
        self.assertEqual(sq.tcam_words(0, 63, 0, 1)[1][0x03], 0x0080)

    def test_tcam_operations_as_the_lab_wrote_them(self):
        self.assertEqual(sq.tcam_op(sq.OP_FLUSH, 3), 0xa003)
        self.assertEqual(sq.tcam_op(sq.OP_READ, 3, 2), 0xd803)
        self.assertEqual(sq.tcam_op(sq.OP_LOAD, 3, 2), 0xb803)
        self.assertEqual(sq.tcam_op(sq.OP_READ, 3, 1), 0xd403)
        self.assertEqual(sq.tcam_op(sq.OP_LOAD, 3, 1), 0xb403)
        self.assertEqual(sq.tcam_op(sq.OP_READ, 3, 0), 0xd003)
        self.assertEqual(sq.tcam_op(sq.OP_LOAD, 3, 0), 0xb003)
        self.assertEqual(sq.tcam_op(sq.OP_NEXT, 2), 0xc002)
        self.assertEqual(sq.tcam_op(sq.OP_LOAD, 255, 0), 0xb0ff)
        self.assertEqual(sq.COUNTER, {3: 1, 255: 2})

    def test_queue_control_scheduling(self):
        # weighted 0x8000, strict for queue 7 0x8001, strict all 0x8007: the lab's writes on port 8
        self.assertEqual([sq.sched_word(m) for m in (0, 1, 2, 7)], [0x8000, 0x8001, 0x8002, 0x8007])

    def test_weight_table(self):
        # UMSD's own example sequence, 7,6,5,7,1,6,7,4: pairs at pointers 0-3, the length at 0x40
        self.assertEqual(sq.weight_words([7, 6, 5, 7, 1, 6, 7, 4]), [0x8067, 0x8175, 0x8261, 0x8347, 0xc008])
        self.assertEqual(sq.weight_words([1, 2, 3]), [0x8021, 0x8103, 0xc003])
        self.assertEqual(sq.weight_words(DEFAULT_WEIGHTS)[-1], 0xc063)
        self.assertEqual(len(sq.weight_words([0] * 128)), 65)
        self.assertEqual(sq.weight_words([0] * 128)[-1], 0xc080)

    def test_port0s_flow_control(self):
        # pointer 0x10 of port register 0x02: the lab's writes on 2026-11-19, read back 0x1088 and 0x108c
        self.assertEqual(sq.FC, 0x10)
        self.assertEqual((sq.PAUSE_ON, sq.PAUSE_OFF), (0x8c, 0x88))
        self.assertEqual(sq.pause_word(sq.PAUSE_OFF), 0x9088)
        self.assertEqual(sq.pause_word(sq.PAUSE_ON), 0x908c)
        # UMSD's gprtSetFlowCtrl with en false: bits 2:0 cleared, ForcedFC kept, bit 7 kept
        self.assertEqual((sq.PAUSE_ON & ~0x07) | 0x08, sq.PAUSE_OFF)
        # set last, put back first
        steps = [('weights', [0], [1]), ('pause', 0x8c, 0x88), ('mode', 0, 1), ('limit:8', [1, 0x8000], [1, 0x8005]),
                 ('entry:3', None, {'dscp': 46, 'mask': 63, 'queue': 7})]
        self.assertEqual([s[0] for s in sq.order(steps)], ['limit:8', 'entry:3', 'mode', 'weights', 'pause'])
        back = [('weights', [1], [0]), ('limit:8', [1, 0x8005], [1, 0x8000]), ('pause', 0x88, 0x8c)]
        self.assertEqual([s[0] for s in sq.order(back)], ['pause', 'limit:8', 'weights'])

    def test_egress_limit_as_umsd_gives_it(self):
        # (eDec, N, kbit/s): AMETHYST_MSD_GET_RATE_LIMIT_PER_BYTE, N = 8,000,000 * eDec / (16 * kbit/s) up
        self.assertEqual(sq.egress(100000), (100, 500, 100000))
        self.assertEqual(sq.egress(480000), (48, 50, 480000))
        self.assertEqual(sq.egress(900000), (90, 50, 900000))
        self.assertEqual(sq.egress(1000000), (100, 50, 1000000))
        self.assertEqual(sq.egress(96000), (96, 500, 96000))
        self.assertEqual(sq.egress(2500000), (25, 5, 2500000))
        self.assertEqual(sq.egress(640), (10, 7813, 639))
        # between the steps: taken down to one
        self.assertEqual(sq.egress(905000), (90, 50, 900000))
        self.assertEqual(sq.egress(155000), (15, 50, 150000))
        self.assertEqual(sq.egress(1500), (1, 500, 1000))
        for bad in (0, 63, 10000001, -5, '100', None, True):
            self.assertIsNone(sq.egress(bad), bad)

    def test_egress_limit_registers(self):
        # 0x09 read first, only bits 6:0 changed; 0x0a layer-2 bytes and N in bits 13:0
        self.assertEqual(sq.limit_words(900000, 0x0001), (0x005a, 0x8032))
        self.assertEqual(sq.limit_words(100000, 0xff81), (0xffe4, 0x81f4))
        self.assertEqual(sq.limit_words(640, 0x0001), (0x000a, 0x9e85))
        self.assertEqual(sq.limit_kbit(0x0001, 0x8005), 100000)
        self.assertEqual(sq.limit_kbit(0x005a, 0x8032), 900000)
        self.assertIsNone(sq.limit_kbit(0x0001, 0x8000))
        self.assertEqual(sq.limit_kbit(0x0001, 0x03e8), 'frames')

    def test_the_census_and_what_moves_in_it(self):
        before = {'0k1': '0xc000', '0k2': '0x0000', '4k1': '0xc700', '4k2': '0xff02', 'next2': '4', 'next3': '4'}
        self.assertEqual(sq.census_moved(before, dict(before), False), [])
        # this run's own entry 3 is what get-next after 2 finds
        self.assertEqual(sq.census_moved(before, dict(before, next2='3'), True), [])
        self.assertEqual(sq.census_moved(before, dict(before, next2='3'), False), ['get-next after 2 3, not 4'])
        self.assertEqual(sq.census_moved(before, dict(before, **{'4k2': '0xff10'}), False), ['4k2 0xff02 -> 0xff10'])
        self.assertTrue(sq.census_moved(before, dict(before, next3='5'), False))
        self.assertTrue(sq.census_moved(before, dict(before, **{'0k1': 'bad'}), False))

    def test_writes_on_the_xgs_3300_alone(self):
        self.assertEqual(sq.writable('AMDA0202-0004', 'octeontx', {'Port1': 1}), '')
        self.assertEqual(sq.writable('AMDA0202-0003', 'octeontx', {'Port1': 1}), 'untested-model')
        self.assertEqual(sq.writable('AMDA0201-0003', 'armada', {'Port1': 1}), 'read-only')
        self.assertEqual(sq.writable('', '', {}), 'no-switch')


class Settings(unittest.TestCase):
    PORTS = {'Port%d' % n: n for n in range(1, 9)}
    PORTS.update({'F3': 9, 'F4': 10})

    def parse(self, conf):
        return sq.parse_conf(conf, self.PORTS)

    def test_a_whole_file(self):
        got = self.parse({'classes': [{'dscp': 46, 'mask': 63, 'queue': 7}, {'dscp': 34, 'queue': 6}],
                          'scheduling': {'Port8': 0, '*': 1}, 'weights': list(range(8)),
                          'limits': {'port2': 100000}})
        self.assertEqual(got['classes'], [(46, 63, 7), (34, 63, 6)])
        self.assertEqual(got['sched'], {n: (0 if n == 8 else 1) for n in range(1, 11)})
        self.assertEqual(got['limits'], {2: 100000})

    def test_refusals(self):
        three = [{'dscp': d, 'queue': 7} for d in (46, 34, 26)]
        for conf in ({'classes': three}, {'classes': [{'dscp': 64, 'queue': 7}]},
                     {'classes': [{'dscp': 46, 'queue': 8}]}, {'classes': [{'dscp': 46, 'mask': 0, 'queue': 7}]},
                     {'classes': [{'dscp': 46, 'queue': 7}, {'dscp': 46, 'queue': 6}]},
                     {'classes': [{'dscp': '46', 'queue': 7}]},
                     {'weights': [7, 6, 5, 4, 3, 2, 1]}, {'weights': []}, {'weights': [0] * 129 + list(range(8))},
                     {'weights': list(range(8)) + [8]},
                     {'scheduling': {'Port8': 8}}, {'scheduling': {'F1': 1}}, {'scheduling': {'0': 1}},
                     {'limits': {'Port8': 63}}, {'limits': {'Port9': 100000}}, {'limits': {'Port8': '100'}},
                     [], 'x'):
            with self.assertRaises(sq.Refused, msg=json.dumps(conf)):
                self.parse(conf)

    def test_every_queue_named_in_the_weights(self):
        with self.assertRaises(sq.Refused) as raised:
            self.parse({'weights': [7, 7, 6, 0]})
        self.assertIn('missing 1, 2, 3, 4, 5', str(raised.exception))


@unittest.skipUnless(POSIX, 'the stand-in chip runs under /bin/sh')
class Chip(unittest.TestCase):
    MODEL = '3300'
    ONE = {'owner': 'test', 'classes': [{'dscp': 46, 'mask': 63, 'queue': 7}], 'scheduling': {'*': 1}}
    EF3 = {3: 0x00f0, 0x1b: 0x5000}

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.dir = self.tmp.name
        self.sw = os.path.join(d, 'chip')
        lib = os.path.join(d, 'lib')
        for sub in (self.sw, os.path.join(lib, 'xgs'), os.path.join(d, 'db'), os.path.join(d, 'run')):
            os.makedirs(sub)
        shutil.copy(MODELS, os.path.join(lib, 'xgs', 'models.py'))
        self.script(os.path.join(lib, 'xgs', 'npu-run.sh'), FAKE_NPU_RUN)
        self.mvsw = os.path.join(d, 'mvsw')
        self.script(self.mvsw, FAKE_MVSW)
        self.logger = os.path.join(d, 'logger')
        self.script(self.logger, FAKE_LOGGER)
        chip_defaults(self.sw)
        self.conf = os.path.join(d, 'db', 'switch-qos.json')
        self.state = os.path.join(d, 'db', 'switch-qos.state')
        self.hand = os.path.join(d, 'db', 'switch.state')
        self.env = dict(os.environ, XGS_LIB=lib, XGS_PYTHON=sys.executable, XGS_SWITCH_MVSW=self.mvsw,
                        XGS_SWITCH_QOS_CONF=self.conf, XGS_SWITCH_QOS_STATE=self.state, XGS_SWITCH_STATE=self.hand,
                        XGS_SWITCH_LOCK=os.path.join(d, 'run', 'lock'), XGS_SWITCH_LOGGER=self.logger,
                        XGS_SWITCH_QOS_BOOT_WAIT='0', XGS_SWITCH_QOS_BOOT_PAUSE='0', XGS_SWITCH_QOS_BOOT_TRIES='1',
                        FAKE_SW=self.sw, XGS_PLANAR_SERIAL=SERIAL[self.MODEL], XGS_SYSTEM_VERSION='x')

    def tearDown(self):
        self.tmp.cleanup()

    @staticmethod
    def script(path, text):
        with open(path, 'w', newline='\n') as f:
            f.write(text)
        os.chmod(path, 0o755)

    def want(self, conf):
        with open(self.conf, 'w') as f:
            json.dump(conf, f)

    def flag(self, name, text=''):
        with open(os.path.join(self.sw, name), 'w') as f:
            f.write(text)

    def unflag(self, *names):
        for name in names:
            try:
                os.remove(os.path.join(self.sw, name))
            except FileNotFoundError:
                pass

    def cut_after(self, sessions):
        """The coprocessor answers this many sessions of the next run, and then no more."""
        self.flag('cut', '%d\n' % sessions)

    def run_q(self, *args):
        self.unflag('log', 'npu-calls', 'nreads')
        p = subprocess.run([sys.executable, TOOL] + list(args), env=self.env, capture_output=True, text=True,
                           timeout=900)
        self.out, self.err, self.rc = p.stdout, p.stderr, p.returncode
        self.unflag('cut')
        self.check_the_rules()
        return p.returncode

    def log(self):
        try:
            with open(os.path.join(self.sw, 'log')) as f:
                return [line.split() for line in f]
        except FileNotFoundError:
            return []

    def writes(self):
        return [(int(d), int(r), int(v, 16)) for op, d, r, v in self.log() if op == 'W']

    def changes(self):
        """The writes that change the chip: not a pointer set to read through, not a TCAM read or
        get-next, not a counter's read."""
        out = []
        for d, r, v in self.writes():
            if (d <= 10 or d == 28) and r == 28 and not v & 0x8000:
                continue
            if d <= 10 and r == 2 and not v & 0x8000:
                continue
            if (d, r) == (31, 0) and (v >> 12) & 7 in (sq.OP_READ, sq.OP_NEXT):
                continue
            if d == 31 and r != 0:
                continue
            if (d, r) == (27, 29):
                continue
            out.append((d, r, v))
        return out

    def reg(self, name):
        with open(os.path.join(self.sw, name)) as f:
            return int(f.read().strip(), 16)

    def set_reg(self, name, v):
        with open(os.path.join(self.sw, name), 'w') as f:
            f.write('0x%04x\n' % v)

    def entry(self, e):
        """(page 0, page 2) of an entry as the chip holds it, the registers not set left out."""
        out = ({}, {})
        for name in os.listdir(self.sw):
            parts = name.split('.')
            if parts[0] == 't' and parts[1] == str(e) and parts[2] in ('0', '2'):
                v = self.reg(name)
                if v:
                    out[0 if parts[2] == '0' else 1][int(parts[3])] = v
        return out

    def weights(self):
        n = self.reg('p.28.64')
        seq = []
        for i in range((n + 1) // 2):
            v = self.reg('p.28.%d' % i)
            seq += [v & 7, (v >> 4) & 7]
        return seq[:n]

    def scheduling(self):
        return [self.reg('p.%d.0' % p) for p in range(1, 11)]

    def fc(self, port, pointer=0x10):
        """A port's flow-control byte as the chip holds it."""
        return self.reg('fc.%d.%d' % (port, pointer))

    def set_fc(self, port, v, pointer=0x10):
        self.set_reg('fc.%d.%d' % (port, pointer), v)

    def pause_writes(self):
        """What the run wrote to port 0's flow control, the pointer's writes aside."""
        return [v for d, r, v in self.changes() if (d, r) == (0, 2)]

    def npu_calls(self):
        try:
            with open(os.path.join(self.sw, 'npu-calls')) as f:
                return len(f.readlines())
        except FileNotFoundError:
            return 0

    def kept(self):
        """The state's items, or None when there is no state."""
        try:
            with open(self.state) as f:
                return json.load(f)['items']
        except FileNotFoundError:
            return None

    def syslog(self):
        try:
            with open(os.path.join(self.sw, 'syslog')) as f:
                return f.read()
        except FileNotFoundError:
            return ''

    def boot(self):
        """The coprocessor boots: the chip as it leaves it."""
        for f in os.listdir(self.sw):
            if f.split('.')[0] in ('r', 'p', 't', 'fc'):
                os.remove(os.path.join(self.sw, f))
        chip_defaults(self.sw)

    def all_as_found(self):
        self.assertEqual(self.entry(3), ({}, {}))
        self.assertEqual(self.entry(255), ({}, {}))
        self.assertEqual(self.reg('r.0.13'), 0x0000)
        self.assertEqual(self.scheduling(), [0] * 10)
        self.assertEqual(self.weights(), DEFAULT_WEIGHTS)
        self.assertEqual(self.fc(0), 0x8c)

    # ---- the rules every run is held to ----
    def check_the_rules(self):
        read = set()
        pointer = {}
        read_pointer = set()
        fc_pointer = {}
        read_fc = set()
        read_entry = set()
        for op, d, r, v in self.log():
            d, r, v = int(d), int(r), int(v, 16)
            if op == 'R':
                read.add((d, r))
                if (d <= 10 or d == 28) and r == 28 and d in pointer:
                    read_pointer.add((d, pointer[d]))
                if d <= 10 and r == 2 and d in fc_pointer:
                    read_fc.add((d, fc_pointer[d]))
                continue
            if d <= 10 and r == 2:
                # flow control: port 0's pointer 0x10 alone, on or off, and read first
                ptr = (v >> 8) & 0x7f
                if v & 0x8000:
                    self.assertEqual((d, ptr), (0, sq.FC), 'flow control pointer 0x%02x of port %d written' % (ptr, d))
                    self.assertIn(v, (0x9088, 0x908c), 'port 0\'s flow control written 0x%04x' % v)
                    self.assertIn((d, ptr), read_fc, 'port 0\'s flow control written unread')
                else:
                    fc_pointer[d] = ptr
                continue
            if (d <= 10 or d == 28) and r == 28:
                ptr = (v >> 8) & 0x7f
                if v & 0x8000:
                    self.assertNotEqual(d, 0, 'port 0\'s Queue Control written')
                    if d == 28 and ptr != sq.WEIGHT_LEN:
                        continue
                    self.assertIn((d, ptr), read_pointer, 'pointer 0x%02x of device %d written unread' % (ptr, d))
                else:
                    pointer[d] = ptr
                continue
            if (d, r) == (31, 0):
                o, e = (v >> 12) & 7, v & 0xff
                if o == sq.OP_READ and (v >> 10) & 3 == 0:
                    read_entry.add(e)
                if o in (sq.OP_FLUSH, sq.OP_LOAD):
                    self.assertIn(e, sq.ENTRIES, 'TCAM entry %d flushed or loaded' % e)
                    self.assertIn(e, read_entry, 'TCAM entry %d written unread' % e)
                continue
            if d == 31 or (d, r) == (27, 29):
                continue
            self.assertNotIn(r, (0x04, 0x06, 0x08, 0x0e), 'port %d register 0x%02x written' % (d, r))
            if d == 0:
                self.assertEqual(r, 0x0d, 'port 0 register 0x%02x written' % r)
            self.assertIn((d, r), read, 'register 0x%02x of device %d written unread' % (r, d))

    # ---- show, apply, clear ----
    def test_apply_then_clear_puts_everything_back(self):
        self.assertEqual(self.run_q('show'), 0, self.err)
        before = self.out
        self.assertEqual(self.changes(), [], 'show changed the chip')
        self.assertIn('TCAM entry 3: free', before)
        self.assertIn('the vendor\'s entries around them: 0 keys 0xc000 0x0000', before)
        self.assertIn('get-next after 2: 4, after 3: 4', before)
        self.assertIn('Port8 (switch port 8): scheduling 0 (weighted, all eight queues); egress limit none', before)
        self.assertIn('weights: 99 turns - queue 7: 33, 6: 25, 5: 17, 4: 12, 3: 6, 2: 3, 1: 2, 0: 1', before)
        self.assertIn('port 0 (the coprocessor) pause: on (0x8c), as found', before)
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        page0, page2 = self.entry(3)
        self.assertEqual(page0, {2: 0xc740, 3: 0xff01, 0x16: 0xff08, 0x17: 0xff00, 0x19: 0xfcb8})
        self.assertEqual(page2, self.EF3)
        self.assertEqual(self.entry(255), ({}, {}))
        self.assertEqual(self.reg('r.0.13'), 0x0001)
        self.assertEqual(self.scheduling(), [1] * 10)
        self.assertEqual(self.reg('p.0.0'), 0)
        # strict classes, no weights: port 0's pause left as found, and not kept
        self.assertEqual((self.pause_writes(), self.fc(0)), ([], 0x8c))
        k = self.kept()
        self.assertNotIn('pause', k)
        self.assertEqual(k['mode'], {'found': 0, 'set': 1})
        self.assertEqual(k['sched:8'], {'found': 0, 'set': 1})
        self.assertEqual(k['entry:3'], {'found': None, 'set': {'dscp': 46, 'mask': 63, 'queue': 7}})
        self.assertIn('switch-qos: entry 3: DSCP 46 mask 0x3f -> queue 7', self.syslog())
        # every write logged with its words
        self.assertIn('switch-qos: port 0 TCAM mode 48 bytes - port 0 register 0xd 0x0000 -> 0x0001', self.syslog())
        self.assertIn('TCAM entry 3: free -> page 0 0x2 0xc740 0x3 0xff01 0x16 0xff08 0x17 0xff00 0x19 0xfcb8, '
                      'page 2 0x3 0x00f0 0x1b 0x5000', self.syslog())
        self.assertIn('Port8 (switch port 8) scheduling 1 (queue 7 strict, the rest weighted) - Queue Control '
                      'pointer 0 0 -> 1 (0x8001)', self.syslog())
        # the load in the measured order: flush, page 2, page 1, page 0 last, then port 0's mode
        ops = [v for d, r, v in self.writes() if (d, r) == (31, 0) and (v >> 12) & 7 in (2, 3)]
        self.assertEqual(ops, [0xa003, 0xb803, 0xb403, 0xb003])
        order = [w for w in self.changes() if w[:2] in ((31, 0), (0, 13))]
        self.assertEqual(order[-1], (0, 13, 0x0001))
        # set already: nothing written twice
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.changes(), [])
        self.assertEqual(self.run_q('show'), 0, self.err)
        self.assertIn('TCAM entry 3: this tool\'s: DSCP 46 mask 0x3f -> queue 7', self.out)
        self.assertIn('get-next after 2: 3, after 3: 4', self.out)
        self.assertIn('Port8 (switch port 8): scheduling 1 (queue 7 strict, the rest weighted)', self.out)
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()
        self.assertFalse(os.path.exists(self.conf) or os.path.exists(self.state))
        # taken away in the measured order: port 0's mode first, then the entry
        order = [w for w in self.changes() if w[:2] in ((31, 0), (0, 13))]
        self.assertEqual(order, [(0, 13, 0x0000), (31, 0, 0xa003)])
        self.assertEqual(self.run_q('show'), 0)
        self.assertEqual(self.out, before)

    def test_two_classes_take_entries_3_and_255(self):
        self.want({'classes': [{'dscp': 46, 'mask': 63, 'queue': 7}, {'dscp': 34, 'mask': 63, 'queue': 6}]})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.entry(3)[1], self.EF3)
        self.assertEqual(self.entry(255), ({2: 0xc740, 3: 0xff01, 0x16: 0xff08, 0x17: 0xff00, 0x19: 0xfc88},
                                           {3: 0x00e0, 0x1b: 0x6000}))
        self.assertEqual(self.entry(254)[0], {2: 0xc000})
        # the second taken away, the first left as it is
        self.want({'classes': [{'dscp': 46, 'mask': 63, 'queue': 7}]})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.entry(255), ({}, {}))
        self.assertEqual(self.entry(3)[1], self.EF3)
        self.assertEqual([w for w in self.changes() if w[:2] == (31, 0)], [(31, 0, 0xa0ff)])
        self.assertEqual(self.reg('r.0.13'), 0x0001)
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual(self.entry(3), ({}, {}))

    def test_an_entry_in_use_refuses_the_run(self):
        self.set_reg('t.255.0.2', 0xc000)
        self.want({'classes': [{'dscp': 46, 'queue': 7}, {'dscp': 34, 'queue': 6}], 'scheduling': {'*': 1}})
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('TCAM entry 255 is in use by something else', self.err)
        self.assertEqual(self.changes(), [])
        self.assertIsNone(self.kept())
        self.assertEqual(self.reg('t.255.0.2'), 0xc000)
        # entry 3 in use: refused as well, with only one class asked for
        self.set_reg('t.3.0.2', 0xc700)
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('TCAM entry 3 is in use by something else', self.err)
        self.assertEqual(self.changes(), [])

    def test_weights_set_and_put_back(self):
        seq = [7, 0, 7, 0, 6, 1, 2, 3, 4, 5] * 3
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 0}, 'weights': seq})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.weights(), seq)
        self.assertEqual(self.reg('p.28.65'), 0x01)
        self.assertEqual(self.kept()['weights']['found'], DEFAULT_WEIGHTS)
        self.assertEqual(self.fc(0), 0x88)
        # the scheduling asked for is what the vendor left: nothing written for it
        self.assertEqual([w for w in self.changes() if w[1] == 28 and w[0] <= 10], [])
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual(self.weights(), DEFAULT_WEIGHTS)
        self.assertEqual(self.fc(0), 0x8c)

    # ---- port 0's pause: off while the weights are written ----
    def test_port0s_pause_off_after_the_weights_and_back_first(self):
        seq = [7, 7, 7, 6, 6, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5]
        self.assertEqual(self.run_q('show'), 0, self.err)
        before = self.out
        self.want({'classes': [{'dscp': 46, 'queue': 7}, {'dscp': 34, 'queue': 6}], 'scheduling': {'*': 0},
                   'weights': seq})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        # the weights' words, then the pause's - the run's last write
        self.assertEqual([w for w in self.changes() if w[:2] in ((28, 28), (0, 2))],
                         [(28, 28, v) for v in sq.weight_words(seq)] + [(0, 2, 0x9088)])
        self.assertEqual(self.changes()[-1], (0, 2, 0x9088))
        self.assertEqual((self.fc(0), self.reg('r.0.2')), (0x88, 0x1088))
        self.assertEqual([self.fc(p) for p in range(1, 11)], [0x08] * 10, 'a front port\'s flow control changed')
        self.assertEqual(self.kept()['pause'], {'found': 0x8c, 'set': 0x88})
        self.assertIn('switch-qos: port 0 pause off (no pause frames to the coprocessor) - port 0 register 0x02 '
                      'pointer 0x10 0x8c -> 0x88 (0x9088) (apply)', self.syslog())
        self.assertEqual(self.run_q('show'), 0, self.err)
        self.assertIn('port 0 (the coprocessor) pause: off (0x88), by this tool; found 0x8c', self.out)
        # put back: the pause first, before the weights and everything else
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual(self.changes()[0], (0, 2, 0x908c))
        self.assertEqual([w for w in self.changes() if w[:2] in ((28, 28), (0, 2))],
                         [(0, 2, 0x908c)] + [(28, 28, v) for v in sq.weight_words(DEFAULT_WEIGHTS)])
        self.assertEqual(self.reg('r.0.2'), 0x108c)
        self.assertIn('switch-qos: port 0 pause on, as found - port 0 register 0x02 pointer 0x10 0x88 -> 0x8c '
                      '(0x908c) (clear)', self.syslog())
        self.all_as_found()
        self.assertIsNone(self.kept())
        self.assertEqual(self.run_q('show'), 0)
        self.assertEqual(self.out, before)

    def test_port0s_pause_not_as_the_coprocessor_leaves_it_refuses_the_run(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 1}, 'weights': list(range(8))})
        for found in (0x88, 0x0c, 0x8d):
            self.set_fc(0, found)
            self.assertEqual(self.run_q('apply'), 1)
            self.assertIn('port 0\'s pause: its flow control (0x02 pointer 0x10) reads 0x%02x, not 0x8c as the '
                          'coprocessor leaves it: nothing written' % found, self.err)
            self.assertEqual(self.changes(), [], 'written over 0x%02x' % found)
            self.assertIsNone(self.kept())
            self.assertEqual(self.fc(0), found)
        # a read that is not pointer 0x10's: refused the same
        self.set_fc(0, 0x8c)
        self.flag('stuck.0.2')
        self.set_reg('r.0.2', 0x008c)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('port 0\'s flow control (port register 0x02) read as 0x008c, not as pointer 0x10\'s', self.err)
        self.assertEqual(self.changes(), [])
        self.assertIsNone(self.kept())
        self.unflag('stuck.0.2')
        # strict classes ask nothing of it: set whatever port 0's flow control holds, and it left as it is
        self.set_fc(0, 0x0c)
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual((self.pause_writes(), self.fc(0)), ([], 0x0c))
        self.assertNotIn('pause', self.kept())

    def test_strict_after_weights_puts_the_pause_back_first(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 0}, 'weights': list(range(8)) * 2})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.fc(0), 0x88)
        # minimums gone, the class strict: no weights, so the pause back as found before anything else
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 1}})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.changes()[0], (0, 2, 0x908c))
        self.assertEqual(self.pause_writes(), [0x908c])
        self.assertEqual((self.fc(0), self.weights(), self.scheduling()), (0x8c, DEFAULT_WEIGHTS, [1] * 10))
        self.assertNotIn('pause', self.kept())
        self.assertNotIn('weights', self.kept())
        self.assertIn('switch-qos: port 0 pause on, as found - port 0 register 0x02 pointer 0x10 0x88 -> 0x8c '
                      '(0x908c) (apply)', self.syslog())
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()

    def test_the_pause_kept_in_the_state(self):
        seq = list(range(8)) * 2
        self.want({'weights': seq})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        with open(self.state) as f:
            self.assertEqual(json.load(f)['items']['pause'], {'found': 0x8c, 'set': 0x88})
        # set already: nothing written twice
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.changes(), [])
        self.assertEqual(self.kept()['pause'], {'found': 0x8c, 'set': 0x88})
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()
        # a run cut after the weights, before the pause: what was found kept, the next run finishes
        self.want({'weights': seq})             # clear took the settings file away
        self.cut_after(2)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertEqual((self.weights(), self.fc(0)), (seq, 0x8c))
        self.assertEqual(self.kept()['pause'], {'found': 0x8c, 'set': 0x8c, 'pending': 0x88})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.changes(), [(0, 2, 0x9088)])
        self.assertEqual(self.kept()['pause'], {'found': 0x8c, 'set': 0x88})
        # a value neither found nor set, kept: refused, not turned off over it
        self.set_fc(0, 0x0c)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('reads 0x0c, not 0x8c', self.err)
        self.assertEqual(self.changes(), [])
        self.set_fc(0, 0x88)
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()
        self.assertIsNone(self.kept())

    def test_a_pause_that_does_not_read_back_puts_the_run_back(self):
        self.flag('fcstuck.0.16')
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 0}, 'weights': list(range(8))})
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('not read back as written: port 0 pause off', self.err)
        self.all_as_found()
        self.assertIsNone(self.kept())
        self.assertIn('was put back as found', self.syslog())
        # a put-back of it that does not stick: the rest put back, the pause kept for the next run
        self.unflag('fcstuck.0.16')
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.flag('fcstuck.0.16')
        self.assertEqual(self.run_q('clear'), 1)
        self.assertIn('not read back as written: port 0 pause on, as found', self.err)
        self.assertEqual(self.kept(), {'pause': {'found': 0x8c, 'set': 0x88, 'pending': 0x8c}})
        self.assertEqual((self.weights(), self.entry(3), self.fc(0)), (DEFAULT_WEIGHTS, ({}, {}), 0x88))
        self.unflag('fcstuck.0.16')
        # no settings file: a reapply only puts back
        self.assertEqual(self.run_q('reapply'), 0, self.err)
        self.assertEqual(self.pause_writes(), [0x908c])
        self.all_as_found()
        self.assertIsNone(self.kept())

    def test_check_reads_the_pause_again(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 0}, 'weights': list(range(8))})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.run_q('check', '--json'), 0, self.err)
        self.assertEqual(json.loads(self.out), {'ok': True, 'changed': [], 'read': True})
        # as a coprocessor boot leaves it, before the boot hook's reapply
        self.set_fc(0, 0x8c)
        self.assertEqual(self.run_q('check', '--json'), 1)
        self.assertEqual(json.loads(self.out)['changed'], ['pause'])
        self.assertEqual(self.changes(), [])
        self.assertEqual(self.run_q('show'), 0, self.err)
        self.assertIn('port 0 (the coprocessor) pause: on (0x8c), as found; this tool sets it off again at its '
                      'next reapply', self.out)
        self.assertEqual(self.run_q('reapply'), 0, self.err)
        self.assertEqual(self.pause_writes(), [0x9088])
        self.assertEqual(self.run_q('check'), 0, self.err)
        self.assertIn('the switch holds what this tool set', self.out)

    def test_egress_limit_keeps_the_rest_of_0x09(self):
        self.set_reg('r.8.9', 0xff01)
        self.want({'limits': {'Port8': 900000}})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.reg('r.8.9'), 0xff5a)
        self.assertEqual(self.reg('r.8.10'), 0x8032)
        # eDec first, then N
        self.assertEqual([w for w in self.changes() if w[0] == 8], [(8, 9, 0xff5a), (8, 10, 0x8032)])
        self.assertEqual(self.reg('r.0.13'), 0x0000, 'no class: port 0\'s TCAM mode left alone')
        self.assertEqual(self.run_q('show'), 0)
        self.assertIn('Port8 (switch port 8): scheduling 0 (weighted, all eight queues); egress limit 900000 kbit/s '
                      '(eDec 90, N 50)', self.out)
        self.want({'limits': {'Port8': 100000}})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual((self.reg('r.8.9'), self.reg('r.8.10')), (0xff64, 0x81f4))
        self.assertEqual(self.kept()['limit:8']['found'], [0xff01, 0x8000])
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual((self.reg('r.8.9'), self.reg('r.8.10')), (0xff01, 0x8000))
        # N first, then eDec
        self.assertEqual([w for w in self.changes() if w[0] == 8], [(8, 10, 0x8000), (8, 9, 0xff01)])

    def test_a_limit_not_this_tools_refuses_the_run(self):
        self.set_reg('r.4.10', 0x8005)
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'limits': {'Port4': 100000}})
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('Port4 (switch port 4) has an egress limit already', self.err)
        self.assertEqual(self.changes(), [])
        self.set_reg('r.4.10', 0x8000)
        with open(self.hand, 'w') as f:
            f.write('# switch.sh\nlimit port=4 mbit=100 set=0x8005 found=0x8000\n')
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('switch.sh keeps', self.err)
        self.assertEqual(self.changes(), [])

    def test_a_set_that_does_not_read_back_puts_the_run_back(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 1},
                   'weights': list(range(8)) * 4, 'limits': {'Port2': 480000}})
        self.flag('stuck.8.28')
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('not read back as written: Port8 (switch port 8) scheduling 1', self.err)
        self.all_as_found()
        self.assertEqual((self.reg('r.2.9'), self.reg('r.2.10')), (0x0001, 0x8000))
        self.assertIsNone(self.kept())
        self.assertIn('was put back as found', self.syslog())
        # an entry that does not load: the same
        self.unflag('stuck.8.28')
        self.flag('tstuck.3')
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('not read back as written: entry 3', self.err)
        self.all_as_found()

    def test_a_put_back_that_does_not_stick_keeps_what_was_written(self):
        self.want(self.ONE)
        # port 5 takes this run's write and then no other; port 8 takes none
        self.flag('once.5.28')
        self.flag('stuck.8.28')
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('PUT BACK DIFFERS: Port5 (switch port 5) scheduling 0', self.err)
        self.assertIn('could not put back', self.syslog())
        self.assertEqual(self.kept()['sched:5'], {'found': 0, 'set': 0, 'pending': 1})
        self.assertEqual(self.kept()['mode']['found'], 0)
        self.unflag('stuck.5.28', 'stuck.8.28')
        # port 5 holds this tool's pending value: taken as this tool's, and put back
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()

    # ---- a run cut short (the link gone, the host stopped) ----
    def test_a_run_cut_while_it_changes_the_weights(self):
        a, b = list(range(8)) * 2, list(range(8)) * 3
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 0}, 'weights': a})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 0}, 'weights': b})
        self.cut_after(1)                       # the reads answer, nothing after them
        self.assertEqual(self.run_q('apply'), 1)
        self.assertEqual(self.weights(), a)
        self.assertEqual(self.kept()['weights'], {'found': DEFAULT_WEIGHTS, 'set': a, 'pending': b})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.weights(), b)
        self.assertEqual(self.kept()['weights'], {'found': DEFAULT_WEIGHTS, 'set': b})
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual(self.weights(), DEFAULT_WEIGHTS)

    def test_a_run_cut_after_its_write_landed(self):
        a, b = list(range(8)) * 2, list(range(8)) * 3
        self.want({'weights': a})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.want({'weights': b})
        self.cut_after(2)                       # the reads and the write answer, the read back does not
        self.assertEqual(self.run_q('apply'), 1)
        self.assertEqual(self.weights(), b)
        self.assertEqual(self.kept()['weights']['pending'], b)
        # the next run finds the pending value there: this tool's, now set; nothing to write
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.changes(), [])
        self.assertEqual(self.kept()['weights'], {'found': DEFAULT_WEIGHTS, 'set': b})
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual(self.weights(), DEFAULT_WEIGHTS)

    def test_a_run_cut_while_it_changes_a_limit(self):
        self.want({'limits': {'Port8': 100000}})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.want({'limits': {'Port8': 50000}})
        self.cut_after(1)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertEqual((self.reg('r.8.9'), self.reg('r.8.10')), (0x0064, 0x81f4))
        # the next run takes the limit as this tool's - not "an egress limit already"
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual((self.reg('r.8.9'), self.reg('r.8.10')), (0x0032, 0x81f4))
        self.assertEqual(self.kept()['limit:8']['found'], [0x0001, 0x8000])
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual((self.reg('r.8.9'), self.reg('r.8.10')), (0x0001, 0x8000))
        self.assertIsNone(self.kept())

    def test_a_run_cut_between_a_limits_two_words(self):
        self.want({'limits': {'Port8': 100000}})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        # as a run cut after eDec and before N would leave it
        self.want({'limits': {'Port8': 200000}})
        self.cut_after(1)
        self.assertEqual(self.run_q('apply'), 1)
        self.set_reg('r.8.9', 0x0014)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual((self.reg('r.8.9'), self.reg('r.8.10')), (0x0014, 0x8032))
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual((self.reg('r.8.9'), self.reg('r.8.10')), (0x0001, 0x8000))

    def test_a_run_cut_while_it_changes_an_entry(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}]})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.want({'classes': [{'dscp': 34, 'queue': 7}]})
        self.cut_after(1)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertEqual(self.entry(3)[0][0x19], 0xfcb8)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.entry(3)[0][0x19], 0xfc88)
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()

    def test_a_run_cut_after_an_entrys_flush(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}]})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.want({'classes': [{'dscp': 34, 'queue': 7}]})
        self.cut_after(1)
        self.assertEqual(self.run_q('apply'), 1)
        # as a cut between the flush and the load leaves it: free
        self.unflag(*[f for f in os.listdir(self.sw) if f.startswith('t.3.')])
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.entry(3)[0][0x19], 0xfc88)
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()

    # ---- clear: the settings file first, and never set again ----
    def test_a_clear_that_cannot_put_back_never_sets_again(self):
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.flag('stuck.5.28')
        self.assertEqual(self.run_q('clear'), 1)
        self.assertIn('not read back as written: Port5 (switch port 5) scheduling 0', self.err)
        self.assertFalse(os.path.exists(self.conf), 'the settings file left')
        # what was put back stays put back; port 5 is kept to put back at the next run
        self.assertEqual(self.entry(3), ({}, {}))
        self.assertEqual(self.reg('r.0.13'), 0x0000)
        self.assertEqual(self.scheduling(), [0, 0, 0, 0, 1, 0, 0, 0, 0, 0])
        self.assertEqual(self.kept(), {'sched:5': {'found': 0, 'set': 1, 'pending': 0}})
        self.assertIn('kept to put back at the next run', self.syslog())
        # a coprocessor boot, no settings file: the hook only puts back - nothing is set
        self.unflag('stuck.5.28')
        self.boot()
        self.assertEqual(self.run_q('reapply', '--boot'), 0, self.err)
        self.all_as_found()
        self.assertIsNone(self.kept())

    def test_a_clear_cut_short_leaves_nothing_to_set_again(self):
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.cut_after(1)
        self.assertEqual(self.run_q('clear'), 1)
        self.assertFalse(os.path.exists(self.conf), 'the settings file left')
        self.boot()
        self.assertEqual(self.run_q('reapply', '--boot'), 0, self.err)
        self.all_as_found()
        self.assertIsNone(self.kept())

    def test_reapply_with_no_settings_file_puts_back(self):
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        os.remove(self.conf)
        self.assertEqual(self.run_q('reapply'), 0, self.err)
        self.all_as_found()
        self.assertIsNone(self.kept())

    # ---- the TCAM engine shared with the agent ----
    def test_a_load_that_does_not_read_as_written_is_flushed_at_once(self):
        self.flag('tmix.3')
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 1)
        # flushed in the load's own session, before anything else was asked
        log = self.writes()
        load = log.index((31, 0, 0xb003))
        self.assertIn((31, 0, 0xa003), log[load:], 'not flushed after the load')
        self.assertIn('not read back as written: entry 3', self.err)
        self.all_as_found()
        self.assertIsNone(self.kept())

    def test_a_run_during_which_the_vendors_entries_change_is_not_kept(self):
        self.flag('agent')
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('the vendor\'s TCAM entries changed while this ran (4k2 0xff02 -> 0xff10)', self.err)
        self.assertIn('the vendor\'s TCAM entries changed during a run', self.syslog())
        self.all_as_found()
        self.assertIsNone(self.kept())

    def test_check_reads_the_entries_and_the_mode_again(self):
        self.assertEqual(self.run_q('check', '--json'), 0, self.err)
        self.assertEqual(json.loads(self.out), {'ok': True, 'changed': [], 'read': False})
        self.assertEqual(self.npu_calls(), 0, 'nothing kept: nothing read')
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.run_q('check', '--json'), 0, self.err)
        self.assertEqual(json.loads(self.out), {'ok': True, 'changed': [], 'read': True})
        self.assertEqual(self.changes(), [])
        self.set_reg('t.3.0.25', 0xfc88)
        self.set_reg('r.0.13', 0x0000)
        self.assertEqual(self.run_q('check', '--json'), 1)
        self.assertEqual(json.loads(self.out)['changed'], ['entry:3', 'mode'])
        self.assertEqual(self.changes(), [])

    def test_reads_that_never_agree_write_nothing(self):
        self.flag('flicker', '3\n')
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('did not read the same three times', self.err)
        self.assertEqual(self.changes(), [])
        self.assertIsNone(self.kept())

    def test_a_flicker_now_and_then_is_read_again(self):
        self.flag('flicker', '11\n')
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.assertEqual(self.entry(3)[1], self.EF3)
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual(self.entry(3), ({}, {}))

    def test_coprocessor_not_answering(self):
        self.flag('npu-down')
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('did not answer', self.err)
        self.assertIsNone(self.kept())

    def test_no_switch_tool_on_the_coprocessor(self):
        self.env['XGS_SWITCH_MVSW'] = os.path.join(self.dir, 'nothing-here')
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 1)
        self.assertIn('the switch tool is not on the coprocessor', self.err)
        self.assertEqual(self.log(), [])
        self.assertIsNone(self.kept())

    # ---- after a coprocessor boot ----
    def test_reapply_after_a_boot(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'*': 1}, 'weights': list(range(8)),
                   'limits': {'Port8': 480000}})
        self.assertEqual(self.run_q('apply'), 0, self.err)
        kept = self.kept()
        self.assertEqual(kept['pause'], {'found': 0x8c, 'set': 0x88})
        self.boot()
        # the coprocessor's boot leaves port 0's pause on again
        self.assertEqual(self.fc(0), 0x8c)
        self.assertEqual(self.run_q('reapply', '--boot'), 0, self.err)
        self.assertEqual(self.entry(3)[1], self.EF3)
        self.assertEqual(self.reg('r.0.13'), 0x0001)
        self.assertEqual(self.reg('p.8.0'), 1)
        self.assertEqual(self.weights(), list(range(8)))
        self.assertEqual(self.reg('r.8.10'), 0x8032)
        # and the pause off again, after the weights: the run's last write
        self.assertEqual((self.fc(0), self.changes()[-1]), (0x88, (0, 2, 0x9088)))
        self.assertIn('after a coprocessor boot', self.syslog())
        self.assertEqual(self.kept(), kept)
        # set already: a second reapply writes nothing
        self.assertEqual(self.run_q('reapply'), 0, self.err)
        self.assertEqual(self.changes(), [])
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.all_as_found()
        self.assertEqual(self.reg('r.8.10'), 0x8000)
        # nothing kept and no settings file: reapply asks the coprocessor nothing
        self.assertEqual(self.run_q('reapply', '--boot'), 0, self.err)
        self.assertEqual(self.npu_calls(), 0)

    def test_words_and_can_ask_the_chip_nothing(self):
        self.want({'classes': [{'dscp': 46, 'queue': 7}], 'scheduling': {'Port8': 7}, 'weights': list(range(8)),
                   'limits': {'Port8': 900000}})
        self.assertEqual(self.run_q('words'), 0, self.err)
        got = json.loads(self.out)
        self.assertEqual(got['entries']['3']['page0']['0x19'], '0xfcb8')
        self.assertEqual(got['sched'], {'8': '0x8007'})
        self.assertEqual(got['weights'][-1], '0xc008')
        self.assertEqual(got['pause'], '0x9088')
        self.assertEqual(got['limits']['8'], {'edec': 90, 'n': 50, 'kbit': 900000, '0x0a': '0x8032'})
        self.want(self.ONE)
        self.assertEqual(self.run_q('words'), 0, self.err)
        self.assertEqual((json.loads(self.out)['weights'], json.loads(self.out)['pause']), (None, None))
        self.assertEqual(self.run_q('can'), 0, self.err)
        can = json.loads(self.out)
        self.assertEqual((can['assembly'], can['family'], can['writes'], can['entries']),
                         ('AMDA0202-0004', 'octeontx', True, 2))
        self.assertEqual(can['ports'][0], 'Port1')
        self.assertNotIn('F1', can['ports'])
        # the devices as models.py names them: on the XGS 3300 F1 and F2 come first, oxp0 and oxp1
        self.assertEqual((can['devices']['oxp2'], can['devices']['oxp9'], can['devices']['oxp11']),
                         ('Port1', 'Port8', 'F4'))
        self.assertNotIn('oxp0', can['devices'])
        self.assertEqual(self.npu_calls(), 0)

    def test_show_counts_a_class_hits(self):
        self.want(self.ONE)
        self.assertEqual(self.run_q('apply'), 0, self.err)
        self.flag('ctr.0.9.1', '120471\n')
        self.assertEqual(self.run_q('show', '--json'), 0, self.err)
        got = json.loads(self.out)
        self.assertEqual(got['entries']['3']['hits'], '120471')
        self.assertEqual(got['ports']['Port8'], {'sched': 1, 'limit_kbit': None})
        self.assertEqual(got['pause'], {'value': 0x8c, 'on': True, 'what': 'on (0x8c), as found'})
        self.assertEqual(self.changes(), [])


class Chip3100(Chip):
    """The XGS 3100: the XGS 3300's family, nothing run on one - read only."""
    MODEL = '3100'

    def test_writes_refused_before_anything(self):
        self.want(self.ONE)
        for args in (['apply'], ['reapply'], ['reapply', '--boot']):
            self.assertEqual(self.run_q(*args), 2, args)
            self.assertIn('only the XGS 3300 (AMDA0202-0004) takes writes', self.err)
            self.assertEqual((self.npu_calls(), self.log()), (0, []), args)
        self.assertEqual(self.run_q('can'), 0)
        self.assertEqual((json.loads(self.out)['writes'], json.loads(self.out)['why']), (False, 'untested-model'))
        self.assertEqual(self.run_q('show'), 0, self.err)
        self.assertEqual(self.changes(), [])


class Chip136(Chip):
    """The XGS 136: read only, and no TCAM operation at all."""
    MODEL = '136'

    def test_writes_refused_before_anything(self):
        self.want(self.ONE)
        for args in (['apply'], ['reapply'], ['reapply', '--boot']):
            self.assertEqual(self.run_q(*args), 2, args)
            self.assertIn('only reads', self.err)
            self.assertEqual(self.npu_calls(), 0, args)
            self.assertEqual(self.log(), [], args)
        # clear: nothing of this tool's can be in the switch here - nothing asked, the settings file gone
        self.assertEqual(self.run_q('clear'), 0, self.err)
        self.assertEqual((self.npu_calls(), self.log(), os.path.exists(self.conf)), (0, [], False))
        self.assertEqual(self.run_q('can'), 0)
        self.assertEqual(json.loads(self.out)['writes'], False)
        self.assertEqual(json.loads(self.out)['why'], 'read-only')

    def test_show_reads_port_registers_alone(self):
        self.assertEqual(self.run_q('show'), 0, self.err)
        self.assertIn('port 0: 0x04 0x017f, 0x08 0x2080, 0x0d 0x0000', self.out)
        self.assertIn('the TCAM is not read here', self.out)
        self.assertEqual(self.writes(), [], 'show wrote a register - a TCAM operation among them')
        self.assertEqual({(int(d), int(r)) for op, d, r, v in self.log()}, {(0, 4), (0, 8), (0, 13)})


for name in [n for n in dir(Chip) if n.startswith('test_')]:
    # what the XGS 3300 tests write is refused on the others: each has its own test above
    for cls in (Chip3100, Chip136):
        if name not in cls.__dict__:
            setattr(cls, name, None)


if __name__ == '__main__':
    unittest.main()
