#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/reminder.py against made-up issues and a stand-in GitHub: one reminder, a week after the
owner asked, to the person asked; never twice, never early, never a closing; and the label off when
somebody answers.

    python3 tools/test-reminder.py"""
import datetime
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reminder  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


NOW = datetime.datetime(2026, 11, 20, 12, 0, tzinfo=datetime.timezone.utc)
OWNER = {'login': reminder.OWNER, 'type': 'User'}


def ago(days):
    return (NOW - datetime.timedelta(days=days)).strftime('%Y-%m-%dT%H:%M:%SZ')


def user(login, kind='User'):
    return {'login': login, 'type': kind}


class GitHub:
    def __init__(self, issues):
        self.issues = issues   # number -> (issue, comments)
        self.written = []

    def __call__(self, method, path, body=None):
        if method == 'GET' and '/comments?' in path:
            n = int(path.split('/issues/')[1].split('/')[0])
            return self.issues[n][1]
        if method == 'GET':
            return [i for i, c in self.issues.values()]
        self.written.append((method, path.split('/issues/')[1], body))
        return None


def issue(number, author, comments, pull=False):
    i = {'number': number, 'user': author, 'comments': len(comments), 'labels': [{'name': reminder.LABEL}]}
    if pull:
        i['pull_request'] = {}
    return i, comments


def sweep(gh, dry_run=False):
    said = []
    reminder.sweep(gh, NOW, dry_run=dry_run, say=said.append)
    return said


# The owner asked the issue's author eight days ago: one reminder to the author.
gh = GitHub({1: issue(1, user('asker'), [{'user': user('asker'), 'created_at': ago(10), 'body': 'It hangs.'},
                                         {'user': OWNER, 'created_at': ago(8), 'body': 'Can you send dmesg?'}])})
sweep(gh)
check('a week on, the author is reminded', [w[1] for w in gh.written], ['1/comments'])
body = gh.written[0][2]['body']
check('the reminder names the author', body.startswith('@asker - a gentle reminder'), True)
check('the reminder closes nothing', 'Nothing is closed' in body and all(w[0] == 'POST' for w in gh.written), True)

# The reminder is now the last word: the next sweep says nothing.
gh = GitHub({1: issue(1, user('asker'), [{'user': OWNER, 'created_at': ago(9), 'body': 'Can you send dmesg?'},
                                         {'user': user('github-actions[bot]', 'Bot'), 'created_at': ago(1),
                                          'body': 'reminder ' + reminder.MARK}])})
sweep(gh)
check('never twice', gh.written, [])

# Asked six days ago: too early.
gh = GitHub({2: issue(2, user('asker'), [{'user': OWNER, 'created_at': ago(6), 'body': 'Which port?'}])})
sweep(gh)
check('not before a week', gh.written, [])

# The owner asked somebody else by name, on an issue the problem reporter opened.
gh = GitHub({3: issue(3, user('os-xgs-npu-problem-reports[bot]', 'Bot'),
                      [{'user': OWNER, 'created_at': ago(8),
                        'body': '@stauraum, could you run the three commands? Thanks @AbdelmonemAwad'}])})
sweep(gh)
check('the person named is the one reminded', gh.written[0][2]['body'].startswith('@stauraum - '), True)

# Nobody to remind: a bot's issue and no name in the question.
gh = GitHub({4: issue(4, user('os-xgs-npu-problem-reports[bot]', 'Bot'),
                      [{'user': OWNER, 'created_at': ago(8), 'body': 'Whose appliance is this?'}])})
sweep(gh)
check('nobody named on a bot issue: nothing', gh.written, [])

# The last word is somebody else's: they answered, nothing to remind.
gh = GitHub({5: issue(5, user('asker'), [{'user': OWNER, 'created_at': ago(9), 'body': 'Which port?'},
                                         {'user': user('asker'), 'created_at': ago(8), 'body': 'Port 3.'}])})
sweep(gh)
check('an answered question is left', gh.written, [])

# A pull request with the label is not swept, and a dry run writes nothing.
gh = GitHub({6: issue(6, user('coder'), [{'user': OWNER, 'created_at': ago(8), 'body': 'Rebase?'}], pull=True)})
sweep(gh)
check('a pull request is not swept', gh.written, [])
gh = GitHub({7: issue(7, user('asker'), [{'user': OWNER, 'created_at': ago(8), 'body': 'Which port?'}])})
said = sweep(gh, dry_run=True)
check('a dry run writes nothing', (gh.written, said[0].startswith('would write on #7:')), ([], True))


# An answer takes the label off; the owner's comment or a bot's does not.
def comment(login, kind='User', labelled=True):
    gh = GitHub({})
    labels = [{'name': reminder.LABEL}] if labelled else []
    reminder.answered({'action': 'created', 'issue': {'number': 9, 'labels': labels},
                       'comment': {'user': user(login, kind)}}, gh, say=lambda s: None)
    return gh.written


check('an answer takes the label off', comment('asker'), [('DELETE', '9/labels/waiting%20for%20reply', None)])
check("the owner's comment leaves it", comment(reminder.OWNER), [])
check("a bot's comment leaves it", comment('github-actions[bot]', 'Bot'), [])
check('an issue without the label is left', comment('asker', labelled=False), [])

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
