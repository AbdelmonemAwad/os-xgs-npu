#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The model an issue from a form names, as its label.

    python3 tools/labels.py          act on the issue event GitHub Actions hands over

Every issue form asks for the appliance - "XGS 107", "XGS 3300", ... - under the heading Appliance.
When an issue is opened, or edited to name another model, the model becomes its label, so a model's
issues can be listed in one click. "Another model" and a body without the heading leave it unlabelled.
A model label that no longer matches is taken off; the problem reporter's own labels ("Sophos XGS
3300r1") are not touched. The labels for pull requests come from .github/labeler.yml instead.
"""

import json
import os
import re
import sys
import urllib.parse
import urllib.request

REPO = os.environ.get('GITHUB_REPOSITORY', 'AbdelmonemAwad/os-xgs-npu')
SECTION = re.compile(r'^###\s*Appliance\s*\n+\s*(.+?)\s*$', re.M)
MODEL = re.compile(r'^XGS \d+[A-Za-z]?$')


def api(method, path, body=None):
    req = urllib.request.Request(
        'https://api.github.com' + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'Accept': 'application/vnd.github+json',
                 'X-GitHub-Api-Version': '2022-11-28',
                 'User-Agent': 'os-xgs-npu labels'})
    with urllib.request.urlopen(req, timeout=30) as r:
        raw = r.read()
    return json.loads(raw) if raw else None


def model(body):
    """The model the form's Appliance answer names, or None."""
    m = SECTION.search(body or '')
    if not m:
        return None
    answer = m.group(1).strip()
    return answer if MODEL.match(answer) else None


def run(event, call, say=print):
    issue = event.get('issue') or {}
    if 'pull_request' in issue or event.get('action') not in ('opened', 'edited'):
        say('not an issue opened or edited')
        return
    number = issue['number']
    want = model(issue.get('body'))
    have = [l['name'] for l in issue.get('labels', [])]
    for name in have:
        if MODEL.match(name) and name != want:
            call('DELETE', '/repos/%s/issues/%d/labels/%s' % (REPO, number, urllib.parse.quote(name)))
            say('took %s off #%d' % (name, number))
    if want and want not in have:
        call('POST', '/repos/%s/issues/%d/labels' % (REPO, number), {'labels': [want]})
        say('labelled #%d %s' % (number, want))
    elif not want:
        say('#%d names no model' % number)


def main():
    with open(os.environ['GITHUB_EVENT_PATH'], encoding='utf-8') as f:
        run(json.load(f), api)
    return 0


if __name__ == '__main__':
    sys.exit(main())
