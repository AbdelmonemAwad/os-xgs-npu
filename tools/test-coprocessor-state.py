#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/coprocessor.py's state when a look cannot read the coprocessor: the page's file says
so, and the reading before it is kept - for the next look's carried-over contents and for a problem
report's vendor build. Until 2026-11-06 the failed look replaced it.

And since 2026-11-06 (the review's D2 and G10): a read in sessions of 20 s at most, each within what
the watch over the coprocessor waits for (feeder.py's URGENT_WAIT); a session refused asked again, and
a piece not answered left out rather than guessed; and the page's read started in the background, its
answer at once - the last reading and the read under way - with one read at a time. The last part
runs the script in processes of its own, with stand-ins for npu-run.sh and daemon(8).

    python3 tools/test-coprocessor-state.py"""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'coprocessor.py')
FEEDER = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'feeder.py')

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load(tmp):
    module = load_module('coprocessor_under_test', TOOL)
    module.STATE = os.path.join(tmp, 'coprocessor.json')
    module.LAST_GOOD = os.path.join(tmp, 'coprocessor.last-good.json')
    module.LOCK = os.path.join(tmp, 'coprocessor.lock')
    module.READING = os.path.join(tmp, 'coprocessor.reading')
    module.CONSOLE = (os.path.join(tmp, 'console.log'),)
    module.run = lambda argv, timeout=60: ''       # no board, no sysctl
    module.npu_run = lambda seconds, command: ('', 1)      # the console answered nothing
    module.models = None
    module.PAUSE = 0
    return module


def read(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


GOOD = {'at': 100, 'software': {'sfversion': '21.0.0.169'}, 'emmc': []}

with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp)
    # a good reading written by a version before LAST_GOOD existed
    with open(m.STATE, 'w', encoding='utf-8') as f:
        json.dump(GOOD, f)
    m.collect()
    check('a look that could not read: the page is told', read(m.STATE).get('error'), 'unread')
    check('the reading before it is kept', read(m.LAST_GOOD).get('software'), GOOD['software'])
    check('and it is what last_good() gives', m.last_good().get('at'), 100)
    m.collect()
    check('a second look that could not read keeps it still', read(m.LAST_GOOD).get('at'), 100)


# A coprocessor as npu-run.sh would bring its answers back: who it is, its eMMC (the running copy p2,
# the other p3, the board maker's p1, /persistent on p4), each piece's lines and its last line.
IDENTITY = '\n'.join([
    'sfversion 23.0.0.329', 'kernel 5.4.0-test', 'os Test Linux', 'model Test board', 'cpus 4', 'memkb 4000000',
    'cmdline console=ttyS0 root=/dev/mmcblk0p2 ro',
    'part 179 0 7634944 mmcblk0', 'part 179 1 100000 mmcblk0p1', 'part 179 2 1500000 mmcblk0p2',
    'part 179 3 1500000 mmcblk0p3', 'part 179 4 50000 mmcblk0p4',
    'mount /dev/mmcblk0p4 /persistent ext4 rw,relatime 0 0', 'end identity'])
LINES = {
    'end identity': IDENTITY,
    'end flash': 'atf v2.4(release):test\nubootflash U-Boot 2019.10-10.22.03 (Nov 22 2024)\nend flash',
    'end health': 'wdt sbsa-gwdt sbsa-gwdt.0: Initialized with 10s timeout\n'
                  'wdtfeed watchdog -t 5 /dev/watchdog \nedac 19 0 0\nend health',
    'end fastpath': 'fastpath 3f430ea4+(PROD)\nend fastpath',
    'end mmcblk0p1': 'slot mmcblk0p1 os Board Linux\nslot mmcblk0p1 usedkb 50000\nend mmcblk0p1',
    'end mmcblk0p3': 'slot mmcblk0p3 sfversion 22.0.2.546\nslot mmcblk0p3 modules 5.4.0-10.22.03\n'
                     'end mmcblk0p3',
}


def coprocessor(refuse=(), refuse_times=99):
    """npu_run as a coprocessor answering LINES, its asks kept in a list; the pieces named in refuse
    refused the console (75) their first refuse_times asks."""
    asks, refused = [], {}

    def npu_run(seconds, command):
        end = command.rsplit('echo "', 1)[1].rstrip('"')
        asks.append((seconds, end))
        if end in refuse and refused.get(end, 0) < refuse_times:
            refused[end] = refused.get(end, 0) + 1
            return '', 75
        return LINES.get(end, '') + '\n', 0
    return npu_run, asks


feeder = load_module('feeder_under_test', FEEDER)

with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp)
    m.npu_run, asks = coprocessor()
    got = m.collect(slots=True)
    check('a read of the closed partitions: a session for each piece and each closed partition',
          [end for _, end in asks],
          ['end identity', 'end flash', 'end health', 'end fastpath', 'end mmcblk0p1', 'end mmcblk0p3'])
    longest = max(seconds for seconds, _ in asks)
    check('no session reads the console for more than 20 s (until 2026-11-06: 60 and 90)', longest <= 20, True)
    check('each session with its seven seconds fits in the watch\'s wait for a session under way',
          longest + 7 <= feeder.URGENT_WAIT, True)
    check('the reading is whole', (got['software']['sfversion'], got['software']['fastpath'],
                                   got['health']['fed_by'] is not None), ('23.0.0.329', {'build': '3f430ea4', 'kind': 'PROD'}, True))
    inside = {r['partition']: r.get('inside') for r in got['emmc'][0]['partitions']}
    check('what each closed partition holds', (inside['mmcblk0p1'], inside['mmcblk0p3']['sfversion']),
          ({'os': 'Board Linux', 'usedkb': '50000'}, '22.0.2.546'))
    slots_at = got.get('slots_at')
    check('and when they were read', isinstance(slots_at, int), True)

    # The health piece refused every time it is asked: asked TRIES times, and left unread - not a
    # watchdog nothing feeds - while the rest of the reading stands.
    m.npu_run, asks = coprocessor(refuse=('end health',))
    got = m.collect()
    check('a session refused the console is asked again, three times in all',
          [end for _, end in asks].count('end health'), 3)
    check('a piece never answered is not read, not guessed', got.get('health'), None)
    check('the pieces that answered stand', (got['software']['sfversion'], got['software']['firmware'].get('atf_flash')),
          ('23.0.0.329', 'v2.4(release):test'))
    check('a plain read keeps what the last look inside found',
          [r for r in got['emmc'][0]['partitions'] if r['partition'] == 'mmcblk0p3'][0]['inside']['sfversion'],
          inside['mmcblk0p3']['sfversion'])

    # refused once, then answered
    m.npu_run, asks = coprocessor(refuse=('end fastpath',), refuse_times=1)
    got = m.collect()
    check('refused once and asked again: answered', got['software']['fastpath'], {'build': '3f430ea4', 'kind': 'PROD'})

    # A look inside refused: the partition keeps what the last look found, and when that was.
    m.npu_run, asks = coprocessor(refuse=('end mmcblk0p1', 'end mmcblk0p3'))
    got = m.collect(slots=True)
    inside = {r['partition']: r.get('inside') for r in got['emmc'][0]['partitions']}
    check('looks inside refused: what was found before stays', inside['mmcblk0p3']['sfversion'], '22.0.2.546')
    check('and so does when it was found', got.get('slots_at'), slots_at)

    # Who the coprocessor is not answered: nothing more is asked, and the look is unread.
    m.npu_run, asks = coprocessor(refuse=('end identity',))
    got = m.collect()
    check('who it is not answered: unread, and nothing more asked',
          (got.get('error'), sorted(set(end for _, end in asks))), ('unread', ['end identity']))

    # A piece cut short - its last line missing - is left out.
    def cut(seconds, command):
        end = command.rsplit('echo "', 1)[1].rstrip('"')
        if end == 'end health':
            return 'wdt sbsa-gwdt sbsa-gwdt.0: Initialized\n', 1
        return LINES.get(end, '') + '\n', 0
    m.npu_run = cut
    check('a piece cut before its last line is left out', m.collect().get('health'), None)

    # The mark of a read started a moment ago counts before the read has the lock; an old one does not.
    m.write(m.READING, {'what': 'slots', 'since': int(time.time())})
    check('a read started a moment ago is under way', (m.under_way() or {}).get('what'), 'slots')
    m.write(m.READING, {'what': 'slots', 'since': int(time.time()) - 60})
    check('a mark a minute old, no lock held: no read under way', m.under_way(), None)
    os.unlink(m.READING)


# The page's read in processes of its own: coprocessor.py through a wrapper that points its files at
# a temporary directory, npu-run.sh a script that answers as the coprocessor above after DELAY seconds,
# and daemon(8) one that starts the wrapper in the background and returns at once, as daemon -f does.
DELAY = 1.5
WRAPPER = r'''
import importlib.util, os, sys
spec = importlib.util.spec_from_file_location('coprocessor', %(tool)r)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
here = %(here)r
m.STATE, m.LAST_GOOD = os.path.join(here, 'state.json'), os.path.join(here, 'last-good.json')
m.LOCK, m.READING = os.path.join(here, 'lock'), os.path.join(here, 'reading')
m.CONSOLE = (os.path.join(here, 'console.log'),)
m.NPU_RUN, m.BOARD, m.DAEMON = (os.path.join(here, n) for n in ('npu-run.sh', 'board.sh', 'daemon'))
m.models = None
m.run = lambda argv, timeout=60: (m.subprocess.run(argv, capture_output=True, text=True, errors='replace').stdout
                                  if argv[:2] == ['/bin/sh', m.BOARD] else '')
sys.exit(m.main(['coprocessor.py'] + sys.argv[1:]))
'''


def script(path, text):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    os.chmod(path, 0o755)


with tempfile.TemporaryDirectory() as here:
    script(os.path.join(here, 'wrapper.py'), WRAPPER % {'tool': TOOL, 'here': here})
    script(os.path.join(here, 'board.sh'), 'echo "XGS3300 octeontx"\n')
    answers = ''.join("*'echo \"%s\"') cat <<'EOF'\n%s\nEOF\n;;\n" % (end, text) for end, text in LINES.items())
    script(os.path.join(here, 'npu-run.sh'),
           '#!/bin/sh\necho "$1 ${2##*echo }" >> "%s"\nsleep %s\ncase "$2" in\n%sesac\n'
           % (os.path.join(here, 'asks'), DELAY, answers))
    script(os.path.join(here, 'daemon'),
           '#!/bin/sh\n[ "$1" = -f ] && shift\n"$1" "%s" "$3" < /dev/null > /dev/null 2>&1 &\nexit 0\n'
           % os.path.join(here, 'wrapper.py'))
    with open(os.path.join(here, 'state.json'), 'w', encoding='utf-8') as f:
        json.dump(GOOD, f)

    def page(*words):
        """The wrapper run as configd runs the script: (its answer, the seconds it took)."""
        began = time.monotonic()
        done = subprocess.run([sys.executable, os.path.join(here, 'wrapper.py')] + list(words),
                              capture_output=True, text=True, errors='replace', timeout=120)
        return json.loads(done.stdout or '{}'), time.monotonic() - began

    def identity_asks():
        try:
            with open(os.path.join(here, 'asks'), encoding='utf-8') as f:
                return sum(1 for line in f if line.rstrip().endswith('"end identity"'))
        except OSError:
            return 0

    first, took = page('start', 'collect')
    check('the page\'s read answers at once (%.1f s), while the read takes %d s and more' % (took, 4 * DELAY),
          took < 3, True)
    check('its answer is the last reading', first.get('at'), 100)
    check('and the read under way', (first.get('reading') or {}).get('what'), 'collect')
    again, took = page('start', 'slots')
    check('a second press while it runs starts nothing, and says what is under way',
          (took < 3, (again.get('reading') or {}).get('what')), (True, 'collect'))
    time.sleep(DELAY)
    shown, _ = page('show')
    check('show says the read is under way while it runs', (shown.get('at'), 'reading' in shown), (100, True))
    # a read asked by hand meanwhile - as the boot's hook asks - waits for the one running, and takes
    # its answer rather than asking the coprocessor again
    by_hand, took = page('collect')
    check('a read asked while another runs waits for it, and takes its answer',
          (by_hand.get('software', {}).get('kernel'), identity_asks()), ('5.4.0-test', 1))
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        shown, _ = page('show')
        if 'reading' not in shown:
            break
        time.sleep(0.5)
    check('once it is done, show gives the new reading and no read under way',
          (shown.get('software', {}).get('kernel'), 'reading' in shown), ('5.4.0-test', False))
    check('one read in all, for three asks', identity_asks(), 1)
    check('the mark gone with it', os.path.exists(os.path.join(here, 'reading')), False)
    # the new read of the closed partitions, now that nothing runs
    page('start', 'slots')
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        shown, _ = page('show')
        if 'reading' not in shown and shown.get('slots_at'):
            break
        time.sleep(0.5)
    inside = {r['partition']: (r.get('inside') or {}).get('sfversion') for r in shown['emmc'][0]['partitions']}
    check('pressed again once it is done: the closed partitions read', inside['mmcblk0p3'], '22.0.2.546')

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
