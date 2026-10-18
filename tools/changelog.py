#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""CHANGELOG.md, written from the commits, and the notes each release is published with.

    tools/changelog.py                     rewrite the Unreleased section from the commits since the
                                           last release
    tools/changelog.py --release VERSION   the same, then make that section VERSION, dated today, and
                                           open a new empty Unreleased above it
    tools/changelog.py --notes VERSION     print VERSION's section, as the release's notes
    tools/changelog.py --top               print the newest released version, or nothing
    tools/changelog.py --due DAYS COUNT    print what waits for a release, as Markdown, and exit 1 if a
                                           release is due: a fix waits, or improvements have waited
                                           DAYS days or number COUNT

Every change reaches main as one squashed commit whose subject is its pull request's title and number,
so the commits since the last tag ARE the change list. Each goes under one heading:

    Fixes                       the pull request carries the label `fix`, or, without labels, its title
                                says it fixes something, or names a crash, a panic, a hang or a leak
    Improvements and additions  everything else that changes what an appliance runs
    Documentation               only pages, README and images changed

A heading is decided from the labels when `gh` can read them, and from the title and the files the
commit touched when it cannot - so the tool gives the same answer on a laptop and in CI.
"""

import datetime
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FILE = ROOT / 'CHANGELOG.md'
REPO = 'AbdelmonemAwad/os-xgs-npu'
HEADINGS = ['Fixes', 'Improvements and additions', 'Documentation']
VERSION = re.compile(r'^## (\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?) - (\d{4}-\d{2}-\d{2})\s*$')
FIXWORDS = re.compile(r'\b(fix|fixes|fixed|fixing|broken|crash|panic|hang|regression|leak)\b', re.I)
DOCS = re.compile(r'^(docs/|README\.md$|CONTRIBUTING\.md$|DESIGN\.md$|CHANGELOG\.md$|RELEASING\.md$)')

INTRO = '''# Changelog

What changed in each release of os-xgs-npu, newest first. **Unreleased** is what has been merged since
the last release and waits for the next one.

Written by `tools/changelog.py` from the commits on `main` - every change reaches it as one commit
named after its pull request - and each release is published with its own section as its notes, so
the Updates page on an appliance shows exactly this. How a release is made, and when:
[RELEASING.md](RELEASING.md). The project is in alpha - every number so far ends in `-alpha.N` - so try a
release on an appliance you can reach before you trust it with a network you depend on.
'''


def git(*args):
    return subprocess.run(['git', '-C', str(ROOT)] + list(args), capture_output=True, text=True,
                          check=True).stdout


def last_tag():
    try:
        return git('describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*').strip()
    except subprocess.CalledProcessError:
        return ''


def labels(number):
    """The pull request's labels through gh, or None when gh cannot say."""
    try:
        out = subprocess.run(['gh', 'pr', 'view', str(number), '-R', REPO, '--json', 'labels'],
                             capture_output=True, text=True, timeout=30)
        if out.returncode != 0:
            return None
        return [l['name'] for l in json.loads(out.stdout).get('labels', [])]
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def commits(since):
    """(sha, date, subject, number, files) for every first-parent commit after since, oldest first."""
    rng = (since + '..HEAD') if since else 'HEAD'
    out = git('log', '--first-parent', '--reverse', '--format=%H%x1f%cs%x1f%s', rng)
    found = []
    for line in out.splitlines():
        sha, date, subject = line.split('\x1f', 2)
        m = re.search(r'\(#(\d+)\)\s*$', subject)
        files = git('show', '--name-only', '--format=', sha).split()
        found.append((sha, date, subject, int(m.group(1)) if m else None, files))
    return found


def heading(subject, number, files):
    names = labels(number) if number else None
    if names is not None:
        if 'fix' in names or 'bug' in names:
            return 'Fixes'
        if 'docs' in names or 'documentation' in names:
            return 'Documentation'
    if FIXWORDS.search(subject):
        return 'Fixes'
    if files and all(DOCS.match(f) for f in files):
        return 'Documentation'
    return 'Improvements and additions'


def entry(subject, number):
    title = re.sub(r'\s*\(#\d+\)\s*$', '', subject).strip()
    link = f' ([#{number}](https://github.com/{REPO}/pull/{number}))' if number else ''
    return f'- {title}{link}'


def unreleased_body(since):
    groups = {h: [] for h in HEADINGS}
    for sha, date, subject, number, files in commits(since):
        if subject.startswith('Release '):
            continue
        groups[heading(subject, number, files)].append(entry(subject, number))
    body = []
    for h in HEADINGS:
        if groups[h]:
            body += [f'### {h}', ''] + groups[h] + ['']
    return body or ['Nothing yet.', '']


def split(text):
    """The intro, then (header, body lines) for Unreleased and each version, in order."""
    lines = text.split('\n')
    sections, cur = [], None
    for line in lines:
        if line.startswith('## '):
            cur = (line, [])
            sections.append(cur)
        elif cur is not None:
            cur[1].append(line)
    return sections


def render(sections):
    out = [INTRO]
    for header, body in sections:
        while body and body[-1] == '':
            body = body[:-1]
        while body and body[0] == '':
            body = body[1:]
        out.append(header)
        out.append('')
        out += body
        out.append('')
    return '\n'.join(out).rstrip('\n') + '\n'


def load():
    if FILE.exists():
        return split(FILE.read_text(encoding='utf-8'))
    return [('## Unreleased', [])]


def released(sections):
    return [s for s in sections if VERSION.match(s[0])]


def main(argv):
    sections = load()
    if not sections or sections[0][0] != '## Unreleased':
        sections.insert(0, ('## Unreleased', []))
    if '--notes' in argv:
        want = argv[argv.index('--notes') + 1]
        for header, body in released(sections):
            if VERSION.match(header).group(1) == want:
                print('\n'.join(body).strip())
                return 0
        print(f'no section for {want} in CHANGELOG.md', file=sys.stderr)
        return 1
    if '--top' in argv:
        rel = released(sections)
        if rel:
            print(VERSION.match(rel[0][0]).group(1))
        return 0
    since = last_tag()
    if '--due' in argv:
        days, count = int(argv[argv.index('--due') + 1]), int(argv[argv.index('--due') + 2])
        waiting = [(d, s, n, f) for _, d, s, n, f in commits(since) if not s.startswith('Release ')]
        fixes = [w for w in waiting if heading(w[1], w[2], w[3]) == 'Fixes']
        others = [w for w in waiting if heading(w[1], w[2], w[3]) == 'Improvements and additions']
        oldest = min((datetime.date.fromisoformat(w[0]) for w in others), default=None)
        age = (datetime.date.today() - oldest).days if oldest else 0
        due = bool(fixes) or len(others) >= count or (bool(others) and age >= days)
        print(f'Since {since or "the first commit"}: {len(fixes)} fix(es), {len(others)} improvement(s), '
              f'the oldest improvement {age} day(s) old.')
        for title, group in (('Fixes - a release is due as soon as one waits', fixes),
                             (f'Improvements and additions - gathered until {count} wait or the oldest is '
                              f'{days} days old', others)):
            if group:
                print(f'\n**{title}**\n')
                for d, s, n, f in group:
                    print(entry(s, n))
        return 1 if due else 0
    body = unreleased_body(since)
    if '--release' in argv:
        version = argv[argv.index('--release') + 1]
        if not re.match(r'^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$', version):
            print('a version is 0.1.0 or 0.1.0-alpha.1', file=sys.stderr)
            return 1
        today = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
        sections[0] = ('## Unreleased', ['Nothing yet.', ''])
        sections.insert(1, (f'## {version} - {today}', body))
    else:
        sections[0] = ('## Unreleased', body)
    FILE.write_text(render(sections), encoding='utf-8', newline='\n')
    print(f'wrote CHANGELOG.md ({len(released(sections))} release(s))')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
