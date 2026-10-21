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
import subprocess
import sys
from urllib.parse import urljoin

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import devices  # noqa: E402 - website/devices.py, beside this file
GH = 'https://github.com/AbdelmonemAwad/os-xgs-npu'
ITEM = re.compile(r'\s*(?:[-*+]|\d+[.)])\s')
ALERT = re.compile(r'^>\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*$')
ALERT_KIND = {'NOTE': 'note', 'TIP': 'tip', 'IMPORTANT': 'info', 'WARNING': 'warning', 'CAUTION': 'danger'}
NOT_A_HEADING = re.compile(r' {0,3}#{1,6}[^\s#]')

catalogues = {}
# English, then the languages build.py builds a site of its own for (its LANGS), in the order the
# language switch lists them; a language is shown when its catalogue is in website/i18n/.
LANGS = ['en', 'ar', 'de', 'es', 'fr', 'tr', 'ru', 'ja']


def home_of(lang):
    return '/' if lang == 'en' else '/%s/' % lang


def on_config(config):
    for p in sorted((HERE / 'i18n').glob('*.json')):
        catalogues[p.stem] = json.loads(p.read_text(encoding='utf-8'))
    config.extra['alternate'] = [
        {'name': catalogues[lang]['meta']['language'], 'link': home_of(lang), 'lang': lang,
         'locale': catalogues[lang]['meta'].get('locale', lang)}
        for lang in LANGS if lang in catalogues
    ]
    config.extra['release'] = os.environ.get('NPUFORGE_RELEASE', '')
    try:
        config.extra['facts'] = json.loads(os.environ.get('NPUFORGE_FACTS') or '{}')
    except ValueError:
        config.extra['facts'] = {}
    # The home page's repositories and their counts, as build.py read them from GitHub.
    try:
        config.extra['repos'] = json.loads(os.environ.get('NPUFORGE_REPOS') or '{}')
    except ValueError:
        config.extra['repos'] = {}
    config.extra['t'] = catalogues.get('en', {})
    # Every model, as tools/devices-table.py writes it from scripts/xgs/models.py and tools/device_facts.py -
    # the home page's table; devices.py writes the pages from the same file.
    try:
        config.extra['devices'] = json.loads((HERE / 'data' / 'devices.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        config.extra['devices'] = {}
    # The home page's picture, the two appliances run here drawn, in each language's words.
    config.extra['hero'] = {}
    if config.extra['devices'].get('pages'):
        for lang in LANGS:
            base = home_of(lang).rstrip('/')
            if lang in catalogues and 'devices' in catalogues[lang]:
                config.extra['hero'][lang] = devices.hero_svg(config.extra['devices'], catalogues[lang]['devices'], base)
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
        # GitHub makes a heading of a line of #s only when a space follows them; Python-Markdown
        # makes one of "#165 open for a week" too - a wrapped line that starts with an issue's number.
        if NOT_A_HEADING.match(line):
            line = re.sub(r'^( {0,3})#', r'\1\\#', line)
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
    # A page that is another's opening (a measurement's news post) names that page as the one to index.
    if page.meta.get('canonical'):
        page.canonical_url = page.meta['canonical']
    lang = page.meta.get('lang', 'en')
    context['t'] = catalogues.get(lang, catalogues.get('en', {}))
    context['lang'] = lang
    context['seo'] = seo(page, config, lang)
    return context


# ------------------------------------------------------------------------------ for search engines
#
# What a search engine reads about a page beyond its words: a description of its own, the day it
# last changed, the picture it leads with, the same page in the other languages, and the structured
# data (schema.org, as JSON-LD) that says what it is. main.html writes them into the page's head;
# build.py makes each language's sitemap from what is recorded here.

SITE = 'https://npuforge.org'
ROOT = HERE.parent
TAG = re.compile(r'<[^>]+>')
FIRST_P = re.compile(r'<p>(.*?)</p>', re.S)
FIRST_IMG = re.compile(r'<img [^>]*src="([^"]+)"')
dates = {}          # repository path -> its last commit, ISO 8601 with the zone; 'wiki:' + page for the wiki's
english_of = {}     # a translated page's path -> the English page's path
versions = {}       # an English page's path -> {language: that page's path in it}, English included
recorded = {}       # canonical URL -> {lastmod, lang, alternates}, for build.py's sitemap


def git_dates():
    """Every file's last commit time, in one pass over the history (the workflow checks it out whole)."""
    if dates:
        return
    try:
        out = subprocess.run(['git', 'log', '--format=@%cI', '--name-only', '--no-renames'], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return
    day = ''
    for line in out.splitlines():
        if line.startswith('@'):
            day = line[1:]
        elif line and line not in dates:
            dates[line] = day
    # The story's pages are the wiki's; build.py says where its checkout is.
    wiki = os.environ.get('NPUFORGE_WIKI')
    if wiki and os.path.isdir(wiki):
        try:
            out = subprocess.run(['git', 'log', '--format=@%cI', '--name-only', '--no-renames'], cwd=wiki,
                                 capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            return
        for line in out.splitlines():
            if line.startswith('@'):
                day = line[1:]
            elif line.endswith('.md') and 'wiki:' + line[:-3] not in dates:
                dates['wiki:' + line[:-3]] = day


def translations():
    """The pages that exist in more than one language: the models', the modules', the status and the
    releases pages, which build.py writes in every language, and each translated page, which names the
    English one it translates."""
    if english_of:
        return
    try:
        data = json.loads((HERE / 'data' / 'devices.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        data = {}
    written = (['devices/'] + ['devices/%s/' % p['slug'] for p in data.get('pages', [])] +
               ['modules/'] + ['modules/%s/' % m['slug'] for m in data.get('modules', [])] +
               ['status/', 'releases/'])
    for lang in LANGS[1:]:
        if lang not in catalogues:
            continue
        english_of[home_of(lang)] = '/'
        for path in written:
            english_of[home_of(lang) + path] = '/' + path
        pages = HERE / 'i18n' / lang
        for f in sorted(pages.rglob('*.md')) if pages.exists() else []:
            m = re.search(r'^source: *(\S+)', f.read_text(encoding='utf-8'), re.M)
            if not m:
                continue
            src = m.group(1)
            en = '/' + (src[len('website/pages/'):] if src.startswith('website/pages/') else src)
            en = re.sub(r'(?:index|README)?\.md$', '', en)
            en = en if en.endswith('/') else en + '/'
            english_of[home_of(lang) + re.sub(r'(?:^|(?<=/))index/$', '', re.sub(r'\.md$', '/', f.relative_to(pages).as_posix()))] = en
    for path, en in english_of.items():
        versions.setdefault(en, {'en': en})[path.split('/')[1]] = path


def source_of(page):
    """The repository file a page was made from, by its edit link, or None (the wiki's, generated)."""
    m = re.match(re.escape(GH) + r'/edit/main/(.+)$', page.meta.get('edit_url') or '')
    if m:
        return m.group(1)
    m = re.match(re.escape(GH) + r'/wiki/([^/]+)/_edit$', page.meta.get('edit_url') or '')
    if m:
        return 'wiki:' + m.group(1)
    if page.meta.get('edit_url'):
        return None
    return 'website/pages/' + page.file.src_uri


# Paragraphs that say what a page is rather than what is in it: a move, a review, a translation.
NOT_A_SUMMARY = ('Moved here', 'Last changed', '\u062a\u0631\u062c\u0645\u0629\u0020\u0635\u0641\u062d\u0629')


def describe(html, limit=158):
    """The page's own first paragraphs, cut at a word, as its description: one that is short or
    leads into a list or a table (it ends in a colon) is joined by the next."""
    text = ''
    for m in FIRST_P.finditer(html):
        para = ' '.join(TAG.sub('', m.group(1)).split())
        para = para.replace('&amp;', '&').replace('&quot;', '"').replace('&#39;', "'").replace('&lt;', '<').replace('&gt;', '>')
        if len(para) < 20 or para.startswith(NOT_A_SUMMARY):
            continue
        text = (text + ' ' + para).strip()
        if len(text) >= 90 and not text.endswith(':'):
            break
    return cut(text, limit)


def cut(text, limit):
    """A description no longer than a search engine shows of one, cut at a word."""
    if len(text) > limit:
        text = text[:limit].rsplit(' ', 1)[0].rstrip(',;:-') + '…'
    return text


def image_size(path):
    """(width, height) of a PNG or a JPEG from its own header, or None."""
    try:
        with open(path, 'rb') as f:
            head = f.read(26)
            if head[:8] == b'\x89PNG\r\n\x1a\n':
                return int.from_bytes(head[16:20], 'big'), int.from_bytes(head[20:24], 'big')
            if head[:2] != b'\xff\xd8':
                return None
            f.seek(2)
            while True:
                marker = f.read(2)
                if len(marker) < 2 or marker[0] != 0xFF:
                    return None
                if marker[1] in (0xD8, 0x01) or 0xD0 <= marker[1] <= 0xD7:
                    continue
                size = int.from_bytes(f.read(2), 'big')
                if marker[1] in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                    f.read(1)
                    h, w = int.from_bytes(f.read(2), 'big'), int.from_bytes(f.read(2), 'big')
                    return w, h
                f.seek(size - 2, 1)
    except OSError:
        return None


IMG = re.compile(r'<img ([^>]*?)\s*/?>')


def sized(html, page, config):
    """Every picture in a page's text told its size, so the page does not jump as it loads, and all
    but the first left until it is scrolled to. A picture's address is relative to where the page is
    served, which is its URL's folder under the pages."""
    base = os.path.join(config['docs_dir'], page.url)
    first = [True]

    def one(m):
        attrs = m.group(1)
        src = re.search(r'src="([^"]+)"', attrs)
        extra = ''
        if src and 'width=' not in attrs and not re.match(r'^[a-z]+:|^/', src.group(1)):
            wh = image_size(os.path.normpath(os.path.join(base, src.group(1).split('#')[0])))
            if wh:
                extra += ' width="%d" height="%d"' % wh
        if 'loading=' not in attrs:
            extra += ' decoding="async"' + ('' if first[0] else ' loading="lazy"')
        first[0] = False
        return '<img %s%s />' % (attrs, extra)
    return IMG.sub(one, html)


def on_page_content(html, page, config, files):
    html = sized(html, page, config)
    if not page.meta.get('description'):
        d = describe(html)
        if d:
            page.meta['description'] = d
    m = FIRST_IMG.search(html)
    if m and not page.meta.get('image'):
        src = m.group(1)
        if not re.match(r'^[a-z]+:', src):
            base = config['site_url'] + page.url
            src = urljoin(base, src)
        if src.startswith(SITE) and re.search(r'\.(png|jpe?g|webp)$', src, re.I):
            page.meta['image'] = src
    return html


def seo(page, config, lang):
    git_dates()
    translations()
    url = page.canonical_url or (config['site_url'] + page.url)
    path = url[len(SITE):] if url.startswith(SITE) else '/' + page.url
    src = source_of(page)
    # A news post's own date (Material gives it as a date, or as {created: ...}), else the source's.
    d = page.meta.get('date')
    if isinstance(d, dict):
        d = d.get('updated') or d.get('created')
    # as search engines take a date: with its time and zone - a post's day at its start, in UTC; a
    # file's last commit as git has it
    published = str(d)[:10] + 'T00:00:00+00:00' if d else None
    modified = published
    if src and src in dates and (not modified or dates[src] > modified):
        modified = dates[src]
    english = path if lang == 'en' else english_of.get(path)
    others = versions.get(english, {})
    alternates = [(lang, SITE + path)] + [(other, SITE + others[other]) for other in LANGS if other != lang and other in others]
    if english:
        alternates.append(('x-default', SITE + english))
    # a hidden page stays out of the sitemap too, and so does one that names another as the one to index
    if not page.meta.get('hidden') and not page.meta.get('canonical'):
        recorded[url] = {'lastmod': modified, 'lang': lang, 'alternates': alternates}
    # The page's own description, the home page's from the catalogue, else the language's own; as long
    # as a result shows - Japanese, a character twice as wide, about half as many.
    words = catalogues.get(lang, {}).get('meta', {})
    view = type(page).__name__
    title = None
    if view in ('View', 'Archive', 'Category'):
        # The news' lists - the newest posts, a month's, a category's - and their further pages: a
        # title and a description of their own, where the theme gives every page of a list the same.
        more = re.search(r'/page/(\d+)/$', page.url)
        title = (page.title if view != 'Category' else 'News: %s' % page.title) + (', page %s' % more.group(1) if more else '')
        description = {
            'View': "Every release, every dated measurement and the project's own posts, newest first",
            'Archive': "The news of %s: the releases, the dated measurements and the project's own posts" % page.title,
            'Category': 'The news under %s, newest first' % page.title,
        }[view] + (', page %s.' % more.group(1) if more else '.')
    elif page.is_homepage and words.get('description'):
        description = words['description']
    else:
        # a page with no paragraph to describe it (one of badges, say) is described by its title
        description = (page.meta.get('description') or (None if page.is_homepage else page.title)
                       or words.get('description') or config['site_description'])
    description = cut(' '.join(TAG.sub('', str(description)).split()), 100 if lang == 'ja' else 160)
    kind = 'home' if page.is_homepage else ('post' if type(page).__name__ == 'Post' else 'article')
    return {'lang': lang, 'title': title, 'modified': modified, 'published': published, 'alternates': alternates, 'path': path, 'kind': kind,
            'image': page.meta.get('image') or SITE + '/docs/images/hardware/bench/three-appliances-2026-10-17.jpg',
            'description': description}


def on_post_build(config):
    """The pages recorded for the sitemap, beside the site (build.py reads it and removes it)."""
    out = pathlib.Path(config['site_dir']) / '.seo.json'
    out.write_text(json.dumps(recorded, ensure_ascii=False, indent=0), encoding='utf-8')
