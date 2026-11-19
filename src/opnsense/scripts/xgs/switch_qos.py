#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The switch chip's queues (88E6193X, Marvell's UMSD "Amethyst" family), for the plugins that give
traffic a priority or a share of a port - os-trafficpriority first. A sibling of switch.sh, which sets
a mirror and an egress limit by hand and writes no TCAM entry; this one writes what a caller asks for
in a settings file, keeps what it found, puts it back, and sets it again after every coprocessor boot.

    switch_qos.py show [--json]      read only: port 0's TCAM mode, entries 3 and 255 and their hits,
                                     each front port's scheduling and egress limit, the chip's weights
    switch_qos.py apply              the settings file into the chip; what is there read first and kept
    switch_qos.py reapply [--boot]   the same, after a coprocessor boot (the start hook, recover.sh)
    switch_qos.py clear              everything this tool set put back as found; the settings file gone
    switch_qos.py can                no bus: whether this model takes writes, and its ports behind the
                                     switch with their devices, as JSON (for the caller's page)
    switch_qos.py words              no bus: the register words the settings file makes, as JSON

THE SETTINGS FILE, /var/db/os-xgs-npu/switch-qos.json, written by the caller (os-trafficpriority's
apply.py), read here and never written but by "clear", which removes it:

    {"owner": "os-trafficpriority",
     "classes": [{"dscp": 46, "mask": 63, "queue": 7}],     at most two: TCAM entries 3 and 255
     "scheduling": {"*": 1},                                "*" every front port behind the switch,
                                                            or a front port's name; UMSD's mode 0-7
     "weights": [7, 0, 6, ...],                             the chip's WRR sequence, or none
     "limits": {"Port2": 100000}}                           a front port's egress limit, in kbit/s

WHY A TCAM ENTRY ON PORT 0. Every frame the coprocessor sends into the switch waits in queue 0
(docs/measurements/qsched-2026-10-25.md): it carries a 4-byte DSA tag whose priority is 0 on a frame
without a VLAN, and the switch takes the frame's queue from the tag, not from its DSCP, whatever port
0's DSCP table and priority source say. A TCAM entry on port 0 keyed on the frame's DSCP behind that
tag sets the queue instead. Measured on the second XGS 3300 (vendor version 22): on 2026-11-11 EF from
the coprocessor in queue 7 with this very entry; on 2026-11-17 again, with the TCAM counter counting
exactly the frames queue 7 sent (120,471 of 120,471), and at TCP line rate in 33 runs of 60 s the two
streams together held 916 to 987 Mbit/s - the collapse to about 370 seen on 2026-10-25 did not come
back. One entry serves every front port at once: the queue is set where the frame enters the switch.

THE ENTRY, as measured (one per class; mask and queue are the caller's):
    page 0  reg 0x02 0xc740    frame type 1 (the coprocessor's DSA-tagged frames) under its mask
            reg 0x03 0xff01    from port 0 only
            reg 0x16 0xff08, reg 0x17 0xff00    EtherType 0x0800 at octets 16-17, behind the tag
            reg 0x19 (mask << 2) << 8 | dscp << 2   the TOS byte, the ECN bits left out
    page 2  reg 0x03 0x0080 | queue << 4       QPri override, the queue
            reg 0x1b 0x4000 | counter << 12    IncTcamCtr: entry 3 counts in TCAMCounter1 (measured),
                                               entry 255 in TCAMCounter2 (not measured)
    every other page-0 and page-2 register 0; loaded as UMSD's Amethyst_msdTCAM.c (BSD-3) and Linux's
    mv88e6390_tcam_entry_add load one: flush, page 2, page 1, page 0 last, which makes it valid. Then
    port 0's TCAM mode (port register 0x0d bits 1:0) set to 1, 48 bytes; its other bits kept.
The vendor's own entries all key frame type 0, so they never see port 0's frames; only entries 3 and
255 were free on the XGS 3300 (all 256 read on 2026-11-11, 254 in use). Two classes at most, then.
Entry 255 sits behind the vendor's drop-all 254, which keys type 0 too: that it works is expected,
not measured. IPv4 only: an IPv6 key needs EtherType 0x86dd and a traffic class across two octets,
an entry of its own; not offered. An entry in use by anything else is never touched: the run is
refused.

SCHEDULING, per egress port: Queue Control (port register 0x1c), pointer 0, written as
0x8000 | pointer << 8 | data and read by writing the pointer alone (UMSD Amethyst_msdQueueCtrl.c,
gprtSetQueueCtrl / gprtSetPortSched). 0 weighted on all eight queues (as the vendor leaves every port);
1 to 6 the top 1 to 6 queues strict and the rest weighted; 7 strict on all. Measured on port 8 of the
second XGS 3300 on 2026-11-17: 0, 1 and 7 written, read back and put back. Port 0, the coprocessor's
uplink, is never set here.

THE WEIGHTS, one sequence for the whole chip: Global2 register 0x1c, the QoS weight table - a
sequence of up to 128 queue numbers, two to a pointer (pointer i << 8, data bits 2:0 and 6:4), its
length at pointer 0x40 (UMSD gsysSetQoSWeight / gsysGetQoSWeight). A weighted port serves its queues
in this order, each as often as it appears. The second XGS 3300 reads a length of 99 (2026-11-17),
UMSD's default weights 33, 25, 17, 12, 6, 3, 2 and 1 for queues 7 to 0. Every port in weighted mode
takes the same sequence, port 0 among them; a sequence here must name each of the eight queues at
least once, so that none is ever shut out. The sequence was read on the appliance, never written.

THE EGRESS LIMIT, per port, not per queue: port register 0x09 bits 6:0 (eDec) and 0x0a bits 15:14
(count mode: 2, layer-2 bytes) and 13:0 (N). UMSD's scale (Amethyst_msdPortCtrl.c,
grcSetEgressRate and its macro): eDec = the rate in 64 kbit/s below 1 Mbit/s, in Mbit/s to 100, in
10 Mbit/s to 1 Gbit/s, in 100 Mbit/s above; N = 8,000,000 * eDec / (16 * kbit/s), rounded up; so
100 Mbit/s is eDec 100 and N 500, 480 Mbit/s eDec 48 and N 50, 900 eDec 90 and N 50. A rate between
the steps is taken down to one. 0x09 is read first and only its bits 6:0 are changed. Measured with
eDec 1 as the vendor leaves it (switch.sh's 480/N steps, docs/measurements/switch-3300-2026-11-11.md);
other values of eDec not measured. A port with a limit that is not this tool's - switch.sh's, or
anything else's - is refused.

READS AND WRITES as switch.sh makes them, on the coprocessor through npu-run.sh and contrib/mvsw:
every value read three times in a row and used only when the three agree, in up to four rounds
(the network agent polls the same bus); every register written was read in the same run; what was
found kept in the state file BEFORE anything is written; every write read back and compared; and a
run that cannot write all it set out to puts back what it changed, reads again, and says so. A run
refused - an entry or a limit not this tool's, a value that never read the same - writes nothing.
Every change is logged (tag xgs-switch). One run at a time, under switch.sh's lock.

THE XGS 136 is read only here. Its switch is the same chip, but its network agent shares the bus and
reads of the host's have taken ports down there (#639, #706); its TCAM has never been read. So
"show" reads only port 0's TCAM mode and the TCAM census - entries 3, 254 and 255 and the first entry
after 2 - and apply, reapply and clear refuse before anything is read.

THE STATE, /var/db/os-xgs-npu/switch-qos.state (JSON): what this tool set and what it found. A
coprocessor boot erases the switch's registers; reapply then finds the vendor's values where this
tool's were, takes them as what to put back, and sets the settings file's again.
"""

import json
import os
import re
import subprocess
import sys
import time

LIB = os.environ.get('XGS_LIB', '/usr/local/opnsense/scripts')
PY = os.environ.get('XGS_PYTHON', '/usr/local/bin/python3')
NPU_RUN = os.path.join(LIB, 'xgs', 'npu-run.sh')
MODELS = os.path.join(LIB, 'xgs', 'models.py')
MVSW = os.environ.get('XGS_SWITCH_MVSW', '/persistent/os-xgs-npu/mvsw')
CONF = os.environ.get('XGS_SWITCH_QOS_CONF', '/var/db/os-xgs-npu/switch-qos.json')
STATE = os.environ.get('XGS_SWITCH_QOS_STATE', '/var/db/os-xgs-npu/switch-qos.state')
HAND = os.environ.get('XGS_SWITCH_STATE', '/var/db/os-xgs-npu/switch.state')      # switch.sh's state
LOCK = os.environ.get('XGS_SWITCH_LOCK', '/var/run/xgs-switch.lock')                # switch.sh's lock
LOGGER = os.environ.get('XGS_SWITCH_LOGGER', '/usr/bin/logger')
# at boot, the coprocessor's link and the switch's frame limit go first: the processes waited for
BOOT_WAIT = int(os.environ.get('XGS_SWITCH_QOS_BOOT_WAIT', '600'))
BOOT_TRIES = int(os.environ.get('XGS_SWITCH_QOS_BOOT_TRIES', '3'))
BOOT_PAUSE = int(os.environ.get('XGS_SWITCH_QOS_BOOT_PAUSE', '30'))
BEFORE_US = ('mgmt-link.sh coprocessor', 'switch-jumbo.sh')

TCAM = 0x1f
GLOBAL1 = 0x1b
GLOBAL2 = 0x1c
ENTRIES = (3, 255)
COUNTER = {3: 1, 255: 2}               # IncTcamCtr's counter for each entry; TCAMCounterN is bank 1 0x08 + N
P0_REGS = list(range(0x02, 0x1c))      # keys 0x02-0x05, then frame octets 0-21 at 0x06-0x1b
P1_REGS = list(range(0x02, 0x1c))      # frame octets 22-47
P2_REGS = [0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x1b]
FREE = 0x00ff                          # page 0 keys1 of an entry not in use (UMSD: no valid entry)
KEYS1 = 0xc740                         # frame type mask 11, source-port mask 0x7xx, frame type 1
KEYS2 = 0xff01                         # source-port mask 0xff, port 0
OP_FLUSH, OP_LOAD, OP_NEXT, OP_READ = 2, 3, 4, 5
WEIGHT_LEN = 0x40                      # the weight table's length pointer
MODES = {0: 'weighted, all eight queues', 1: 'queue 7 strict, the rest weighted',
         2: 'queues 7-6 strict, the rest weighted', 3: 'queues 7-5 strict, the rest weighted',
         4: 'queues 7-4 strict, the rest weighted', 5: 'queues 7-3 strict, the rest weighted',
         6: 'queues 7-2 strict, the rest weighted', 7: 'strict, all eight queues'}
NOREAD = 'did not read the same three times in four rounds (the bus is shared with the network agent)'


class Refused(Exception):
    """Nothing was written, and why."""


class Failed(Exception):
    """Something could not be read or written."""


def x4(value):
    return '0x%04x' % (value & 0xffff)


# ---- the register words: what each setting writes ----

def tos_word(dscp, mask):
    """Page 0's octet-19 register: the TOS byte under its mask, mask << 8 | data, the ECN bits out."""
    tos_mask = (mask << 2) & 0xfc
    return (tos_mask << 8) | ((dscp << 2) & tos_mask)


def tcam_words(dscp, mask, queue, counter):
    """The entry for one class: ({page-0 register: word}, {page-2 register: word}), every register of
    both pages named, the ones not set 0."""
    page0 = dict.fromkeys(P0_REGS, 0)
    page0.update({0x02: KEYS1, 0x03: KEYS2, 0x16: 0xff08, 0x17: 0xff00, 0x19: tos_word(dscp, mask)})
    page2 = dict.fromkeys(P2_REGS, 0)
    page2.update({0x03: 0x0080 | ((queue & 7) << 4), 0x1b: 0x4000 | ((counter & 3) << 12)})
    return page0, page2


def tcam_op(op, entry, page=0):
    """The TCAM operation register's word: busy, the operation, the page, the entry."""
    return 0x8000 | (op << 12) | (page << 10) | (entry & 0xff)


def sched_word(mode):
    """Queue Control's write of the scheduling, pointer 0 (UMSD gprtSetQueueCtrl)."""
    return 0x8000 | (0 << 8) | (mode & 0xff)


def weight_words(sequence):
    """The weight table's writes, in order: each pair of queue numbers at its pointer, a last one alone,
    then the length at pointer 0x40 (UMSD gsysSetQoSWeight)."""
    out = []
    for i in range(len(sequence) // 2):
        out.append(0x8000 | (i << 8) | (sequence[2 * i] & 7) | ((sequence[2 * i + 1] & 7) << 4))
    if len(sequence) % 2:
        out.append(0x8000 | ((len(sequence) // 2) << 8) | (sequence[-1] & 7))
    out.append(0x8000 | (WEIGHT_LEN << 8) | len(sequence))
    return out


def egress(kbit):
    """UMSD's egress limit for a rate in kbit/s, taken down to its step: (eDec, N, kbit/s it gives),
    or None below 64 kbit/s or above 10 Gbit/s (grcSetEgressRate, AMETHYST_MSD_GET_RATE_LIMIT_PER_BYTE)."""
    if not isinstance(kbit, int) or kbit < 64 or kbit > 10000000:
        return None
    if kbit < 1000:
        step = 64
    elif kbit <= 100000:
        step = 1000
    elif kbit <= 1000000:
        step = 10000
    else:
        step = 100000
    rate = kbit // step * step
    edec = rate // step
    n = (8000000 * edec + 16 * rate - 1) // (16 * rate)
    return edec, n, 500000 * edec // n


def limit_words(kbit, found09):
    """(0x09, 0x0a) for a limit: eDec in 0x09's bits 6:0, its other bits as found; layer-2 bytes and N
    in 0x0a."""
    edec, n, _ = egress(kbit)
    return (found09 & ~0x7f & 0xffff) | edec, 0x8000 | (n & 0x3fff)


def limit_kbit(r09, r0a):
    """What a port's egress limit gives, from its registers: kbit/s, None for no limit, or a word for a
    count mode other than layer-2 bytes."""
    n = r0a & 0x3fff
    if n == 0:
        return None
    edec = r09 & 0x7f
    mode = (r0a >> 14) & 3
    if mode == 0:
        return 'frames'
    return 500000 * edec // n if edec else 0


# ---- the settings file ----

def parse_conf(conf, ports):
    """The caller's settings, checked: {'classes': [(dscp, mask, queue)], 'sched': {sw: mode},
    'weights': [..] or None, 'limits': {sw: kbit}}. ports: {label: switch port}."""
    if not isinstance(conf, dict):
        raise Refused('the settings file is not a JSON object')
    out = {'classes': [], 'sched': {}, 'weights': None, 'limits': {}}
    classes = conf.get('classes') or []
    if not isinstance(classes, list) or len(classes) > len(ENTRIES):
        raise Refused('at most %d classes: TCAM entries 3 and 255 are the only ones free' % len(ENTRIES))
    seen = set()
    for c in classes:
        if not isinstance(c, dict):
            raise Refused('a class is not an object')
        dscp, mask, queue = c.get('dscp'), c.get('mask', 63), c.get('queue')
        if not all(isinstance(v, int) and not isinstance(v, bool) for v in (dscp, mask, queue)):
            raise Refused('a class needs a whole dscp, mask and queue')
        if not (0 <= dscp <= 63 and 1 <= mask <= 63 and 0 <= queue <= 7):
            raise Refused('dscp 0-63, mask 1-63, queue 0-7: %s' % json.dumps(c, sort_keys=True))
        if (dscp & mask, mask) in seen:
            raise Refused('two classes with the same DSCP %d' % dscp)
        seen.add((dscp & mask, mask))
        out['classes'].append((dscp & mask, mask, queue))
    sched = conf.get('scheduling') or {}
    if not isinstance(sched, dict):
        raise Refused('scheduling is not an object')
    # every port first, then each one named
    for name, mode in sorted(sched.items(), key=lambda kv: kv[0] != '*'):
        if not isinstance(mode, int) or isinstance(mode, bool) or not 0 <= mode <= 7:
            raise Refused('a scheduling mode is 0 to 7: %s' % name)
        for sw in (sorted(set(ports.values())) if name == '*' else [port_of(name, ports)]):
            out['sched'][sw] = mode
    weights = conf.get('weights')
    if weights is not None:
        if (not isinstance(weights, list) or not 1 <= len(weights) <= 128 or
                not all(isinstance(q, int) and not isinstance(q, bool) and 0 <= q <= 7 for q in weights)):
            raise Refused('weights: 1 to 128 queue numbers, each 0 to 7')
        missing = sorted(set(range(8)) - set(weights))
        if missing:
            raise Refused('weights: every queue at least once, or a weighted port never serves it - '
                          'missing %s' % ', '.join(str(q) for q in missing))
        out['weights'] = list(weights)
    limits = conf.get('limits') or {}
    if not isinstance(limits, dict):
        raise Refused('limits is not an object')
    for name, kbit in limits.items():
        sw = port_of(name, ports)
        if not isinstance(kbit, int) or isinstance(kbit, bool) or egress(kbit) is None:
            raise Refused('%s: an egress limit from 64 kbit/s to 10 Gbit/s' % name)
        out['limits'][sw] = kbit
    return out


def port_of(name, ports):
    for label, sw in ports.items():
        if label.lower() == str(name).lower():
            return sw
    raise Refused('%s: not a front port behind the switch here - those are: %s' % (name, ' '.join(sorted(ports))))


# ---- the appliance ----

PREFIX = {'octeontx': 'oxp', 'armada': 'npup'}      # models.py's PREFIX: a front port's device


def model(devices=None):
    """(family, {label: switch port} of the front ports behind the switch), from models.py; and their
    devices into devices, {device: label}, when it is given."""
    def run(*args):
        try:
            done = subprocess.run([PY, MODELS] + list(args), capture_output=True, text=True, errors='replace',
                                  timeout=30)
        except (OSError, subprocess.SubprocessError):
            return ''
        return done.stdout if done.returncode == 0 else ''
    head = run().split()
    family = head[1] if len(head) > 1 else ''
    ports = {}
    for line in run('--ports').splitlines():
        f = line.split()
        if len(f) >= 4 and re.match(r'^0x[0-9a-fA-F]{4}$', f[1]):
            tag = int(f[1], 16)
            if tag & 0x8000 and (tag >> 8) & 0x1f:
                ports[f[3]] = (tag >> 8) & 0x1f
                if devices is not None and family in PREFIX and f[0].isdigit():
                    devices[PREFIX[family] + f[0]] = f[3]
    return family, ports


def log(message):
    try:
        subprocess.run([LOGGER, '-t', 'xgs-switch', '-p', 'daemon.notice', message], timeout=10,
                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        pass
    print(message)


# The coprocessor's side, one line as switch.sh has it:
#   r dev reg / w dev reg value     mvsw once
#   h value                         a value is 0x and four hex digits (mvsw prints its errors as words)
#   b dev reg                       wait for bit 15 (busy) to clear, up to 50 reads
#   t <reader> ...                  three reads that agree, up to four rounds; "bad" otherwise
#   q dev ptr                       a pointer register (Queue Control, the weight table): the pointer
#                                   written alone, then read
#   o word                          a TCAM operation, waited for
#   c port counter bank             an RMON counter (UMSD statsReadCounter), read once
PRE = ('M=%s; ' % MVSW +
       'r(){ $M 0 2 read $1 $2;}; w(){ $M 0 2 write $1 $2 $3 >/dev/null;}; '
       'h(){ case $1 in 0x[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) return 0;; esac; return 1;}; '
       'b(){ _i=0; while [ $_i -lt 50 ]; do _v=$(r $1 $2); h "$_v" && [ $((_v & 0x8000)) -eq 0 ] && return 0; '
       '_i=$((_i+1)); done; return 1;}; '
       't(){ _n=0; while [ $_n -lt 4 ]; do _a=$("$@"); _c=$("$@"); _d=$("$@"); h "$_a" && [ "$_a" = "$_c" ] && '
       '[ "$_c" = "$_d" ] && { echo $_a; return 0;}; _n=$((_n+1)); done; echo bad; return 1;}; '
       'q(){ b $1 0x1c || { echo busy; return;}; w $1 0x1c $(printf 0x%04x $(($2 << 8))); r $1 0x1c;}; '
       'o(){ b 0x1f 0x00 || return 1; w 0x1f 0x00 $1; b 0x1f 0x00;}; '
       'c(){ b 0x1b 0x1d || { echo busy; return;}; w 0x1b 0x1d $(printf 0x%04x $((0x8000 | (4 << 12) | '
       '(($1 + 1) << 5) | ($2 & 0x1f) | ($3 << 10)))); b 0x1b 0x1d || { echo busy; return;}; '
       'echo $(( ($(r 0x1b 0x1e) << 16) | $(r 0x1b 0x1f) ));};')


def ask(seconds, commands):
    """Run on the coprocessor; its key=value lines as a dict. Failed when it does not answer."""
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, str(seconds), PRE + ' ' + commands], stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, errors='replace', timeout=seconds + 60)
    except (OSError, subprocess.SubprocessError) as exc:
        raise Failed('the coprocessor could not be asked: %s' % exc)
    if done.returncode != 0:
        said = (done.stderr.strip().splitlines() or [''])[-1]
        raise Failed('the coprocessor did not answer (npu-run.sh exit %d: %s)' % (done.returncode, said))
    out = {}
    for line in done.stdout.replace('\r', '').splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            out.setdefault(key.strip(), value.strip())
    return out


def value(res, key):
    v = res.get(key, '')
    if not re.match(r'^0x[0-9a-fA-F]{4}$', v):
        raise Refused('%s %s (read as %r): nothing written' % (key, NOREAD, v))
    return int(v, 16)


# ---- reading ----

def read_entry_cmds(entry):
    """An entry's page-0 keys; the rest of pages 0 and 2 only when it is in use - a free one's are not
    needed, and each costs three reads."""
    regs0 = ' '.join(str(r) for r in P0_REGS if r != 0x02)
    regs2 = ' '.join(str(r) for r in P2_REGS)
    return ('o %s && { k=$(t r 0x1f 0x02); echo e%dp0r2=$k; if [ "$k" != %s ]; then for g in %s; do '
            'echo e%dp0r$g=$(t r 0x1f $g); done; o %s && for g in %s; do echo e%dp2r$g=$(t r 0x1f $g); done; '
            'fi; };' % (x4(tcam_op(OP_READ, entry, 0)), entry, x4(FREE), regs0, entry,
                        x4(tcam_op(OP_READ, entry, 2)), regs2, entry))


def entry_from(res, entry):
    """(page 0 words, page 2 words) of an entry as read; a free entry's keys, the rest 0."""
    keys1 = value(res, 'e%dp0r2' % entry)
    if keys1 == FREE:
        page0 = dict.fromkeys(P0_REGS, 0)
        page0[0x02] = FREE
        return page0, dict.fromkeys(P2_REGS, 0)
    return ({r: value(res, 'e%dp0r%d' % (entry, r)) for r in P0_REGS},
            {r: value(res, 'e%dp2r%d' % (entry, r)) for r in P2_REGS})


def read_weights_cmds():
    return ('n=$(t q 0x1c %d); echo wn=$n; if h "$n"; then k=$(( (($n & 0xff) + 1) / 2 )); i=0; '
            'while [ $i -lt $k ]; do echo w$i=$(t q 0x1c $i); i=$((i+1)); done; fi;' % WEIGHT_LEN)


def weights_from(res):
    n = value(res, 'wn') & 0xff
    seq = []
    for i in range((n + 1) // 2):
        v = value(res, 'w%d' % i)
        seq.append(v & 7)
        seq.append((v >> 4) & 7)
    return seq[:n]


def read_all(want, state, ports):
    """One session: every register a run may write, three reads each."""
    sched = sorted(set(want['sched']) | {int(p) for p in state.get('sched', {})})
    limits = sorted(set(want['limits']) | {int(p) for p in state.get('limits', {})})
    cmd = 'echo m0=$(t r 0 0x0d);'
    for e in ENTRIES:
        cmd += read_entry_cmds(e)
    for p in sched:
        cmd += ' echo s%d=$(t q %d 0);' % (p, p)
    if want['weights'] is not None or state.get('weights'):
        cmd += read_weights_cmds()
    for p in limits:
        cmd += ' echo d%d=$(t r %d 0x09); echo l%d=$(t r %d 0x0a);' % (p, p, p, p)
    return ask(240, cmd + ' echo end=1')


# ---- deciding ----

def hand_limits():
    """The ports switch.sh keeps an egress limit on."""
    try:
        with open(HAND, encoding='utf-8', errors='replace') as f:
            return {int(m.group(1)) for m in re.finditer(r'^limit port=([0-9]+) ', f.read(), re.M)}
    except OSError:
        return set()


def load_state():
    try:
        with open(STATE, encoding='utf-8') as f:
            state = json.load(f)
        return state if isinstance(state, dict) else {}
    except (OSError, ValueError):
        return {}


def save_state(state):
    if not any(state.get(k) for k in ('entries', 'tcam_mode', 'sched', 'weights', 'limits')):
        try:
            os.remove(STATE)
        except FileNotFoundError:
            pass
        return
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE + '.new', 'w', encoding='utf-8') as f:
        json.dump(state, f, indent=1, sort_keys=True)
        f.write('\n')
        f.flush()
        os.fsync(f.fileno())
    os.replace(STATE + '.new', STATE)


def words_of(kept):
    return ({int(k): v for k, v in kept['p0'].items()}, {int(k): v for k, v in kept['p2'].items()})


def plan(want, state, res, ports):
    """What to write, from what is wanted, what this tool kept and what the chip holds now:
    (steps, new state, notes). A step is (what, before, after, kind, key); a conflict is Refused."""
    names = {sw: label for label, sw in ports.items()}

    def port(sw):
        return '%s (switch port %d)' % (names.get(int(sw), '?'), int(sw))
    steps, notes, refused = [], [], []
    new = {'entries': {}, 'sched': {}, 'limits': {}}

    # the entries
    wanted = {}
    for i, (dscp, mask, queue) in enumerate(want['classes']):
        e = ENTRIES[i]
        wanted[e] = {'dscp': dscp, 'mask': mask, 'queue': queue}
        p0, p2 = tcam_words(dscp, mask, queue, COUNTER[e])
        wanted[e]['p0'] = {str(k): v for k, v in p0.items()}
        wanted[e]['p2'] = {str(k): v for k, v in p2.items()}
    for e in ENTRIES:
        cur = entry_from(res, e)
        kept = state.get('entries', {}).get(str(e))
        free = cur[0][0x02] == FREE
        mine = kept is not None and cur == words_of(kept)
        if e in wanted:
            w = words_of(wanted[e])
            if cur == w:
                pass
            elif free or mine:
                steps.append(('entry %d: DSCP %d mask 0x%02x -> queue %d' % (
                    e, wanted[e]['dscp'], wanted[e]['mask'], wanted[e]['queue']),
                    None if free else words_of(kept), w, 'entry', e))
            else:
                refused.append('TCAM entry %d is in use by something else (page 0 keys %s %s): nothing written'
                               % (e, x4(cur[0][0x02]), x4(cur[0][0x03])))
            new['entries'][str(e)] = wanted[e]
        elif kept is not None:
            if mine:
                steps.append(('entry %d flushed' % e, cur, None, 'entry', e))
            elif not free:
                notes.append('TCAM entry %d holds something this tool did not write since: left as it is' % e)

    # port 0's TCAM mode
    m0 = value(res, 'm0')
    kept = state.get('tcam_mode')
    if want['classes']:
        if m0 & 3 not in (0, 1):
            refused.append('port 0 is in TCAM mode %d, not as measured: nothing written' % (m0 & 3))
        found = kept['found'] if kept and (m0 & 3) == (kept['set'] & 3) else m0
        target = (m0 & ~3 & 0xffff) | 1
        if m0 & 3 != 1:
            steps.append(('port 0 TCAM mode 48 bytes', m0, target, 'reg', (0, 0x0d)))
        new['tcam_mode'] = {'found': found, 'set': target}
    elif kept:
        if m0 & 3 == kept['set'] & 3:
            target = (m0 & ~3 & 0xffff) | (kept['found'] & 3)
            if target != m0:
                steps.append(('port 0 TCAM mode as found', m0, target, 'reg', (0, 0x0d)))
        else:
            notes.append('port 0 TCAM mode changed since by something else (%s): left as it is' % x4(m0))

    # scheduling
    for p in sorted(set(want['sched']) | {int(k) for k in state.get('sched', {})}):
        cur = value(res, 's%d' % p) & 0xff
        kept = state.get('sched', {}).get(str(p))
        if p in want['sched']:
            mode = want['sched'][p]
            found = kept['found'] if kept and cur == kept['set'] else cur
            if cur != mode:
                steps.append(('%s scheduling %d (%s)' % (port(p), mode, MODES[mode]), cur, mode, 'sched', p))
            new['sched'][str(p)] = {'found': found, 'set': mode}
        elif kept:
            if cur == kept['set'] and cur != kept['found']:
                steps.append(('%s scheduling as found (%d)' % (port(p), kept['found']), cur, kept['found'],
                              'sched', p))
            elif cur != kept['set']:
                notes.append('%s scheduling changed since by something else (%d): left as it is' % (port(p), cur))

    # the weights
    kept = state.get('weights')
    if want['weights'] is not None or kept:
        cur = weights_from(res)
        if want['weights'] is not None:
            found = kept['found'] if kept and cur == kept['set'] else cur
            if not found:
                refused.append('the weight table reads empty: nothing written')
            if cur != want['weights']:
                steps.append(('the chip\'s weights, %d turns' % len(want['weights']), cur, want['weights'],
                              'weights', None))
            new['weights'] = {'found': found, 'set': want['weights']}
        elif cur == kept['set']:
            steps.append(('the chip\'s weights as found, %d turns' % len(kept['found']), cur, kept['found'],
                          'weights', None))
        else:
            notes.append('the chip\'s weights changed since by something else: left as they are')

    # egress limits
    hand = hand_limits()
    for p in sorted(set(want['limits']) | {int(k) for k in state.get('limits', {})}):
        r09, r0a = value(res, 'd%d' % p), value(res, 'l%d' % p)
        kept = state.get('limits', {}).get(str(p))
        is_set = kept is not None and (r09 & 0x7f, r0a) == (kept['set09'] & 0x7f, kept['set0a'])
        if p in want['limits']:
            kbit = want['limits'][p]
            if p in hand:
                refused.append('%s has an egress limit switch.sh keeps (switch.sh limit off first): nothing '
                               'written' % port(p))
                continue
            f09, f0a = (kept['found09'], kept['found0a']) if is_set else (r09, r0a)
            t09, t0a = limit_words(kbit, r09)
            if (r0a & 0x3fff) and not is_set and (r09 & 0x7f, r0a) != (t09 & 0x7f, t0a):
                refused.append('%s has an egress limit already (0x09 %s, 0x0a %s), not this tool\'s: nothing '
                               'written' % (port(p), x4(r09), x4(r0a)))
                continue
            if (r09, r0a) != (t09, t0a):
                steps.append(('%s egress limit %s kbit/s (eDec %d, N %d)' % (port(p), egress(kbit)[2],
                              t09 & 0x7f, t0a & 0x3fff), (r09, r0a), (t09, t0a), 'limit', p))
            new['limits'][str(p)] = {'found09': f09, 'found0a': f0a, 'set09': t09, 'set0a': t0a, 'kbit': kbit}
        elif kept:
            if is_set:
                t09 = (r09 & ~0x7f & 0xffff) | (kept['found09'] & 0x7f)
                steps.append(('%s egress limit as found' % port(p), (r09, r0a), (t09, kept['found0a']), 'limit', p))
            else:
                notes.append('%s egress limit changed since by something else: left as it is' % port(p))

    if refused:
        raise Refused('\n'.join(refused))
    return order(steps), new, notes


def order(steps):
    """Safe order: limits and scheduling first; then the entries taken away with port 0's TCAM mode
    put back before them, or loaded with the mode set after them; the weights last."""
    def rank(step):
        kind, after = step[3], step[2]
        if kind == 'limit':
            return 0
        if kind == 'sched':
            return 1
        if kind == 'reg':
            return 2 if (after & 3) != 1 else 5
        if kind == 'entry':
            return 3 if after is None else 4
        return 6
    return sorted(steps, key=rank)


# ---- writing ----

def entry_cmds(entry, words):
    """An entry flushed, and loaded with words when there are any: page 2, page 1, page 0 last. Not a
    word of it while the engine stays busy - no exit either, which on the console would end its shell."""
    cmd = ['w 0x1f 0x01 0x0000', 'o %s' % x4(tcam_op(OP_FLUSH, entry))]
    if words is not None:
        page0, page2 = words
        cmd.append('o %s' % x4(tcam_op(OP_READ, entry, 2)))
        cmd += ['w 0x1f %s %s' % (hex(r), x4(page2[r])) for r in P2_REGS]
        cmd.append('o %s' % x4(tcam_op(OP_LOAD, entry, 2)))
        cmd.append('o %s' % x4(tcam_op(OP_READ, entry, 1)))
        cmd.append('for g in %s; do w 0x1f $g 0x0000; done' % ' '.join(str(r) for r in P1_REGS))
        cmd.append('o %s' % x4(tcam_op(OP_LOAD, entry, 1)))
        cmd.append('o %s' % x4(tcam_op(OP_READ, entry, 0)))
        cmd += ['w 0x1f %s %s' % (hex(r), x4(page0[r])) for r in P0_REGS if r not in (0x02, 0x03)]
        cmd += ['w 0x1f 0x2 %s' % x4(page0[0x02]), 'w 0x1f 0x3 %s' % x4(page0[0x03])]
        cmd.append('o %s' % x4(tcam_op(OP_LOAD, entry, 0)))
    return 'if b 0x1f 0x00; then %s; else echo tcam%d=busy; fi;' % ('; '.join(cmd), entry)


def step_cmds(step, value_):
    kind, key = step[3], step[4]
    if kind == 'entry':
        return entry_cmds(key, value_)
    if kind == 'reg':
        return 'w %d %s %s;' % (key[0], hex(key[1]), x4(value_))
    if kind == 'sched':
        return 'b %d 0x1c && w %d 0x1c %s;' % (key, key, x4(sched_word(value_)))
    if kind == 'weights':
        return ' '.join('b 0x1c 0x1c && w 0x1c 0x1c %s;' % x4(v) for v in weight_words(value_))
    if kind == 'limit':
        r09, r0a = value_
        # a limit is set with eDec first, then N; taken away with N first, then eDec
        if r0a & 0x3fff:
            return 'w %d 0x09 %s; w %d 0x0a %s;' % (key, x4(r09), key, x4(r0a))
        return 'w %d 0x0a %s; w %d 0x09 %s;' % (key, x4(r0a), key, x4(r09))
    raise ValueError(kind)


def change(step):
    """A step's register words, before and after, for the log."""
    kind, before, after = step[3], step[1], step[2]

    def entry(words):
        if words is None:
            return 'free'
        page0, page2 = words
        return 'page 0 %s, page 2 %s' % (' '.join('%s %s' % (hex(r), x4(v)) for r, v in sorted(page0.items()) if v),
                                         ' '.join('%s %s' % (hex(r), x4(v)) for r, v in sorted(page2.items()) if v))
    if kind == 'entry':
        return 'TCAM entry %d: %s -> %s' % (step[4], entry(before), entry(after))
    if kind == 'reg':
        return 'port %d register %s %s -> %s' % (step[4][0], hex(step[4][1]), x4(before), x4(after))
    if kind == 'sched':
        return 'Queue Control pointer 0 %d -> %d (%s)' % (before, after, x4(sched_word(after)))
    if kind == 'weights':
        return 'Global2 0x1c, %d turns -> %d (%s)' % (len(before), len(after),
                                                    ' '.join(x4(w) for w in weight_words(after)))
    if kind == 'limit':
        return '0x09 %s 0x0a %s -> 0x09 %s 0x0a %s' % (x4(before[0]), x4(before[1]), x4(after[0]), x4(after[1]))
    return ''


def readback_cmds(steps):
    cmd = ''
    for step in steps:
        kind, key = step[3], step[4]
        if kind == 'entry':
            cmd += read_entry_cmds(key)
        elif kind == 'reg':
            cmd += ' echo m0=$(t r 0 0x0d);'
        elif kind == 'sched':
            cmd += ' echo s%d=$(t q %d 0);' % (key, key)
        elif kind == 'weights':
            cmd += read_weights_cmds()
        elif kind == 'limit':
            cmd += ' echo d%d=$(t r %d 0x09); echo l%d=$(t r %d 0x0a);' % (key, key, key, key)
    return cmd


def readback(step, res):
    kind, key = step[3], step[4]
    if kind == 'entry':
        cur = entry_from(res, key)
        return None if cur[0][0x02] == FREE else cur
    if kind == 'reg':
        return value(res, 'm0')
    if kind == 'sched':
        return value(res, 's%d' % key) & 0xff
    if kind == 'weights':
        return weights_from(res)
    if kind == 'limit':
        return value(res, 'd%d' % key), value(res, 'l%d' % key)


def same(step, got, want):
    if step[3] == 'limit':
        return got is not None and want is not None and (got[0] & 0x7f, got[1]) == (want[0] & 0x7f, want[1])
    return got == want


def write(steps, which):
    """Write each step's value (which: 2 the new one, 1 the one before), read them all back: the steps
    that did not read back as written."""
    if not steps:
        return []
    # a session each, in order: a command line of a few hundred characters to some 2,700 (the weights),
    # where all of them at once would be several thousand - over the console too, should the link not
    # answer
    for s in steps:
        try:
            ask(120, step_cmds(s, s[which]) + ' echo done=1')
        except Failed as exc:
            print(str(exc), file=sys.stderr)
    try:
        res = ask(240, readback_cmds(steps) + ' echo end=1')
    except Failed as exc:
        print(str(exc), file=sys.stderr)
        return list(steps)
    bad = []
    for s in steps:
        try:
            got = readback(s, res)
        except Refused:
            got = 'unreadable'
        if not same(s, got, s[which]):
            bad.append(s)
    return bad


# ---- the commands ----

def converge(want, why):
    """Bring the chip to what is wanted (nothing wanted: put everything back). Exit status."""
    family, ports = model()
    if family == 'armada':
        print('the XGS 136 family: this tool only reads the switch here (its bus is shared with the network '
              'agent, #639, #706); nothing written', file=sys.stderr)
        return 2
    if family != 'octeontx' or not ports:
        print('not an appliance whose switch this tool knows: nothing written', file=sys.stderr)
        return 2
    state = load_state()
    try:
        want = parse_conf(want, ports)
        res = read_all(want, state, ports)
        if res.get('end') != '1':
            raise Failed('the reads did not run to their end on the coprocessor')
        steps, new, notes = plan(want, state, res, ports)
    except (Refused, Failed) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    for note in notes:
        log('switch-qos: ' + note)
    if not steps:
        save_state(dict(new, owner=want_owner(), applied=stamp()) if any(new.values()) else {})
        print('the switch holds this already (%s)' % why)
        return 0
    # what was found, kept BEFORE anything is written: a run cut short still leaves what to put back
    pending = merge(state, new)
    save_state(pending)
    bad = write(steps, 2)
    if not bad:
        for s in steps:
            log('switch-qos: %s - %s (%s)' % (s[0], change(s), why))
        save_state(dict(new, owner=want_owner(), applied=stamp()) if any(new.values()) else {})
        return 0
    print('not read back as written: %s - what this run changed is put back' % '; '.join(s[0] for s in bad),
          file=sys.stderr)
    undo = list(reversed(steps))
    left = write(undo, 1)
    if left:
        print('PUT BACK DIFFERS: %s - the state keeps what was found; switch_qos.py show, then clear'
              % '; '.join(s[0] for s in left), file=sys.stderr)
        log('switch-qos: a run that did not read back as written could not put back %s'
            % '; '.join(s[0] for s in left))
        return 1
    log('switch-qos: a run that did not read back as written (%s) was put back as found: %s'
        % ('; '.join(s[0] for s in bad), '; '.join('%s - %s, then back' % (s[0], change(s)) for s in steps)))
    save_state(state)
    return 1


OWNER = {'value': ''}


def want_owner():
    return OWNER['value']


def stamp():
    return time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime())


def merge(state, new):
    """The state while a run writes: every found value of either, so a cut run leaves both to put back."""
    out = json.loads(json.dumps(state))
    for k in ('entries', 'sched', 'limits'):
        out.setdefault(k, {}).update(new.get(k, {}))
    for k in ('tcam_mode', 'weights'):
        if new.get(k):
            out[k] = new[k]
    return out


def read_conf():
    try:
        with open(CONF, encoding='utf-8') as f:
            conf = json.load(f)
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as exc:
        raise Refused('%s cannot be read: %s' % (CONF, exc))
    if isinstance(conf, dict):
        OWNER['value'] = str(conf.get('owner', ''))[:64]
    return conf


def locked(fn):
    import fcntl
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    with open(LOCK, 'a') as handle:
        deadline = time.time() + 60
        while True:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.time() > deadline:
                    print('another run holds %s: nothing done' % LOCK, file=sys.stderr)
                    return 75
                time.sleep(1)
        return fn()


def cmd_apply(why='apply'):
    try:
        conf = read_conf()
    except Refused as exc:
        print(str(exc), file=sys.stderr)
        return 1
    if conf is None:
        if load_state():
            return converge({}, why + ', no settings file: put back')
        print('no settings file (%s): nothing to set' % CONF)
        return 0
    return converge(conf, why)


def cmd_clear():
    rc = converge({}, 'clear') if load_state() else 0
    if rc == 0:
        for path in (CONF, STATE):
            try:
                os.remove(path)
            except FileNotFoundError:
                pass
        print('nothing of this tool\'s is left in the switch')
    return rc


def cmd_reapply(boot):
    if not os.path.exists(CONF) and not load_state():
        return 0
    if not boot:
        return locked(lambda: cmd_apply('reapply'))
    # at boot: after the coprocessor's link is given its address and the frame limit is set
    waited = 0
    while waited < BOOT_WAIT and any(busy(p) for p in BEFORE_US):
        time.sleep(5)
        waited += 5
    rc = 1
    for i in range(BOOT_TRIES):
        rc = locked(lambda: cmd_apply('after a coprocessor boot'))
        if rc in (0, 2):
            break
        time.sleep(BOOT_PAUSE)
    if rc not in (0, 2):
        log('switch-qos: not set again after the coprocessor boot (%d tries); switch_qos.py reapply' % BOOT_TRIES)
    return rc


def busy(pattern):
    try:
        return subprocess.run(['/bin/pgrep', '-f', pattern], capture_output=True, timeout=10).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def cmd_can():
    devices = {}
    family, ports = model(devices)
    if family == 'octeontx' and ports:
        why = ''
    elif family == 'armada':
        why = 'read-only'
    else:
        why = 'no-switch'
    print(json.dumps({'family': family, 'writes': why == '', 'why': why,
                      'ports': sorted(ports, key=lambda k: ports[k]), 'devices': devices,
                      'entries': len(ENTRIES)}, sort_keys=True))
    return 0


def cmd_words():
    family, ports = model()
    try:
        conf = read_conf() or {}
        want = parse_conf(conf, ports)
    except Refused as exc:
        print(json.dumps({'error': str(exc)}))
        return 1
    out = {'entries': {}, 'sched': {}, 'weights': None, 'limits': {}}
    for i, (dscp, mask, queue) in enumerate(want['classes']):
        e = ENTRIES[i]
        p0, p2 = tcam_words(dscp, mask, queue, COUNTER[e])
        out['entries'][str(e)] = {'page0': {hex(r): x4(v) for r, v in p0.items() if v},
                                  'page2': {hex(r): x4(v) for r, v in p2.items() if v}}
    for p, mode in want['sched'].items():
        out['sched'][str(p)] = x4(sched_word(mode))
    if want['weights'] is not None:
        out['weights'] = [x4(v) for v in weight_words(want['weights'])]
    for p, kbit in want['limits'].items():
        edec, n, got = egress(kbit)
        out['limits'][str(p)] = {'edec': edec, 'n': n, 'kbit': got, '0x0a': x4(0x8000 | n)}
    print(json.dumps(out, sort_keys=True))
    return 0


def cmd_show(as_json):
    family, ports = model()
    if family not in ('armada', 'octeontx') or not ports:
        print('not an appliance whose switch this tool knows', file=sys.stderr)
        return 1
    if family == 'armada':
        # the census and port 0's mode alone: as few reads as phase 0's step B0 plans
        cmd = 'echo m0=$(t r 0 0x0d);'
        for e in (3, 254, 255):
            cmd += ' o %s && echo e%dk1=$(t r 0x1f 0x02) && echo e%dk2=$(t r 0x1f 0x03);' % (
                x4(tcam_op(OP_READ, e, 0)), e, e)
        cmd += ' o %s && echo next=$(r 0x1f 0x00); echo end=1' % x4(tcam_op(OP_NEXT, 2))
    else:
        cmd = 'echo m0=$(t r 0 0x0d);'
        for e in ENTRIES:
            cmd += read_entry_cmds(e)
            cmd += ' echo h%d=$(c 0 %d 1);' % (e, 0x08 + COUNTER[e])
        for p in sorted(set(ports.values())):
            cmd += ' echo s%d=$(t q %d 0); echo d%d=$(t r %d 0x09); echo l%d=$(t r %d 0x0a);' % (p, p, p, p, p, p)
        cmd += read_weights_cmds() + ' echo end=1'
    try:
        res = ask(240, cmd)
    except Failed as exc:
        print(str(exc), file=sys.stderr)
        return 1
    out = describe(family, ports, res)
    if as_json:
        print(json.dumps(out, sort_keys=True))
    else:
        for line in out['lines']:
            print(line)
    return 0 if not out['unreadable'] else 1


def describe(family, ports, res):
    """What show read, as lines and as values."""
    lines, out, bad = [], {'family': family}, []

    def get(key):
        v = res.get(key, '')
        if re.match(r'^0x[0-9a-fA-F]{4}$', v):
            return int(v, 16)
        bad.append(key)
        return None
    m0 = get('m0')
    out['tcam_mode'] = None if m0 is None else m0 & 3
    lines.append('port 0 (the coprocessor) TCAM mode: %s' % (
        'unreadable' if m0 is None else {0: 'off', 1: '48-byte', 2: '96-byte', 3: 'mode 3'}[m0 & 3]))
    state = load_state()
    out['entries'] = {}
    if family == 'armada':
        for e in (3, 254, 255):
            k1, k2 = get('e%dk1' % e), get('e%dk2' % e)
            what = 'unreadable' if k1 is None or k2 is None else (
                'free' if k1 == FREE else 'in use (keys %s %s)' % (x4(k1), x4(k2)))
            out['entries'][str(e)] = what
            lines.append('TCAM entry %d: %s' % (e, what))
        lines.append('TCAM get-next after 2: %s' % res.get('next', '?'))
        lines.append('the XGS 136 family: nothing else read, and nothing is written here')
    else:
        for e in ENTRIES:
            try:
                cur = entry_from(res, e)
            except Refused:
                bad.append('entry %d' % e)
                lines.append('TCAM entry %d: unreadable' % e)
                continue
            kept = state.get('entries', {}).get(str(e))
            if cur[0][0x02] == FREE:
                what = 'free'
            elif kept and cur == words_of(kept):
                what = 'this tool\'s: DSCP %d mask 0x%02x -> queue %d, %s frames' % (
                    kept['dscp'], kept['mask'], kept['queue'], res.get('h%d' % e, '?'))
            else:
                what = 'in use by something else (keys %s %s)' % (x4(cur[0][0x02]), x4(cur[0][0x03]))
            out['entries'][str(e)] = {'what': what, 'hits': res.get('h%d' % e)}
            lines.append('TCAM entry %d: %s' % (e, what))
        out['ports'] = {}
        for label, p in sorted(ports.items(), key=lambda kv: kv[1]):
            s, d, l_ = get('s%d' % p), get('d%d' % p), get('l%d' % p)
            if None in (s, d, l_):
                lines.append('%s (switch port %d): unreadable' % (label, p))
                continue
            rate = limit_kbit(d, l_)
            lim = 'none' if rate is None else ('frame mode, N %d' % (l_ & 0x3fff) if rate == 'frames' else
                                               '%d kbit/s (eDec %d, N %d)' % (rate, d & 0x7f, l_ & 0x3fff))
            out['ports'][label] = {'sched': s & 0xff, 'limit_kbit': rate}
            lines.append('%s (switch port %d): scheduling %d (%s); egress limit %s' % (
                label, p, s & 0xff, MODES.get(s & 0xff, '?'), lim))
        try:
            seq = weights_from(res)
            out['weights'] = seq
            counts = ', '.join('%d: %d' % (q, seq.count(q)) for q in range(7, -1, -1))
            lines.append('weights: %d turns - queue %s' % (len(seq), counts))
        except Refused:
            bad.append('weights')
            lines.append('weights: unreadable')
    lines.append('kept by this tool: %s' % (json.dumps(state, sort_keys=True) if state else 'nothing'))
    out['unreadable'] = bad
    out['lines'] = lines
    return out


def usage():
    print(__doc__.split('THE SETTINGS FILE')[0].strip().split('\n\n', 1)[1], file=sys.stderr)
    return 64


def main(argv):
    if not argv:
        return usage()
    cmd, rest = argv[0], argv[1:]
    if cmd == 'show' and rest in ([], ['--json']):
        return cmd_show(rest == ['--json'])
    if cmd == 'apply' and not rest:
        return locked(cmd_apply)
    if cmd == 'reapply' and rest in ([], ['--boot']):
        return cmd_reapply(rest == ['--boot'])
    if cmd == 'clear' and not rest:
        return locked(cmd_clear)
    if cmd == 'can' and not rest:
        return cmd_can()
    if cmd == 'words' and not rest:
        return cmd_words()
    return usage()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
