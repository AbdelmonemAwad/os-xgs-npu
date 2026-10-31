#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/transceivers.py's collect-due against a made-up cage: a read keeps each cage's link
as it found it, and the next one is due when five minutes have passed or a link is not what it was -
a far end that restarted no longer leaves "no light" on the page for five minutes.
    python3 tools/test-transceivers.py"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'opnsense',
                                'scripts', 'xgs'))
import transceivers  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s' % ('ok  ' if ok else 'FAIL', name) + ('' if ok else ': got %r, want %r' % (got, want)))


transceivers.STATE = os.path.join(tempfile.mkdtemp(prefix='transceivers-test-'), 'transceivers.json')
transceivers.tell_driver = lambda answer, devices=None: answer
transceivers.cage_ports = lambda: {'F1': {'label': 'F1', 'device': 'oxp0', 'status': 'no carrier'}}
transceivers.coprocessor = lambda seconds, command: ('@@F1\nF1 is not present\n', '')

answer = transceivers.collect()
check('a read keeps the cages\' interfaces', answer.get('devices'), {'F1': 'oxp0'})
check('a read keeps each cage\'s link as it found it', answer.get('links'), {'F1': 'no carrier'})

link = {'F1': 'no carrier'}
transceivers.links_now = lambda devices: dict(link)
check('nothing changed, read a moment ago: not due', transceivers.due(), False)
link['F1'] = 'active'
check('the far end came back: due', transceivers.due(), True)
link['F1'] = 'no carrier'
state = transceivers.show()
state['at'] = int(time.time()) - transceivers.READ_EVERY
transceivers.keep(state)
check('five minutes since the last read: due', transceivers.due(), True)
state = transceivers.show()
state['at'] = int(time.time())
del state['devices']
transceivers.keep(state)
check('a read from before the links were kept: due', transceivers.due(), True)

print('all passed' if failures == 0 else '%d failed' % failures)
sys.exit(1 if failures else 0)
