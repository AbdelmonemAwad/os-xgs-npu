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
branch and its pull request up to date if it is open already and the branch holds only this one's own
commit. A pull request opened with the workflow's own token starts no checks, so it starts the
plugin's checks.yml on the branch itself.

The text is a first draft. Reading it and putting it right, merging, and the annotated tag release.yml
builds the package from, all stay with a person. Once a person has pushed to the open pull request's
branch, the branch and the pull request are theirs: this leaves both as they are and says so (since
2026-11-06). While the Makefile carries a version that has no tag yet - a release merged and not
tagged - this does nothing at all.
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
OWN = re.compile(r'Release \d+\.\d+\.\d+')


def git(*args, cwd='.'):
    return subprocess.run(['git'] + list(args), cwd=cwd, capture_output=True, text=True, encoding='utf-8',
                          errors='replace', check=True).stdout


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
    return subprocess.run(['gh'] + list(args), capture_output=True, text=True, encoding='utf-8',
                          errors='replace', check=True).stdout


def origin_tip(cwd='.'):
    """The release branch's commit on origin as this checkout last fetched it, or '' when it had none.

    The workflow's checkout, with fetch-depth 0, fetches every branch; run by hand, fetch first."""
    try:
        return git('rev-parse', '--verify', '-q', 'refs/remotes/origin/%s^{commit}' % BRANCH, cwd=cwd).strip()
    except subprocess.CalledProcessError:
        return ''


def ident(which, cwd='.'):
    """'Name <email>' as git would write it now, which being GIT_AUTHOR_IDENT or GIT_COMMITTER_IDENT."""
    return git('var', which, cwd=cwd).strip().rsplit(' ', 2)[0]


def others(tip, cwd='.'):
    """The commits on the release branch at tip that this did not make, newest first, one line each;
    none when main has all but one and that one is this one's own.

    Its own is what it pushes: a single 'Release X.Y.Z' on main's history that changes the Makefile and
    pkg-descr alone, authored and committed by the identity it commits with - the workflow's bot. A
    person's correction, the commit amended or rebased by a person (its committer changes), or main
    merged into the branch is someone's work."""
    if not tip:
        return []
    rows = [line.split('\x1f') for line in git(
        'log', '--format=%h%x1f%s%x1f%P%x1f%an%x1f%an <%ae>%x1f%cn%x1f%cn <%ce>', 'HEAD..' + tip,
        cwd=cwd).splitlines()]
    me = (ident('GIT_AUTHOR_IDENT', cwd), ident('GIT_COMMITTER_IDENT', cwd))

    def own(row):
        h, subject, parents, _, author, _, committer = row
        return ((author, committer) == me and len(parents.split()) == 1 and OWN.fullmatch(subject) is not None
                and set(git('diff-tree', '--no-commit-id', '--name-only', '-r', h, cwd=cwd).split())
                <= {'Makefile', 'pkg-descr'})

    theirs = [r for r in rows if not own(r)]
    if not theirs and len(rows) > 1:
        theirs = rows  # this pushes one commit, never several: a branch of several is not its own
    return ['%s %s (%s)' % (h, subject, an if an == cn else '%s, committed by %s' % (an, cn))
            for h, subject, _, an, _, cn, _ in theirs]


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
    # Until 2026-11-06 the branch was reset and force-pushed at every push to main, and the pull
    # request's text written over, so a correction a person had pushed to the open pull request was gone
    # with the next change merged. The pull request is looked up first now, and while it is open a
    # branch holding anything this did not make is left to the person, with its pull request. With
    # none open, a branch left by a release merged or closed is reset as before: a squash merge has its
    # corrections on main, and a closed pull request keeps its commits.
    have = gh('pr', 'list', '--head', BRANCH, '--state', 'open', '--json', 'number', '--jq', '.[0].number').strip()
    tip = origin_tip()
    theirs = others(tip) if have else []
    if theirs:
        print('left pull request #%s as it is: %s carries commits this did not make, so bringing it up to '
              'date is a person\'s now (closed, it is drafted again at the next push to main):' % (have, BRANCH))
        for line in theirs:
            print('  ' + line)
        return 0
    if a.dry_run:
        what = 'bring pull request #%s up to date as' % have if have else 'open'
        print('would %s Release %s on %s:\n\n%s' % (what, next_version, BRANCH, text))
        return 0
    git('checkout', '-B', BRANCH)
    apply(next_version, found)
    git('add', 'Makefile', 'pkg-descr')
    git('commit', '-q', '-m', 'Release %s' % next_version)
    # Replaced only while it stands where it was read above: a push to it since - a person's, or another
    # run's - makes git refuse this one rather than overwrite that, and the next push to main decides again.
    try:
        git('push', '--porcelain', '--force-with-lease=%s:%s' % (BRANCH, tip), 'origin', BRANCH)
    except subprocess.CalledProcessError as e:
        if '(stale info)' not in (e.stdout or ''):
            raise
        print('left %s as it is: it moved on origin after this read it, and the next push to main decides '
              'again' % BRANCH)
        return 0
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
