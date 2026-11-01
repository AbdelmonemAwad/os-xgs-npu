#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""A thank-you, once, to each person whose first contribution has just landed here.

    python3 tools/thanks.py                         act on the event GitHub Actions hands over
    python3 tools/thanks.py --number N [--dry-run]  look at issue or pull request N as it stands now
                                                    (what the workflow's manual run does); with
                                                    --dry-run, say what would be written, write nothing

What counts, and only the first time for each person:

    a report from the appliance form (label compatibility) or the measurement form (label hardware),
    the moment it is opened;
    an issue closed as completed - a problem reported and solved - for whoever opened it;
    a pull request merged, for whoever wrote it.

Nobody is thanked twice: the label `thanked` goes on the issue or pull request that was answered, and
anybody who has one so labelled here already is passed over. The owner and bots are never thanked.
The comment is written by github-actions[bot], so it reads as what it is.
"""

import argparse
import json
import os
import sys
import urllib.parse
import urllib.request

REPO = os.environ.get('GITHUB_REPOSITORY', 'AbdelmonemAwad/os-xgs-npu')
OWNER = 'AbdelmonemAwad'
LABEL = 'thanked'
REPORT_LABELS = {'compatibility', 'hardware'}
MARK = '<!-- thanks -->'
VIDEOS = 'https://www.youtube.com/@npuforge'
TAIL = ('If it is useful to you, a \u2b50 helps other owners find it, and the videos are at '
        + VIDEOS)
TEXT = {
    'report': 'Thank you, @{login} - reports from real appliances are what this project is built on. '
              + TAIL,
    'merged': 'Thank you, @{login} - contributions like this one are what this project is built on. '
              + TAIL,
}


def api(method, path, body=None):
    """GitHub's REST API with the workflow's token; the decoded answer, or None for an empty one."""
    req = urllib.request.Request(
        'https://api.github.com' + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'Accept': 'application/vnd.github+json',
                 'X-GitHub-Api-Version': '2022-11-28',
                 'User-Agent': 'os-xgs-npu thanks'})
    with urllib.request.urlopen(req, timeout=30) as r:
        raw = r.read()
    return json.loads(raw) if raw else None


def contribution(event_name, event):
    """(number, user, kind, labels) for the contribution this event completes, or None."""
    action = event.get('action')
    if event_name == 'issues':
        issue = event.get('issue') or {}
        if 'pull_request' in issue:
            return None
        labels = {l['name'] for l in issue.get('labels', [])}
        if action == 'opened' and labels & REPORT_LABELS:
            return issue['number'], issue['user'], 'report', labels
        if action == 'closed' and issue.get('state_reason') == 'completed':
            return issue['number'], issue['user'], 'report', labels
        return None
    if event_name == 'pull_request_target':
        pr = event.get('pull_request') or {}
        if action == 'closed' and pr.get('merged'):
            return pr['number'], pr['user'], 'merged', {l['name'] for l in pr.get('labels', [])}
    return None


def as_event(number, call):
    """Issue or pull request N as it stands now, written as the event that would have completed it."""
    issue = call('GET', '/repos/%s/issues/%d' % (REPO, number))
    if 'pull_request' in issue:
        pr = call('GET', '/repos/%s/pulls/%d' % (REPO, number))
        return 'pull_request_target', {'action': 'closed', 'pull_request': pr}
    if issue.get('state') == 'closed':
        return 'issues', {'action': 'closed', 'issue': issue}
    return 'issues', {'action': 'opened', 'issue': issue}


def thanked_before(login, call):
    query = 'repo:%s author:%s label:%s' % (REPO, login, LABEL)
    found = call('GET', '/search/issues?' + urllib.parse.urlencode({'q': query, 'per_page': 1}))
    return (found or {}).get('total_count', 0) > 0


def run(event_name, event, call, dry_run=False, say=print):
    found = contribution(event_name, event)
    if found is None:
        say('nothing in this event to thank anybody for')
        return
    number, user, kind, labels = found
    login = user.get('login', '')
    if login == OWNER or user.get('type') == 'Bot' or login.endswith('[bot]'):
        say('#%d is by %s, who is not thanked' % (number, login))
        return
    if LABEL in labels or thanked_before(login, call):
        say('@%s has been thanked before' % login)
        return
    body = TEXT[kind].format(login=login) + '\n\n' + MARK
    if dry_run:
        say('would write on #%d:\n%s' % (number, body))
        return
    call('POST', '/repos/%s/issues/%d/comments' % (REPO, number), {'body': body})
    call('POST', '/repos/%s/issues/%d/labels' % (REPO, number), {'labels': [LABEL]})
    say('thanked @%s on #%d' % (login, number))


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--number', type=int)
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args()
    # The text carries a star; a console that is not UTF-8 would stop on it.
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    if a.number:
        event_name, event = as_event(a.number, api)
    else:
        event_name = os.environ.get('GITHUB_EVENT_NAME', '')
        with open(os.environ['GITHUB_EVENT_PATH'], encoding='utf-8') as f:
            event = json.load(f)
    run(event_name, event, api, dry_run=a.dry_run)
    return 0


if __name__ == '__main__':
    sys.exit(main())
