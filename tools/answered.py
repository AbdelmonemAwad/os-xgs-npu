#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Whether the developer has answered an issue: the labels `unanswered` and `answered`, and one
automated note when thirty minutes pass with no answer.

    python3 tools/answered.py                          act on the event GitHub Actions hands over
    python3 tools/answered.py --sweep [--dry-run]      every open issue as it stands now (hourly)
    python3 tools/answered.py --number N [--dry-run]   issue N as it stands now; with --dry-run, say
                                                       what would be done and do nothing

A person - anybody but the developer or a bot - who opens an issue or comments on an open one waits
for an answer from that moment, and the issue carries `unanswered`; the developer's comment turns it
into `answered`. A report an appliance's owner wrote by hand in the problem reporter (label `manual`)
is that person's own message, waited on from its opening, though the issue is the reporter's and the
owner's GitHub name unknown; any other issue a bot opened counts from a person's first comment on it.
An issue labelled `no auto reply` is left alone.

When thirty minutes pass with no comment from the developer, one note says the developer is not
available at the moment and will reply as soon as possible: once for each wait, and never twice in a
day on one issue. The run of the event waits out the thirty minutes itself. Every later event on the
same issue cancels it, since answered.yml's concurrency group is the issue, and looks again: the
developer's comment ends the wait, anybody else's waits on from the same start. The hourly sweep
writes the note a wait missed, however long ago it fell due, but no sooner than fifteen minutes after,
so the two never write together. A note deleted by hand is not remembered and is written again while
the wait lasts; `no auto reply` is the way to silence an issue.
"""

import argparse
import datetime
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO = os.environ.get('GITHUB_REPOSITORY', 'AbdelmonemAwad/os-xgs-npu')
OWNER = 'AbdelmonemAwad'
WAITING = 'unanswered'
ANSWERED = 'answered'
LEFT_OUT = 'no auto reply'
MANUAL = 'manual'
MARK = '<!-- away -->'
AUTOMATED = '<sub>This is an automated message.</sub>'
WAIT = datetime.timedelta(minutes=30)
DAY = datetime.timedelta(days=1)
GRACE = datetime.timedelta(minutes=15)


def api(method, path, body=None):
    """GitHub's REST API with the workflow's token; the decoded answer, or None for an empty one."""
    req = urllib.request.Request(
        'https://api.github.com' + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'Accept': 'application/vnd.github+json',
                 'X-GitHub-Api-Version': '2022-11-28',
                 'User-Agent': 'os-xgs-npu answered'})
    with urllib.request.urlopen(req, timeout=30) as r:
        raw = r.read()
    return json.loads(raw) if raw else None


def now():
    return datetime.datetime.now(datetime.timezone.utc)


def bot(user):
    return (user or {}).get('type') == 'Bot' or (user or {}).get('login', '').endswith('[bot]')


def person(user):
    """A human other than the developer."""
    login = (user or {}).get('login', '')
    return bool(login) and login != OWNER and not bot(user)


def when(stamp):
    return datetime.datetime.strptime(stamp, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=datetime.timezone.utc)


def comments_of(number, call):
    found, page = [], 1
    while True:
        batch = call('GET', '/repos/%s/issues/%d/comments?per_page=100&page=%d' % (REPO, number, page)) or []
        found += batch
        if len(batch) < 100:
            return found
        page += 1


def stretch(issue, comments):
    """Where an issue stands, or None when no person has written on it: the label it should carry,
    and while a person waits, since when, for whom and after how many messages; with the times of the
    notes written so far."""
    words, notes = [], []
    if person(issue.get('user')):
        words.append((issue['user']['login'], when(issue['created_at'])))
    elif bot(issue.get('user')) and MANUAL in {x['name'] for x in issue.get('labels', [])}:
        words.append((None, when(issue['created_at'])))
    for c in comments:
        user = c.get('user') or {}
        if user.get('login') == OWNER or person(user):
            words.append((user['login'], when(c['created_at'])))
        elif bot(user) and MARK in (c.get('body') or ''):
            notes.append(when(c['created_at']))
    if all(login == OWNER for login, _ in words):
        return None
    if words[-1][0] == OWNER:
        return {'label': ANSWERED, 'notes': notes}
    i = len(words)
    while i and words[i - 1][0] != OWNER:
        i -= 1
    waiting = words[i:]
    names = []
    for login, _ in waiting:
        if login and login not in names:
            names.append(login)
    return {'label': WAITING, 'since': waiting[0][1], 'names': names, 'count': len(waiting),
            'notes': notes}


def due(state):
    """When this wait's note falls due - however long ago - or None when it is not to be written at
    all. A day since the last note is counted to the time the new one falls due."""
    if state is None or state['label'] != WAITING:
        return None
    since = state['since']
    at = since + WAIT
    if any(n >= since or at - n < DAY for n in state['notes']):
        return None
    return at


def note(names, count):
    """The note, greeting by name whoever is waiting - nobody by name for a report written by hand."""
    named = ['@' + n for n in names]
    if not named:
        hello = 'Hello'
    elif len(named) == 1:
        hello = 'Hello ' + named[0]
    else:
        hello = 'Hello ' + ', '.join(named[:-1]) + ' and ' + named[-1]
    return ('%s, thank you for your %s. The developer is not available at the moment and will reply '
            'as soon as possible.\n\n%s\n\n%s'
            % (hello, 'message' if count == 1 else 'messages', AUTOMATED, MARK))


def label(number, have, want, call, dry_run=False, say=print):
    """`want` on issue N and the other label off - both off when `want` is None, as on an issue no
    person has written on any more - writing nothing when that is so already."""
    changes = []
    if want and want not in have:
        changes.append(('POST', '/repos/%s/issues/%d/labels' % (REPO, number), {'labels': [want]}))
    for other in (WAITING, ANSWERED):
        if other != want and other in have:
            changes.append(('DELETE', '/repos/%s/issues/%d/labels/%s'
                            % (REPO, number, urllib.parse.quote(other)), None))
    if not changes:
        return
    if dry_run:
        say('#%d: would be labelled %s' % (number, want) if want else '#%d: labels would come off' % number)
        return
    for method, path, body in changes:
        try:
            call(method, path, body)
        except urllib.error.HTTPError as e:
            # The sweep and an event's run can take the same label off together; the second finds it gone.
            if not (method == 'DELETE' and e.code == 404):
                raise
    say('#%d: labelled %s' % (number, want) if want else '#%d: labels taken off' % number)


def look(number, call, clock=now, sleep=time.sleep, dry_run=False, say=print, wait=True, grace=None):
    """Issue N as it stands: its label put right, and its note written when due - waited for when
    `wait`, and when `grace` is given (the sweep's), only once that long past due."""
    issue = call('GET', '/repos/%s/issues/%d' % (REPO, number))
    if 'pull_request' in issue:
        say('#%d is a pull request' % number)
        return
    if issue.get('state') != 'open':
        say('#%d is closed' % number)
        return
    have = {l['name'] for l in issue.get('labels', [])}
    if LEFT_OUT in have:
        say('#%d is labelled %s: left alone' % (number, LEFT_OUT))
        return
    state = stretch(issue, comments_of(number, call))
    if state is None:
        label(number, have, None, call, dry_run, say)
        say('#%d: no person has written here' % number)
        return
    label(number, have, state['label'], call, dry_run, say)
    at = due(state)
    if at is None:
        return
    t = clock()
    if t < at + (grace or datetime.timedelta(0)):
        if not wait or dry_run:
            say('#%d: its note falls due at %s' % (number, at.strftime('%Y-%m-%d %H:%M UTC')))
            return
        # Then looks again, and waits again if the note fell due later meanwhile (the comment that
        # began the wait was deleted, say).
        sleep((at - t).total_seconds() + 5)
        look(number, call, clock, sleep, dry_run, say)
        return
    body = note(state['names'], state['count'])
    if dry_run:
        say('would write on #%d:\n%s' % (number, body))
        return
    call('POST', '/repos/%s/issues/%d/comments' % (REPO, number), {'body': body})
    say('#%d: note written' % number)


def sweep(call, clock=now, dry_run=False, say=print):
    """Every open issue: its label put right, and the note a wait missed. One issue that cannot be read
    does not stop the others; the numbers of those that could not be are returned."""
    failed, page = [], 1
    while True:
        batch = call('GET', '/repos/%s/issues?state=open&per_page=100&page=%d' % (REPO, page)) or []
        for issue in batch:
            if 'pull_request' in issue:
                continue
            try:
                look(issue['number'], call, clock, dry_run=dry_run, say=say, wait=False, grace=GRACE)
            except OSError as e:
                failed.append(issue['number'])
                say('#%d could not be looked at: %s' % (issue['number'], e))
        if len(batch) < 100:
            return failed
        page += 1


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--sweep', action='store_true')
    p.add_argument('--number', type=int)
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args()
    if a.number:
        look(a.number, api, dry_run=a.dry_run, wait=False)
    elif a.sweep:
        return 1 if sweep(api, dry_run=a.dry_run) else 0
    else:
        with open(os.environ['GITHUB_EVENT_PATH'], encoding='utf-8') as f:
            issue = json.load(f).get('issue') or {}
        if issue.get('number') and 'pull_request' not in issue:
            look(issue['number'], api)
        else:
            print('no issue in this event')
    return 0


if __name__ == '__main__':
    sys.exit(main())
