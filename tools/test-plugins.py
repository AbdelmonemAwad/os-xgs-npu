#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/plugins.py's check against a made-up plugin in a temporary directory: a check that
fails is the check's answer and leaves the last update's record as it was; an update that finished
during the check keeps its own; and a failure an older check wrote there is not shown once a later
check has found the release.
    python3 tools/test-plugins.py"""
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'opnsense',
                                'scripts', 'xgs'))
import plugins  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s' % ('ok  ' if ok else 'FAIL', name) + ('' if ok else ': got %r, want %r' % (got, want)))


root = tempfile.mkdtemp(prefix='plugins-test-')
share = os.path.join(root, 'share')
script = os.path.join(share, 'os-demo', 'self-update.py')
os.makedirs(os.path.dirname(script))
open(script, 'w').close()
plugins.SHARE = share
plugins.STATE = os.path.join(root, 'check.json')
plugins.PROGRESS = os.path.join(root, 'progress.json')
plugins.SEEN = os.path.join(root, 'seen.json')
plugins.PLUGINS = [('os-demo', script, 'a demo')]
plugins.MIRROR = 'https://mirror.invalid'
plugins.remember_installed = lambda: None
plugins.running = lambda path: False
plugins.runner_alive = lambda prog: False
plugins.suits = lambda name: True
plugins.source_path = lambda path: None
LAST = os.path.join(share, 'os-demo', 'update.json')
UPDATED = {'status': 'updated', 'message': 'updated to v1.0.1 (abc)', 'tag': 'v1.0.1', 'sha': 'abc', 'at': 1000}
FOUND = {'tag': 'v1.0.1', 'sha': 'abc', 'take': False, 'compare': 'identical'}
with open(os.path.join(share, 'os-demo', 'installed.json'), 'w') as handle:
    json.dump({'commit': 'abc', 'date': '2026-10-30T09:00:00Z'}, handle)


def write_last(record):
    with open(LAST, 'w') as handle:
        json.dump(record, handle)


def answers(*replies):
    """ask() as self-update.py check answers it: a failure written where the plugin keeps its last
    update's record, as its say() does; a release found, printed and not written."""
    queue = list(replies)

    def ask(path, api):
        reply = queue.pop(0)
        if reply.get('status') == 'failed':
            write_last(dict(reply, at=int(time.time())))
        elif reply.get('status') == 'updated':
            write_last(dict(reply, at=int(time.time())))
        return reply
    return ask


def row():
    return plugins.listing()['plugins'][0]


RESET = {'status': 'failed', 'message': 'could not reach GitHub: <urlopen error [Errno 54] Connection reset by peer>'}

# Both the copy and GitHub fail: the check's answer says so, the last update stays.
write_last(UPDATED)
plugins.ask = answers(RESET, RESET)
r = [p for p in plugins.check()['plugins'] if p['name'] == 'os-demo'][0]
check('a failed check is the check\'s answer', (r['check_status'], r['check_message'][:22]), ('failed', 'could not reach GitHub'))
check('a failed check leaves the last update as it was', json.load(open(LAST)), UPDATED)
check('the page shows the last update, not the failure', (r['last_status'], r['last_message']), ('updated', UPDATED['message']))

# The copy fails, GitHub answers: no trace of the copy's failure.
write_last(UPDATED)
plugins.ask = answers(RESET, FOUND)
plugins.check()
r = row()
check('the copy failed and GitHub answered: the release found', r['latest'], 'v1.0.1')
check('the copy failed and GitHub answered: the last update as it was', json.load(open(LAST)), UPDATED)

# No update ever: a failed check leaves no record behind.
os.unlink(LAST)
plugins.ask = answers(RESET, RESET)
plugins.check()
check('no update ever: a failed check leaves no record', os.path.exists(LAST), False)

# An update that finished during the check keeps its own record.
write_last(UPDATED)
done = {'status': 'updated', 'message': 'updated to v1.0.2 (def)', 'tag': 'v1.0.2', 'sha': 'def'}
plugins.ask = answers(done, FOUND)
plugins.check()
check('an update that finished meanwhile keeps its record', json.load(open(LAST)).get('message'), done['message'])

# What a check before the fix wrote there: not shown once a later check has found the release.
write_last(dict(RESET, at=int(time.time()) - 3600))
plugins.ask = answers(FOUND)
plugins.check()
r = row()
check('an older failure is not shown after a check found the release', (r['last_status'], r['last_message']), ('', ''))
plugins.write_json(plugins.STATE, {'at': int(time.time()) - 7200, 'plugins': {'os-demo': FOUND}})
r = row()
check('a failure newer than the last check is still shown', r['last_status'], 'failed')

print('all passed' if failures == 0 else '%d failed' % failures)
sys.exit(1 if failures else 0)
