#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Every file under src/ is installed by install/install.sh and taken away by install/uninstall.sh.

A file a pull request adds under src/ and leaves out of the installer is only on the boxes it was
copied to by hand: the coprocessor's console logger and its two boot hooks were in the tree from
2026-10-25 (#771) and on no appliance that an update or a ready-made image set up until 2026-10-27,
while the lab's, which had them by hand, logged their consoles. Nothing failed; a support bundle from
any other box simply had no console in it.

Read, not run: the two scripts are read for the paths they name under ${SRC}/src/ and ${PREFIX}/,
with each `for x in a b c` list put in for ${x}, and a glob matched against the tree. A file under
src/<path> is installed to ${PREFIX}/<path>; uninstall.sh takes it away if it names that path or a
directory above it. Exit 1 with every file either script misses.

    python3 tools/check-install-lists.py
"""

import fnmatch
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tree():
    out = subprocess.run(['git', '-C', ROOT, 'ls-files', 'src'], capture_output=True, text=True, check=True).stdout
    return [f[len('src/'):] for f in out.split() if '__pycache__' not in f]


def statements(path):
    """The script's lines with a backslash continuation joined, and its loops' words by variable."""
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        text = f.read().replace('\\\n', ' ')
    loops = {}
    for m in re.finditer(r'\bfor (\w+) in ([^;\n]+);\s*do\b', text):
        loops.setdefault(m.group(1), set()).update(m.group(2).split())
    return text, loops


def expand(pattern, loops):
    """A path with ${x} in it, once for each word x takes in any of the script's loops."""
    m = re.search(r'\$\{(\w+)\}', pattern)
    if not m or m.group(1) not in loops:
        return [pattern]
    out = []
    for word in sorted(loops[m.group(1)]):
        out.extend(expand(pattern[:m.start()] + word + pattern[m.end():], loops))
    return out


def named(text, loops, prefix):
    """Every path the script names after `prefix`, quotes taken out, loops and all."""
    paths = set()
    for m in re.finditer(re.escape(prefix) + r'"?/?([^\s;|&)<>]+)', text):
        raw = m.group(1).replace('"', '').rstrip('/')
        paths.update(expand(raw, loops))
    return paths


def main():
    files = tree()
    inst, inst_loops = statements('install/install.sh')
    uninst, uninst_loops = statements('install/uninstall.sh')
    sources = {p[len('src/'):] if p.startswith('src/') else None for p in named(inst, inst_loops, '${SRC}')}
    sources.discard(None)
    removed = named(uninst, uninst_loops, '${PREFIX}')
    not_installed = [f for f in files if not any(fnmatch.fnmatchcase(f, s) for s in sources)]
    not_removed = [f for f in files
                   if not any(fnmatch.fnmatchcase(f, r) or f.startswith(r + '/') for r in removed)]
    for f in not_installed:
        print('install/install.sh does not install src/%s' % f)
    for f in not_removed:
        print('install/uninstall.sh does not take away ${PREFIX}/%s' % f)
    if not_installed or not_removed:
        return 1
    print('%d file(s) under src/: every one installed by install.sh and taken away by uninstall.sh' % len(files))
    return 0


if __name__ == '__main__':
    sys.exit(main())
