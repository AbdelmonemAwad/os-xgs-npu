#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""A plugin's next release, as a pull request, once what it installs has changed since its last one.

    python3 plugin-release-pr.py [--dry-run]    in a plugin's checkout of main, with its tags

Run in each plugin's repository by .github/workflows/plugin-release-pr.yml, which the plugin's own
release-pr.yml calls at every push to main. A plugin is released when what an appliance installs from
it changes - src/, pkg/, install/ or the Makefile - and not for its README, its pages or its
workflows. When such a change has reached main since the newest tag, this raises PLUGIN_VERSION by one
in its last part, writes a first block for the version into pkg-descr's changelog from the changes'
titles, and opens the pull request `Release X.Y.Z` from the branch release-next - or brings that
branch and its pull request up to date if it is open already. A pull request opened with the
workflow's own token starts no checks, so it starts the plugin's checks.yml on the branch itself.

The text is a first draft. Reading it and putting it right, merging, and the annotated tag release.yml
builds the package from, all stay with a person. While the Makefile carries a version that has no tag
yet - a release merged and not tagged - this does nothing at all.
"""

import argparse
import re
import subprocess
import sys
import textwrap

BRANCH = 'release-next'
INSTALLED = re.compile(r'^(src/|pkg/|install/|Makefile$)')
VERSION = re.compile(r'^(PLUGIN_VERSION=\s*)(\d+\.\d+\.\d+)\s*$', re.M)
HEADER = 'Plugin Changelog\n================\n\n'


def git(*args, cwd='.'):
    return subprocess.run(['git'] + list(args), cwd=cwd, capture_output=True, text=True, check=True).stdout


def version(cwd='.'):
    with open('%s/Makefile' % cwd, encoding='utf-8') as f:
        m = VERSION.search(f.read())
    return m.group(2) if m else None


def last_tag(cwd='.'):
    try:
        return git('describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*', cwd=cwd).strip()
    except subprocess.CalledProcessError:
        return ''


def changes(tag, cwd='.'):
    """(subject, files) for every first-parent commit since tag, oldest first."""
    out = git('log', '--first-parent', '--reverse', '--format=%H%x1f%s', tag + '..HEAD', cwd=cwd)
    found = []
    for line in out.splitlines():
        sha, subject = line.split('\x1f', 1)
        found.append((subject, git('show', '--name-only', '--format=', sha, cwd=cwd).split()))
    return found


def bump(v):
    major, minor, patch = (int(x) for x in v.split('.'))
    return '%d.%d.%d' % (major, minor, patch + 1)


def plan(cwd='.'):
    """(next version, the installed changes' subjects) when a release is due, else (None, why)."""
    v, tag = version(cwd), last_tag(cwd)
    if not v:
        return None, 'the Makefile has no PLUGIN_VERSION'
    if not tag:
        return None, 'no release has been tagged yet: the first one is a person\'s'
    if 'v' + v != tag:
        return None, 'the Makefile says %s and the newest tag is %s: a release is under way' % (v, tag)
    due = [s for s, files in changes(tag, cwd)
           if not s.startswith('Release ') and any(INSTALLED.match(f) for f in files)]
    if not due:
        return None, 'nothing an appliance installs has changed since %s' % tag
    return bump(v), due


def title(subject):
    return re.sub(r'\s*\(#\d+\)\s*$', '', subject).strip()


def apply(next_version, subjects, cwd='.'):
    """Write the version into the Makefile and its block into pkg-descr's changelog."""
    path = '%s/Makefile' % cwd
    with open(path, encoding='utf-8') as f:
        text = f.read()
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(VERSION.sub(lambda m: m.group(1) + next_version, text, count=1))
    path = '%s/pkg-descr' % cwd
    with open(path, encoding='utf-8') as f:
        text = f.read()
    # The width the plugins' changelogs are written to by hand.
    bullets = ''.join(textwrap.fill(title(s), width=74, initial_indent='* ', subsequent_indent='  ') + '\n'
                      for s in subjects)
    block = '%s\n\n%s\n' % (next_version, bullets)
    if HEADER in text:
        text = text.replace(HEADER, HEADER + block, 1)
    else:
        text = text.rstrip('\n') + '\n\n' + HEADER + block
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def body(repo, tag, next_version, subjects):
    lines = ['The next release of %s: what an appliance installs has changed since %s.' % (repo, tag), '',
             '**Changes since %s**, a first draft of the notes:' % tag, '']
    for s in subjects:
        n = re.search(r'\(#(\d+)\)\s*$', s)
        lines.append('- %s%s' % (title(s), ' (#%s)' % n.group(1) if n else ''))
    lines += ['', '**Before merging:** read the version\'s block in pkg-descr and this text, put right a line '
              'that says less than the change did, and add what was tested and where.', '',
              '**After merging:** push the annotated tag `v%s` on the merge commit, with the notes as its '
              'message; release.yml builds the package from it and publishes the release.' % next_version,
              '', 'Opened by release-pr.yml, which also started checks.yml on the branch.']
    return '\n'.join(lines) + '\n'


def gh(*args):
    return subprocess.run(['gh'] + list(args), capture_output=True, text=True, check=True).stdout


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args()
    next_version, found = plan()
    if next_version is None:
        print(found)
        return 0
    tag = last_tag()
    repo = gh('repo', 'view', '--json', 'nameWithOwner', '--jq', '.nameWithOwner').strip()
    text = body(repo.split('/')[-1], tag, next_version, found)
    if a.dry_run:
        print('would open Release %s on %s:\n\n%s' % (next_version, BRANCH, text))
        return 0
    git('checkout', '-B', BRANCH)
    apply(next_version, found)
    git('add', 'Makefile', 'pkg-descr')
    git('commit', '-q', '-m', 'Release %s' % next_version)
    git('push', '-q', '--force', 'origin', BRANCH)
    have = gh('pr', 'list', '--head', BRANCH, '--state', 'open', '--json', 'number', '--jq', '.[0].number').strip()
    if have:
        gh('pr', 'edit', have, '--title', 'Release %s' % next_version, '--body', text)
        print('brought pull request #%s up to date: Release %s' % (have, next_version))
    else:
        url = gh('pr', 'create', '--base', 'main', '--head', BRANCH, '--title', 'Release %s' % next_version,
                 '--body', text).strip()
        print('opened %s' % url)
    gh('workflow', 'run', 'checks.yml', '--ref', BRANCH)
    print('started checks.yml on %s' % BRANCH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
