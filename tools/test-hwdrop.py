#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/hwdrop.py - which connections in hardware a cut drops on the coprocessor, read from
each driver's own table as flows.py reads it, and that "Cut in hardware" off drops nothing. Each
verdict carries the revision the table printed beside the index (since 2026-11-06), so an index that
has gone to another connection between the reading and the write is refused by the driver and not
counted. The tables are made up; nothing here comes from an appliance.

    python3 tools/test-hwdrop.py
"""
import importlib.util
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs'))
spec = importlib.util.spec_from_file_location(
    'hwdrop', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'hwdrop.py'))
hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hd)

FAILED = []


def check(what, got, want):
    if got != want:
        FAILED.append(what)
        print('FAIL %s: got %r, want %r' % (what, got, want))
    else:
        print('ok   %s' % what)


def word(ip):
    """The XGS 3300's table writes an IPv4 address as its 32-bit word, low byte first."""
    return '0x' + bytes(reversed([int(x) for x in ip.split('.')])).hex()


# Each connection with a revision of its own, so a write is seen to carry its connection's.
OCTEP = ''.join(
    '%4d rev %d  proto 6  sweep asks pf with the orig tuple, list 1\n'
    '      orig  %s:%d -> %s:%d  slot 7 rev 1  active  in oxp2\n'
    '      reply %s:%d -> %s:%d  slot 8 rev 1  active  in oxp3\n'
    % (i, r, word(s), sp, word(d), dp, word(d), dp, word(s), sp)
    for i, r, s, sp, d, dp in ((2, 7, '10.0.0.2', 50001, '198.51.100.7', 443),
                               (3, 1, '10.0.0.2', 50002, '203.0.113.9', 443),
                               (4, 5, '10.0.0.3', 50003, '198.51.100.7', 80),
                               # the port of 2 used again, to the server of 3
                               (5, 3, '10.0.0.2', 50001, '203.0.113.9', 443)))
NPUEP = ''.join(
    '  %d rev %d plain 12s | orig mflow 5 nhop 2 %s:%d -> %s:%d | reply mflow 6 nhop 1 %s:%d -> %s:%d  active\n'
    % (i, r, s, sp, d, dp, d, dp, s, sp)
    for i, r, s, sp, d, dp in ((5, 2, '192.168.1.119', 51000, '198.51.100.7', 443),
                               (6, 9, '192.168.1.120', 51001, '203.0.113.9', 22)))


class Driver:
    """The two sysctls hwdrop reads and writes, answered as each driver answers them. now is the
    revision a slot holds when the write comes - another than the table's is an index given to
    another connection in between - and a driver of 2026-11-06 refuses a verdict whose revision is
    not the slot's; an older one (older=True) reads the index and the verdict and stops there."""

    def __init__(self, family, now=None, older=False):
        self.family, self.writes, self.last = family, [], ''
        self.now, self.older = dict(now or {}), older

    def sysctl(self, name):
        if self.family == 'octeontx' and name == 'dev.octep.0.dp.flow_table':
            return OCTEP
        if self.family == 'armada' and name == 'dev.npuep.0.flow.flows':
            return 'made 2 (auto 2) taken 0 gone 0\n' + NPUEP
        return None

    def run(self, cmd, **kw):
        class Done:
            pass
        done = Done()
        done.returncode, done.stdout = 0, ''
        if '-n' in cmd:
            done.stdout = self.last
        else:
            oid, value = cmd[1].split('=', 1)
            self.writes.append((oid, value))
            words = value.split()
            idx = words[0]
            if not self.older and len(words) > 2 and idx in self.now and words[2] != self.now[idx]:
                self.last = ('connection %s is rev %s now, not %s: the index has gone to another '
                             'connection; nothing changed\n' % (idx, self.now[idx], words[2]))
            else:
                self.last = 'connection %s: verdict 1 (drop) on the coprocessor\n' % idx
        return done


def use(driver):
    hd.flows.sysctl = driver.sysctl
    hd.subprocess.run = driver.run


cfg = tempfile.NamedTemporaryFile('w', suffix='.xml', delete=False)
cfg.write('<?xml version="1.0"?>\n<opnsense><OPNsense><XGS><general></general></XGS></OPNsense></opnsense>\n')
cfg.close()
off = tempfile.NamedTemporaryFile('w', suffix='.xml', delete=False)
off.write('<?xml version="1.0"?>\n<opnsense><OPNsense><XGS><general><cut_in_hardware>0</cut_in_hardware>'
          '</general></XGS></OPNsense></opnsense>\n')
off.close()

d = Driver('octeontx')
use(d)
check('XGS 3300: both connections to a listed address dropped',
      hd.drop_addresses({'198.51.100.7'}, cfg.name), 2)
check('XGS 3300: through dp.flow_verdict, by slot and its revision',
      d.writes, [('dev.octep.0.dp.flow_verdict', '2 1 7'), ('dev.octep.0.dp.flow_verdict', '4 1 5')])

d = Driver('octeontx')
use(d)
check('XGS 3300: a JA4 cut drops only the connection with both its ends',
      hd.drop_ends([{('10.0.0.2', 50002), ('203.0.113.9', 443)}], cfg.name), 1)
check('XGS 3300: that one', d.writes, [('dev.octep.0.dp.flow_verdict', '3 1 1')])

# Two states cut at once: a connection whose source is one's and destination the other's is neither
# (the review of 2026-11-06, D12) - until then the ends of both were one set, and 5 was dropped too.
d = Driver('octeontx')
use(d)
check('XGS 3300: two cut at once drop those two only',
      hd.drop_ends([{('10.0.0.2', 50001), ('198.51.100.7', 443)}, {('10.0.0.2', 50002), ('203.0.113.9', 443)}],
                   cfg.name), 2)
check('XGS 3300: not the one that takes an end from each', d.writes,
      [('dev.octep.0.dp.flow_verdict', '2 1 7'), ('dev.octep.0.dp.flow_verdict', '3 1 1')])

d = Driver('armada')
use(d)
check('XGS 136: a listed address dropped through flow.verdict', hd.drop_addresses({'203.0.113.9'}, cfg.name), 1)
check('XGS 136: by its index and its revision', d.writes, [('dev.npuep.0.flow.verdict', '6 1 9')])

# Slot 4 given to another connection (rev 6) after the table showed rev 5: the write names rev 5,
# the driver refuses it, and only slot 2 is counted.
d = Driver('octeontx', now={'4': '6'})
use(d)
check('XGS 3300: an index reused since the table was read is refused, the other dropped',
      hd.drop_addresses({'198.51.100.7'}, cfg.name), 1)
check('XGS 3300: both written with the revisions the table showed',
      d.writes, [('dev.octep.0.dp.flow_verdict', '2 1 7'), ('dev.octep.0.dp.flow_verdict', '4 1 5')])

d = Driver('armada', now={'6': '10'})
use(d)
check('XGS 136: an index reused since the table was read is refused', hd.drop_addresses({'203.0.113.9'}, cfg.name), 0)
check('XGS 136: written with the revision the table showed', d.writes, [('dev.npuep.0.flow.verdict', '6 1 9')])

# A driver older than 2026-11-06 reads the index and the verdict of the same write and stops there:
# both dropped, as before - the reused index with them, which only a newer driver can refuse.
d = Driver('octeontx', now={'4': '6'}, older=True)
use(d)
check('older driver: the same writes drop by index, as before', hd.drop_addresses({'198.51.100.7'}, cfg.name), 2)

d = Driver('octeontx')
use(d)
check('"Cut in hardware" off: nothing dropped', hd.drop_addresses({'198.51.100.7'}, off.name), 0)
check('"Cut in hardware" off: nothing written', d.writes, [])
check('a missing config.xml is the default, on', hd.enabled('/nonexistent/config.xml'), True)

# The table past its first page (the review of 2026-11-06, D13). Each driver prints the first 4,096
# connections and counts the rest; until then that page was all hwdrop.py read, and a connection past
# it was never dropped on the coprocessor. Now, when the first page says there are more, the whole
# table is read again a page at a time: the place a page starts from written to one sysctl, the page
# read from another, walked from that place down the live list. The stand-in prints three a page
# where a driver prints 4,096, so a table of ten is read in four pages after the first; only the
# index of each verdict written is compared, so these hold whatever else a write carries beside it.
import fcntl  # noqa: E402

ROOT = os.path.join(HERE, '..')
WORDS = (r'"from %u, %d in the table\n"', r'"next from %d\n"', r'"the last page\n"',
         r'"and %d more connections, not printed\n"')


class Done:
    def __init__(self, stdout='', returncode=0):
        self.stdout, self.returncode = stdout, returncode


class Paged:
    """A driver whose table holds more than its first page prints, as each family prints it: the
    first page, the place a page starts from, and the page - the connections below that place in
    the live list, from just below it down, `size` at most. older: a driver without the pages, whose
    sysctl(8) refuses the place. meddle: another reader's place written once between this one's
    write and its read. gone: a connection that goes after the reading's first page, the last of
    the live list moved into its place, as the drivers move it."""

    def __init__(self, family, live, size=3, older=False, meddle=False, gone=None):
        self.family, self.live, self.size = family, list(live), size
        self.older, self.meddle, self.gone = older, meddle, gone
        self.place, self.pages, self.writes, self.places, self.last = 0, 0, [], [], ''
        if family == 'octeontx':
            self.oids = ('dev.octep.0.dp.flow_table', 'dev.octep.0.dp.flow_table_from',
                         'dev.octep.0.dp.flow_table_page')
        else:
            self.oids = ('dev.npuep.0.flow.flows', 'dev.npuep.0.flow.flows_from', 'dev.npuep.0.flow.flows_page')

    def entry(self, c):
        i, s, sp, d, dp = c
        if self.family == 'octeontx':
            return ('%4d rev 1  proto 6  sweep asks pf with the orig tuple, list 1\n'
                    '      counted  nothing yet\n'
                    '      orig  %s:%d -> %s:%d  slot 7 rev 1  active  in oxp2\n'
                    '      reply %s:%d -> %s:%d  slot 8 rev 1  active  in oxp3\n'
                    % (i, word(s), sp, word(d), dp, word(d), dp, word(s), sp))
        return ('%3d rev 1 plain 12s | orig mflow 5 nhop 2 %s:%d -> %s:%d | reply mflow 6 nhop 1 %s:%d -> %s:%d\n'
                '      counted  nothing yet\n' % (i, s, sp, d, dp, d, dp, s, sp))

    def first(self):
        text = '' if self.family == 'octeontx' else 'auto on | connections %d of 1024\n' % len(self.live)
        text += ''.join(self.entry(c) for c in self.live[:self.size])
        if len(self.live) > self.size:
            text += 'and %d more connections, not printed\n' % (len(self.live) - self.size)
        return text

    def page(self):
        if self.gone is not None and self.pages == 1:
            at = [c[0] for c in self.live].index(self.gone)
            last = self.live.pop()
            if at < len(self.live):
                self.live[at] = last
        self.pages += 1
        k = min(self.place, len(self.live))
        text, n = 'from %d, %d in the table\n' % (self.place, len(self.live)), 0
        while k > 0 and n < self.size:
            k -= 1
            text += self.entry(self.live[k])
            n += 1
        return text + ('next from %d\n' % k if k > 0 else 'the last page\n')

    def sysctl(self, name):
        if name == self.oids[0]:
            return self.first()
        if name == self.oids[2] and not self.older:
            return self.page()
        return None

    def run(self, cmd, **kw):
        if '-n' in cmd:
            return Done(self.last)
        oid, value = cmd[1].split('=', 1)
        if oid == self.oids[1]:
            if self.older:
                return Done('', 1)          # sysctl: unknown oid
            self.places.append(int(value))
            self.place = 7 if self.meddle else int(value)
            self.meddle = False
            return Done()
        self.writes.append((oid, value))
        self.last = 'connection %s: verdict 1 (drop) on the coprocessor\n' % value.split()[0]
        return Done()

    def dropped(self):
        return [int(v.split()[0]) for _, v in self.writes]


def ten(family):
    """Ten connections at places 0 to 9, indexes 2 to 11; the listed server's at places 0, 6 and 9."""
    server = '198.51.100.7'
    return [(2 + p, ('10.0.0.%d' if family == 'octeontx' else '192.168.1.%d') % (10 + p), 50000 + p,
             server if p in (0, 6, 9) else '203.0.113.9', 443) for p in range(10)]


lock = tempfile.NamedTemporaryFile(delete=False)
lock.close()
hd.LOCK = lock.name

for family, name in (('octeontx', 'XGS 3300'), ('armada', 'XGS 136')):
    d = Paged(family, ten(family))
    use(d)
    check(name + ': all three connections to a listed address dropped, two of them past the first page',
          hd.drop_addresses({'198.51.100.7'}, cfg.name), 3)
    check(name + ': each by its index, once - the first page\'s is on the last page too', sorted(d.dropped()),
          [2, 8, 11])
    check(name + ': read from the end of the live list down, a page at a time, and the place left at 0',
          d.places, [4294967295, 7, 4, 1, 0])

d = Paged('octeontx', ten('octeontx'), older=True)
use(d)
check('a driver without the pages: the first page alone, as before', hd.drop_addresses({'198.51.100.7'}, cfg.name), 1)
check('a driver without the pages: that one, and no page read', (d.dropped(), d.pages), ([2], 0))

d = Paged('armada', ten('armada'), meddle=True)
use(d)
check('another reader\'s place written in between: the page read again', hd.drop_addresses({'198.51.100.7'}, cfg.name), 3)
check('another reader\'s place written in between: the place written again', d.places[:2], [4294967295, 4294967295])

# Index 3, at place 1, goes after the reading's first page, and index 11 - read on that page - moves
# into its place, below where the next page starts: read twice, written once.
d = Paged('octeontx', ten('octeontx'), gone=3)
use(d)
check('a connection gone during the reading: every other one still found', hd.drop_addresses({'198.51.100.7'}, cfg.name), 3)
check('a connection gone during the reading: the one moved down written once', sorted(d.dropped()), [2, 8, 11])

# Another reader holds the lock, and the place is its own: the first page alone, as before.
held = open(hd.LOCK, 'w')
fcntl.flock(held, fcntl.LOCK_EX)
wait, hd.LOCK_WAIT = getattr(hd, 'LOCK_WAIT', 30), 0
d = Paged('octeontx', ten('octeontx'))
use(d)
check('the lock held by another reader: the first page alone', hd.drop_addresses({'198.51.100.7'}, cfg.name), 1)
check('the lock held by another reader: no place written', d.places, [])
hd.LOCK_WAIT = wait
held.close()

# The words read back are the drivers' own: each source prints them, and registers the sysctls read.
for name, path, oids in (('XGS 3300', ('contrib', 'octep', 'octep_dp.c'), Paged('octeontx', []).oids),
                         ('XGS 136', ('contrib', 'npuep', 'npuflow.c'), Paged('armada', []).oids)):
    with open(os.path.join(ROOT, *path), encoding='utf-8') as fh:
        src = fh.read()
    check(name + ': the first page, the place and the page registered',
          [o for o in oids if 'OID_AUTO, "%s"' % o.rsplit('.', 1)[1] not in src], [])
    check(name + ': the page\'s words printed', [w for w in WORDS if w not in src], [])
    check(name + ': read by hwdrop.py', [o for o in oids if not any(o in t for t in getattr(hd, 'TABLES', ()))], [])
check('the first page\'s last line read back', bool(getattr(hd, 'MORE', None) and
                                                   hd.MORE.search('and %d more connections, not printed\n' % 6)), True)
check('a page\'s first line read back', bool(getattr(hd, 'PAGE_FROM', None) and
                                             hd.PAGE_FROM.fullmatch('from %u, %d in the table' % (4294967295, 10))), True)
check('a page\'s last line read back', bool(getattr(hd, 'NEXT_FROM', None) and
                                            hd.NEXT_FROM.search('next from %d\n' % 7)), True)

os.unlink(lock.name)
os.unlink(cfg.name)
os.unlink(off.name)
print('%d failed' % len(FAILED) if FAILED else 'all passed')
sys.exit(1 if FAILED else 0)
