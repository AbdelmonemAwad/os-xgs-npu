#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/thanks.py against made-up events and a stand-in GitHub: who is thanked, for what, once - though
the search has not yet seen the label just put on - and who never is; and thanks.yml, one run at a time
for each person rather than for everybody.

    python3 tools/test-thanks.py"""
import os
import re
import sys
import urllib.parse

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import thanks  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


class GitHub:
    """Records what is written. The issues list answers from the labels as they are; the search only from
    the people thanked before it was made, as GitHub's search index takes a while to see a label just
    put on."""

    def __init__(self, thanked=(), items=None):
        self.thanked = set(thanked)
        self.indexed = set(thanked)
        self.items = items or {}
        self.written = []

    def __call__(self, method, path, body=None):
        if method == 'GET' and path.startswith('/search/issues?'):
            login = path.split('author%3A', 1)[1].split('+', 1)[0]
            return {'total_count': 1 if login in self.indexed else 0}
        if method == 'GET' and path.startswith('/repos/%s/issues?' % thanks.REPO):
            q = urllib.parse.parse_qs(path.split('?', 1)[1])
            mine = q.get('creator', [''])[0] in self.thanked and q.get('labels') == ['thanked']
            return [{'number': 1}] if mine and q.get('state') == ['all'] else []
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
check('the thanks says it is automated', body.endswith('\n\n' + thanks.AUTOMATED + '\n\n' + thanks.MARK), True)
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

# Someone thanked before, on an item without the label: passed over.
gh = GitHub(thanked={'veteran'})
go(gh, 'issues', {'action': 'opened', 'issue': issue(50, 'veteran', ['compatibility'])})
check('someone thanked before is passed over', gh.written, [])

# One person's two contributions back to back - a pull request merged and the issue it closed - in two
# runs, one after the other. The label the first put on is not in the search yet: until 2026-11-06 the
# second asked the search, and here it found nobody thanked and thanked the person again.
gh = GitHub()
merged = {'number': 90, 'user': user('fixer'), 'labels': [], 'merged': True}
go(gh, 'pull_request_target', {'action': 'closed', 'pull_request': merged})
go(gh, 'issues', {'action': 'closed', 'issue': issue(91, 'fixer', ['bug'], 'closed', 'completed')})
check('a label put on a moment ago is seen, though search has not caught up', len(comments(gh)), 1)

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

# One run at a time for each person, so that one person's two contributions cannot both be the first;
# not for everybody, since GitHub keeps one run waiting in a group and drops the one before it.
with open(os.path.join(os.path.dirname(here), '.github', 'workflows', 'thanks.yml'), encoding='utf-8') as f:
    flow = f.read()
check('one run at a time for each person, not for everybody',
      ('github.event.issue.user.login' in flow, 'github.event.pull_request.user.login' in flow,
       re.search(r'^\s*group:\s*thanks\s*$', flow, re.M) is None), (True, True, True))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
