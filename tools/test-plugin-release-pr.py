#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/plugin-release-pr.py against a made-up plugin in a temporary git repository: a release is due
only when what an appliance installs changed since the newest tag, never while a release is under way,
and its version and changelog block are written as the plugins write them by hand.

    python3 tools/test-plugin-release-pr.py"""
import importlib.util
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

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
