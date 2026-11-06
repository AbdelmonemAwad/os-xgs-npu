#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/plugins.py's check against a made-up plugin in a temporary directory: a check that
fails is the check's answer and leaves the last update's record as it was; an update that finished
during the check keeps its own; and a failure an older check wrote there is not shown once a later
check has found the release. And since 2026-11-06 (the review's G10): one check at a time - one
asked while another runs waits for it and takes its answer - and the page's Check answered at once,
the check running on in the background.
    python3 tools/test-plugins.py"""
import json
import os
import subprocess
import sys
import tempfile
import threading
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
plugins.CHECK_LOCK = os.path.join(root, 'check.lock')
plugins.CHECKING = os.path.join(root, 'checking.json')
plugins.PLUGINS = [('os-demo', script, 'a demo')]
plugins.MIRROR = 'https://mirror.invalid'
plugins.remember_installed = lambda: None
plugins.running = lambda path: False
plugins.runner_alive = lambda prog: False
plugins.suits = lambda name: True
plugins.source_path = lambda path: None
plugins.RETRY_PAUSE = 0
LAST = os.path.join(share, 'os-demo', 'update.json')
UPDATED = {'status': 'updated', 'message': 'updated to v1.0.1 (abc)', 'tag': 'v1.0.1', 'sha': 'abc', 'at': 1000}
FOUND = {'tag': 'v1.0.1', 'sha': 'abc', 'take': False, 'compare': 'identical'}
with open(os.path.join(share, 'os-demo', 'installed.json'), 'w') as handle:
    json.dump({'commit': 'abc', 'date': '2026-10-30T09:00:00Z'}, handle)


def write_last(record):
    with open(LAST, 'w') as handle:
        json.dump(record, handle)


asked = []


def answers(*replies):
    """ask() as self-update.py check answers it: a failure written where the plugin keeps its last
    update's record, as its say() does; a release found, printed and not written. Each ask's API is
    kept in asked."""
    queue = list(replies)
    del asked[:]

    def ask(path, api):
        asked.append(api)
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

# Both the copy and GitHub fail, each asked twice: the check's answer says so, the last update stays.
write_last(UPDATED)
plugins.ask = answers(RESET, RESET, RESET, RESET)
r = [p for p in plugins.check()['plugins'] if p['name'] == 'os-demo'][0]
check('a fast reset is asked again, of the copy and of GitHub', asked, [plugins.MIRROR, plugins.MIRROR, '', ''])
check('a failed check is the check\'s answer', (r['check_status'], r['check_message'][:22]), ('failed', 'could not reach GitHub'))
check('a failed check leaves the last update as it was', json.load(open(LAST)), UPDATED)
check('the page shows the last update, not the failure', (r['last_status'], r['last_message']), ('updated', UPDATED['message']))

# The copy fails twice, GitHub answers: no trace of the copy's failure.
write_last(UPDATED)
plugins.ask = answers(RESET, RESET, FOUND)
plugins.check()
r = row()
check('the copy failed and GitHub answered: the release found', r['latest'], 'v1.0.1')
check('the copy failed and GitHub answered: the last update as it was', json.load(open(LAST)), UPDATED)

# A reset, then the copy answers on the second ask: GitHub is not asked at all.
plugins.ask = answers(RESET, FOUND)
plugins.check()
check('a reset, then the copy answers: asked twice, GitHub not at all', asked, [plugins.MIRROR, plugins.MIRROR])
check('a reset, then the copy answers: the release found', row()['latest'], 'v1.0.1')

# A refusal is no connection failure: not asked again.
REFUSED = {'status': 'failed', 'message': 'GitHub refused the request (403)'}
plugins.ask = answers(REFUSED, REFUSED)
plugins.check()
check('a refusal is not asked again', asked, [plugins.MIRROR, ''])

# A failure that took long - a timeout - is not asked again.
plugins.RETRY_FAST = -1
plugins.ask = answers(RESET, RESET)
plugins.check()
check('a slow failure is not asked again', asked, [plugins.MIRROR, ''])
plugins.RETRY_FAST = 15

# No update ever: a failed check leaves no record behind.
os.unlink(LAST)
plugins.ask = answers(RESET, RESET, RESET, RESET)
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

# One check at a time: a check asked while another runs waits for it, and takes its answer rather than
# asking again. Until 2026-11-06 a second Check ran beside the first, each putting back the other's
# failure where the plugin keeps its last update's record.
gate, began, slow_asks = threading.Event(), threading.Event(), []


def slow(path, api):
    """A self-update's check that answers once the gate opens: GitHub slow to answer."""
    slow_asks.append(api)
    began.set()
    gate.wait(20)
    return FOUND


plugins.ask = slow
check('nothing under way, no check said to be', plugins.listing()['checking'], None)
first = threading.Thread(target=plugins.check)
first.start()
began.wait(20)
check('while a check runs, the list says so', plugins.listing()['checking'] is not None, True)
second_answer = {}
second = threading.Thread(target=lambda: second_answer.update(plugins.check()))
second.start()
time.sleep(0.5)
check('a second check waits for the first rather than ask beside it', (second.is_alive(), len(slow_asks)), (True, 1))
gate.set()
first.join(20)
second.join(20)
check('and takes its answer, asking no one again', (len(slow_asks), second_answer['plugins'][0]['latest']),
      (1, 'v1.0.1'))
check('both done: no check under way', plugins.listing()['checking'], None)

# The page's Check: started in the background, detached, and the list answered at once with the check
# under way; a second press while it runs starts nothing. daemon(8) is a thread here.
gate.clear()
began.clear()
del slow_asks[:]
started, threads = [], []


class Detached:
    """subprocess for plugins.py, its Popen a thread that runs the check daemon(8) would have started."""

    def __getattr__(self, name):
        return getattr(subprocess, name)

    @staticmethod
    def Popen(argv, **kwargs):
        started.append(argv)
        thread = threading.Thread(target=plugins.check)
        thread.start()
        threads.append(thread)


plugins.subprocess = Detached()
try:
    at = time.monotonic()
    answer = plugins.check_start()
    took = time.monotonic() - at
    check('the page\'s Check answers at once (%.2f s) with the check under way' % took,
          (took < 2, answer['checking'] is not None, answer['plugins'][0]['name']), (True, True, 'os-demo'))
    began.wait(20)
    again = plugins.check_start()
    check('a second press while it runs starts nothing, and says a check is under way',
          (len(started), again['checking'] is not None), (1, True))
    check('what is started is the check itself, detached', started[0][:2] + started[0][-1:],
          ['/usr/sbin/daemon', '-f', 'check'])
    gate.set()
    for thread in threads:
        thread.join(20)
    check('the check done: asked once, its answer kept, and no check under way',
          (len(slow_asks), row()['latest'], plugins.listing()['checking']), (1, 'v1.0.1', None))
    check('the mark of the press gone with it', os.path.exists(plugins.CHECKING), False)
finally:
    plugins.subprocess = subprocess

print('all passed' if failures == 0 else '%d failed' % failures)
sys.exit(1 if failures else 0)
