#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""One reminder, a week on, when an issue waits on somebody's answer - and never a closing.

    python3 tools/reminder.py --sweep [--dry-run]   look at every open issue labelled `waiting for reply`
    python3 tools/reminder.py                       act on the comment event GitHub Actions hands over

When the owner asks somebody for something on an issue, the issue gets the label `waiting for reply`.
Once a day the sweep looks at each such issue: if the last word on it is still the owner's, and it is a
week old, the person asked - whoever that word names with @, or else whoever opened the issue - is
reminded, once, kindly. The reminder is then the last word, so nothing is said twice. When anybody but
the owner or a bot comments, the label comes off by itself: the conversation is moving again. Nothing
here ever closes an issue.
"""

import argparse
import datetime
import json
import math
import os
import re
import sys
import urllib.parse
import urllib.request

REPO = os.environ.get('GITHUB_REPOSITORY', 'AbdelmonemAwad/os-xgs-npu')
OWNER = 'AbdelmonemAwad'
LABEL = 'waiting for reply'
DAYS = 7
MARK = '<!-- reminder -->'
MENTION = re.compile(r'(?<![\w`])@([A-Za-z0-9](?:[A-Za-z0-9-]{0,38}))')


def api(method, path, body=None):
    req = urllib.request.Request(
        'https://api.github.com' + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'Accept': 'application/vnd.github+json',
                 'X-GitHub-Api-Version': '2022-11-28',
                 'User-Agent': 'os-xgs-npu reminder'})
    with urllib.request.urlopen(req, timeout=30) as r:
        raw = r.read()
    return json.loads(raw) if raw else None


def person(user):
    """A human other than the owner."""
    login = (user or {}).get('login', '')
    return bool(login) and login != OWNER and user.get('type') != 'Bot' and not login.endswith('[bot]')


def when(stamp):
    return datetime.datetime.strptime(stamp, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=datetime.timezone.utc)


def asked(comment, issue):
    """Whom the owner's comment waits on: the people it names with @, or else the issue's author."""
    names = [n for n in MENTION.findall(comment.get('body') or '')
             if n.lower() != OWNER.lower() and not n.lower().endswith('bot')]
    names = [n for i, n in enumerate(names) if n.lower() not in [x.lower() for x in names[:i]]]
    if names:
        return names
    return [issue['user']['login']] if person(issue.get('user')) else []


def text(names):
    return ('%s - a gentle reminder, in case this slipped by: the question above is still open, '
            'whenever you have a moment. Nothing is closed in the meantime.\n\n%s'
            % (', '.join('@' + n for n in names), MARK))


def last_comment(issue, call):
    count = issue.get('comments', 0)
    if not count:
        return None
    page = math.ceil(count / 100)
    found = call('GET', '/repos/%s/issues/%d/comments?per_page=100&page=%d' % (REPO, issue['number'], page))
    return found[-1] if found else None


def sweep(call, now, dry_run=False, say=print):
    query = urllib.parse.urlencode({'labels': LABEL, 'state': 'open', 'per_page': 100})
    for issue in call('GET', '/repos/%s/issues?%s' % (REPO, query)) or []:
        number = issue['number']
        if 'pull_request' in issue:
            continue
        last = last_comment(issue, call)
        if last is None or (last.get('user') or {}).get('login') != OWNER:
            say('#%d: the last word is not the owner\'s' % number)
            continue
        if now - when(last['created_at']) < datetime.timedelta(days=DAYS):
            say('#%d: asked less than %d days ago' % (number, DAYS))
            continue
        names = asked(last, issue)
        if not names:
            say('#%d: nobody to remind' % number)
            continue
        if dry_run:
            say('would write on #%d:\n%s' % (number, text(names)))
            continue
        call('POST', '/repos/%s/issues/%d/comments' % (REPO, number), {'body': text(names)})
        say('reminded %s on #%d' % (', '.join(names), number))


def answered(event, call, say=print):
    """A comment by somebody other than the owner or a bot takes the label off."""
    issue = event.get('issue') or {}
    if event.get('action') != 'created' or 'pull_request' in issue:
        return
    if LABEL not in [l['name'] for l in issue.get('labels', [])]:
        return
    if not person((event.get('comment') or {}).get('user')):
        return
    call('DELETE', '/repos/%s/issues/%d/labels/%s' % (REPO, issue['number'], urllib.parse.quote(LABEL)))
    say('#%d answered: label taken off' % issue['number'])


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--sweep', action='store_true')
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args()
    if a.sweep:
        sweep(api, datetime.datetime.now(datetime.timezone.utc), dry_run=a.dry_run)
    else:
        with open(os.environ['GITHUB_EVENT_PATH'], encoding='utf-8') as f:
            answered(json.load(f), api)
    return 0


if __name__ == '__main__':
    sys.exit(main())
