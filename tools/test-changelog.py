#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/changelog.py in a made-up repository, gh stood in for: what a release's section takes from the
commits and what it leaves out - a release's own commit and the site, but not a change whose title
begins with the word Release; the lines --release names to read again, those a later change of the
same release says it fixes the same issue as, or names; the commit --commit gives a version's tag; and
release.yml, which tags that commit, from main alone, and builds the site again once the release is out.

    python3 tools/test-changelog.py"""
import contextlib
import importlib.util
import io
import os
import pathlib
import re
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('changelog', os.path.join(here, 'changelog.py'))
cl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cl)

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def git(cwd, *args):
    return subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@example.org',
                           '-c', 'commit.gpgsign=false', '-c', 'tag.gpgsign=false', '-c', 'core.autocrlf=false']
                          + list(args), cwd=cwd, check=True, capture_output=True, text=True, encoding='utf-8',
                          errors='replace').stdout


def write(cwd, path, text):
    full = os.path.join(cwd, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def commit(cwd, subject, path=None, text='x\n'):
    if path:
        write(cwd, path, text)
    git(cwd, 'add', '-A')
    git(cwd, 'commit', '-q', '-m', subject)
    return git(cwd, 'rev-parse', 'HEAD').strip()


# What gh answers for each pull request - its labels and its description - and the ones it cannot read.
# Nothing here asks GitHub: both of changelog.py's ways to gh are stood in for.
PULLS = {
    2: (['fix'], 'The ports come up at boot.\n\nFix for #1'),
    3: ([], 'Only the boards whose values have run.\n\nFix for #1\n\nNarrows what #2 did to two boards.'),
    4: (['docs'], 'Measured #2 and #3 on the lab.'),
}
UNREAD = set()


def pull(number):
    return None if number in UNREAD else PULLS.get(number, ([], ''))


def labels(number):
    got = pull(number)
    return None if got is None else got[0]


cl.pull, cl.labels = pull, labels


def run(*argv):
    """changelog.py's main() with argv: (its exit status, what it printed, what it said on stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            status = cl.main(list(argv))
        except Exception as e:  # the tool before a change may not know an option: a failure, not a stop
            status = 'raised %r' % e
    return status, out.getvalue(), err.getvalue()


def section(text, version):
    m = re.search(r'^## %s - \d{4}-\d{2}-\d{2}\n\n(.*?)(?=^## |\Z)' % re.escape(version), text, re.M | re.S)
    return m.group(1).strip('\n').split('\n') if m else None


def link(n):
    return '([#%d](https://github.com/%s/pull/%d))' % (n, cl.REPO, n)


with tempfile.TemporaryDirectory() as d:
    git(d, 'init', '-q', '-b', 'main')
    commit(d, 'A start', 'src/a.py')
    git(d, 'tag', 'v0.1.0-alpha.1')
    commit(d, 'driver: the ports come up at boot (#2)', 'src/b.py')
    commit(d, "Release the coprocessor's reset before the ports (#3)", 'src/c.py')
    commit(d, 'docs: the boot measured (#4)', 'docs/boot.md')
    commit(d, 'Site: a page of its own (#5)', 'website/pages/x.md')
    # A release's own commit before its tag is made - the moment between the merge and the tag.
    commit(d, 'Release 0.1.0-alpha.1.1 (#6)', 'CHANGELOG.md', '# Changelog\n\n## Unreleased\n\nNothing yet.\n')
    cl.ROOT, cl.FILE = pathlib.Path(d), pathlib.Path(d) / 'CHANGELOG.md'

    # Until 2026-11-06 every title that began with "Release " was taken for a release's own commit.
    status, out, _ = run('--due', '30', '10')
    check('a change whose title begins with Release waits for a release',
          (status, out.split('\n')[0], "- Release the coprocessor's reset before the ports %s" % link(3) in out),
          (1, 'Since v0.1.0-alpha.1: 1 fix(es), 1 improvement(s), the oldest improvement 0 day(s) old.', True))
    check("a release's own commit and site-only ones do not", ('alpha.1.1' in out, 'Site: a page' in out),
          (False, False))

    status, out, _ = run('--release', '0.1.0-alpha.2')
    with open(os.path.join(d, 'CHANGELOG.md'), encoding='utf-8') as f:
        written = f.read()
    check("release section: each change under its heading, one named Release among them",
          (status, section(written, '0.1.0-alpha.2')),
          (0, ['### Fixes', '', '- driver: the ports come up at boot %s' % link(2), '',
               '### Improvements and additions', '',
               "- Release the coprocessor's reset before the ports %s" % link(3), '',
               '### Documentation', '', '- docs: the boot measured %s' % link(4)]))

    # #3 fixes the issue #2 fixes, and its description names #2: #2's title may say more than the release
    # does. #4 names both, but pages alone describe a change and make none. Until 2026-11-06 --release named
    # nothing, and 0.1.0-alpha.14's line for #1048 - narrowed by #1049 - was put right by hand.
    check('--release names lines a later change of the same release may have made untrue',
          [line for line in out.split('\n') if line.startswith('  ')],
          ['  #2 driver: the ports come up at boot: #3 fixes #1 too and names it'])

    # --commit: the commit that added a version's section, whatever came after it on main.
    released = commit(d, 'Release 0.1.0-alpha.2 (#7)')
    commit(d, 'driver: after the release (#8)', 'src/d.py')
    with open(os.path.join(d, 'CHANGELOG.md'), encoding='utf-8') as f:
        text = f.read()
    write(d, 'CHANGELOG.md', text.replace('## Unreleased\n\nNothing yet.',
                                          '## Unreleased\n\n- driver: after the release', 1))
    commit(d, 'CHANGELOG: what waits (#9)')
    check('--commit: commit that added its section, not the newest',
          run('--commit', '0.1.0-alpha.2'), (0, released + '\n', ''))
    with open(os.path.join(d, 'CHANGELOG.md'), encoding='utf-8') as f:
        text = f.read()
    write(d, 'CHANGELOG.md', text.replace('## 0.1.0-alpha.2 - ', '## 0.1.0-alpha.2.1 - 2026-11-07\n\n- More\n\n'
                                          '## 0.1.0-alpha.2 - ', 1))
    small = commit(d, 'Release 0.1.0-alpha.2.1 (#10)')
    check('--commit: a version whose number another begins with keeps its own commit',
          (run('--commit', '0.1.0-alpha.2')[1], run('--commit', '0.1.0-alpha.2.1')[1]),
          (released + '\n', small + '\n'))
    status, out, err = run('--commit', '0.1.0-alpha.9')
    check('--commit: a version with no section gives nothing, and fails', (status, out, bool(err)), (1, '', True))

with tempfile.TemporaryDirectory() as d:
    git(d, 'init', '-q', '-b', 'main')
    commit(d, 'A start', 'src/a.py')
    git(d, 'tag', 'v0.1.0-alpha.1')
    commit(d, 'driver: the ports come up at boot (#2)', 'src/b.py')
    commit(d, 'driver: only two boards (#3)', 'src/c.py')
    cl.ROOT, cl.FILE = pathlib.Path(d), pathlib.Path(d) / 'CHANGELOG.md'
    UNREAD.add(3)
    status, out, _ = run('--release', '0.1.0-alpha.2')
    UNREAD.clear()
    check('a pull request gh cannot read is said, for its lines to be read by hand',
          (status, [line for line in out.split('\n') if line.startswith('  ')]),
          (0, ['  gh could not read #3, so the lines before it were not compared with it']))

# release.yml, which runs changelog.py: the tag goes on the commit --commit gives, a release is made from
# main alone and one run at a time, and the site is built again once the release is out.
with open(os.path.join(os.path.dirname(here), '.github', 'workflows', 'release.yml'), encoding='utf-8') as f:
    flow = f.read()
check("tag goes on the commit that added its version, not the run's own",
      ('target=$(python3 tools/changelog.py --commit "${VERSION}")' in flow, '--target "${target}"' in flow,
       'GITHUB_SHA' in flow), (True, True, False))
check('a release is made from main alone, one run at a time',
      (re.search(r"^    if: github\.ref == 'refs/heads/main'$", flow, re.M) is not None,
       re.search(r'^concurrency:\n  group: release\n  cancel-in-progress: false$', flow, re.M) is not None),
      (True, True))
check('site built again once a release is out',
      (re.search(r'^\s+run: gh workflow run website\.yml --ref main$', flow, re.M) is not None,
       re.search(r'^      actions: write\b', flow, re.M) is not None), (True, True))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
