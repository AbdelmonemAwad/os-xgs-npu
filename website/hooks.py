#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""MkDocs hooks for npuforge.org.

The pages are written for GitHub, and GitHub's Markdown differs from Python-Markdown's in a few
places the pages use. Here those are made to read the same: a list straight after a line of text
starts a list, and GitHub's alerts (> [!NOTE]) become admonitions. The rest gives the templates
what they need: the home page's words from website/i18n/, the language switch, the release, and
the edit link of a page gathered from somewhere other than website/pages/.
"""
import json
import os
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
GH = 'https://github.com/AbdelmonemAwad/os-xgs-npu'
ITEM = re.compile(r'\s*(?:[-*+]|\d+[.)])\s')
ALERT = re.compile(r'^>\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*$')
ALERT_KIND = {'NOTE': 'note', 'TIP': 'tip', 'IMPORTANT': 'info', 'WARNING': 'warning', 'CAUTION': 'danger'}

catalogues = {}


def on_config(config):
    for p in sorted((HERE / 'i18n').glob('*.json')):
        catalogues[p.stem] = json.loads(p.read_text(encoding='utf-8'))
    config.extra['alternate'] = [
        {'name': catalogues[lang]['meta']['language'], 'link': link, 'lang': lang}
        for lang, link in (('en', '/'), ('ar', '/ar/')) if lang in catalogues
    ]
    config.extra['release'] = os.environ.get('NPUFORGE_RELEASE', '')
    config.extra['t'] = catalogues.get('en', {})
    return config


def github_markdown(markdown):
    out, fence, prev = [], None, ''
    lines = markdown.split('\n')
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.lstrip()
        mark = re.match(r'(`{3,}|~{3,})', stripped)
        if fence:
            if mark and stripped.startswith(fence):
                fence = None
            out.append(line)
            i += 1
            continue
        if mark:
            fence = mark.group(1)
        alert = ALERT.match(line)
        if alert:
            body = []
            i += 1
            while i < len(lines) and lines[i].startswith('>'):
                body.append(lines[i][1:][1:] if lines[i].startswith('> ') else lines[i][1:])
                i += 1
            if out and out[-1].strip():
                out.append('')
            out.append('!!! %s' % ALERT_KIND[alert.group(1)])
            out.append('')
            out += [('    ' + b) if b.strip() else '' for b in body]
            out.append('')
            prev = ''
            continue
        # GitHub lets a list interrupt a paragraph; Python-Markdown wants a blank line first.
        if (ITEM.match(line) and prev.strip() and not ITEM.match(prev) and not prev.startswith((' ', '\t'))
                and not prev.lstrip().startswith(('|', '#', '>', '<', '!!!'))):
            out.append('')
        out.append(line)
        prev = line
        i += 1
    return '\n'.join(out)


def on_page_markdown(markdown, page, config, files):
    return github_markdown(markdown)


def on_page_context(context, page, config, nav):
    if page.meta.get('edit_url'):
        page.edit_url = page.meta['edit_url']
    else:
        page.edit_url = GH + '/edit/main/website/pages/' + page.file.src_uri
    lang = page.meta.get('lang', 'en')
    context['t'] = catalogues.get(lang, catalogues.get('en', {}))
    context['lang'] = lang
    return context
