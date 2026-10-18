#!/usr/bin/env python3
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
# SPDX-License-Identifier: BSD-2-Clause
"""Move Markdown pages and keep every relative link in the tree pointing where it did.

    move-doc-pages.py OLD=NEW [OLD=NEW ...]

Each pair is a path relative to the repository root. The page is moved with `git mv`, and
then every Markdown file in the tree - moved or not - has each relative link and image
re-pointed: a link is resolved from where the file was, mapped through the moves, and
written back relative to where the file is now. Links that resolve to nothing are left
alone and reported, so a link that was already broken is not hidden by the move. Run
tools/check-doc-links.py after.
"""
import os
import posixpath
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK = re.compile(r'(?<!\\)\]\(([^)\s]+)((?:\s+"[^"]*")?\))')
SKIP = ('http://', 'https://', 'mailto:', '#')


def pages():
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in ('.git', 'kernel-sources')]
        for f in files:
            if f.endswith('.md'):
                # Only a leading "./" goes: lstrip('./') took the dot of ".github" too, and the
                # pull request template was then looked for as "github/...".
                p = posixpath.join(os.path.relpath(base, ROOT).replace(os.sep, '/'), f)
                yield p[2:] if p.startswith('./') else p


def main(argv):
    moves = {}
    for arg in argv:
        if '=' not in arg:
            print(__doc__)
            return 2
        old, new = arg.split('=', 1)
        moves[old.strip('/')] = new.strip('/')
    if not moves:
        print(__doc__)
        return 2

    before = list(pages())
    for old, new in moves.items():
        os.makedirs(os.path.join(ROOT, os.path.dirname(new)), exist_ok=True)
        subprocess.check_call(['git', 'mv', old, new], cwd=ROOT)
        print(f'moved {old} -> {new}')

    unresolved = 0
    for was in before:
        now = moves.get(was, was)
        path = os.path.join(ROOT, now)
        text = open(path, encoding='utf-8').read()
        was_dir = posixpath.dirname(was)
        now_dir = posixpath.dirname(now)

        def fix(m):
            nonlocal unresolved
            target, tail = m.group(1), m.group(2)
            if target.startswith(SKIP):
                return m.group(0)
            link, frag = (target.split('#', 1) + [''])[:2]
            absolute = posixpath.normpath(posixpath.join(was_dir, link)) if link else ''
            if not link:
                return m.group(0)
            if absolute in moves:
                absolute = moves[absolute]
            elif not os.path.exists(os.path.join(ROOT, absolute)):
                print(f'  {now}: {target} resolved to nothing before the move; left as it was')
                unresolved += 1
                return m.group(0)
            fresh = posixpath.relpath(absolute, now_dir) if now_dir else absolute
            if link.endswith('/') and not fresh.endswith('/'):
                fresh += '/'
            if frag:
                fresh += '#' + frag
            return '](' + fresh + tail

        fixed = LINK.sub(fix, text)
        if fixed != text:
            open(path, 'w', encoding='utf-8', newline='').write(fixed)
            print(f'rewrote links in {now}')
    if unresolved:
        print(f'{unresolved} link(s) were already broken and are untouched')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
