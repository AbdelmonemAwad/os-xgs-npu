#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check that what the documents point at still exists.

The documents here argue from the tree: they name a file, a sysctl, a constant, and the reader is
expected to go and look. That is the method, and it is also the thing that rots first. A file is
renamed, a sysctl is spelled differently, a constant is folded into another - and the prose keeps
its old name and stays perfectly readable while pointing at nothing. A reader believes it, because
there is nothing in the sentence to suggest otherwise.

Three things are refused:

  1. a repository path named in a document that does not exist
  2. a `path:NNN` citation into this repository whose line number is past the end of that file, or
     whose backticked symbol has drifted away from the line being cited
  3. a `dev.<driver>.<n>.<path>` sysctl, or an `OCTEP_*`/`NPUEP_*`-style constant, that the named
     driver's sources do not contain

The third is the useful one in practice. The sysctl names are the repository's user interface - they
are what a reader types - and a leaf that was renamed in the driver leaves every document that cites
it quietly wrong.

What this cannot do is check a claim about behaviour. A page saying a module is never loaded, beside
a hook that loads it, is a defect no name check finds; that one is caught by opening the code the
sentence is about. See issue #96.

Paths outside this repository - the vendor's GPL tree, an appliance rootfs - cannot be checked from
here. They are skipped by name rather than silently: run with -v to list them.
"""

import os
import re
import sys

OURS = ('contrib/', 'src/', 'docs/', 'tools/', 'install/', '.github/')

REF = re.compile(r'(?<![\w/.-])((?:[\w.-]+/)+[\w.-]+\.(?:c|h|py|sh|md|json|xml|yml|yaml|conf|txt))'
                 r'(?::(\d+))?')
SYSCTL = re.compile(r'\bdev\.([a-z]+)\.\d+\.([a-z_]+(?:\.[a-z_]+)*)')
CONST = re.compile(r'\b((?:OCTEP|NPUEP|NPUGIU|NPUNWA)_[A-Z0-9_]{3,})\b')
FENCE = re.compile(r'^\s*```')

SKIP_DIRS = {'.git', 'node_modules', '__pycache__'}


def docs(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if name.endswith('.md') or name == 'pkg-descr':
                yield os.path.join(dirpath, name)


def driver_text(root, name):
    """Every C source and header of one driver, concatenated, or None if there is no such driver."""
    d = os.path.join(root, 'contrib', name)
    if not os.path.isdir(d):
        return None
    out = []
    for f in sorted(os.listdir(d)):
        if f.endswith(('.c', '.h')):
            with open(os.path.join(d, f), encoding='utf-8', errors='replace') as fh:
                out.append(fh.read())
    return '\n'.join(out)


def backticked(line):
    """Identifiers quoted on the same line - what a citation is usually there to support."""
    out = []
    for tok in re.findall(r'`([^`]{2,60})`', line):
        tok = tok.strip()
        if re.fullmatch(r'[A-Za-z_][\w.]*(?:\(\))?', tok) and '/' not in tok:
            out.append(tok.rstrip('()'))
    return out


def main():
    verbose = '-v' in sys.argv
    root = os.path.abspath(next((a for a in sys.argv[1:] if not a.startswith('-')), '.'))

    bad = []
    paths_checked = lines_checked = symbols_checked = 0
    sysctls, consts, outside = set(), set(), set()
    body_cache, driver_cache = {}, {}

    def body(path):
        if path not in body_cache:
            try:
                with open(path, encoding='utf-8', errors='replace') as fh:
                    body_cache[path] = fh.read().split('\n')
            except OSError:
                body_cache[path] = None
        return body_cache[path]

    def driver(name):
        if name not in driver_cache:
            driver_cache[name] = driver_text(root, name)
        return driver_cache[name]

    for doc in sorted(docs(root)):
        rel = os.path.relpath(doc, root).replace('\\', '/')
        with open(doc, encoding='utf-8') as fh:
            text = fh.read().split('\n')
        in_fence = False
        for n, line in enumerate(text, 1):
            if FENCE.match(line):
                in_fence = not in_fence
                continue

            for m in REF.finditer(line):
                ref, lineno = m.group(1), m.group(2)
                if not ref.startswith(OURS):
                    outside.add(ref)
                    continue
                paths_checked += 1
                src = body(os.path.join(root, ref))
                if src is None:
                    bad.append('%s:%d names %s, which does not exist' % (rel, n, ref))
                    continue
                if lineno is None:
                    continue
                want = int(lineno)
                lines_checked += 1
                if want < 1 or want > len(src):
                    bad.append('%s:%d cites %s:%d, but that file has %d lines'
                               % (rel, n, ref, want, len(src)))
                    continue
                if in_fence:
                    continue
                syms = backticked(line)
                if not syms:
                    continue
                window = '\n'.join(src[max(0, want - 4):want + 3])
                whole = '\n'.join(src)
                if not any(s in window for s in syms):
                    moved = [s for s in syms if s in whole]
                    if moved:
                        bad.append('%s:%d cites %s:%d for `%s`, which is in that file but not '
                                   'within three lines of %d - the citation has drifted'
                                   % (rel, n, ref, want, moved[0], want))
                else:
                    symbols_checked += 1

            for m in SYSCTL.finditer(line):
                drv, path = m.group(1), m.group(2)
                sysctls.add((drv, path))
                text_of = driver(drv)
                if text_of is None:
                    bad.append('%s:%d names dev.%s.0.%s, but contrib/%s does not exist'
                               % (rel, n, drv, path, drv))
                    continue
                for part in path.split('.'):
                    # A node or leaf is accepted if the driver spells it as a string anywhere: most
                    # are literals at the SYSCTL_ADD_* call, but the facility nodes come from a name
                    # table, so requiring the call site would report those as missing.
                    if not re.search(r'"%s"' % re.escape(part), text_of):
                        bad.append('%s:%d names dev.%s.0.%s, and "%s" appears nowhere in contrib/%s'
                                   % (rel, n, drv, path, part, drv))
                        break

            for m in CONST.finditer(line):
                name = m.group(1)
                consts.add(name)
                drv = {'OCTEP': 'octep'}.get(name.split('_')[0], 'npuep')
                text_of = driver(drv)
                if text_of and not re.search(r'\b%s\b' % re.escape(name), text_of):
                    bad.append('%s:%d names %s, which is not defined in contrib/%s'
                               % (rel, n, name, drv))

    if verbose and outside:
        print('outside this repository, not checked:')
        for s in sorted(outside):
            print('   ', s)

    for b in sorted(set(bad)):
        print('doc reference:', b)

    print('%d repository path(s) checked, %d with a line number and %d of those confirmed by the '
          'symbol beside them; %d sysctl name(s) and %d constant(s) resolved; %d path(s) outside '
          'the repository skipped.'
          % (paths_checked, lines_checked, symbols_checked, len(sysctls), len(consts), len(outside)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
