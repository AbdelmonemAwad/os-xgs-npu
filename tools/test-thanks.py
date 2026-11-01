#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/thanks.py against made-up events and a stand-in GitHub: who is thanked, for what, once, and
who never is.

    python3 tools/test-thanks.py"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import thanks  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


class GitHub:
    """Records what is written; the search answers from the people thanked so far."""

    def __init__(self, thanked=(), items=None):
        self.thanked = set(thanked)
        self.items = items or {}
        self.written = []

    def __call__(self, method, path, body=None):
        if method == 'GET' and path.startswith('/search/issues?'):
            login = path.split('author%3A', 1)[1].split('+', 1)[0]
            return {'total_count': 1 if login in self.thanked else 0}
        if method == 'GET':
            return self.items[path]
        self.written.append((method, path, body))
        if path.endswith('/labels'):
            self.thanked.add(self.last_login)
        return None


def user(login, kind='User'):
    return {'login': login, 'type': kind}


def issue(number, login, labels=(), state='open', reason=None, kind='User'):
    return {'number': number, 'user': user(login, kind), 'labels': [{'name': n} for n in labels],
            'state': state, 'state_reason': reason}


def go(gh, event_name, event, dry_run=False):
    said = []
    found = thanks.contribution(event_name, event)
    gh.last_login = found[1]['login'] if found else None
    thanks.run(event_name, event, gh, dry_run=dry_run, say=said.append)
    return said


comments = lambda gh: [w for w in gh.written if w[1].endswith('/comments')]  # noqa: E731

# A first report from the appliance form: thanked once, with the star and the videos, and labelled.
gh = GitHub()
go(gh, 'issues', {'action': 'opened', 'issue': issue(10, 'owner1', ['compatibility'])})
check('a first appliance report is thanked', len(comments(gh)), 1)
body = comments(gh)[0][2]['body']
check('the thanks names the person', '@owner1 ' in body, True)
check('the thanks asks for a star', '⭐' in body, True)
check('the thanks gives the videos', 'https://www.youtube.com/@npuforge' in body, True)
check('the thanks is the report text', 'reports from real appliances' in body, True)
check('the report is labelled', ('POST', '/repos/%s/issues/10/labels' % thanks.REPO,
                                 {'labels': ['thanked']}) in gh.written, True)

# The same person's next report, and the same report closed later: nothing more.
go(gh, 'issues', {'action': 'opened', 'issue': issue(11, 'owner1', ['hardware'])})
go(gh, 'issues', {'action': 'closed', 'issue': issue(10, 'owner1', ['compatibility', 'thanked'],
                                                     'closed', 'completed')})
check('nobody is thanked twice', len(comments(gh)), 1)

# A measurement report from someone new counts too.
gh = GitHub()
go(gh, 'issues', {'action': 'opened', 'issue': issue(12, 'measurer', ['hardware'])})
check('a measurement report is thanked', len(comments(gh)), 1)

# A problem report is not thanked when it is opened, but when it is closed as completed.
gh = GitHub()
go(gh, 'issues', {'action': 'opened', 'issue': issue(20, 'reporter', ['bug'])})
check('a problem report is not thanked on opening', len(comments(gh)), 0)
go(gh, 'issues', {'action': 'closed', 'issue': issue(20, 'reporter', ['bug'], 'closed', 'not_planned')})
check('an issue closed as not planned is not thanked', len(comments(gh)), 0)
go(gh, 'issues', {'action': 'closed', 'issue': issue(20, 'reporter', ['bug'], 'closed', 'completed')})
check('an issue closed as completed is thanked', len(comments(gh)), 1)

# A pull request: thanked when merged, with its own first sentence; not when closed unmerged.
gh = GitHub()
pr = {'number': 30, 'user': user('coder'), 'labels': [], 'merged': False}
go(gh, 'pull_request_target', {'action': 'closed', 'pull_request': pr})
check('a pull request closed unmerged is not thanked', len(comments(gh)), 0)
go(gh, 'pull_request_target', {'action': 'closed', 'pull_request': dict(pr, merged=True)})
check('a merged pull request is thanked', len(comments(gh)), 1)
check('the merged text', 'contributions like this one' in comments(gh)[0][2]['body'], True)

# The owner and bots are never thanked, whatever they did.
gh = GitHub()
go(gh, 'issues', {'action': 'opened', 'issue': issue(40, 'AbdelmonemAwad', ['compatibility'])})
go(gh, 'issues', {'action': 'closed', 'issue': issue(41, 'os-xgs-npu-problem-reports[bot]', ['bug'],
                                                     'closed', 'completed', 'Bot')})
go(gh, 'pull_request_target', {'action': 'closed',
                               'pull_request': {'number': 42, 'user': user('dependabot[bot]', 'Bot'),
                                                'labels': [], 'merged': True}})
check('the owner and bots are not thanked', gh.written, [])

# Someone the search already knows as thanked, on an item without the label: passed over.
gh = GitHub(thanked={'veteran'})
go(gh, 'issues', {'action': 'opened', 'issue': issue(50, 'veteran', ['compatibility'])})
check('someone thanked before is passed over', gh.written, [])

# A dry run says what it would write and writes nothing.
gh = GitHub()
said = go(gh, 'issues', {'action': 'opened', 'issue': issue(60, 'newcomer', ['compatibility'])},
          dry_run=True)
check('a dry run writes nothing', gh.written, [])
check('a dry run says what it would write', said[0].startswith('would write on #60:'), True)

# A manual run reads the item as it stands: an open report, a merged pull request.
gh = GitHub(items={'/repos/%s/issues/70' % thanks.REPO: issue(70, 'manual', ['compatibility'])})
check('an open item reads as opened', thanks.as_event(70, gh)[1]['action'], 'opened')
pr71 = {'number': 71, 'user': user('m'), 'labels': [], 'merged': True}
gh = GitHub(items={'/repos/%s/issues/71' % thanks.REPO: dict(issue(71, 'm'), pull_request={}),
                   '/repos/%s/pulls/71' % thanks.REPO: pr71})
check('a pull request reads as the pull request', thanks.as_event(71, gh)[0], 'pull_request_target')

# Other events are left alone.
check('a labelled issue is no contribution',
      thanks.contribution('issues', {'action': 'labeled', 'issue': issue(80, 'x', ['compatibility'])}),
      None)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
