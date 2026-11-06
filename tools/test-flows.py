#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/flows.py release - Return to host on the Flows page - against each driver's
dp.flow_release / flow.release, the drivers answered as they answer. Since 2026-11-06 the page posts
the revision its row was read with, and flows.py writes "<index> <rev>": an index that has gone to
another connection since is refused, and nothing is handed back. Without a revision the index is
written alone, exactly as before. Nothing here comes from an appliance.

    python3 tools/test-flows.py
"""
import contextlib
import importlib.util
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'flows', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'flows.py'))
fl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fl)

FAILED = []
OCTEP = 'dev.octep.0.dp.flow_release'
NPUEP = 'dev.npuep.0.flow.release'


def check(what, got, want):
    if got != want:
        FAILED.append(what)
        print('FAIL %s: got %r, want %r' % (what, got, want))
    else:
        print('ok   %s' % what)


class Driver:
    """One driver's release sysctl. now is each slot's revision when the write comes; a driver of
    2026-11-06 refuses an index whose revision is not the one written beside it, and an older one
    (older=True) reads the index and stops there, whatever follows it."""

    def __init__(self, oid, now, older=False):
        self.oid, self.now, self.older, self.writes, self.last = oid, dict(now), older, [], ''

    def sysctl(self, name):
        return self.last if name == self.oid else None

    def run(self, cmd, **kw):
        class Done:
            pass
        done = Done()
        done.returncode, done.stdout, done.stderr = 0, '', ''
        oid, value = cmd[1].split('=', 1)
        self.writes.append((oid, value))
        words = value.split()
        idx = words[0]
        if idx not in self.now:
            # an older driver says back the whole write, a newer one the index alone
            self.last = 'no connection %s in hardware\n' % (value if self.older else idx)
        elif not self.older and len(words) > 1 and words[1] != self.now[idx]:
            self.last = ('connection %s is rev %s now, not %s: the index has gone to another connection; '
                         'nothing changed\n' % (idx, self.now[idx], words[1]))
        else:
            self.last = 'connection %s handed back to the host, and kept there until pf lets it go\n' % idx
        return done


def use(driver):
    fl.sysctl = driver.sysctl
    fl.subprocess.run = driver.run


def answer(call, *args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        call(*args)
    return json.loads(out.getvalue())


def by_argv(*argv):
    fl.sys.argv = ['flows.py'] + list(argv)
    return answer(fl.main)


# The XGS 3300: slot 5 holds rev 3 now.
d = Driver(OCTEP, {'5': '3'})
use(d)
a = answer(fl.release, '5')
check('XGS 3300: without a revision, the index alone is written, as before', d.writes, [(OCTEP, '5')])
check('XGS 3300: and handed back', (a['released'], a['message'][:26]), (True, 'connection 5 handed back t'))

d = Driver(OCTEP, {'5': '3'})
use(d)
a = answer(fl.release, '5', '3')
check('XGS 3300: with the revision the table showed, "<index> <rev>" is written', d.writes, [(OCTEP, '5 3')])
check('XGS 3300: the revision is the slot\'s: handed back', a['released'], True)

d = Driver(OCTEP, {'5': '3'})
use(d)
a = answer(fl.release, '5', '2')
check('XGS 3300: slot 5 given to another connection since (rev 3, not 2): not handed back', a['released'], False)
check('XGS 3300: and the driver\'s refusal is the answer', a['message'],
      'connection 5 is rev 3 now, not 2: the index has gone to another connection; nothing changed')
a = answer(fl.release, '7', '3')
check('XGS 3300: a slot with no connection at all: not handed back, said by its index',
      (a['released'], a['message']), (False, 'no connection 7 in hardware'))

# A driver older than 2026-11-06 reads "5 2" as 5 and hands it back: the same write still works there.
d = Driver(OCTEP, {'5': '3'}, older=True)
use(d)
a = answer(fl.release, '5', '2')
check('older driver: "<index> <rev>" read as the index, handed back as before', (d.writes, a['released']),
      ([(OCTEP, '5 2')], True))

d = Driver(OCTEP, {'5': '3'})
use(d)
check('a revision that is not digits: refused here, nothing written',
      (answer(fl.release, '5', 'x'), d.writes),
      ({'released': False, 'message': 'not a connection number: 5 x'}, []))
# A driver would stop reading at a digit that is not ASCII (here the superscript two, written as
# its number so that this file stays ASCII) and go by the index alone, unsaid.
check('a revision in digits that are not ASCII: refused here, nothing written',
      (answer(fl.release, '5', chr(0xb2))['released'], d.writes), (False, []))
check('an index that is not digits: refused, as before',
      (answer(fl.release, 'x5'), d.writes), ({'released': False, 'message': 'not a connection number: x5'}, []))

# How configd runs it: the action is "flows.py release %s %s", and given the index alone the second
# arrives empty, or not at all.
d = Driver(OCTEP, {'5': '3'})
use(d)
by_argv('release', '5')
by_argv('release', '5', '')
by_argv('release', ' 5 ', ' 3 ')
check('flows.py release 5, release 5 "", release 5 3: the index alone twice, then with its revision',
      d.writes, [(OCTEP, '5'), (OCTEP, '5'), (OCTEP, '5 3')])

# The XGS 136: slot 6 holds rev 9 now.
d = Driver(NPUEP, {'6': '9'})
use(d)
check('XGS 136: with the revision the table showed, handed back', answer(fl.release, '6', '9')['released'], True)
check('XGS 136: through flow.release, "<index> <rev>"', d.writes, [(NPUEP, '6 9')])
a = answer(fl.release, '6', '8')
check('XGS 136: slot 6 given to another connection since: refused, said',
      (a['released'], a['message'][:31]), (False, 'connection 6 is rev 9 now, not '))
check('XGS 136: without a revision, the index alone, as before', (answer(fl.release, '6')['released'], d.writes[-1]),
      (True, (NPUEP, '6')))

d = Driver('dev.none.0.release', {})
use(d)
check('no driver attached: said so, nothing written',
      (answer(fl.release, '5', '3')['message'], d.writes),
      ('no driver that can hand a connection back is attached', []))

# The row the page releases carries the revision: both tables give each connection its rev.
octep_rows = fl.octep('   5 rev 3  proto 6  sweep asks pf with the orig tuple, list 1\n'
                      '      orig  0x0a0200c0:50001 -> 0x076433c6:443  slot 7 rev 1  active  in oxp2\n')
npuep_rows = fl.npuep('  6 rev 9 plain 12s | orig mflow 5 nhop 2 192.0.2.20:51001 -> 203.0.113.9:22 | '
                      'reply mflow 6 nhop 1 203.0.113.9:22 -> 192.0.2.20:51001  active\n')
check('the tables\' rows carry the index and its revision',
      [(f['id'], f['rev']) for f in octep_rows + npuep_rows], [(5, 3), (6, 9)])

print('%d failed' % len(FAILED) if FAILED else 'all passed')
sys.exit(1 if FAILED else 0)
