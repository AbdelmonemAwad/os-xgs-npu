#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check that what the documents point at still exists.

The documents here argue from the tree: they name a file, a sysctl, a constant, and the reader is
expected to go and look. That is the method, and it is also the thing that rots first. A file is
renamed, a sysctl is spelled differently, a constant is folded into another - and the prose keeps
its old name and stays perfectly readable while pointing at nothing. A reader believes it, because
there is nothing in the sentence to suggest otherwise.

Four things are refused:

  1. a repository path named in a document that does not exist
  2. a `path:NNN` citation into this repository whose line number is past the end of that file, or
     whose backticked symbol has drifted away from the line being cited
  3. a `dev.<driver>.<n>.<path>` sysctl, or an `OCTEP_*`/`NPUEP_*`-style constant, that the named
     driver's sources do not contain
  4. with `--vendor <tree>`, a bare `giu_nic.c:1714`-style citation into somebody else's source
     that is past the end of that file, or whose symbol has moved away from it

The sysctl names are the part that earns it day to day. They are this repository's user interface -
what a reader actually types - and a leaf renamed in the driver leaves every document citing it
quietly wrong.

The fourth cannot run in CI, which has no vendor tree, and is for whoever holds one. Run it after
unpacking a different SDK drop: a new release renumbers every line of every file while every
citation here goes on reading perfectly plausibly.

    python3 tools/check-doc-references.py . --vendor path/to/the/unpacked/sources

What this cannot do is check a claim about behaviour. A page saying a module is never loaded, beside
a hook that loads it, is a defect no name check finds; that one is caught by opening the code the
sentence is about. See issue #96.
"""

import os
import re
import sys

OURS = ('contrib/', 'src/', 'docs/', 'tools/', 'install/', '.github/')

REF = re.compile(r'(?<![\w/.-])((?:[\w.-]+/)+[\w.-]+\.(?:c|h|py|sh|md|json|xml|yml|yaml|conf|txt))'
                 r'(?::(\d+))?')
# A citation into a file that is not in this repository, written the way those are written here:
# a bare basename and a line.
VENDOR_REF = re.compile(r'(?<![\w/.-])([A-Za-z0-9_-]+\.(?:c|h)):(\d+)(?!\d)')
SYSCTL = re.compile(r'\bdev\.([a-z]+)\.\d+\.([a-z_]+(?:\.[a-z_]+)*)')
CONST = re.compile(r'\b((?:OCTEP|NPUEP|NPUGIU|NPUNWA)_[A-Z0-9_]{3,})\b')
FENCE = re.compile(r'^\s*```')

SKIP_DIRS = {'.git', 'node_modules', '__pycache__'}

NEWLINE = chr(10)


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
    return NEWLINE.join(out)


def vendor_index(root):
    """Every .c and .h under a vendor tree, by basename. One basename can have several files."""
    index = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if name.endswith(('.c', '.h')):
                index.setdefault(name, []).append(os.path.join(dirpath, name))
    return index


def backticked(line):
    """Identifiers quoted on the same line - what a citation is usually there to support."""
    out = []
    for tok in re.findall(r'`([^`]{2,60})`', line):
        tok = tok.strip()
        if re.fullmatch(r'[A-Za-z_][\w.]*(?:\(\))?', tok) and '/' not in tok:
            out.append(tok.rstrip('()'))
    return out


def near(src, want, syms):
    """Is any of these symbols within three lines of the cited one? A sysctl cited by its path,
    `rpc.allow_write`, is defined by its quoted leaf - `OID_AUTO, "allow_write"` - so that
    spelling counts too."""
    window = NEWLINE.join(src[max(0, want - 4):want + 3])
    for s in syms:
        if s in window:
            return True
        if '.' in s and re.search(r'OID_AUTO,\s*"' + re.escape(s.rsplit('.', 1)[-1]) + '"', window):
            return True
    return False


def main():
    argv = sys.argv[1:]
    verbose = '-v' in argv
    vendor_root = None
    if '--vendor' in argv:
        i = argv.index('--vendor')
        if i + 1 >= len(argv):
            print('--vendor needs the path of a vendor source tree')
            return 2
        vendor_root = os.path.abspath(argv[i + 1])
        del argv[i:i + 2]
    root = os.path.abspath(next((a for a in argv if not a.startswith('-')), '.'))

    bad = []
    paths_checked = lines_checked = symbols_checked = 0
    vendor_checked = vendor_absent = vendor_confirmed = 0
    sysctls, consts, outside = set(), set(), set()
    body_cache, driver_cache = {}, {}
    vendor = vendor_index(vendor_root) if vendor_root else None

    def body(path):
        if path not in body_cache:
            try:
                with open(path, encoding='utf-8', errors='replace') as fh:
                    body_cache[path] = fh.read().split(NEWLINE)
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
            text = fh.read().split(NEWLINE)
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
                if not syms and n > 1 and not FENCE.match(text[n - 2]) and \
                   not [t for t in re.findall(r'`([^`]+)`', line) if '/' not in t]:
                    # A sentence that wraps puts the symbol on one line and the citation on the
                    # next - "because `octep_nwa_do_request()` sleeps" / "(`contrib/...:605`)".
                    # Read as one line, the citation was checked; read as two, it was not, and
                    # one stayed stale through a pull request that moved the function (#283).
                    # Only when this line quotes nothing but paths: a line quoting its own
                    # expression - `wakeup(&sc->busy);` - is about that, not about the line
                    # before, and reading the previous line's symbol into it flagged the
                    # driver's name as a drifted citation.
                    syms = backticked(text[n - 2])
                if not syms:
                    continue
                if near(src, want, syms):
                    symbols_checked += 1
                else:
                    moved = [s for s in syms if s in NEWLINE.join(src)]
                    if moved:
                        bad.append('%s:%d cites %s:%d for `%s`, which is in that file but not '
                                   'within three lines of %d - the citation has drifted'
                                   % (rel, n, ref, want, moved[0], want))

            if vendor is not None:
                for m in VENDOR_REF.finditer(line):
                    name, want = m.group(1), int(m.group(2))
                    cands = vendor.get(name)
                    if not cands:
                        vendor_absent += 1
                        if verbose:
                            print('vendor: %s:%d cites %s, which is not in the tree given'
                                  % (rel, n, name))
                        continue
                    vendor_checked += 1
                    fits = [(c, body(c)) for c in cands
                            if body(c) is not None and 1 <= want <= len(body(c))]
                    if not fits:
                        longest = max((len(body(c)) for c in cands if body(c)), default=0)
                        bad.append('%s:%d cites %s:%d, and no file of that name in the vendor tree '
                                   'has that many lines - the longest has %d'
                                   % (rel, n, name, want, longest))
                        continue
                    syms = backticked(line)
                    if not syms or in_fence:
                        continue
                    if any(near(src, want, syms) for _, src in fits):
                        vendor_confirmed += 1
                    else:
                        present = [s for s in syms
                                   if any(s in NEWLINE.join(src) for _, src in fits)]
                        if present:
                            bad.append('%s:%d cites %s:%d for `%s`, which is in that file but not '
                                       'within three lines of %d - the citation has drifted'
                                       % (rel, n, name, want, present[0], want))

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
                    if not re.search('"%s"' % re.escape(part), text_of):
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
        print('outside this repository, not checked as paths:')
        for s in sorted(outside):
            print('   ', s)

    for b in sorted(set(bad)):
        print('doc reference:', b)

    print('%d repository path(s) checked, %d with a line number and %d of those confirmed by the '
          'symbol beside them; %d sysctl name(s) and %d constant(s) resolved; %d path(s) outside '
          'the repository skipped.'
          % (paths_checked, lines_checked, symbols_checked, len(sysctls), len(consts), len(outside)))
    if vendor is not None:
        print('%d vendor citation(s) found in %s, %d of them confirmed by the symbol beside them; '
              '%d named a file that tree does not have.'
              % (vendor_checked, vendor_root, vendor_confirmed, vendor_absent))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
