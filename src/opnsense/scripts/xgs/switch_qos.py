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

    switch_qos.py show [--json]      read only: port 0's TCAM mode, entries 3 and 255 and their hits, the
                                     vendor's entries around them, each front port's scheduling and
                                     egress limit, the chip's weights, what the state keeps
    switch_qos.py apply              the settings file into the chip; what is there read first and kept
    switch_qos.py reapply [--boot]   the same after a coprocessor boot (the start hook, recover.sh); with
                                     no settings file, what the state still holds put back
    switch_qos.py clear              the settings file removed first, then everything this tool set put
                                     back as found
    switch_qos.py check [--json]     read only, a few reads: whether entries 3 and 255 and port 0's TCAM
                                     mode still hold what this tool set (the caller's status page)
    switch_qos.py can                no bus: whether this model takes writes, and its ports behind the
                                     switch with their devices, as JSON (for the caller's page)
    switch_qos.py words              no bus: the register words the settings file makes, as JSON

THE SETTINGS FILE, /var/db/os-xgs-npu/switch-qos.json, written by the caller (os-trafficpriority's
apply.py), read here and never written but by "clear", which removes it before it puts anything back:

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
back. One entry serves every front port at once: the queue is set where the frame enters the switch,
for EVERY frame carrying the DSCP, whoever marked it - which frames carry it is the caller's to see to.

THE ENTRY, as measured (one per class; mask and queue are the caller's):
    page 0  reg 0x02 0xc740    frame type 1 (the coprocessor's DSA-tagged frames) under its mask
            reg 0x03 0xff01    from port 0 only
            reg 0x16 0xff08, reg 0x17 0xff00    EtherType 0x0800 at octets 16-17, behind the tag
            reg 0x19 (mask << 2) << 8 | dscp << 2   the TOS byte, the ECN bits left out
    page 2  reg 0x03 0x0080 | queue << 4       QPri override, the queue
            reg 0x1b 0x4000 | counter << 12    IncTcamCtr: entry 3 counts in TCAMCounter1 (measured),
                                               entry 255 in TCAMCounter2 (not measured)
    every other page-0 and page-2 register written 0; loaded as UMSD's Amethyst_msdTCAM.c (BSD-3) and
    Linux's mv88e6390_tcam_entry_add load one: flush, page 2, page 1, page 0 last, which makes it valid.
    Then port 0's TCAM mode (port register 0x0d bits 1:0) set to 1, 48 bytes; its other bits kept.
    What is compared when an entry is read back are the seven registers the lab read back after a load
    on the XGS 3300 (P0_CHECKED, P2_CHECKED): the others are written 0 and not compared, as no read of
    them after a load has been made.
The vendor's own entries all key frame type 0, so they never see port 0's frames; only entries 3 and
255 were free on the XGS 3300 (all 256 read on 2026-11-11, 254 in use). Two classes at most, then.
Entry 255 sits behind the vendor's drop-all 254, which keys type 0 too: that it works is expected,
not measured. IPv4 only: an IPv6 key needs EtherType 0x86dd and a traffic class across two octets,
an entry of its own; not offered. An entry in use by anything else is never touched: the run is
refused.

THE TCAM ENGINE IS SHARED. The coprocessor's network agent loads entries through the same command
engine, with no lock between it and this host (the agent arms a port's address with one), so an
operation of the agent's between two of these could mix two entries' words. So, in the same session as
each load, the entry is read back and flushed at once when it does not read as written; the vendor's
entries beside ours - 0, 1, 2, 4 and 254 - and the TCAM's get-next after 2 and after 3 are read before
anything is written and again after, and a run that finds them changed is refused, its writes put
back; and "check" reads entries 3 and 255 and port 0's mode again whenever the caller asks.

SCHEDULING, per egress port: Queue Control (port register 0x1c), pointer 0, written as
0x8000 | pointer << 8 | data and read by writing the pointer alone (UMSD Amethyst_msdQueueCtrl.c,
gprtSetQueueCtrl / gprtSetPortSched). 0 weighted on all eight queues (as the vendor leaves every port);
1 to 6 the top 1 to 6 queues strict and the rest weighted; 7 strict on all. Measured on port 8 of the
second XGS 3300 on 2026-11-17: 0, 1 and 7 written, read back and put back. Port 0, the coprocessor's
uplink, is never set here.

THE WEIGHTS, one sequence for the whole chip: Global2 register 0x1c, the QoS weight table - a
sequence of up to 128 queue numbers, two to a pointer (pointer i << 8, data bits 2:0 and 6:4), its
length at pointer 0x40 (UMSD gsysSetQoSWeight / gsysGetQoSWeight). A weighted port serves its queues
in this order, each as often as it appears. On the second XGS 3300 (2026-11-17) the length read 99 -
the sum of the default weights UMSD's source names, 33, 25, 17, 12, 6, 3, 2 and 1 for queues 7 to 0;
the sequence itself was not read, and none has been written. Every port in weighted mode takes the
same sequence, port 0 among them; a sequence here must name each of the eight queues at least once, so
that none is ever shut out.

THE EGRESS LIMIT, per port, not per queue: port register 0x09 bits 6:0 (eDec) and 0x0a bits 15:14
(count mode: 2, layer-2 bytes) and 13:0 (N). UMSD's scale (Amethyst_msdPortCtrl.c,
grcSetEgressRate and its macro): eDec = the rate in 64 kbit/s below 1 Mbit/s, in Mbit/s to 100, in
10 Mbit/s to 1 Gbit/s, in 100 Mbit/s above; N = 8,000,000 * eDec / (16 * kbit/s), rounded up; so
100 Mbit/s is eDec 100 and N 500, 480 Mbit/s eDec 48 and N 50, 900 eDec 90 and N 50. A rate between
the steps is taken down to one. 0x09 is read first and only its bits 6:0 are changed. Measured with
eDec 1 as the vendor leaves it (switch.sh's 480/N steps, docs/measurements/switch-3300-2026-11-11.md);
other values of eDec not measured. A port with a limit that is not this tool's - switch.sh's, or
anything else's - is refused.

READS AND WRITES as switch.sh makes them, on the coprocessor through npu-run.sh and contrib/mvsw -
whose presence there is asked first: every value read three times in a row and used only when the
three agree, in up to four rounds (the network agent polls the same bus); every register written was
read in the same run; every write read back and compared, and logged with its words (tag xgs-switch).
A run refused - an entry or a limit not this tool's, a value that never read the same - writes nothing.
A run that sets things and cannot write all it set out to puts back what it changed, reads again, and
says so. A run that puts things back - clear, or a reapply with no settings file - never sets again
what it has put back: what it could not put back stays in the state for the next run. One run at a
time, under switch.sh's lock.

THE STATE, /var/db/os-xgs-npu/switch-qos.state (JSON), one item for each thing this tool has a hand
in: "found", what was there before this tool ever wrote it - what is put back; "set", what this tool
last wrote and read back; and, while a run writes, "pending", what it is writing. The pending value is
kept before the run writes, and becomes the set one only once it has read back; a run cut short - the
link gone, the host stopped - leaves it, and the next run takes the chip's value as this tool's when it
is the found, the set or the pending one (or anything, for a value written in several words, the
weights and a limit), and finishes or puts back from there: what was found is never lost. A coprocessor
boot erases the switch's registers; reapply then finds the found values back and sets the settings
file's again.

ONE MODEL WRITES. Only the XGS 3300 (assembly AMDA0202-0004), the model all of this was measured on,
takes writes. The others of its family - the XGS 2100, 2300 and 3100 - read only, until they are run.
The XGS 136's family is read only as well: its switch is the same chip, but its network agent shares the
bus and reads of the host's have taken ports down there (#639, #706), and its TCAM has never been read.
So "show" there reads port registers alone - port 0's 0x04, 0x08 and 0x0d - and no TCAM operation at
all, and apply, reapply and clear refuse before anything is asked.
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
# the one model all of this was measured on, and the only one that takes writes
WRITES = 'AMDA0202-0004'

ENTRIES = (3, 255)
COUNTER = {3: 1, 255: 2}               # IncTcamCtr's counter for each entry; TCAMCounterN is bank 1 0x08 + N
P0_REGS = list(range(0x02, 0x1c))      # keys 0x02-0x05, then frame octets 0-21 at 0x06-0x1b: all written
P1_REGS = list(range(0x02, 0x1c))      # frame octets 22-47: all written 0
P2_REGS = [0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x1b]
# what a load is compared with when read back: the registers lab-tp-p0.sh read back after one on the
# XGS 3300 (2026-11-17); the rest are written 0 and not compared
P0_CHECKED = (0x02, 0x03, 0x16, 0x17, 0x19)
P2_CHECKED = (0x03, 0x1b)
CENSUS = (0, 1, 2, 4, 254)             # the vendor's entries around ours, read before and after a write
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


def checked(meta, entry):
    """What a loaded entry reads back as, in the registers compared: {'p0': {reg: word}, 'p2': ...}, the
    registers as strings (JSON's keys); None for a free entry (meta None)."""
    if meta is None:
        return None
    page0, page2 = tcam_words(meta['dscp'], meta['mask'], meta['queue'], COUNTER[entry])
    return {'p0': {str(r): page0[r] for r in P0_CHECKED}, 'p2': {str(r): page2[r] for r in P2_CHECKED}}


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
    if not isinstance(kbit, int) or isinstance(kbit, bool) or kbit < 64 or kbit > 10000000:
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
        if egress(kbit) is None:
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
    """(assembly, family, {label: switch port} of the front ports behind the switch), from models.py; and
    their devices into devices, {device: label}, when it is given."""
    def run(*args):
        try:
            done = subprocess.run([PY, MODELS] + list(args), capture_output=True, text=True, errors='replace',
                                  timeout=30)
        except (OSError, subprocess.SubprocessError):
            return ''
        return done.stdout if done.returncode == 0 else ''
    head = run().split()
    assembly = head[0] if head else ''
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
    return assembly, family, ports


def writable(assembly, family, ports):
    """'' when this appliance's switch takes writes here, else why not: read-only (the XGS 136's family),
    untested-model (the XGS 3300's family, a model nothing was run on), no-switch."""
    if family == 'armada' and ports:
        return 'read-only'
    if family != 'octeontx' or not ports:
        return 'no-switch'
    return '' if assembly == WRITES else 'untested-model'


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
    """Run on the coprocessor; its key=value lines as a dict. Failed when it does not answer, or when
    the switch tool is not there (nothing at all is asked of the switch then)."""
    try:
        body = commands.strip().rstrip(';')
        done = subprocess.run(['/bin/sh', NPU_RUN, str(seconds),
                               '%s if [ -x $M ]; then %s; else echo nomvsw=1; fi' % (PRE, body)],
                              stdin=subprocess.DEVNULL, capture_output=True, text=True, errors='replace',
                              timeout=seconds + 60)
    except (OSError, subprocess.SubprocessError) as exc:
        raise Failed('the coprocessor could not be asked: %s' % exc)
    if done.returncode != 0:
        said = (done.stderr.strip().splitlines() or [''])[-1]
        raise Failed('the coprocessor did not answer (npu-run.sh exit %d: %s)' % (done.returncode, said))
    out = {}
    for line in done.stdout.replace('\r', '').splitlines():
        if '=' in line:
            key, value_ = line.split('=', 1)
            out.setdefault(key.strip(), value_.strip())
    if out.get('nomvsw') == '1':
        raise Failed('the switch tool is not on the coprocessor (%s): switch-jumbo.sh puts it there at every '
                     'boot, and it has not yet - nothing read or written' % MVSW)
    return out


def value(res, key):
    v = res.get(key, '')
    if not re.match(r'^0x[0-9a-fA-F]{4}$', v):
        raise Refused('%s %s (read as %r): nothing written' % (key, NOREAD, v))
    return int(v, 16)


# ---- reading ----

def read_entry_cmds(entry, tag='e'):
    """An entry's page-0 keys; the rest of the compared registers only when it is in use."""
    regs0 = ' '.join(str(r) for r in P0_CHECKED if r != 0x02)
    regs2 = ' '.join(str(r) for r in P2_CHECKED)
    return ('o %s && { k=$(t r 0x1f 0x02); echo %s%dp0r2=$k; if [ "$k" != %s ]; then for g in %s; do '
            'echo %s%dp0r$g=$(t r 0x1f $g); done; o %s && for g in %s; do echo %s%dp2r$g=$(t r 0x1f $g); done; '
            'fi; };' % (x4(tcam_op(OP_READ, entry, 0)), tag, entry, x4(FREE), regs0, tag, entry,
                        x4(tcam_op(OP_READ, entry, 2)), regs2, tag, entry))


def entry_from(res, entry, tag='e'):
    """An entry as read, in the registers compared: None when it is free."""
    keys1 = value(res, '%s%dp0r2' % (tag, entry))
    if keys1 == FREE:
        return None
    return {'p0': {str(r): value(res, '%s%dp0r%d' % (tag, entry, r)) for r in P0_CHECKED},
            'p2': {str(r): value(res, '%s%dp2r%d' % (tag, entry, r)) for r in P2_CHECKED}}


def census_cmds(tag):
    """The vendor's entries around ours - their page-0 keys - and the TCAM's get-next after 2 and 3."""
    cmd = ''
    for e in CENSUS:
        cmd += ' o %s && { echo %s%dk1=$(t r 0x1f 0x02); echo %s%dk2=$(t r 0x1f 0x03); };' % (
            x4(tcam_op(OP_READ, e, 0)), tag, e, tag, e)
    for after in (2, 3):
        cmd += ' o %s && echo %sn%d=$(t r 0x1f 0x00);' % (x4(tcam_op(OP_NEXT, after)), tag, after)
    return cmd


def census_from(res, tag):
    """The census as read: {name: word}; a value that did not read the same is 'bad'."""
    out = {}
    for e in CENSUS:
        for k in ('k1', 'k2'):
            out['%d%s' % (e, k)] = res.get('%s%d%s' % (tag, e, k), 'none')
    for after in (2, 3):
        v = res.get('%sn%d' % (tag, after), 'none')
        # the operation register after a get-next: its entry, in bits 7:0
        out['next%d' % after] = ('%d' % (int(v, 16) & 0xff)) if re.match(r'^0x[0-9a-fA-F]{4}$', v) else v
    return out


def census_moved(before, after, entry3_valid):
    """What of the census changed during a run - the vendor's entries, and the get-next after 3 - and
    whether the get-next after 2 fits entry 3 as this run leaves it: 3 when 3 is in use, else the one
    after 3. Empty when nothing did."""
    moved = []
    for key in sorted(before):
        if key == 'next2':
            continue
        if before[key] != after.get(key) or 'bad' in (before[key], after.get(key)):
            moved.append('%s %s -> %s' % (key, before[key], after.get(key)))
    want2 = '3' if entry3_valid else after.get('next3')
    if after.get('next2') != want2:
        moved.append('get-next after 2 %s, not %s' % (after.get('next2'), want2))
    return moved


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


def read_cmds(keys):
    """The reads of the items named, and of port 0's TCAM mode and both entries always."""
    cmd = 'echo m0=$(t r 0 0x0d);'
    for e in ENTRIES:
        cmd += read_entry_cmds(e)
    for key in sorted(keys):
        kind, _, arg = key.partition(':')
        if kind == 'sched':
            cmd += ' echo s%s=$(t q %s 0);' % (arg, arg)
        elif kind == 'limit':
            cmd += ' echo d%s=$(t r %s 0x09); echo l%s=$(t r %s 0x0a);' % (arg, arg, arg, arg)
    if 'weights' in keys:
        cmd += read_weights_cmds()
    return cmd


def current(key, res):
    """An item's value as read: an entry's compared words or None, port 0's 0x0d, a port's scheduling,
    the weight sequence, a port's [0x09, 0x0a]."""
    kind, _, arg = key.partition(':')
    if kind == 'entry':
        return entry_from(res, int(arg))
    if kind == 'mode':
        return value(res, 'm0')
    if kind == 'sched':
        return value(res, 's%s' % arg) & 0xff
    if kind == 'weights':
        return weights_from(res)
    if kind == 'limit':
        return [value(res, 'd%s' % arg), value(res, 'l%s' % arg)]
    raise ValueError(key)


# ---- the state ----

def load_state():
    """The state, {'items': {key: item}, 'owner', 'applied'}; an older form, or none, is no items."""
    try:
        with open(STATE, encoding='utf-8') as f:
            state = json.load(f)
    except (OSError, ValueError):
        return {'items': {}}
    if not isinstance(state, dict) or not isinstance(state.get('items'), dict):
        return {'items': {}}
    return state


def save_state(items, owner=''):
    if not items:
        try:
            os.remove(STATE)
        except FileNotFoundError:
            pass
        return
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE + '.new', 'w', encoding='utf-8') as f:
        json.dump({'items': items, 'owner': owner, 'applied': stamp()}, f, indent=1, sort_keys=True)
        f.write('\n')
        f.flush()
        os.fsync(f.fileno())
    os.replace(STATE + '.new', STATE)


def same(key, a, b):
    """Whether two values of an item are the same as the chip has them: an entry by its compared words
    (a value kept is a class's dscp, mask and queue, one read the words), port 0's mode by bits 1:0, a
    limit by eDec and 0x0a."""
    kind, _, arg = key.partition(':')
    if kind == 'entry':
        a = checked(a, int(arg)) if a is not None and 'dscp' in a else a
        b = checked(b, int(arg)) if b is not None and 'dscp' in b else b
        return a == b
    if a is None or b is None:
        return a is b
    if kind == 'mode':
        return (a & 3) == (b & 3)
    if kind == 'limit':
        return (a[0] & 0x7f, a[1]) == (b[0] & 0x7f, b[1])
    return a == b


# ---- deciding ----

def hand_limits():
    """The ports switch.sh keeps an egress limit on."""
    try:
        with open(HAND, encoding='utf-8', errors='replace') as f:
            return {int(m.group(1)) for m in re.finditer(r'^limit port=([0-9]+) ', f.read(), re.M)}
    except OSError:
        return set()


def wanted(want):
    """Each item the settings ask for, by key: an entry's class, port 0's mode ('on'), a port's
    scheduling, the weights, a port's limit in kbit/s."""
    out = {}
    for i, (dscp, mask, queue) in enumerate(want['classes']):
        out['entry:%d' % ENTRIES[i]] = {'dscp': dscp, 'mask': mask, 'queue': queue}
    if want['classes']:
        out['mode'] = 'on'
    for p, mode in want['sched'].items():
        out['sched:%d' % p] = mode
    if want['weights'] is not None:
        out['weights'] = want['weights']
    for p, kbit in want['limits'].items():
        out['limit:%d' % p] = kbit
    return out


def port_name(key, ports):
    """A port item's front port, as the Ports page names it, and its switch port."""
    arg = key.partition(':')[2]
    names = {sw: label for label, sw in ports.items()}
    return '%s (switch port %s)' % (names.get(int(arg), '?'), arg) if arg.isdigit() else ''


def describe(key, target, ports):
    kind, _, arg = key.partition(':')
    port = port_name(key, ports) if kind != 'entry' else ''
    if kind == 'entry':
        return ('entry %s: DSCP %d mask 0x%02x -> queue %d' % (arg, target['dscp'], target['mask'], target['queue'])
                if target else 'entry %s flushed' % arg)
    if kind == 'mode':
        return 'port 0 TCAM mode %s' % ('48 bytes' if target & 3 == 1 else 'as found (%d)' % (target & 3))
    if kind == 'sched':
        return '%s scheduling %d (%s)' % (port, target, MODES.get(target, '?'))
    if kind == 'weights':
        return 'the chip\'s weights, %d turns' % len(target)
    if kind == 'limit':
        rate = limit_kbit(target[0], target[1])
        return '%s egress limit %s' % (port, 'none' if rate is None else '%s kbit/s (eDec %d, N %d)' % (
            rate, target[0] & 0x7f, target[1] & 0x3fff))
    return key


def plan(want, state, res, ports):
    """What to write, from what is wanted, what this tool keeps and what the chip holds now:
    (steps, items after, items while writing, items as they stand, notes). A step is (key, before,
    after); a conflict is Refused. An item's pending value, left by a run cut short, is taken as set
    when the chip holds it; the items as they stand are the kept ones once that is settled - what a run
    that is not kept goes back to."""
    asked = wanted(want)
    kept = {k: dict(v) for k, v in state.get('items', {}).items()}
    steps, notes, refused = [], [], []
    after, during, base = {}, {}, {}
    hand = hand_limits()
    for key in sorted(set(asked) | set(kept)):
        kind, _, arg = key.partition(':')
        cur = current(key, res)
        item = kept.get(key)
        cut = False
        if item is not None and 'pending' in item:
            # a run was cut while it wrote this: its value counts as this tool's either way
            cut = True
            if same(key, cur, item['pending']):
                item['set'] = item['pending']
            del item['pending']
        known = item is not None and (cut or same(key, cur, item.get('set')) or same(key, cur, item.get('found')))
        if kind == 'entry' and item is not None and cut and cur is not None and \
                not same(key, cur, item.get('set')):
            known = False           # an entry reads free, as set, or as pending: anything else is not ours
        if item is not None and known:
            base[key] = {'found': item['found'], 'set': recognised(key, cur, item)}
        if key in asked:
            target = asked[key]
            if kind == 'mode':
                if cur & 3 not in (0, 1):
                    refused.append('port 0 is in TCAM mode %d, not as measured: nothing written' % (cur & 3))
                    continue
                target = (cur & ~3 & 0xffff) | 1
            elif kind == 'limit':
                if int(arg) in hand:
                    refused.append('%s has an egress limit switch.sh keeps (switch.sh limit off first): '
                                   'nothing written' % port_name(key, ports))
                    continue
                target = list(limit_words(target, cur[0]))
            elif kind == 'weights' and not cur:
                refused.append('the weight table reads empty: nothing written')
                continue
            if item is None:
                if kind == 'entry' and cur is not None and not same(key, cur, target):
                    refused.append('TCAM entry %s is in use by something else (page 0 keys %s %s): nothing written'
                                   % (arg, x4(cur['p0']['2']), x4(cur['p0']['3'])))
                    continue
                if kind == 'limit' and cur[1] & 0x3fff and not same(key, cur, target):
                    refused.append('%s has an egress limit already (0x09 %s, 0x0a %s), not this tool\'s: '
                                   'nothing written' % (port_name(key, ports), x4(cur[0]), x4(cur[1])))
                    continue
                item = {'found': None if kind == 'entry' else cur, 'set': None if kind == 'entry' else cur}
            elif not known:
                if kind in ('entry', 'limit'):
                    refused.append('%s holds what this tool did not write (%s): nothing written' % (
                        key.replace(':', ' '), json.dumps(cur, sort_keys=True)))
                    continue
                notes.append('%s changed since by something else (%s): taken as what to put back' % (
                    key.replace(':', ' '), json.dumps(cur)))
                item = {'found': cur, 'set': cur}
            before = recognised(key, cur, item)
            after[key] = {'found': item['found'], 'set': target}
            if same(key, cur, target):
                continue
            during[key] = {'found': item['found'], 'set': before, 'pending': target}
            steps.append((key, before, target))
        else:
            # kept and no longer asked for: put back what was found
            found = item['found']
            if kind == 'mode':
                found = (cur & ~3 & 0xffff) | (found & 3)
            elif kind == 'limit':
                found = [(cur[0] & ~0x7f & 0xffff) | (found[0] & 0x7f), found[1]]
            if same(key, cur, found):
                continue
            if not known:
                notes.append('%s changed since by something else: left as it is' % key.replace(':', ' '))
                continue
            before = recognised(key, cur, item)
            during[key] = {'found': item['found'], 'set': before, 'pending': found}
            steps.append((key, before, found))
    if refused:
        raise Refused('\n'.join(refused))
    return order(steps), after, during, base, notes


def recognised(key, cur, item):
    """What the chip holds of an item, as this tool can write it again: an entry's class (or None when
    free), else the value read."""
    if key.startswith('entry:'):
        if cur is None:
            return None
        for v in (item.get('set'), item.get('pending')):
            if v is not None and same(key, cur, v):
                return v
        return None
    return cur


def order(steps):
    """Safe order: limits and scheduling first; then the entries taken away with port 0's TCAM mode
    put back before them, or loaded with the mode set after them; the weights last."""
    def rank(step):
        kind = step[0].partition(':')[0]
        if kind == 'limit':
            return 0
        if kind == 'sched':
            return 1
        if kind == 'mode':
            return 2 if (step[2] & 3) != 1 else 5
        if kind == 'entry':
            return 3 if step[2] is None else 4
        return 6
    return sorted(steps, key=rank)


# ---- writing ----

def entry_cmds(entry, meta):
    """An entry flushed, and loaded with a class's words when there is one: page 2, page 1, page 0
    last - then, in the same session, read back and flushed at once if it does not read as written.
    Not a word of it while the engine stays busy - no exit either, which on the console would end its
    shell."""
    cmd = ['w 0x1f 0x01 0x0000', 'o %s' % x4(tcam_op(OP_FLUSH, entry))]
    if meta is not None:
        page0, page2 = tcam_words(meta['dscp'], meta['mask'], meta['queue'], COUNTER[entry])
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
        # the agent's operations share the engine: read the entry back now, in this session
        verify = ['_ok=1', 'o %s' % x4(tcam_op(OP_READ, entry, 0))]
        verify += ['[ "$(t r 0x1f %s)" = %s ] || _ok=0' % (hex(r), x4(page0[r])) for r in P0_CHECKED]
        verify.append('o %s' % x4(tcam_op(OP_READ, entry, 2)))
        verify += ['[ "$(t r 0x1f %s)" = %s ] || _ok=0' % (hex(r), x4(page2[r])) for r in P2_CHECKED]
        verify.append('if [ $_ok = 0 ]; then o %s; echo verify%d=flushed; fi' % (x4(tcam_op(OP_FLUSH, entry)), entry))
        cmd += verify
    return 'if b 0x1f 0x00; then %s; else echo tcam%d=busy; fi;' % ('; '.join(cmd), entry)


def step_cmds(step, value_):
    key = step[0]
    kind, _, arg = key.partition(':')
    if kind == 'entry':
        return entry_cmds(int(arg), value_)
    if kind == 'mode':
        return 'w 0 0xd %s;' % x4(value_)
    if kind == 'sched':
        return 'b %s 0x1c && w %s 0x1c %s;' % (arg, arg, x4(sched_word(value_)))
    if kind == 'weights':
        return ' '.join('b 0x1c 0x1c && w 0x1c 0x1c %s;' % x4(v) for v in weight_words(value_))
    if kind == 'limit':
        r09, r0a = value_
        # a limit is set with eDec first, then N; taken away with N first, then eDec
        if r0a & 0x3fff:
            return 'w %s 0x09 %s; w %s 0x0a %s;' % (arg, x4(r09), arg, x4(r0a))
        return 'w %s 0x0a %s; w %s 0x09 %s;' % (arg, x4(r0a), arg, x4(r09))
    raise ValueError(kind)


def change(step):
    """A step's register words, before and after, for the log."""
    key, before, after = step
    kind, _, arg = key.partition(':')

    def entry(meta):
        if meta is None:
            return 'free'
        page0, page2 = tcam_words(meta['dscp'], meta['mask'], meta['queue'], COUNTER[int(arg)])
        return 'page 0 %s, page 2 %s' % (' '.join('%s %s' % (hex(r), x4(v)) for r, v in sorted(page0.items()) if v),
                                         ' '.join('%s %s' % (hex(r), x4(v)) for r, v in sorted(page2.items()) if v))
    if kind == 'entry':
        return 'TCAM entry %s: %s -> %s' % (arg, entry(before), entry(after))
    if kind == 'mode':
        return 'port 0 register 0xd %s -> %s' % (x4(before), x4(after))
    if kind == 'sched':
        return 'Queue Control pointer 0 %d -> %d (%s)' % (before, after, x4(sched_word(after)))
    if kind == 'weights':
        return 'Global2 0x1c, %d turns -> %d (%s)' % (len(before), len(after),
                                                    ' '.join(x4(w) for w in weight_words(after)))
    if kind == 'limit':
        return '0x09 %s 0x0a %s -> 0x09 %s 0x0a %s' % (x4(before[0]), x4(before[1]), x4(after[0]), x4(after[1]))
    return ''


def write(steps, which):
    """Write each step's value (which: 2 the new one, 1 the one before), one session each, and read
    them all back with the census after them: (the steps that did not read back as written, the census
    as read after, whether entry 3 is in use after; or None for those two when it could not be read)."""
    for s in steps:
        try:
            ask(120, step_cmds(s, s[which]) + ' echo done=1')
        except Failed as exc:
            print(str(exc), file=sys.stderr)
    try:
        res = ask(240, read_cmds({s[0] for s in steps}) + census_cmds('a') + ' echo end=1')
    except Failed as exc:
        print(str(exc), file=sys.stderr)
        return list(steps), None, None
    bad = []
    for s in steps:
        try:
            got = current(s[0], res)
        except Refused:
            got = 'unreadable'
        if got == 'unreadable' or not same(s[0], got, s[which]):
            bad.append(s)
    try:
        valid3 = entry_from(res, 3) is not None
    except Refused:
        valid3 = None
    return bad, census_from(res, 'a'), valid3


# ---- the commands ----

def converge(want, why, sets):
    """Bring the chip to what is wanted. sets: a run that sets things - its failure puts back what it
    changed; else a run that only puts things back (clear, a reapply with no settings file), which never
    sets again what it has put back. Exit status."""
    assembly, family, ports = model()
    why_not = writable(assembly, family, ports)
    if why_not == 'read-only':
        print('the XGS 136 family: this tool only reads the switch here (its bus is shared with the network '
              'agent, #639, #706); nothing written', file=sys.stderr)
        return 2
    if why_not == 'untested-model':
        print('%s: nothing of this was run on this model - only the XGS 3300 (%s) takes writes; nothing '
              'written' % (assembly, WRITES), file=sys.stderr)
        return 2
    if why_not:
        print('not an appliance whose switch this tool knows: nothing written', file=sys.stderr)
        return 2
    state = load_state()
    owner = OWNER['value'] or state.get('owner', '')
    try:
        want = parse_conf(want, ports)
        # a run that asks for nothing only puts back, whoever calls it
        sets = sets and bool(wanted(want))
        keys = set(wanted(want)) | set(state.get('items', {}))
        res = ask(240, read_cmds(keys) + census_cmds('b') + ' echo end=1')
        if res.get('end') != '1':
            raise Failed('the reads did not run to their end on the coprocessor')
        steps, after, during, base, notes = plan(want, state, res, ports)
    except (Refused, Failed) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    census_before = census_from(res, 'b')
    for note in notes:
        log('switch-qos: ' + note)
    if not steps:
        save_state(after, owner)
        print('the switch holds this already (%s)' % why)
        return 0
    # what was found, and what this run writes, kept BEFORE it writes: a run cut short leaves both
    save_state(dict(after, **during), owner)
    bad, census_after, valid3 = write(steps, 2)
    moved = census_moved(census_before, census_after, valid3) if census_after is not None else []
    if not bad and not moved:
        for s in steps:
            log('switch-qos: %s - %s (%s)' % (describe(s[0], s[2], ports), change(s), why))
        save_state(after, owner)
        return 0
    if moved:
        print('the vendor\'s TCAM entries changed while this ran (%s): the coprocessor\'s network agent loads '
              'entries through the same engine, unlocked - this run is not kept' % '; '.join(moved), file=sys.stderr)
        log('switch-qos: the vendor\'s TCAM entries changed during a run (%s)' % '; '.join(moved))
    if bad:
        print('not read back as written: %s' % '; '.join(describe(s[0], s[2], ports) for s in bad), file=sys.stderr)
    if not sets:
        # putting back: what read back as found is done, the rest left for the next run - never set again
        kept = dict(after)
        bad_keys = {s[0] for s in bad}
        for key, item in during.items():
            if key in bad_keys:
                kept[key] = item
        for s in steps:
            if s[0] not in bad_keys:
                log('switch-qos: %s - %s (%s)' % (describe(s[0], s[2], ports), change(s), why))
        save_state(kept, owner)
        if bad:
            log('switch-qos: could not put back %s; kept to put back at the next run'
                % '; '.join(describe(s[0], s[2], ports) for s in bad))
        return 1
    undo = list(reversed(steps))
    left, _, _ = write(undo, 1)
    if left:
        print('PUT BACK DIFFERS: %s - the state keeps what was found and what this run wrote; switch_qos.py '
              'show, then clear' % '; '.join(describe(s[0], s[1], ports) for s in left), file=sys.stderr)
        log('switch-qos: a run that was not kept could not put back %s'
            % '; '.join(describe(s[0], s[1], ports) for s in left))
        return 1
    log('switch-qos: a run that was not kept was put back as found: %s'
        % '; '.join('%s - %s, then back' % (describe(s[0], s[2], ports), change(s)) for s in steps))
    save_state(base, owner)
    return 1


OWNER = {'value': ''}


def stamp():
    return time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime())


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
        if load_state().get('items'):
            return converge({}, why + ', no settings file: put back', False)
        print('no settings file (%s): nothing to set' % CONF)
        return 0
    return converge(conf, why, True)


def cmd_clear():
    """The settings file goes first, whatever follows: nothing sets the switch again from it - not the
    boot hook either. Then what the state holds is put back; what cannot be stays in the state."""
    try:
        os.remove(CONF)
    except FileNotFoundError:
        pass
    if not load_state().get('items'):
        print('nothing of this tool\'s is left in the switch')
        return 0
    rc = converge({}, 'clear', False)
    if rc == 0:
        print('nothing of this tool\'s is left in the switch')
    return rc


def cmd_reapply(boot):
    if not os.path.exists(CONF) and not load_state().get('items'):
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
    assembly, family, ports = model(devices)
    why = writable(assembly, family, ports)
    print(json.dumps({'assembly': assembly, 'family': family, 'writes': why == '', 'why': why,
                      'ports': sorted(ports, key=lambda k: ports[k]), 'devices': devices,
                      'entries': len(ENTRIES)}, sort_keys=True))
    return 0


def cmd_words():
    _, _, ports = model()
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


def cmd_check(as_json):
    """Whether entries 3 and 255 and port 0's TCAM mode still hold what this tool set: a few reads,
    for the caller's status. Nothing read when this tool keeps nothing, or on a model it does not write."""
    state = load_state()
    items = state.get('items', {})
    out = {'ok': True, 'changed': [], 'read': False}
    keys = [k for k in items if k.startswith('entry:') or k == 'mode']
    assembly, family, ports = model() if keys else ('', '', {})
    if keys and not writable(assembly, family, ports):
        try:
            res = ask(60, read_cmds(set()) + ' echo end=1')
            out['read'] = True
            for key in sorted(keys):
                item = items[key]
                cur = current(key, res)
                if not same(key, cur, item.get('set')) and not same(key, cur, item.get('pending', item.get('set'))):
                    out['ok'] = False
                    out['changed'].append(key)
        except (Refused, Failed) as exc:
            out['ok'] = False
            out['error'] = str(exc)
    if as_json:
        print(json.dumps(out, sort_keys=True))
    elif not out['read']:
        print('nothing of this tool\'s to check')
    else:
        print('the switch holds what this tool set' if out['ok'] else
              'changed since this tool set it: %s' % (', '.join(out['changed']) or out.get('error', '')))
    return 0 if out['ok'] else 1


def cmd_show(as_json):
    assembly, family, ports = model()
    if family not in ('armada', 'octeontx') or not ports:
        print('not an appliance whose switch this tool knows', file=sys.stderr)
        return 1
    if family == 'armada':
        # port registers alone: no TCAM operation at all - the engine is shared with the network agent
        cmd = 'echo p4=$(t r 0 0x04); echo p8=$(t r 0 0x08); echo m0=$(t r 0 0x0d); echo end=1'
    else:
        cmd = 'echo m0=$(t r 0 0x0d);'
        for e in ENTRIES:
            cmd += read_entry_cmds(e)
            cmd += ' echo h%d=$(c 0 %d 1);' % (e, 0x08 + COUNTER[e])
        cmd += census_cmds('c')
        for p in sorted(set(ports.values())):
            cmd += ' echo s%d=$(t q %d 0); echo d%d=$(t r %d 0x09); echo l%d=$(t r %d 0x0a);' % (p, p, p, p, p, p)
        cmd += read_weights_cmds() + ' echo end=1'
    try:
        res = ask(240, cmd)
    except Failed as exc:
        print(str(exc), file=sys.stderr)
        return 1
    out = shown(assembly, family, ports, res)
    if as_json:
        print(json.dumps(out, sort_keys=True))
    else:
        for line in out['lines']:
            print(line)
    return 0 if not out['unreadable'] else 1


def shown(assembly, family, ports, res):
    """What show read, as lines and as values."""
    lines, out, bad = [], {'assembly': assembly, 'family': family}, []

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
    items = state.get('items', {})
    out['entries'] = {}
    if family == 'armada':
        p4, p8 = get('p4'), get('p8')
        out['port0'] = {'0x04': p4, '0x08': p8, '0x0d': m0}
        lines.append('port 0: 0x04 %s, 0x08 %s, 0x0d %s' % tuple('unreadable' if v is None else x4(v)
                                                                for v in (p4, p8, m0)))
        lines.append('the XGS 136 family: port registers alone - the TCAM is not read here, its engine being '
                     'shared with the network agent - and nothing is written')
    else:
        for e in ENTRIES:
            try:
                cur = entry_from(res, e)
            except Refused:
                bad.append('entry %d' % e)
                lines.append('TCAM entry %d: unreadable' % e)
                continue
            item = items.get('entry:%d' % e)
            if cur is None:
                what = 'free'
            elif item and same('entry:%d' % e, cur, item.get('set')):
                what = 'this tool\'s: DSCP %d mask 0x%02x -> queue %d, %s frames' % (
                    item['set']['dscp'], item['set']['mask'], item['set']['queue'], res.get('h%d' % e, '?'))
            else:
                what = 'in use by something else (keys %s %s)' % (x4(cur['p0']['2']), x4(cur['p0']['3']))
            out['entries'][str(e)] = {'what': what, 'hits': res.get('h%d' % e)}
            lines.append('TCAM entry %d: %s' % (e, what))
        census = census_from(res, 'c')
        out['census'] = census
        lines.append('the vendor\'s entries around them: %s; get-next after 2: %s, after 3: %s' % (
            ', '.join('%d keys %s %s' % (e, census['%dk1' % e], census['%dk2' % e]) for e in CENSUS),
            census['next2'], census['next3']))
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
    lines.append('kept by this tool: %s' % (json.dumps(items, sort_keys=True) if items else 'nothing'))
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
    if cmd == 'check' and rest in ([], ['--json']):
        return cmd_check(rest == ['--json'])
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
