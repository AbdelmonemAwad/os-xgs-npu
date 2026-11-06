#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/plugin-release-pr.py against a made-up plugin in a temporary git repository: a release is due
only when what an appliance installs changed since the newest tag, never while a release is under way,
and its version and changelog block are written as the plugins write them by hand. Its main(), against
a stand-in origin and gh: the pull request is opened when none is, brought up to date while its branch
holds only this one's commit, and left as it is once a person has pushed to that branch.

    python3 tools/test-plugin-release-pr.py"""
import contextlib
import importlib.util
import io
import os
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('plugin_release_pr', os.path.join(here, 'plugin-release-pr.py'))
prp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prp)

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def git(cwd, *args):
    subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@example.org', '-c', 'commit.gpgsign=false',
                    '-c', 'tag.gpgsign=false'] + list(args), cwd=cwd, check=True, capture_output=True)


def write(cwd, path, text):
    os.makedirs(os.path.dirname(os.path.join(cwd, path)) or cwd, exist_ok=True)
    with open(os.path.join(cwd, path), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def commit(cwd, subject, path, text):
    write(cwd, path, text)
    git(cwd, 'add', path)
    git(cwd, 'commit', '-q', '-m', subject)


MAKEFILE = 'PLUGIN_NAME=\t\tdemo\nPLUGIN_VERSION=\t\t1.0.6\nPLUGIN_COMMENT=\t\tA demo\n'
DESCR = 'A demo.\n\nPlugin Changelog\n================\n\n1.0.6\n\n* The last one\n'

with tempfile.TemporaryDirectory() as d:
    git(d, 'init', '-q', '-b', 'main')
    write(d, 'Makefile', MAKEFILE)
    write(d, 'pkg-descr', DESCR)
    commit(d, 'A start', 'src/demo.py', 'x = 1\n')
    git(d, 'tag', '-a', 'v1.0.6', '-m', 'v1.0.6')

    check('nothing since the tag', prp.plan(d), (None, 'nothing an appliance installs has changed since v1.0.6'))
    commit(d, 'README: a banner (#45)', 'README.md', 'demo\n')
    commit(d, 'Docs: a page (#46)', 'docs/page.md', 'page\n')
    check('pages and README are no release', prp.plan(d)[0], None)

    commit(d, 'Keys stop the rotation (#47)', 'src/opnsense/scripts/demo/keys.py', 'k = 1\n')
    commit(d, 'Release 9.9.9 (#48)', 'pkg/post-install.sh', 'true\n')
    commit(d, 'A dependency for the panel, and a very long title that goes on past the width of '
              'a line in pkg-descr (#49)', 'Makefile', MAKEFILE + 'PLUGIN_DEPENDS=\t\tlcdproc\n')
    nxt, subjects = prp.plan(d)
    check('an installed change makes a release due', nxt, '1.0.7')
    check("the changes, oldest first, not a release's own", subjects,
          ['Keys stop the rotation (#47)', 'A dependency for the panel, and a very long title that goes on '
           'past the width of a line in pkg-descr (#49)'])

    prp.apply(nxt, subjects, d)
    with open(os.path.join(d, 'Makefile'), encoding='utf-8') as f:
        mk = f.read()
    check('the Makefile carries the version, tabs kept', 'PLUGIN_VERSION=\t\t1.0.7\n' in mk, True)
    with open(os.path.join(d, 'pkg-descr'), encoding='utf-8') as f:
        descr = f.read()
    check('the block goes first under the heading',
          descr.split(prp.HEADER, 1)[1].startswith('1.0.7\n\n* Keys stop the rotation\n'), True)
    block = descr.split(prp.HEADER, 1)[1].split('\n\n1.0.6', 1)[0].split('\n')[2:]
    check('a long title wraps as the plugins wrap, two spaces in',
          (all(len(line) <= 74 for line in block),
           [line[:2] for line in block if line and not line.startswith('* ')] == ['  ']),
          (True, True))
    check('a wrapped title reads whole', ' '.join(line.strip(' *') for line in block[1:] if line),
          'A dependency for the panel, and a very long title that goes on past the width of a line in pkg-descr')
    check('the earlier blocks stay', descr.endswith('1.0.6\n\n* The last one\n'), True)

    git(d, 'add', 'Makefile', 'pkg-descr')
    git(d, 'commit', '-q', '-m', 'Release 1.0.7 (#50)')
    check('merged and not tagged: under way', prp.plan(d)[1],
          'the Makefile says 1.0.7 and the newest tag is v1.0.6: a release is under way')

    text = prp.body('os-demo', 'v1.0.6', '1.0.7', subjects)
    check('the body links the changes', '- Keys stop the rotation (#47)' in text, True)
    check('the body says what comes after merging', 'annotated tag `v1.0.7`' in text, True)

with tempfile.TemporaryDirectory() as d:
    git(d, 'init', '-q', '-b', 'main')
    write(d, 'Makefile', MAKEFILE)
    commit(d, 'A start', 'src/demo.py', 'x = 1\n')
    check('never tagged: left to a person', prp.plan(d)[0], None)

check('the last part goes up by one', prp.bump('1.2.9'), '1.2.10')

# main() itself. Its origin is a bare repository beside the plugin's checkout, and GIT_ALLOW_PROTOCOL
# has git refuse any remote that is not a path on this machine; gh is a stand-in that answers as GitHub
# would and keeps what it was asked. Until 2026-11-06 main() reset and force-pushed release-next at
# every push to main, so a correction a person had pushed to the open pull request was wiped.
os.environ['GIT_ALLOW_PROTOCOL'] = 'file'
BOT = ('github-actions[bot]', '41898282+github-actions[bot]@users.noreply.github.com')
PERSON = ('A Person', 'person@example.org')
URL = 'https://example.org/os-demo/pull/3'
LEFT = ("left pull request #3 as it is: release-next carries commits this did not make, so bringing it up to "
        "date is a person's now (closed, it is drafted again at the next push to main):")


class Hub:
    """gh as main() calls it: the pull request open on release-next, if any, and all it was asked."""

    def __init__(self, open_pr=''):
        self.open_pr, self.asked = open_pr, []

    def __call__(self, *args):
        self.asked.append(args)
        if args[:2] == ('repo', 'view'):
            return 'AbdelmonemAwad/os-demo\n'
        if args[:2] == ('pr', 'list'):
            return self.open_pr + '\n'
        if args[:2] == ('pr', 'create'):
            return URL + '\n'
        if args[:2] in (('pr', 'edit'), ('workflow', 'run')):
            return ''
        raise AssertionError('gh %s was not expected' % ' '.join(args))

    def verbs(self):
        return [' '.join(a[:2]) for a in self.asked]


real_git, real_gh = prp.git, prp.gh


def run(checkout, hub, *argv, before_push=None):
    """main() in checkout with hub for gh: (its exit status, what it printed, the git commands it ran)."""
    ran = []

    def seen(*args, cwd='.'):
        ran.append(args)
        if args[0] == 'push' and before_push:
            before_push()
        return real_git(*args, cwd=cwd)

    argv_was, back = sys.argv, os.getcwd()
    prp.git, prp.gh, sys.argv = seen, hub, ['plugin-release-pr.py'] + list(argv)
    out = io.StringIO()
    os.chdir(checkout)
    try:
        with contextlib.redirect_stdout(out):
            status = prp.main()
    finally:
        os.chdir(back)
        prp.git, prp.gh, sys.argv = real_git, real_gh, argv_was
    return status, out.getvalue(), ran


def read(cwd, *args):
    return subprocess.run(['git'] + list(args), cwd=cwd, check=True, capture_output=True, text=True,
                          encoding='utf-8', errors='replace').stdout.strip()


def commit_as(who, cwd, subject, path, edit):
    with open(os.path.join(cwd, path), encoding='utf-8') as f:
        text = f.read()
    write(cwd, path, edit(text))
    git(cwd, 'add', path)
    git(cwd, '-c', 'user.name=' + who[0], '-c', 'user.email=' + who[1], 'commit', '-q', '-m', subject)


def pushes(ran):
    return [args for args in ran if args[0] == 'push']


with tempfile.TemporaryDirectory() as d:
    origin, work, person = (os.path.join(d, n) for n in ('origin.git', 'work', 'person'))
    git(d, 'init', '-q', '--bare', origin)
    git(d, 'init', '-q', '-b', 'main', work)
    # The identity the workflow gives its checkout before it runs the tool.
    for key, value in (('user.name', BOT[0]), ('user.email', BOT[1]), ('commit.gpgsign', 'false')):
        git(work, 'config', key, value)
    write(work, 'Makefile', MAKEFILE)
    write(work, 'pkg-descr', DESCR)
    git(work, 'add', 'Makefile', 'pkg-descr')
    commit(work, 'A start', 'src/demo.py', 'x = 1\n')
    git(work, 'tag', '-a', 'v1.0.6', '-m', 'v1.0.6')
    commit(work, 'Keys stop the rotation (#47)', 'src/opnsense/scripts/demo/keys.py', 'k = 1\n')
    git(work, 'remote', 'add', 'origin', origin)
    git(work, 'push', '-q', 'origin', 'main', '--tags')

    hub = Hub()
    status, out, ran = run(work, hub)
    first = read(origin, 'rev-parse', 'release-next')
    check('none open: the branch is pushed, a pull request opened and its checks started',
          (status, out.splitlines(), hub.verbs()),
          (0, ['opened ' + URL, 'started checks.yml on release-next'],
           ['repo view', 'pr list', 'pr create', 'workflow run']))
    check('... as Release 1.0.7, from release-next to main', hub.asked[2][:8],
          ('pr', 'create', '--base', 'main', '--head', 'release-next', '--title', 'Release 1.0.7'))
    check("... the branch is main and this one's commit, pushed only where no branch was",
          (read(origin, 'log', '-1', '--format=%s|%an|%cn', first),
           read(origin, 'rev-parse', first + '^') == read(work, 'rev-parse', 'main'), pushes(ran)),
          ('Release 1.0.7|%s|%s' % (BOT[0], BOT[0]), True,
           [('push', '--porcelain', '--force-with-lease=release-next:', 'origin', 'release-next')]))

    git(work, 'checkout', '-q', 'main')
    commit(work, 'Rotation waits for the panel (#51)', 'src/opnsense/scripts/demo/rotate.py', 'r = 1\n')
    git(work, 'push', '-q', 'origin', 'main')
    hub = Hub('3')
    status, out, ran = run(work, hub)
    second = read(origin, 'rev-parse', 'release-next')
    check("open, its branch only this one's commit: brought up to date as before",
          (status, out.splitlines(), hub.verbs()),
          (0, ['brought pull request #3 up to date: Release 1.0.7', 'started checks.yml on release-next'],
           ['repo view', 'pr list', 'pr edit', 'workflow run']))
    check('... the branch reset onto main, over its earlier commit and only that',
          (read(origin, 'rev-parse', second + '^') == read(work, 'rev-parse', 'main'), pushes(ran)),
          (True, [('push', '--porcelain', '--force-with-lease=release-next:' + first, 'origin', 'release-next')]))
    check('... the new change in the block and in the text',
          ('* Rotation waits for the panel' in read(origin, 'show', second + ':pkg-descr'), hub.asked[2][:5],
           '- Rotation waits for the panel (#51)' in hub.asked[2][6]),
          (True, ('pr', 'edit', '3', '--title', 'Release 1.0.7'), True))

    git(work, 'checkout', '-q', 'main')
    check('its own commit, seen from main, is its own', prp.others(second, work), [])
    git(work, 'checkout', '-q', '-b', 'amended', second)
    git(work, '-c', 'user.name=' + PERSON[0], '-c', 'user.email=' + PERSON[1], 'commit', '-q', '--amend',
        '--no-edit')
    amended = read(work, 'log', '-1', '--format=%h')
    git(work, 'checkout', '-q', 'main')
    check('amended by a person, its author kept: not its own', prp.others(amended, work),
          ['%s Release 1.0.7 (%s, committed by %s)' % (amended, BOT[0], PERSON[0])])

    git(d, 'clone', '-q', '-b', 'release-next', origin, person)
    commit_as(PERSON, person, 'pkg-descr: what the keys stop, said whole', 'pkg-descr',
              lambda t: t.replace('* Keys stop the rotation\n', '* Keys stop the rotation of the panel\n'))
    git(person, 'push', '-q', 'origin', 'release-next')
    theirs = read(origin, 'rev-parse', 'release-next')
    commit(work, 'The panel dims at night (#52)', 'src/opnsense/scripts/demo/dim.py', 'n = 1\n')
    git(work, 'push', '-q', 'origin', 'main')
    git(work, 'fetch', '-q', 'origin')  # as the workflow's checkout fetches every branch
    hub = Hub('3')
    status, out, ran = run(work, hub)
    check("open, a person's commit on its branch: no push, nothing written over, no checks started",
          (status, [a[0] for a in ran if a[0] in ('checkout', 'commit', 'push')],
           read(origin, 'rev-parse', 'release-next') == theirs, hub.verbs()),
          (0, [], True, ['repo view', 'pr list']))
    check('... and it says what it left and whose the commit is', out.splitlines(),
          [LEFT, '  %s pkg-descr: what the keys stop, said whole (A Person)' % read(origin, 'log', '-1',
                                                                                  '--format=%h', theirs)])
    status, out, ran = run(work, Hub('3'), '--dry-run')
    check('... a dry run says the same and pushes nothing', (status, out.splitlines()[0], pushes(ran)),
          (0, LEFT, []))

    hub = Hub()
    status, out, ran = run(work, hub)
    fourth = read(origin, 'rev-parse', 'release-next')
    check('none open, a branch left by a release merged or closed: reset and opened as before',
          (status, read(origin, 'rev-parse', fourth + '^') == read(work, 'rev-parse', 'main'), hub.verbs(),
           pushes(ran)),
          (0, True, ['repo view', 'pr list', 'pr create', 'workflow run'],
           [('push', '--porcelain', '--force-with-lease=release-next:' + theirs, 'origin', 'release-next')]))

    def person_pushes():
        git(person, 'fetch', '-q', 'origin')
        git(person, 'reset', '-q', '--hard', 'origin/release-next')
        commit_as(PERSON, person, 'pkg-descr: what was tested, and where', 'pkg-descr', lambda t: t + '\n')
        git(person, 'push', '-q', 'origin', 'release-next')

    git(work, 'checkout', '-q', 'main')
    commit(work, 'Fans follow the board (#53)', 'src/opnsense/scripts/demo/fans.py', 'f = 1\n')
    git(work, 'push', '-q', 'origin', 'main')
    hub = Hub('3')
    status, out, ran = run(work, hub, before_push=person_pushes)
    check("a person's push between the look and this one's: refused, kept, and said",
          (status, read(origin, 'rev-parse', 'release-next') == read(person, 'rev-parse', 'HEAD'), hub.verbs(),
           out.splitlines()),
          (0, True, ['repo view', 'pr list'],
           ['left release-next as it is: it moved on origin after this read it, and the next push to main '
            'decides again']))

# One run at a time for each plugin: with the push refused once the branch moved since a run read it,
# two runs side by side would have the later one refused and the newer change left out.
with open(os.path.join(os.path.dirname(here), '.github', 'workflows', 'plugin-release-pr.yml'),
          encoding='utf-8') as f:
    flow = f.read()
check('the workflow runs one at a time for each plugin, none cut off',
      ('group: plugin-release-pr-${{ github.repository }}' in flow, 'cancel-in-progress: false' in flow),
      (True, True))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
