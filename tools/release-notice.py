#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Tell each issue a release fixes that the fix is out.

    python3 tools/release-notice.py VERSION              write the notices for the release just made
    python3 tools/release-notice.py VERSION --dry-run    say what would be written, write nothing

The release's notes name its pull requests. A pull request names the issue it fixes with a line of its
own that starts `Fix for #N` (or `Fix for #N, #M`) - wording GitHub does not take as an order to close
the issue, so an issue stays open until whoever reported it says the fix works - or with GitHub's own
`Fixes #N`, which closes it on merge. Each issue named either way is told, once, under the release's
version, with a link to the release and the way an appliance takes it. A pull request that only
mentions an issue in passing tells it nothing.

Run by .github/workflows/release.yml after it has published; an error here never undoes a release.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.request

REPO = os.environ.get('GITHUB_REPOSITORY', 'AbdelmonemAwad/os-xgs-npu')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PULL = re.compile(r'\[#(\d+)\]\(https://github\.com/[^/]+/[^/]+/pull/\d+\)')
FIX_FOR = re.compile(r'^\s*fix for\s+(#\d+(?:\s*(?:,|and)\s*#\d+)*)', re.I | re.M)
CLOSES = re.compile(r'\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\s*:?\s+#(\d+)\b', re.I)


def api(method, path, body=None):
    req = urllib.request.Request(
        'https://api.github.com' + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'Accept': 'application/vnd.github+json',
                 'X-GitHub-Api-Version': '2022-11-28',
                 'User-Agent': 'os-xgs-npu release notice'})
    with urllib.request.urlopen(req, timeout=30) as r:
        raw = r.read()
    return json.loads(raw) if raw else None


def notes(version):
    return subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'changelog.py'), '--notes', version],
                          capture_output=True, text=True, check=True).stdout


def pulls(text):
    """The pull requests a release's notes name, in order, each once."""
    seen = []
    for m in PULL.finditer(text):
        n = int(m.group(1))
        if n not in seen:
            seen.append(n)
    return seen


def fixed(body):
    """The issues a pull request's description says it fixes, in order, each once."""
    found = []
    for m in FIX_FOR.finditer(body or ''):
        found += [int(x) for x in re.findall(r'#(\d+)', m.group(1))]
    found += [int(x) for x in CLOSES.findall(body or '')]
    return [n for i, n in enumerate(found) if n not in found[:i]]


def mark(version):
    return '<!-- release-notice v%s -->' % version


def notice(version, pr):
    link = 'https://github.com/%s/releases/tag/v%s' % (REPO, version)
    return ('The fix is out: **[%s](%s)**, released today, carries %s (#%d). An appliance takes it '
            'through **XGS: Updates**.\n\n%s' % (version, link, pr['title'], pr['number'], mark(version)))


def run(version, text, call, dry_run=False, say=print):
    told = set()
    for number in pulls(text):
        pr = call('GET', '/repos/%s/pulls/%d' % (REPO, number))
        for issue in fixed(pr.get('body')):
            if issue in told or issue == number:
                continue
            told.add(issue)
            item = call('GET', '/repos/%s/issues/%d' % (REPO, issue))
            if 'pull_request' in item:
                say('#%d is a pull request, not an issue' % issue)
                continue
            comments = call('GET', '/repos/%s/issues/%d/comments?per_page=100' % (REPO, issue)) or []
            if any(mark(version) in (c.get('body') or '') for c in comments):
                say('#%d has been told about %s already' % (issue, version))
                continue
            body = notice(version, pr)
            if dry_run:
                say('would write on #%d:\n%s' % (issue, body))
                continue
            call('POST', '/repos/%s/issues/%d/comments' % (REPO, issue), {'body': body})
            say('told #%d that %s carries #%d' % (issue, version, number))
    if not told:
        say('no pull request in %s names an issue it fixes' % version)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('version')
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args()
    # A pull request's title can be anything; a console that is not UTF-8 would stop on it.
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    run(a.version, notes(a.version), api, dry_run=a.dry_run)
    return 0


if __name__ == '__main__':
    sys.exit(main())
