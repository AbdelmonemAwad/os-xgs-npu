#!/usr/bin/env python3
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
# SPDX-License-Identifier: BSD-2-Clause
"""Write docs/images/README.md: every image in docs/images, by folder, with what it shows and the
pages that use it.

    python3 tools/images-index.py           write it
    python3 tools/images-index.py --check   exit 1 if the file is not what this would write

What an image shows is the alt text it is given by the first page that shows it, in the order the
pages are walked; a page that only links to it counts as using it; an image no page uses is listed as such, so a photograph that lost its page is
seen. The photographs of the appliances and of the web interface are shown as thumbnails, the
measurements' photographs as a list beside the page they belong to. The page keeps the review date
it already has, so a check in CI does not depend on the day it runs.
"""
import datetime
import os
import posixpath
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGES = 'docs/images'
OUT = IMAGES + '/README.md'
EXT = ('.png', '.jpg', '.jpeg', '.svg', '.gif', '.webp')
MD_IMG = re.compile(r'!\[([^\]]*)\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
MD_LINK = re.compile(r'(?<![!\\])\[[^\]]*\]\(([^)\s]+\.(?:png|jpe?g|svg|gif|webp))(?:\s+"[^"]*")?\)', re.I)
HTML_IMG = re.compile(r'<img\b[^>]*>', re.I)
ATTR = re.compile(r'(\w+)="([^"]*)"')

FOLDERS = {
    'hardware': 'Photographs and drawings of the appliances, one folder each',
    'hardware/xgs3300': 'The XGS 3300: the board, its parts and its front panel',
    'hardware/xgs136': 'The XGS 136: the board, its parts, and the USB adapter that kept it reachable',
    'hardware/xg330': 'The XG 330, where the story starts',
    'hardware/bench': 'The bench the appliances are measured on',
    'measurements': "Each measurement's photographs, in a folder named after its page",
    'screens': 'The web interface, page by page, in English and in Arabic',
    'readme': "The README's banner and status board, drawn by tools/readme-art.py",
}


def md_pages():
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = sorted(d for d in dirs if d not in ('.git', 'kernel-sources', 'node_modules'))
        for f in sorted(files):
            if f.endswith('.md'):
                p = os.path.relpath(os.path.join(base, f), ROOT).replace(os.sep, '/')
                if p != OUT:
                    yield p


def references():
    """{image path from the root: [(alt, page), ...]} for every image a page shows."""
    refs = {}
    for page in md_pages():
        text = open(os.path.join(ROOT, page), encoding='utf-8').read()
        found = [(m.group(1), m.group(2)) for m in MD_IMG.finditer(text)]
        # A link to an image uses it too - a table's "in" and "out" - but its text is no caption.
        found += [('', m.group(1)) for m in MD_LINK.finditer(text)]
        for tag in HTML_IMG.findall(text):
            a = dict((k.lower(), v) for k, v in ATTR.findall(tag))
            if 'src' in a:
                found.append((a.get('alt', ''), a['src']))
        for src in re.findall(r'srcset="([^"]+)"', text):
            found.append(('', src))
        for alt, src in found:
            if src.startswith(('http://', 'https://', 'data:')):
                continue
            # A page in another language, under website/i18n/, names an image from the site's root.
            src = src.split('#')[0]
            path = posixpath.normpath(src[1:] if src.startswith('/')
                                      else posixpath.join(posixpath.dirname(page), src))
            if path.startswith(IMAGES + '/'):
                refs.setdefault(path, []).append((alt.strip(), page))
    return refs


def images():
    out = []
    for base, dirs, files in os.walk(os.path.join(ROOT, IMAGES)):
        dirs.sort()
        for f in sorted(files):
            if f.lower().endswith(EXT):
                out.append(os.path.relpath(os.path.join(base, f), ROOT).replace(os.sep, '/'))
    return out


def rel(path):
    """A path from docs/images/README.md."""
    return posixpath.relpath(path, IMAGES)


def link_page(page):
    return f'[{posixpath.basename(page)}]({posixpath.relpath(page, IMAGES)})'


def caption(path, refs):
    for alt, _ in refs.get(path, []):
        if alt:
            return alt.replace('|', '&#124;')
    return ''


def used_on(path, refs):
    pages = []
    for _, page in refs.get(path, []):
        if page not in pages:
            pages.append(page)
    return ', '.join(link_page(p) for p in pages) if pages else '*no page yet*'


def thumbs(paths, refs, per_row=4):
    rows = ['<table>']
    for i in range(0, len(paths), per_row):
        rows.append('<tr>')
        for p in paths[i:i + per_row]:
            cap = caption(p, refs) or posixpath.basename(p)
            rows.append(f'<td width="{100 // per_row}%" valign="top"><a href="{rel(p)}"><img src="{rel(p)}" '
                        f'alt="{cap.replace(chr(34), "&quot;")}" width="100%"></a><br><sub>{cap}</sub></td>')
        rows.append('</tr>')
    rows.append('</table>')
    return '\n'.join(rows)


def render(old):
    refs = references()
    imgs = images()
    m = re.match(r'<!-- reviewed: (\d{4}-\d{2}-\d{2}) -->', old or '')
    reviewed = m.group(1) if m else datetime.date.today().isoformat()
    by_dir = {}
    for p in imgs:
        by_dir.setdefault(posixpath.dirname(rel(p)), []).append(p)

    L = [f'<!-- reviewed: {reviewed} -->', '# Images', '',
         'Every image in the repository, in one place and by kind. The pages use them from here, and so',
         'does the [wiki](https://github.com/AbdelmonemAwad/os-xgs-npu/wiki), which keeps no images of its',
         'own. Each photograph was taken on the appliances themselves; what each one shows is the alt text',
         'its page gives it, and the pages that use it are named beside it.', '',
         'This page is written by `tools/images-index.py`, and the checks fail when it is out of date: add an',
         'image to its folder, use it on a page, and run the tool.', '',
         '**Licence.** The photographs and screenshots are &copy; 2026 Abdelmonem Awad, under',
         '[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/): use them freely, naming the author and',
         'linking here. Each carries that notice in a strip added below it - never over the picture, so a',
         'measurement is still what the screen showed - and in its metadata: `tools/mark-images.py` adds both',
         'to a new image, and the checks fail on one without them.', '',
         '| folder | what it holds | images |', '|---|---|---|']
    for top in ('screens', 'hardware', 'measurements', 'readme'):
        n = sum(1 for p in imgs if rel(p).startswith(top + '/'))
        L.append(f'| [{top}/]({top}/) | {FOLDERS[top]} | {n} |')
    L += ['', f'{len(imgs)} images in all.', '']

    # The web interface, English beside Arabic.
    L += ['## The web interface', '',
          'Full screen, dark theme, photographed on 2026-10-19 on the second XGS 3300. Each page is described',
          'in [docs/guide/the-pages.md](../guide/the-pages.md), English first and Arabic under it.', '',
          '<table>', '<tr><th width="20%">page</th><th width="40%">English</th><th width="40%">Arabic</th></tr>']
    en = by_dir.get('screens/en', [])
    for p in en:
        name = posixpath.basename(p)
        ar = IMAGES + '/screens/ar/' + name
        cap = caption(p, refs).replace(', in English', '') or name
        cell_ar = (f'<a href="{rel(ar)}"><img src="{rel(ar)}" alt="{cap} - Arabic" width="100%"></a>'
                   if ar in imgs else '<sub>English only</sub>')
        L.append(f'<tr><td valign="top"><sub>{cap}</sub></td><td valign="top"><a href="{rel(p)}"><img src="{rel(p)}" '
                 f'alt="{cap} - English" width="100%"></a></td><td valign="top">{cell_ar}</td></tr>')
    L += ['</table>', '']

    # The appliances.
    L += ['## Hardware', '']
    for d in sorted(k for k in by_dir if k.startswith('hardware/')):
        paths = by_dir[d]
        L += [f'### {d}/ - {FOLDERS.get(d, "")} ({len(paths)})', '', thumbs(paths, refs), '']

    # The measurements, a list per folder beside its page.
    L += ['## Measurements', '',
          "Each folder holds one measurement's photographs - the windows the runs were taken in, as they",
          'finished - and is named after the page that tables them.', '']
    for d in sorted(k for k in by_dir if k.startswith('measurements/')):
        paths = by_dir[d]
        page = 'docs/measurements/' + d.split('/')[1] + '.md'
        head = f'### {d}/ ({len(paths)})'
        L += [head, '']
        if os.path.exists(os.path.join(ROOT, page)):
            L += [f'The page: {link_page(page)}.', '']
        L += ['| image | what it shows | used on |', '|---|---|---|']
        for p in paths:
            L.append(f'| [{posixpath.basename(p)}]({rel(p)}) | {caption(p, refs)} | {used_on(p, refs)} |')
        L.append('')

    L += ["## The README's drawings", '', '| image | used on |', '|---|---|']
    for p in by_dir.get('readme', []):
        L.append(f'| [{posixpath.basename(p)}]({rel(p)}) | {used_on(p, refs)} |')
    L.append('')

    unused = [p for p in imgs if p not in refs]
    L += ['## Not on any page yet', '']
    if unused:
        L += ['Kept for a page that will use them - or for the wiki, which links here:', '']
        L += [f'- [{rel(p)}]({rel(p)})' for p in unused]
    else:
        L.append('Every image here is used by a page.')
    L.append('')
    return '\n'.join(L)


def main(argv):
    path = os.path.join(ROOT, OUT)
    old = open(path, encoding='utf-8').read() if os.path.exists(path) else ''
    new = render(old)
    if '--check' in argv:
        if old != new:
            print(f'{OUT} is out of date: run python3 tools/images-index.py')
            return 1
        print(f'{OUT} lists every image')
        return 0
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(new)
    print(f'wrote {OUT}: {len(images())} images')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
