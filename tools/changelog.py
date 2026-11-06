#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""CHANGELOG.md, written from the commits, and the notes each release is published with.

    tools/changelog.py                     rewrite the Unreleased section from the commits since the
                                           last release
    tools/changelog.py --release VERSION   the same, then make that section VERSION, dated today, and
                                           open a new empty Unreleased above it; then name the lines to
                                           read again (below)
    tools/changelog.py --notes VERSION     print VERSION's section, as the release's notes
    tools/changelog.py --top               print the newest released version, or nothing
    tools/changelog.py --commit VERSION    print the commit that added VERSION's section, the one its
                                           tag goes on, or exit 1
    tools/changelog.py --due DAYS COUNT    print what waits for a release, as Markdown, and exit 1 if a
                                           release is due: a fix waits, or improvements have waited
                                           DAYS days or number COUNT

Every change reaches main as one squashed commit whose subject is its pull request's title and number,
so the commits since the last tag ARE the change list - all but a release's own, the commit titled with
its version alone (`Release 0.1.0-alpha.14 (#1053)`). Until 2026-11-06 every title that began with
"Release " was taken for one, so a change named "Release the coprocessor's reset ..." would have been
left out of the changelog, of what makes a release due, and so of the notices. Each goes under one
heading:

    Fixes                       the pull request carries the label `fix`, or, without labels, its title
                                says it fixes something, or names a crash, a panic, a hang or a leak
    Improvements and additions  everything else that changes what an appliance runs
    Documentation               only pages, README and images changed

A heading is decided from the labels when `gh` can read them, and from the title and the files the
commit touched when it cannot - so the tool gives the same answer on a laptop and in CI.

THE SITE IS NOT A RELEASE. A commit that touches only the site (website/) or the problem reports'
relay (relay/), and their workflows, is left out: both are services deployed from here by their own
workflows, an appliance installs neither (.gitattributes leaves them out of every archive), and a
release's notes are what an appliance's Updates page shows. Such a commit is not counted by --due
either, so the site never makes a release due. Since 2026-10-22; v0.1.0-alpha.5.2 had carried one.

A LINE IS ITS PULL REQUEST'S TITLE AS IT WAS MERGED, and a later change of the same release can make it
untrue: in 0.1.0-alpha.14, #1049 narrowed what #1048's title said every board got, and the line was put
right by hand in the release's pull request. So --release, once it has written the section, names each
line that a later pull request of the same release says it fixes an issue of too ("Fix for #N", or
GitHub's "Fixes #N", read as tools/release-notice.py reads them for the notices), or names - unless that
later one changed pages alone, which describe a change and make none. It changes nothing in the file:
what the line should say is for whoever reads it (RELEASING.md). Since 2026-11-06.
"""

import datetime
import functools
import importlib.util
import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
FILE = ROOT / 'CHANGELOG.md'
REPO = 'AbdelmonemAwad/os-xgs-npu'
HEADINGS = ['Fixes', 'Improvements and additions', 'Documentation']
VERSION = re.compile(r'^## (\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?) - (\d{4}-\d{2}-\d{2})\s*$')
RELEASE = re.compile(r'^Release \d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?(?: \(#\d+\))?\s*$')
FIXWORDS = re.compile(r'\b(fix|fixes|fixed|fixing|broken|crash|panic|hang|regression|leak)\b', re.I)
DOCS = re.compile(r'^(docs/|README\.md$|CONTRIBUTING\.md$|DESIGN\.md$|CHANGELOG\.md$|RELEASING\.md$)')
SITE = re.compile(r'^(website/|relay/|\.github/workflows/(website|relay)\.yml$)')

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
                          encoding='utf-8', errors='replace', check=True).stdout


def last_tag():
    try:
        return git('describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*').strip()
    except subprocess.CalledProcessError:
        return ''


def added(version):
    """The commit on main's own line that added VERSION's heading to CHANGELOG.md - the release's own,
    which its tag goes on - or '' when none did. A later change to the file, the next release's above
    it or a correction of its date, leaves the number of such headings as it was, so it is not this."""
    try:
        return git('log', '--first-parent', '-1', '--format=%H', '-S## %s - ' % version, '--',
                   'CHANGELOG.md').strip()
    except subprocess.CalledProcessError:
        return ''


@functools.lru_cache(maxsize=None)
def pull(number):
    """The pull request's labels and description through gh, or None when gh cannot say; asked once
    for both."""
    try:
        out = subprocess.run(['gh', 'pr', 'view', str(number), '-R', REPO, '--json', 'labels,body'],
                             capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
        if out.returncode != 0:
            return None
        got = json.loads(out.stdout)
        return [l['name'] for l in got.get('labels', [])], got.get('body') or ''
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def labels(number):
    """The pull request's labels through gh, or None when gh cannot say."""
    got = pull(number)
    return None if got is None else got[0]


@functools.lru_cache(maxsize=None)
def notice_rules():
    """tools/release-notice.py, for its reading of what a pull request says it fixes."""
    spec = importlib.util.spec_from_file_location('release_notice', HERE / 'release-notice.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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


def site_only(files):
    """Only the site or the relay changed: nothing an appliance installs."""
    return bool(files) and all(SITE.match(f) for f in files)


def docs_only(files):
    """Only pages changed: they describe a change and make none."""
    return bool(files) and all(DOCS.match(f) for f in files)


def pending(since):
    """The commits since the last release that the next one takes: all but a release's own and those
    that touched only the site."""
    return [c for c in commits(since) if not RELEASE.match(c[2]) and not site_only(c[4])]


def heading(subject, number, files):
    names = labels(number) if number else None
    if names is not None:
        if 'fix' in names or 'bug' in names:
            return 'Fixes'
        if 'docs' in names or 'documentation' in names:
            return 'Documentation'
    # Pages alone are Documentation whatever their title says: until 2026-11-06 the title was read
    # first, and #1038 - pages alone, on "a panic's record" - was counted a fix and made a release due
    # (#1046). A person's `fix` label above still counts.
    if docs_only(files):
        return 'Documentation'
    if FIXWORDS.search(subject):
        return 'Fixes'
    return 'Improvements and additions'


def title_of(subject):
    return re.sub(r'\s*\(#\d+\)\s*$', '', subject).strip()


def entry(subject, number):
    link = f' ([#{number}](https://github.com/{REPO}/pull/{number}))' if number else ''
    return f'- {title_of(subject)}{link}'


def to_read(found):
    """The lines of a release a later change of the same release may have made untrue, as (number,
    subject, reasons) oldest first, and the pull requests gh could not read. A reason is a later pull
    request that says it fixes an issue this one does too, or that names this one in its title or
    description, unless it changed pages alone."""
    known, unread = [], []
    for _, _, subject, number, files in found:
        if not number:
            continue
        got = pull(number)
        if got is None:
            unread.append(number)
        else:
            known.append((number, subject, files, got[1]))
    flagged = []
    for i, (number, subject, _, body) in enumerate(known):
        mine, reasons = set(notice_rules().fixed(body)), []
        for later, later_subject, later_files, later_body in known[i + 1:]:
            said = []
            same = [n for n in notice_rules().fixed(later_body) if n in mine]
            if same:
                said.append('fixes %s too' % ', '.join('#%d' % n for n in same))
            if not docs_only(later_files) and re.search(r'#%d\b' % number, later_subject + '\n' + later_body):
                said.append('names it')
            if said:
                reasons.append('#%d %s' % (later, ' and '.join(said)))
        if reasons:
            flagged.append((number, subject, reasons))
    return flagged, unread


def unreleased_body(found):
    groups = {h: [] for h in HEADINGS}
    for sha, date, subject, number, files in found:
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
    if '--commit' in argv:
        want = argv[argv.index('--commit') + 1]
        sha = added(want)
        if not sha:
            print(f'no commit added a section for {want} to CHANGELOG.md', file=sys.stderr)
            return 1
        print(sha)
        return 0
    since = last_tag()
    found = pending(since)
    if '--due' in argv:
        days, count = int(argv[argv.index('--due') + 1]), int(argv[argv.index('--due') + 2])
        waiting = [(d, s, n, f) for _, d, s, n, f in found]
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
    body = unreleased_body(found)
    version = None
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
    if version:
        flagged, unread = to_read(found)
        if flagged or unread:
            print(f'\nRead these again before the pull request: a later change of {version} may have made '
                  f'them untrue.')
        for number, subject, reasons in flagged:
            print(f'  #{number} {title_of(subject)}: {"; ".join(reasons)}')
        if unread:
            them = 'it' if len(unread) == 1 else 'them'
            print(f'  gh could not read {", ".join(f"#{n}" for n in unread)}, so the lines before {them} were '
                  f'not compared with {them}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
