#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Build npuforge.org, the project's site, from what is already in the repository.

The site is MkDocs Material over three sources, gathered into one tree first so that every page
keeps the links it has on GitHub:

  * docs/, every page and image as it is, under /docs/;
  * the wiki's story - the eleven chapters, the timeline and how the project works - under
    /story/, from a checkout of the wiki beside this repository or the one given with --wiki;
  * the repository's own pages - the changelog, contributing, the design, releasing, security
    and the code of conduct - under /project/, and website/pages/ - the home page, the contact
    page and the plugin family - at the top.

A link that stays inside those becomes the page it names on the site; a link to anything else in
the repository - a driver's source, a script - goes to the file on GitHub. Nothing under docs/ is
changed to make the site work: a page reads the same on GitHub and here.

The words of the home page are in website/i18n/, one catalogue per language, and the Arabic home
page is made from its catalogue - no Arabic is written into a template or a page.

    python3 website/build.py              build into website/.build/site
    python3 website/build.py --check      the same, with every warning fatal, as the checks run it
    python3 website/build.py --serve      build, then serve it on http://127.0.0.1:8000
"""
import argparse
import hashlib
import json
import os
import pathlib
import posixpath
import re
import shutil
import subprocess
import sys
import urllib.request
from urllib.parse import unquote

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
BUILD = HERE / '.build'
SRC = BUILD / 'src'
SRC_AR = BUILD / 'src-ar'

REPO = 'AbdelmonemAwad/os-xgs-npu'
GH = 'https://github.com/' + REPO
# Absolute links into the repository's main branch, which the wiki uses for everything it shows.
REPO_PREFIXES = (
    'https://raw.githubusercontent.com/%s/main/' % REPO,
    'https://raw.githubusercontent.com/%s/refs/heads/main/' % REPO,
    GH + '/blob/main/',
    GH + '/tree/main/',
    GH + '/raw/main/',
)
WIKI_PREFIX = GH + '/wiki/'

# The repository's own pages, and where each is on the site. README.md is not among them: the
# site's home page is its own, and a link to the README goes to GitHub, anchors and all.
PROJECT_PAGES = {
    'CHANGELOG.md': 'project/changelog.md',
    'CONTRIBUTING.md': 'project/contributing.md',
    'DESIGN.md': 'project/design.md',
    'RELEASING.md': 'project/releasing.md',
    'SECURITY.md': 'project/security.md',
    'CODE_OF_CONDUCT.md': 'project/code-of-conduct.md',
}

# The wiki's story, in its order. The wiki's other pages are copies of pages under docs/, so a
# link to one of them goes to the page it copies.
STORY = [
    '1-Two-computers-in-one-box',
    '2-The-XGS-136-arrives',
    '3-Eight-months-of-looking',
    '4-The-first-XGS-3300',
    '5-Reverse-engineering-from-the-open-sources',
    '6-A-firmware-of-its-own-and-a-board-of-many-chips',
    '7-The-host-and-the-coprocessor',
    '8-Fourteen-ports-on-the-XGS-136',
    '9-The-XGS-3300-and-the-last-hop',
    '10-From-ports-to-offload',
    '11-An-appliance-again',
    'Timeline',
    'How-this-project-works',
]
STORY_PARTS = [('Before the repository', STORY[0:7]), ('In the repository', STORY[7:11]),
               ('Alongside', STORY[11:13])]
WIKI_ELSEWHERE = {
    'Home': 'story/index.md',
    'Installing': 'docs/guide/installing.md',
    'The-pages': 'docs/guide/the-pages.md',
    'Photographs': 'docs/images/README.md',
    'Measurements': 'docs/measurements/README.md',
}

# The documentation's sections, in the order docs/INDEX.md gives its kinds, with the guide's
# pages in the order a reader new to the project wants them.
GUIDE_ORDER = ['what-works', 'installing', 'updating', 'the-pages', 'how-it-works', 'limitations',
               'families-and-hardware', 'background', 'measured']
DOC_SECTIONS = [
    ('Guide', 'guide'),
    ('Appliances', 'hardware'),
    ('Families', 'families'),
    ('ARMADA driver', 'npuep'),
    ('OCTEON TX driver', 'octep'),
    ('OCTEON TX provenance', 'octeontx'),
    ('Findings', 'findings'),
    ('Images', 'images'),
]
DOC_REFERENCE = ['the-road.md', 'provenance.md', 'problem-reports.md', 'INDEX.md', 'REVIEW.md']

# The compatibility list: the issues .github/ISSUE_TEMPLATE/appliance.yml makes, and the choices the
# form offers - a report's row carries these and nothing else written by its author.
COMPAT_LABEL = 'compatibility'
NEW_APPLIANCE = GH + '/issues/new?template=appliance.yml'
APPLIANCES = ['XGS 87', 'XGS 107', 'XGS 116', 'XGS 126', 'XGS 136', 'XGS 138', 'XGS 2100', 'XGS 2300',
              'XGS 3100', 'XGS 3300', 'XGS 4300', 'XGS 4500', 'XGS 5500', 'XGS 6500', 'Another model']
INSTALLED = ['An image from xgs-image-builder', 'The installer, on an OPNsense already there', 'Something else']
WORKS = ['every front port', 'flows in hardware', "IPsec in the coprocessor's engine", 'the front panel',
         'temperatures and fans', 'updates from XGS - Updates']


def github_issues(label):
    """Every issue with the label, open or closed - read without a token, or with the workflow's."""
    out, page = [], 1
    while True:
        url = 'https://api.github.com/repos/%s/issues?labels=%s&state=all&per_page=100&page=%d' % (REPO, label, page)
        headers = {'User-Agent': 'npuforge.org build', 'Accept': 'application/vnd.github+json'}
        if os.environ.get('GITHUB_TOKEN'):
            headers['Authorization'] = 'Bearer ' + os.environ['GITHUB_TOKEN']
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=20) as r:
            batch = json.loads(r.read().decode('utf-8'))
        out += [i for i in batch if 'pull_request' not in i]
        if len(batch) < 100:
            return out
        page += 1


def form_fields(body):
    """An issue form's answers, by the label each was asked under: GitHub writes them as
    '### Label', a blank line, and the answer."""
    fields, current = {}, None
    for line in body.replace('\r\n', '\n').split('\n'):
        m = re.match(r'^### (.+)$', line)
        if m:
            current = m.group(1).strip()
            fields[current] = []
        elif current is not None:
            fields[current].append(line)
    return {k: '\n'.join(v).strip().replace('_No response_', '') for k, v in fields.items()}


# What worker/index.js answers itself rather than a file of the site (wrangler.toml, run_worker_first).
WORKER_PATHS = ('/api/', '/.well-known/security.txt')

warnings = []


def slug(name):
    return name.lower()


def story_path(name):
    return 'story/%s.md' % slug(name)


def page_url(site_path):
    """The URL directory a page is served from, with MkDocs' directory URLs - '' is the top."""
    if not site_path.endswith('.md'):
        return site_path
    stem = site_path[:-3]
    base = posixpath.basename(stem)
    if base in ('index', 'README'):
        d = posixpath.dirname(stem)
        return d + '/' if d else ''
    return stem + '/'


def relative(target, origin_dir):
    """target and origin_dir are paths from the top of the site; '' is the top."""
    rel = posixpath.relpath(target or '.', origin_dir.rstrip('/') or '.')
    if target.endswith('/') and not rel.endswith('/'):
        rel += '/'
    return rel


class Site:
    def __init__(self, wiki):
        self.wiki = wiki
        self.map = {}           # repository path (or 'wiki:Name') -> site path
        self.sources = {}       # site path -> (kind, origin) for edit links and link resolution

    # -- gathering -----------------------------------------------------------------------------

    def gather(self):
        if SRC.exists():
            shutil.rmtree(SRC)
        SRC.mkdir(parents=True)
        for f in sorted((ROOT / 'docs').rglob('*')):
            if f.is_file():
                rp = f.relative_to(ROOT).as_posix()
                self.map[rp] = rp
                self.sources[rp] = ('repo', rp)
        for rp, sp in PROJECT_PAGES.items():
            if (ROOT / rp).exists():
                self.map[rp] = sp
                self.sources[sp] = ('repo', rp)
        if self.wiki:
            for name in STORY:
                if (self.wiki / (name + '.md')).exists():
                    self.map['wiki:' + name] = story_path(name)
                    self.sources[story_path(name)] = ('wiki', name)
        for name, sp in WIKI_ELSEWHERE.items():
            self.map.setdefault('wiki:' + name, sp)
        for f in sorted((HERE / 'pages').rglob('*')):
            if f.is_file():
                sp = f.relative_to(HERE / 'pages').as_posix()
                self.sources[sp] = ('site', 'website/pages/' + sp)
                self.map['website/pages/' + sp] = sp

        for sp, (kind, origin) in self.sources.items():
            out = SRC / sp
            out.parent.mkdir(parents=True, exist_ok=True)
            src = {'repo': ROOT / origin, 'wiki': self.wiki / (origin + '.md') if self.wiki else None,
                   'site': ROOT / origin}[kind]
            if not sp.endswith('.md'):
                shutil.copyfile(src, out)
                continue
            text = src.read_text(encoding='utf-8')
            # The site's own pages link the way every other page does, by where things are in the
            # repository, so the same links are right on GitHub and here.
            text = self.rewrite(text, kind, origin, sp)
            if kind != 'site':
                text = self.front_matter(kind, origin, text) + text
            out.write_text(text, encoding='utf-8', newline='\n')
        if self.wiki:
            self.story_index()
        self.news()
        self.compatibility()
        self.badges()

    # -- news ----------------------------------------------------------------------------------

    def news(self):
        """news/: a post for every release, from CHANGELOG.md, and for every dated measurement, from
        its page - written by nothing but the work itself - and the hand-written ones in
        website/news/. Material's blog lays them out; feed.xml is the same posts as RSS."""
        posts = SRC / 'news' / 'posts'
        posts.mkdir(parents=True, exist_ok=True)
        entries = []

        changelog = (ROOT / 'CHANGELOG.md').read_text(encoding='utf-8').replace('\r\n', '\n')
        for m in re.finditer(r'^## (\d+\.\d+\.\d+\S*) - (\d{4}-\d{2}-\d{2})\n(.*?)(?=^## |\Z)', changelog, re.M | re.S):
            ver, date, body = m.group(1), m.group(2), m.group(3).strip()
            slug = 'release-' + re.sub(r'[^a-z0-9]+', '-', ver.lower()).strip('-')
            sp = 'news/posts/%s.md' % slug
            lead = body.split('\n\n', 1)[0] if not body.startswith('#') else ''
            rest = body[len(lead):].strip() if lead else body
            rest = re.sub(r'^### ', '## ', rest, flags=re.M)
            text = '# Release %s\n\n%s\n\n<!-- more -->\n\n%s\n\nThe release, with its files: [v%s on GitHub](%s/releases/tag/v%s).\n' % (
                ver, lead or 'What changed, written from the commits.', rest, ver, GH, ver)
            text = self.rewrite(text, 'repo', 'CHANGELOG.md', sp)
            entries.append(dict(date=date, slug=slug, title='Release %s' % ver, category='Releases',
                                summary=lead or 'What changed in %s, written from the commits.' % ver, text=text))

        for page in sorted((ROOT / 'docs' / 'measurements').glob('*-20[0-9][0-9]-[0-9][0-9]-[0-9][0-9].md')):
            date = page.stem[-10:]
            body = page.read_text(encoding='utf-8').replace('\r\n', '\n')
            title = next((l[2:].strip() for l in body.split('\n') if l.startswith('# ')), page.stem)
            after = body.split('\n# ', 1)[-1].split('\n', 1)[-1].strip()
            lead = after.split('\n\n', 1)[0].strip()
            slug = 'measured-' + page.stem
            sp = 'news/posts/%s.md' % slug
            text = '# %s\n\n%s\n\n<!-- more -->\n\nThe whole page, with how it was taken and what it does not show: [%s](../../docs/measurements/%s).\n' % (
                title, lead, title, page.name)
            # the lead's own links are the page's, so they are resolved from where the page is
            text = self.rewrite(text.replace('../../docs/measurements/' + page.name, page.name), 'repo',
                                'docs/measurements/' + page.name, sp)
            plain = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', lead)
            plain = re.sub(r'(\*\*|__|`|~~)', '', plain)
            entries.append(dict(date=date, slug=slug, title=title, category='Measurements',
                                summary=' '.join(plain.split()), text=text))

        for f in sorted((HERE / 'news').glob('*.md')) if (HERE / 'news').exists() else []:
            raw = f.read_text(encoding='utf-8')
            meta = dict(re.findall(r'^(date|slug|title|summary): *(.+)$', raw.split('\n---', 1)[0], re.M))
            sp = 'news/posts/%s.md' % meta['slug']
            body = raw.split('\n---', 1)[1].lstrip('\n')
            text = self.rewrite(body, 'site', 'website/news/' + f.name, sp)
            entries.append(dict(date=meta['date'], slug=meta['slug'], title=meta['title'].strip('"'),
                                category='The project', summary=meta.get('summary', '').strip('"'), text=text))

        for e in entries:
            fm = ('---\ndate: %s\nslug: %s\ncategories:\n  - %s\ndescription: %s\ncomments: true\n'
                  'edit_url: %s/edit/main/website/build.py\n---\n\n') % (
                e['date'], e['slug'], e['category'], json.dumps(e['summary'][:300], ensure_ascii=False), GH)
            (posts / ('%s.md' % e['slug'])).write_text(fm + e['text'], encoding='utf-8', newline='\n')
        self.feed(sorted(entries, key=lambda e: (e['date'], e['slug']), reverse=True)[:40])

    def feed(self, entries):
        from email.utils import format_datetime
        import datetime
        import html as h
        items = []
        for e in entries:
            when = datetime.datetime.strptime(e['date'], '%Y-%m-%d').replace(tzinfo=datetime.timezone.utc)
            url = 'https://npuforge.org/news/%s/' % e['slug']
            items.append('    <item>\n      <title>%s</title>\n      <link>%s</link>\n      <guid isPermaLink="true">%s</guid>\n'
                         '      <pubDate>%s</pubDate>\n      <category>%s</category>\n      <description>%s</description>\n    </item>'
                         % (h.escape(e['title']), url, url, format_datetime(when), h.escape(e['category']), h.escape(e['summary'])))
        xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom">\n  <channel>\n'
               '    <title>npuforge - news</title>\n    <link>https://npuforge.org/news/</link>\n'
               '    <atom:link href="https://npuforge.org/news/feed.xml" rel="self" type="application/rss+xml"/>\n'
               '    <description>Releases and measurements of os-xgs-npu: Sophos XGS hardware, back as open firewalls.</description>\n'
               '    <language>en</language>\n%s\n  </channel>\n</rss>\n') % '\n'.join(items)
        (SRC / 'news' / 'feed.xml').write_text(xml, encoding='utf-8', newline='\n')

    # -- compatibility -------------------------------------------------------------------------

    def compatibility(self):
        """compatibility.md: the appliances the project measures, then every owner's report - the
        issues with the label `compatibility`, made by .github/ISSUE_TEMPLATE/appliance.yml. Only the
        form's fixed choices and two short fields checked against a pattern reach the page; the free
        text stays on GitHub, behind the report's link."""
        cat = json.loads((HERE / 'i18n' / 'en.json').read_text(encoding='utf-8'))['compatibility']
        lines = ['---', 'edit_url: %s/edit/main/website/build.py' % GH, '---', '', '# ' + cat['title'], '',
                 cat['lede'], '', '[%s](%s){ .md-button .md-button--primary }' % (cat['add'], NEW_APPLIANCE), '',
                 '## ' + cat['measured'], '', '| appliance | board | family | what works | where it is written |',
                 '|---|---|---|---|---|']
        for row in cat['ours']:
            lines.append('| **%s** | `%s` | %s | %s | [%s](%s) |' % (row['name'], row['board'], row['family'],
                                                                  row['works'], row['link_text'], row['link']))
        lines += ['', '## ' + cat['reported'], '']
        try:
            issues = github_issues(COMPAT_LABEL)
        except (OSError, ValueError) as e:
            print('the compatibility reports could not be read (%s)' % e, file=sys.stderr)
            issues = None
        rows = []
        for i in issues or []:
            if i.get('state_reason') == 'not_planned' or any(l['name'] in ('invalid', 'duplicate', 'spam') for l in i.get('labels', [])):
                continue
            f = form_fields(i.get('body') or '')
            appliance = f.get('Appliance', '')
            if appliance not in APPLIANCES:
                continue
            board = f.get('Board', '')
            board = board if re.fullmatch(r'AMDA\d{4}', board) else ''
            version = f.get('Version', '')
            version = version if re.fullmatch(r'[A-Za-z0-9 .,+-]{1,60}', version) else ''
            installed = f.get('Installed from', '')
            installed = installed if installed in INSTALLED else ''
            ticked = re.findall(r'^- \[[xX]\] (.+)$', f.get('What works', ''), re.M)
            works = [w for w in WORKS if w in ticked]
            rows.append('| %s | %s | %s | %s | %s | [#%d](%s) %s |' % (
                appliance, '`%s`' % board if board else '-', version.replace('|', '') or '-',
                ', '.join(works) or 'nothing ticked', installed or '-', i['number'], i['html_url'], i['created_at'][:10]))
        if issues is None:
            lines.append(cat['unreadable'])
        elif not rows:
            lines.append(cat['none'])
        else:
            lines += ['| appliance | board | version | what works | installed from | the report |', '|---|---|---|---|---|---|'] + rows
        lines += ['', cat['note'], '']
        text = self.rewrite('\n'.join(lines), 'site', 'website/pages/compatibility.md', 'compatibility.md')
        (SRC / 'compatibility.md').write_text(text, encoding='utf-8', newline='\n')

    def badges(self):
        """The badges two pages show come from img.shields.io and GitHub. A copy of each is taken at
        build time and served from the site, so reading a page sends nobody else the reader's address."""
        cache = BUILD / 'badges'
        cache.mkdir(exist_ok=True)
        out = SRC / 'assets' / 'badges'
        # The pages show them as <img src="...">, so a URL runs to the closing quote - parentheses
        # and all, which some badges' labels have.
        pattern = re.compile(r'(?<=src=")https://(?:img\.shields\.io/|github\.com/[^"]+/badge\.svg)[^"]*')
        for page in SRC.rglob('*.md'):
            text = page.read_text(encoding='utf-8')
            urls = sorted(set(pattern.findall(text)))
            if not urls:
                continue
            for url in urls:
                name = hashlib.sha1(url.encode()).hexdigest()[:16] + '.svg'
                if not (cache / name).exists():
                    try:
                        req = urllib.request.Request(url.replace('&amp;', '&'), headers={'User-Agent': 'npuforge.org build'})
                        with urllib.request.urlopen(req, timeout=20) as r:
                            body = r.read()
                        if b'<svg' not in body[:400]:
                            raise ValueError('not an SVG')
                        (cache / name).write_bytes(body)
                    except (OSError, ValueError) as e:
                        warnings.append('the badge %s could not be copied (%s)' % (url, e))
                        continue
                out.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(cache / name, out / name)
                text = text.replace(url, '/assets/badges/' + name)
            page.write_text(text, encoding='utf-8', newline='\n')

    def front_matter(self, kind, origin, text):
        """The edit link, and the title: MkDocs takes a page's title for the navigation from its
        first line, and the pages start with the line that dates their review."""
        edit = (GH + '/edit/main/' + origin) if kind == 'repo' else (GH + '/wiki/' + origin + '/_edit')
        fm = '---\nedit_url: %s\n' % edit
        if kind == 'wiki' or origin.startswith('docs/'):
            fm += 'comments: true\n'
        h1 = next((l[2:].strip() for l in text.split('\n') if l.startswith('# ')), None)
        if h1:
            plain = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', h1)
            plain = re.sub(r'(\*\*|__|~~|`)', '', plain).strip()
            fm += 'title: %s\n' % json.dumps(plain, ensure_ascii=False)
        return fm + '---\n\n'

    def story_index(self):
        """story/index.md: the chapters, each with its opening sentence."""
        cat = json.loads((HERE / 'i18n' / 'en.json').read_text(encoding='utf-8'))['story']
        lines = ['---', 'edit_url: %s/wiki/_Sidebar/_edit' % GH, '---', '', '# ' + cat['title'], '',
                 cat['lede'], '']
        for part, names in STORY_PARTS:
            lines += ['## ' + part, '', '<div class="grid cards" markdown>', '']
            for name in names:
                p = self.wiki / (name + '.md')
                if not p.exists():
                    continue
                body = p.read_text(encoding='utf-8').splitlines()
                title = next((l[2:].strip() for l in body if l.startswith('# ')), name.replace('-', ' '))
                first = next((l for l in body if l and l[0].isalpha()), '')
                para = []
                for l in body[body.index(first):] if first else []:
                    if not l.strip():
                        break
                    para.append(l.strip())
                sentence = re.split(r'(?<=[.!?])\s', ' '.join(para), maxsplit=1)[0]
                sentence = re.sub(r'\*\*|__|\[([^\]]*)\]\([^)]*\)', lambda m: m.group(1) or '', sentence)
                lines += ['-   **[%s](%s)**' % (title, posixpath.basename(story_path(name))), '',
                          '    ---', '', '    ' + sentence, '']
            lines += ['</div>', '']
        (SRC / 'story' / 'index.md').write_text('\n'.join(lines), encoding='utf-8', newline='\n')

    # -- the Arabic site ------------------------------------------------------------------------

    def gather_ar(self):
        """/ar/: a site of its own, built after the English one - right to left, Material's own
        words in Arabic, its own search. Its pages are website/i18n/ar/ - the home page from the
        Arabic catalogue, the translated guide pages, the contact page - and everything not
        translated is linked where it is, in English.

        A translated page names the English page and the review date it was translated from; when
        the English page has been reviewed since, the Arabic page says so at its top, and the build
        says which."""
        cat = json.loads((HERE / 'i18n' / 'ar.json').read_text(encoding='utf-8'))
        if SRC_AR.exists():
            shutil.rmtree(SRC_AR)
        SRC_AR.mkdir(parents=True)
        (SRC_AR / 'index.md').write_text(
            '---\ntemplate: home.html\nlang: ar\ntitle: %s\nhide: [navigation, toc, footer]\n'
            'edit_url: %s/edit/main/website/i18n/ar.json\n---\n' % (json.dumps(cat['meta']['title'], ensure_ascii=False), GH),
            encoding='utf-8', newline='\n')
        for f in sorted((HERE / 'i18n' / 'ar').rglob('*.md')):
            rel = f.relative_to(HERE / 'i18n' / 'ar').as_posix()
            raw = f.read_text(encoding='utf-8').replace('\r\n', '\n')
            head, body = raw[4:].split('\n---\n', 1)
            meta = dict(re.findall(r'^(\w+): *(.*)$', head, re.M))
            notice = ''
            source = ROOT / meta.get('source', '')
            if meta.get('source_reviewed') and source.is_file():
                m = re.match(r'<!-- reviewed: (\d{4}-\d{2}-\d{2}) -->', source.read_text(encoding='utf-8'))
                if m and m.group(1) > meta['source_reviewed']:
                    print('the Arabic %s was translated from %s of %s; that page was reviewed again on %s'
                          % (rel, meta['source'], meta['source_reviewed'], m.group(1)), file=sys.stderr)
                    notice = '!!! warning "%s"\n\n    %s\n\n' % (cat['stale']['title'], cat['stale']['text'] % (
                        meta['source_reviewed'], m.group(1), '/' + meta['source'][:-3].replace('README', '') + '/'))
            fm = '---\nlang: ar\nedit_url: %s/edit/main/website/i18n/ar/%s\n' % (GH, rel)
            fm += ''.join('%s: %s\n' % (k, v) for k, v in meta.items() if k in ('title', 'hide'))
            out = SRC_AR / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            body = body.lstrip('\n')
            if notice and body.startswith('# '):
                h1, rest = body.split('\n', 1)
                body = h1 + '\n\n' + notice + rest
            out.write_text(fm + '---\n\n' + body, encoding='utf-8', newline='\n')

    def config_ar(self):
        """mkdocs.ar.gathered.yml: the English configuration made the Arabic site's. It is written,
        never kept, so the Arabic in it - the menu, the footer - stays in the catalogue."""
        cat = json.loads((HERE / 'i18n' / 'ar.json').read_text(encoding='utf-8'))
        text = (HERE / 'mkdocs.yml').read_text(encoding='utf-8')
        text = re.sub(r'\n  # blog-begin\n.*?\n  # blog-end\n', '\n', text, flags=re.S)
        text = re.sub(r'\nnot_in_nav: \|\n(?:  .*\n)+', '\n', text)
        text = re.sub(r'\ncopyright: >-\n(?:  .*\n)+', '\ncopyright: %s\n' % json.dumps(cat['footer']['copyright'], ensure_ascii=False), text)
        for a, b in (('site_url: https://npuforge.org/', 'site_url: https://npuforge.org/ar/'),
                     ('\ndocs_dir: pages\n', '\ndocs_dir: .build/src-ar\n'),
                     ('\nsite_dir: .build/site\n', '\nsite_dir: .build/site/ar\n'),
                     ('  language: en\n', '  language: ar\n'),
                     ('  logo: assets/logo.svg\n', '  logo: /assets/logo.svg\n'),
                     ('  favicon: assets/favicon.svg\n', '  favicon: /assets/favicon.svg\n'),
                     ('  - assets/site.css\n', '  - /assets/site.css\n'),
                     ('  - assets/site.js\n', '  - /assets/site.js\n'),
                     ('      link: /contact/\n', '      link: /ar/contact/\n')):
            assert a in text, a
            text = text.replace(a, b)
        n = cat['nav']
        nav = ['nav:', '  - %s: index.md' % json.dumps(n['home'], ensure_ascii=False),
               '  - %s:' % json.dumps(n['guide'], ensure_ascii=False)]
        nav += ['    - guide/%s.md' % p for p in ('how-it-works', 'installing', 'updating', 'limitations')
                if (SRC_AR / 'guide' / ('%s.md' % p)).exists()]
        for key, url in (('docs', '/docs/'), ('measurements', '/docs/measurements/'), ('compatibility', '/compatibility/'),
                         ('news', '/news/'), ('story', '/story/')):
            nav.append('  - %s: %s' % (json.dumps(n[key], ensure_ascii=False), url))
        nav.append('  - %s: contact.md' % json.dumps(n['contact'], ensure_ascii=False))
        cfg = HERE / 'mkdocs.ar.gathered.yml'
        cfg.write_text(text + '\n' + '\n'.join(nav) + '\n', encoding='utf-8', newline='\n')
        return cfg

    # -- links ---------------------------------------------------------------------------------

    def resolve(self, target, kind, origin):
        """Where a link in a gathered page goes: ('site', site path, fragment), or ('url', url)."""
        path, frag = (target.split('#', 1) + [''])[:2]
        frag = '#' + frag if frag else ''
        for prefix in REPO_PREFIXES:
            if path.startswith(prefix):
                return self.repo_target(path[len(prefix):], frag, target)
        if path.startswith(WIKI_PREFIX):
            return self.wiki_target(path[len(WIKI_PREFIX):], frag, target)
        # another site, an anchor on the page, or a path from the top of this site - which the
        # check of the built site looks at instead
        if re.match(r'^[a-z][a-z0-9+.-]*:|^/', path, re.I) or not path:
            return None
        if kind == 'wiki':
            if '/' not in path and '.' not in path:
                return self.wiki_target(path, frag, target)
            return None
        base = posixpath.dirname(origin)
        rp = posixpath.normpath(posixpath.join(base, unquote(path)))
        if rp.startswith('../'):
            return None
        return self.repo_target('' if rp == '.' else rp, frag, target)

    def repo_target(self, rp, frag, original):
        rp = rp.rstrip('/')
        if rp in self.map:
            return ('site', self.map[rp], frag)
        if (ROOT / rp).is_dir() if rp else True:
            readme = (rp + '/README.md') if rp else None
            if readme in self.map:
                return ('site', self.map[readme], frag)
            return ('url', GH + ('/tree/main/' + rp if rp else '') + frag)
        if (ROOT / rp).is_file():
            return ('url', GH + '/blob/main/' + rp + frag)
        warnings.append('a link to %s, which is not in the repository' % original)
        return None

    def wiki_target(self, name, frag, original):
        key = 'wiki:' + name.rstrip('/')
        if key in self.map:
            return ('site', self.map[key], frag)
        return ('url', WIKI_PREFIX + name + frag)

    def rewrite(self, text, kind, origin, site_path):
        here_md = posixpath.dirname(site_path)
        here_url = page_url(site_path)

        def md_link(m):
            r = self.resolve(m.group(2).strip('<>'), kind, origin)
            if r is None:
                return m.group(0)
            if r[0] == 'url':
                return m.group(1) + r[1] + m.group(3)
            return m.group(1) + relative(r[1], here_md) + r[2] + m.group(3)

        def html_one(url):
            r = self.resolve(url, kind, origin)
            if r is None:
                return url
            if r[0] == 'url':
                return r[1]
            return relative(page_url(r[1]), here_url) + r[2]

        def html_attr(m):
            attr, q, value = m.group(1), m.group(2), m.group(3)
            if attr == 'srcset':
                parts = []
                for item in value.split(','):
                    bits = item.strip().split(None, 1)
                    if bits:
                        bits[0] = html_one(bits[0])
                    parts.append(' '.join(bits))
                value = ', '.join(parts)
            else:
                value = html_one(value)
            return '%s=%s%s%s' % (attr, q, value, q)

        out, fence = [], None
        for line in text.split('\n'):
            stripped = line.lstrip()
            mark = re.match(r'(`{3,}|~{3,})', stripped)
            if fence:
                if mark and stripped.startswith(fence):
                    fence = None
                out.append(line)
                continue
            if mark:
                fence = mark.group(1)
                out.append(line)
                continue
            # inline code is left as it is written
            pieces = re.split(r'(`+[^`]*`+)', line)
            for i in range(0, len(pieces), 2):
                p = pieces[i]
                p = re.sub(r'(\]\()(<[^>]*>|[^)\s]+)((?:\s+"[^"]*")?\))', md_link, p)
                p = re.sub(r'''\b(href|src|srcset)=(["'])(.*?)\2''', html_attr, p)
                pieces[i] = p
            out.append(''.join(pieces))
        return '\n'.join(out)

    # -- the navigation ------------------------------------------------------------------------

    def nav(self):
        def title(sp):
            p = SRC / sp
            for line in p.read_text(encoding='utf-8').splitlines():
                if line.startswith('# '):
                    return line[2:].strip()
            return posixpath.basename(sp)[:-3]

        docs = [sp for sp in self.sources if sp.startswith('docs/') and sp.endswith('.md')]
        nav = ['nav:', '  - Home: index.md', '  - Documentation:', '    - docs/README.md']
        placed = {'docs/README.md'}
        for label, folder in DOC_SECTIONS:
            pages = sorted(sp for sp in docs if sp.startswith('docs/%s/' % folder))
            if not pages:
                continue
            if folder == 'guide':
                order = {('docs/guide/%s.md' % n): i for i, n in enumerate(GUIDE_ORDER)}
                pages.sort(key=lambda sp: (order.get(sp, 99), sp))
            if folder == 'findings':
                pages = self.findings_order(pages)
            readme = 'docs/%s/README.md' % folder
            if readme in pages:
                pages.remove(readme)
                pages.insert(0, readme)
            nav.append('    - %s:' % label)
            nav += ['      - %s' % sp for sp in pages]
            placed.update(pages)
        nav.append('    - Reference:')
        for name in DOC_REFERENCE:
            sp = 'docs/' + name
            if sp in self.sources:
                nav.append('      - ' + sp)
                placed.add(sp)
        meas = sorted((sp for sp in docs if sp.startswith('docs/measurements/')),
                      key=lambda sp: (not sp.endswith('README.md'), sp))
        placed.update(meas)
        nav.append('  - Measurements:')
        nav += ['    - ' + sp for sp in meas]
        nav.append('  - Compatibility: compatibility.md')
        nav.append('  - News: news/index.md')
        rest = sorted(set(docs) - placed)
        if rest:
            nav.append('    - Other pages:')
            nav += ['      - ' + sp for sp in rest]
        if self.wiki:
            nav.append('  - The story:')
            nav.append('    - story/index.md')
            nav += ['    - ' + story_path(n) for n in STORY if story_path(n) in self.sources]
        nav.append('  - Project:')
        nav.append('    - project/index.md')
        nav += ['    - ' + sp for sp in PROJECT_PAGES.values() if sp in self.sources]
        nav.append('  - Contact: contact.md')
        return '\n'.join(nav) + '\n'

    def findings_order(self, pages):
        """The findings in the order of docs/README.md's table, by day; any it lacks at the end."""
        hub = (ROOT / 'docs' / 'README.md').read_text(encoding='utf-8')
        listed = re.findall(r'\]\((findings/[^)#]+\.md)', hub.split('## The findings, in order', 1)[-1])
        order = []
        for rp in listed:
            sp = 'docs/' + rp
            if sp in pages and sp not in order:
                order.append(sp)
        return order + [sp for sp in pages if sp not in order]

    def config(self):
        text = (HERE / 'mkdocs.yml').read_text(encoding='utf-8')
        text = text.replace('\ndocs_dir: pages\n', '\ndocs_dir: .build/src\n')
        # MkDocs takes the paths in a configuration from the file's own folder, so the gathered
        # one sits beside the original, the navigation added to it.
        cfg = HERE / 'mkdocs.gathered.yml'
        cfg.write_text(text + '\n' + self.nav(), encoding='utf-8', newline='\n')
        return cfg


def check_built(site_dir):
    """Every link and image inside the built site that names a file of it, and that file is there.
    MkDocs checks the Markdown links; the HTML ones - the pictures, the tables written in HTML -
    are only as right as rewrite() made them, and this is where that is found out."""
    missing = {}
    attr = re.compile(r'''\b(?:href|src)="([^"#?]*)[^"]*"''')
    for page in site_dir.rglob('*.html'):
        text = page.read_text(encoding='utf-8', errors='replace')
        for url in attr.findall(text):
            if not url or re.match(r'^[a-z][a-z0-9+.-]*:|^//', url, re.I):
                continue
            if url.startswith('/'):
                if url.startswith(WORKER_PATHS):
                    continue
                target = site_dir / unquote(url.lstrip('/'))
            else:
                target = page.parent / unquote(url)
            if target.is_dir():
                target = target / 'index.html'
            if not target.exists():
                missing.setdefault(page.relative_to(site_dir).as_posix(), set()).add(url)
    for page, urls in sorted(missing.items()):
        for url in sorted(urls):
            warnings.append('%s links to %s, which the site does not have' % (page, url))
    return not missing


def latest_release():
    try:
        tag = subprocess.run(['git', 'describe', '--tags', '--abbrev=0', '--match', 'v*'], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.strip()
        return tag
    except (OSError, subprocess.CalledProcessError):
        return ''


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--wiki', help='a checkout of the wiki (default: os-xgs-npu.wiki beside the repository)')
    ap.add_argument('--check', action='store_true', help='every warning fatal')
    ap.add_argument('--serve', action='store_true', help='serve the built site on 127.0.0.1:8000')
    args = ap.parse_args(argv[1:])

    wiki = pathlib.Path(args.wiki) if args.wiki else ROOT.parent / 'os-xgs-npu.wiki'
    if not (wiki / 'Home.md').exists():
        if args.wiki:
            sys.exit('no wiki at %s' % wiki)
        print('no wiki checkout beside the repository - the story is left out', file=sys.stderr)
        wiki = None
    site = Site(wiki)
    site.gather()
    site.gather_ar()
    cfg = site.config()
    cfg_ar = site.config_ar()
    for w in warnings:
        print('WARNING -', w, file=sys.stderr)
    env = dict(os.environ, NPUFORGE_RELEASE=latest_release())
    cmd = [sys.executable, '-m', 'mkdocs', 'serve' if args.serve else 'build', '-f', str(cfg)]
    if args.check:
        cmd.append('--strict')
    if args.serve:
        cmd += ['--dev-addr', '127.0.0.1:8000', '--no-livereload']
    rc = subprocess.call(cmd, cwd=HERE, env=env)
    if rc == 0 and not args.serve:
        # the Arabic site into /ar/ of the English one, which its build has just emptied
        cmd_ar = [sys.executable, '-m', 'mkdocs', 'build', '-f', str(cfg_ar)] + (['--strict'] if args.check else [])
        rc = subprocess.call(cmd_ar, cwd=HERE, env=env)
    if rc == 0 and not args.serve:
        # the README's status board, drawn from the family pages' tables as they stand - the README
        # shows it from here, so it follows every re-weighing without a commit of its pictures
        rc = subprocess.call([sys.executable, str(ROOT / 'tools' / 'readme-art.py'), '--out', str(BUILD / 'site' / 'readme')])
    if rc == 0 and not args.serve:
        before = len(warnings)
        check_built(BUILD / 'site')
        for w in warnings[before:]:
            print('WARNING -', w, file=sys.stderr)
    if args.check and warnings and rc == 0:
        rc = 1
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
