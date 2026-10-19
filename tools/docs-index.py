#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The documentation's catalogue and its review schedule.

    tools/docs-index.py                  write docs/INDEX.md
    tools/docs-index.py --check          fail if docs/INDEX.md is not what this would write
    tools/docs-index.py --due YYYY-MM-DD list the pages whose review is due by that day, as Markdown
    tools/docs-index.py --stamp FILE...  set the pages' reviewed date to today (UTC)

Every page under docs/ opens with one line, an HTML comment nobody reading the page sees:

    <!-- reviewed: 2026-10-18 -->

the last day someone read the page against the tree and the appliances and either corrected it or
found nothing to correct. How long a page may go before it is read again depends on what kind of page
it is - docs/REVIEW.md says which, and why - and is the CADENCE table below. A page with no line is
listed as never reviewed and is due at once.

The catalogue is written from the pages alone, never from git or the clock, so it changes only when a
page or its line does, and --check can hold it to the tree in CI. The day is only ever an argument:
the monthly workflow passes today's date to --due and keeps one issue open with the answer.
"""

import datetime
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOCS = ROOT / 'docs'
INDEX = DOCS / 'INDEX.md'
MARK = re.compile(r'^<!-- reviewed: (\d{4}-\d{2}-\d{2}) -->\s*$')

# How many days a page may go unread, by the folder it is in. The reasons are docs/REVIEW.md's.
CADENCE = {
    '': 60,             # the pages at the top of docs/: the hub, the road, provenance
    'guide': 60,        # what the README used to carry: the first thing anyone reads
    'families': 90,     # what each family is and how far along it is
    'hardware': 90,
    'npuep': 90,
    'octep': 90,
    'octeontx': 90,
    'measurements': 180,  # a measurement does not change; whether it is still the current one does
    'findings': 365,      # dated records, never rewritten: read for links and later corrections
}
KIND = {
    '': 'the hub and the pages beside it',
    'guide': 'the guide - what the README used to carry',
    'families': 'the coprocessor families',
    'hardware': 'the appliances as objects',
    'npuep': 'the ARMADA driver',
    'octep': 'the OCTEON TX driver',
    'octeontx': 'OCTEON TX provenance',
    'measurements': 'the measurements',
    'findings': 'the findings, dated',
}
ORDER = ['', 'guide', 'families', 'hardware', 'npuep', 'octep', 'octeontx', 'measurements', 'findings']


def pages():
    for p in sorted(DOCS.rglob('*.md'), key=lambda p: p.as_posix()):   # as Linux orders them, on Windows too
        if p == INDEX:
            continue
        rel = p.relative_to(DOCS)
        folder = rel.parts[0] if len(rel.parts) > 1 else ''
        text = p.read_text(encoding='utf-8')
        first = text.split('\n', 1)[0]
        m = MARK.match(first)
        reviewed = datetime.date.fromisoformat(m.group(1)) if m else None
        title = next((l[2:].strip() for l in text.split('\n') if l.startswith('# ')), rel.stem)
        title = re.sub(r'\s+', ' ', title)
        yield {'path': rel.as_posix(), 'folder': folder, 'title': title, 'reviewed': reviewed,
               'days': CADENCE.get(folder, 90)}


def due_on(page):
    if page['reviewed'] is None:
        return None
    return page['reviewed'] + datetime.timedelta(days=page['days'])


def cap(s):
    return s[:1].upper() + s[1:]


def catalogue():
    """One table for every page, so every row has the same columns at the same width - nine tables,
    one per kind, each came out as wide as its own longest title. A kind is a row of its own inside
    it, with an anchor the line at the top jumps to."""
    rows = list(pages())
    folders = sorted({r['folder'] for r in rows}, key=lambda f: ORDER.index(f) if f in ORDER else len(ORDER))
    first = min((r for r in rows if due_on(r)), key=lambda r: (due_on(r), r['path']), default=None)
    out = ['<!-- reviewed: generated -->',
           '# Every page, and when it is read again',
           '',
           '> Written by `tools/docs-index.py` from the pages themselves - do not edit it by hand, run the',
           '> tool. Each page\'s date is the line at its top; how long it may go unread is set by its kind,',
           '> in [REVIEW.md](REVIEW.md). Where to start reading is [README.md](README.md).',
           '',
           f'**{len(rows)} pages** in {len(folders)} kinds.'
           + (f' The next one due is [{first["title"]}]({first["path"]}), on {due_on(first)}.' if first else ''),
           '',
           '**Jump to:** ' + ' &middot; '.join(f'[{cap(KIND.get(f, f))}](#kind-{f or "top"})' for f in folders),
           '',
           '| | page | pages | read every | reviewed | due |',
           '|--:|:---|--:|--:|:-:|:-:|']
    n = 0
    for f in folders:
        group = [r for r in rows if r['folder'] == f]
        where = f'docs/{f}/' if f else 'docs/'
        out.append(f'| | <a name="kind-{f or "top"}"></a>**{cap(KIND.get(f, f))}** &nbsp;<sub>`{where}`</sub> '
                   f'| **{len(group)}** | **{CADENCE.get(f, 90)} days** | | |')
        for r in group:
            n += 1
            d = due_on(r)
            title = r['title'].replace('|', '\\|')
            out.append(f'| {n} | [{title}]({r["path"]}) | | | {r["reviewed"] or "never"} | {d or "now"} |')
    out.append('')
    return '\n'.join(out)


def main(argv):
    if '--check' in argv:
        want = catalogue()
        have = INDEX.read_text(encoding='utf-8') if INDEX.exists() else ''
        if have != want:
            print('docs/INDEX.md is not what tools/docs-index.py writes - run it and commit the result')
            return 1
        print(f'docs/INDEX.md lists all {sum(1 for _ in pages())} pages as they stand')
        return 0
    if '--due' in argv:
        day = datetime.date.fromisoformat(argv[argv.index('--due') + 1])
        late = [r for r in pages() if r['reviewed'] is None or due_on(r) <= day]
        late.sort(key=lambda r: (due_on(r) or datetime.date.min, r['path']))
        # In an issue a relative link resolves against the issue, so the workflow's run links each
        # page in full; run by hand it stays relative to the repository.
        repo = os.environ.get('GITHUB_REPOSITORY')
        server = os.environ.get('GITHUB_SERVER_URL', 'https://github.com')
        base = f'{server}/{repo}/blob/main/' if repo else ''
        for r in late:
            d = due_on(r)
            when = f'due {d}' if d else 'never reviewed'
            print(f'- [ ] [{r["title"]}]({base}docs/{r["path"]}) - {when}, read every {r["days"]} days')
        return 0
    if '--stamp' in argv:
        today = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
        for name in argv[argv.index('--stamp') + 1:]:
            p = pathlib.Path(name)
            text = p.read_text(encoding='utf-8')
            first, _, rest = text.partition('\n')
            line = f'<!-- reviewed: {today} -->'
            text = (line + '\n' + rest) if MARK.match(first) else (line + '\n' + text)
            p.write_text(text, encoding='utf-8', newline='\n')
        return 0
    INDEX.write_text(catalogue(), encoding='utf-8', newline='\n')
    print(f'wrote docs/INDEX.md: {sum(1 for _ in pages())} pages')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
