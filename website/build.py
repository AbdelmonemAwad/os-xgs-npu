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
from urllib.parse import quote, unquote

import cards
import devices
import project_pages

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
BUILD = HERE / '.build'
SRC = BUILD / 'src'
# The languages besides English the site is built in, in the order the language switch lists them;
# each one whose catalogue is in website/i18n/ gets a site of its own under /<lang>/.
LANGS = ['ar', 'de', 'es', 'fr', 'tr', 'ru', 'ja']
LANGUAGE_NAMES = {'ar': 'Arabic', 'de': 'German', 'es': 'Spanish', 'fr': 'French', 'tr': 'Turkish',
                  'ru': 'Russian', 'ja': 'Japanese'}


def src_of(lang):
    return BUILD / ('src-%s' % lang)


def languages():
    return [lang for lang in LANGS if (HERE / 'i18n' / ('%s.json' % lang)).exists()]


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

# The top bar's sections, by what a reader comes for - each a tab with its icon (mkdocs.yml's
# extra.tab_icons, in this order) and its pages in a menu: getting started, the hardware, the
# numbers, the documentation, the story, the project. The guide's pages a new reader wants first are
# Get started's, in that order; the rest of the guide is the documentation's, in the order after.
START_PAGES = ['step-by-step', 'installing', 'updating', 'the-pages', 'limitations', 'faq']
GUIDE_ORDER = ['what-works', 'how-it-works', 'families-and-hardware', 'background', 'measured']
IMAGE_BUILDER = 'https://build.npuforge.org/'
DOC_SECTIONS = [
    ('ARMADA driver', 'npuep'),
    ('OCTEON TX driver', 'octep'),
    ('OCTEON TX provenance', 'octeontx'),
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
        url = 'https://api.github.com/repos/%s/issues?labels=%s&state=all&per_page=100&page=%d' % (REPO, quote(label), page)
        headers = {'User-Agent': 'npuforge.org build', 'Accept': 'application/vnd.github+json'}
        if os.environ.get('GITHUB_TOKEN'):
            headers['Authorization'] = 'Bearer ' + os.environ['GITHUB_TOKEN']
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=20) as r:
            batch = json.loads(r.read().decode('utf-8'))
        out += [i for i in batch if 'pull_request' not in i]
        if len(batch) < 100:
            return out
        page += 1


def github_open_issues():
    """Every open issue - not the pull requests - for the status page, read like the reports above."""
    out, page = [], 1
    while True:
        url = 'https://api.github.com/repos/%s/issues?state=open&per_page=100&page=%d' % (REPO, page)
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
        self.reports = {}       # a model's page slug -> what its owners have reported, for devices.py
        self.lang_pages = {}    # a language -> the pages written in it, for its navigation

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
        # The models an owner runs, for the home page's table, which hooks.py fills from devices.json and
        # this file; the model pages have them from self.reports.
        owners = {}
        for slug in self.reports:
            c = devices.owner_report({'slug': slug}, self.reports)
            if c:
                owners[slug] = {'number': c['number'], 'url': c['url'], 'date': c['date']}
        BUILD.mkdir(parents=True, exist_ok=True)
        (BUILD / 'owners.json').write_text(json.dumps(owners, sort_keys=True), encoding='utf-8')
        # devices/: every model, and a page for each, from website/data/devices.json
        english = json.loads((HERE / 'i18n' / 'en.json').read_text(encoding='utf-8'))
        self.problem_reports()
        self.device_pages = devices.write(SRC, english, 'en', self.reports)
        cards.write(SRC, devices.load(), owners)
        cards.icons(SRC)
        # status.md, releases.md and the hidden compare.md
        try:
            self.open_issues = github_open_issues()
        except (OSError, ValueError) as e:
            print('the open issues could not be read (%s)' % e, file=sys.stderr)
            self.open_issues = None
        project_pages.write(SRC, english, 'en', self.reports, self.open_issues, devices.load())
        # modules/: every expansion module the vendor's board files describe, a page each
        self.module_pages = devices.write_modules(SRC, english, 'en')
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
            # A page that opens with its photograph - and the line in italics under it - keeps it
            # at the top of the post, and the post's lead is the first paragraph of text after them:
            # until 2026-10-22 the lead was simply the first paragraph, and a photograph's alt text,
            # with a "!" in front, became the post's summary.
            paras = [p.strip() for p in after.split('\n\n') if p.strip()]
            photo = ''
            while paras and (re.fullmatch(r'!\[[^\]]*\]\([^)]*\)', paras[0])
                             or re.fullmatch(r'\*[^*].*[^*]\*', paras[0], re.S)):
                if not photo and paras[0].startswith('!'):
                    photo = paras[0] + '\n\n'
                paras.pop(0)
            lead = paras[0] if paras else ''
            slug = 'measured-' + page.stem
            sp = 'news/posts/%s.md' % slug
            text = '# %s\n\n%s%s\n\n<!-- more -->\n\nThe whole page, with how it was taken and what it does not show: [%s](../../docs/measurements/%s).\n' % (
                title, photo, lead, title, page.name)
            # the lead's own links are the page's, so they are resolved from where the page is
            text = self.rewrite(text.replace('../../docs/measurements/' + page.name, page.name), 'repo',
                                'docs/measurements/' + page.name, sp)
            plain = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', lead)
            plain = re.sub(r'(\*\*|__|`|~~)', '', plain)
            # The post is the page's opening and a link to it: the search engines are told the page
            # is the one to index (hooks.py), so the two do not compete under one title.
            entries.append(dict(date=date, slug=slug, title=title, category='Measurements',
                                summary=' '.join(plain.split()), text=text,
                                canonical='https://npuforge.org/docs/measurements/%s/' % page.stem))

        for f in sorted((HERE / 'news').glob('*.md')) if (HERE / 'news').exists() else []:
            raw = f.read_text(encoding='utf-8')
            meta = dict(re.findall(r'^(date|slug|title|summary): *(.+)$', raw.split('\n---', 1)[0], re.M))
            sp = 'news/posts/%s.md' % meta['slug']
            body = raw.split('\n---', 1)[1].lstrip('\n')
            text = self.rewrite(body, 'site', 'website/news/' + f.name, sp)
            entries.append(dict(date=meta['date'], slug=meta['slug'], title=meta['title'].strip('"'),
                                category='The project', summary=meta.get('summary', '').strip('"'), text=text))

        for e in entries:
            fm = ('---\ndate: %s\nslug: %s\ncategories:\n  - %s\ndescription: %s\ncomments: true\n%s'
                  'edit_url: %s/edit/main/website/build.py\n---\n\n') % (
                e['date'], e['slug'], e['category'], json.dumps(e['summary'][:300], ensure_ascii=False),
                'canonical: %s\n' % e['canonical'] if e.get('canonical') else '', GH)
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
            # an owner writes the image's file name with the release and OPNsense's: up to 100 characters
            version = version if re.fullmatch(r'[A-Za-z0-9 .,+-]{1,100}', version) else ''
            installed = f.get('Installed from', '')
            # the ready image's own name written in place of the form's choice is that choice
            if installed not in INSTALLED and installed.startswith('xgs-image-based-on-opnsense'):
                installed = INSTALLED[0]
            installed = installed if installed in INSTALLED else ''
            ticked = re.findall(r'^- \[[xX]\] (.+)$', f.get('What works', ''), re.M)
            works = [w for w in WORKS if w in ticked]
            # the same report on its model's page, devices.py's "from owners' appliances"
            self.reports.setdefault(appliance.lower().replace(' ', '-'), {}).setdefault('compat', []).append({
                'version': version.replace('|', ''), 'works': works, 'installed': installed,
                'number': i['number'], 'url': i['html_url'], 'date': i['created_at'][:10]})
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

    def problem_reports(self):
        """What appliances of each model have reported on their own: the issues the problem reporter
        files (label "problem report"), each labelled with the model it came from ("Sophos XGS 126r1")
        - so a model nobody here has run is seen running on someone's appliance. And a port check
        (kind "untested-model"), the model's ports held against its coprocessor's on such an appliance.
        Only the counts, the kinds, the days and each port check's verdict reach the page; the reports
        stay on GitHub, behind their links."""
        try:
            issues = github_issues('problem report')
        except (OSError, ValueError) as e:
            print('the problem reports could not be read (%s)' % e, file=sys.stderr)
            return
        for i in issues:
            names = [l['name'] for l in i.get('labels', [])]
            label = next((n for n in names if re.fullmatch(r'Sophos XGS \d+w?r\d+', n)), None)
            if not label:
                continue
            slug = 'xgs-' + re.search(r'XGS (\d+)', label).group(1)
            r = self.reports.setdefault(slug, {}).setdefault('problems', {
                'count': 0, 'first': None, 'last': None, 'kinds': {}, 'labels': {}, 'portchecks': []})
            day = i['created_at'][:10]
            r['count'] += 1
            r['first'] = min(r['first'] or day, day)
            r['last'] = max(r['last'] or day, day)
            r['labels'][label] = r['labels'].get(label, 0) + 1
            kind = next((n for n in names if n not in ('problem report', 'sent automatically', 'sent by hand', label)), 'other')
            r['kinds'][kind] = r['kinds'].get(kind, 0) + 1
            if kind == 'untested-model':
                body = i.get('body') or ''
                verdict = 'disagrees' if 'do NOT agree' in body else ('agrees' if 'they agree' in body else 'unread')
                r['portchecks'].append({'verdict': verdict, 'url': i['html_url'], 'date': day})

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

    def gather_lang(self, lang):
        """/<lang>/: a site of its own for each language the catalogues hold, built after the English
        one - its own menu, Material's own words in that language, its own search, right to left for
        Arabic. Its pages are website/i18n/<lang>/ - the home page from the catalogue, the pages
        translated - and the models', the modules', the status and the releases pages written in its
        words; everything not translated is linked where it is, in English.

        A translated page names the English page and the review date it was translated from; when
        the English page has been reviewed since, the translation says so at its top, and the build
        says which."""
        cat = json.loads((HERE / 'i18n' / ('%s.json' % lang)).read_text(encoding='utf-8'))
        src = src_of(lang)
        if src.exists():
            shutil.rmtree(src)
        src.mkdir(parents=True)
        (src / 'index.md').write_text(
            '---\ntemplate: home.html\nlang: %s\ntitle: %s\nhide: [navigation, toc, footer]\n'
            'edit_url: %s/edit/main/website/i18n/%s.json\n---\n' % (lang, json.dumps(cat['meta']['title'], ensure_ascii=False), GH, lang),
            encoding='utf-8', newline='\n')
        pages = HERE / 'i18n' / lang
        for f in sorted(pages.rglob('*.md')) if pages.exists() else []:
            rel = f.relative_to(pages).as_posix()
            raw = f.read_text(encoding='utf-8').replace('\r\n', '\n')
            head, body = raw[4:].split('\n---\n', 1)
            meta = dict(re.findall(r'^(\w+): *(.*)$', head, re.M))
            notice = ''
            source = ROOT / meta.get('source', '')
            if meta.get('source_reviewed') and source.is_file():
                m = re.match(r'<!-- reviewed: (\d{4}-\d{2}-\d{2}) -->', source.read_text(encoding='utf-8'))
                if m and m.group(1) > meta['source_reviewed']:
                    print('the %s %s was translated from %s of %s; that page was reviewed again on %s'
                          % (lang, rel, meta['source'], meta['source_reviewed'], m.group(1)), file=sys.stderr)
                    notice = '!!! warning "%s"\n\n    %s\n\n' % (cat['stale']['title'], cat['stale']['text'] % (
                        meta['source_reviewed'], m.group(1), '/' + meta['source'][:-3].replace('README', '') + '/'))
            fm = '---\nlang: %s\nedit_url: %s/edit/main/website/i18n/%s/%s\n' % (lang, GH, lang, rel)
            fm += ''.join('%s: %s\n' % (k, v) for k, v in meta.items() if k in ('title', 'hide'))
            out = src / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            body = body.lstrip('\n')
            if notice and body.startswith('# '):
                h1, rest = body.split('\n', 1)
                body = h1 + '\n\n' + notice + rest
            out.write_text(fm + '---\n\n' + body, encoding='utf-8', newline='\n')
        self.lang_pages[lang] = {
            'devices': devices.write(src, cat, lang, self.reports),
            'modules': devices.write_modules(src, cat, lang),
            'project': project_pages.write(src, cat, lang, self.reports, self.open_issues, devices.load()),
        }

    def config_lang(self, lang):
        """mkdocs.<lang>.gathered.yml: the English configuration made the language's site's. It is
        written, never kept, so the words in it - the menu, the footer - stay in the catalogue."""
        cat = json.loads((HERE / 'i18n' / ('%s.json' % lang)).read_text(encoding='utf-8'))
        src = src_of(lang)
        text = (HERE / 'mkdocs.yml').read_text(encoding='utf-8')
        text = re.sub(r'\n  # blog-begin\n.*?\n  # blog-end\n', '\n', text, flags=re.S)
        text = re.sub(r'\nnot_in_nav: \|\n(?:  .*\n)+', '\nnot_in_nav: |\n  /compare.md\n', text)
        text = re.sub(r'\ncopyright: >-\n(?:  .*\n)+', '\ncopyright: %s\n' % json.dumps(cat['footer']['copyright'], ensure_ascii=False), text)
        contact = '/%s/contact/' % lang if (src / 'contact.md').exists() else '/contact/'
        for a, b in (('site_url: https://npuforge.org/', 'site_url: https://npuforge.org/%s/' % lang),
                     ('\ndocs_dir: pages\n', '\ndocs_dir: .build/src-%s\n' % lang),
                     ('\nsite_dir: .build/site\n', '\nsite_dir: .build/site/%s\n' % lang),
                     ('  language: en\n', '  language: %s\n' % lang),
                     ('  logo: assets/logo.svg\n', '  logo: /assets/logo.svg\n'),
                     ('  favicon: assets/favicon.svg\n', '  favicon: /assets/favicon.svg\n'),
                     ('  - assets/site.css\n', '  - /assets/site.css\n'),
                     ('  - assets/site.js\n', '  - /assets/site.js\n'),
                     ('      link: /contact/\n', '      link: %s\n' % contact)):
            assert a in text, a
            text = text.replace(a, b)
        n = {k: json.dumps(v, ensure_ascii=False) for k, v in cat['nav'].items()}
        made = self.lang_pages[lang]

        def page(rel, key, english):
            """A page translated into this language, or the English one, under the catalogue's word."""
            return '    - ' + rel if (src / rel).exists() else '    - %s: %s' % (n[key], english)

        def guide(p):
            return page('guide/%s.md' % p, p.replace('-', '_'), '/docs/guide/%s/' % p)

        # The English site's sections, in its order, so the tabs keep their icons.
        nav = ['nav:', '  - %s: index.md' % n['home']]
        nav.append('  - %s:' % n['start'])
        nav += [guide(p) for p in START_PAGES]
        nav.append('    - %s: %s' % (n['builder'], IMAGE_BUILDER))
        nav.append('  - %s:' % n['hardware'])
        nav.append('    - %s:' % n['devices'])
        nav += ['      - ' + sp for sp in made['devices']]
        nav.append('    - %s:' % n['modules'])
        nav += ['      - ' + sp for sp in made['modules']]
        nav.append('    - %s: /compatibility/' % n['compatibility'])
        nav.append('  - %s:' % n['measurements'])
        nav.append(page('measurements/index.md', 'all_measurements', '/docs/measurements/'))
        nav.append(page('measurements/methods.md', 'methods', '/docs/measurements/methods/'))
        nav.append('  - %s:' % n['documentation'])
        nav += [guide('how-it-works'), guide('glossary')]
        nav.append('    - %s: /docs/' % n['docs'])
        nav.append('  - %s: /story/' % n['story'])
        nav.append('  - %s:' % n['project'])
        nav.append(page('project/index.md', 'about', '/project/'))
        nav += ['    - ' + sp for sp in made['project']]
        nav.append('    - %s: /news/' % n['news'])
        nav.append('  - %s: %s' % (n['contact'], 'contact.md' if (src / 'contact.md').exists() else '/contact/'))
        cfg = HERE / ('mkdocs.%s.gathered.yml' % lang)
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
        placed = set()

        def folder_pages(folder):
            """A folder's pages, its README first, the findings in the order docs/README.md gives."""
            pages = sorted(sp for sp in docs if sp.startswith('docs/%s/' % folder))
            if folder == 'findings':
                pages = self.findings_order(pages)
            readme = 'docs/%s/README.md' % folder
            if readme in pages:
                pages.remove(readme)
                pages.insert(0, readme)
            placed.update(pages)
            return pages

        def group(label, pages, indent='    '):
            if pages:
                nav.append('%s- %s:' % (indent, label))
                nav.extend('%s  - %s' % (indent, sp) for sp in pages)

        nav = ['nav:', '  - Home: index.md']
        # getting started: the guide's pages a new reader wants first, and the image builder
        start = [sp for sp in ('docs/guide/%s.md' % n for n in START_PAGES) if sp in self.sources]
        placed.update(start)
        nav.append('  - Get started:')
        nav += ['    - ' + sp for sp in start]
        nav.append('    - Download or build an image: ' + IMAGE_BUILDER)
        # the hardware: every model, every module, owners' reports, the families, the two appliances
        nav.append('  - Hardware:')
        group('Models', self.device_pages)
        group('Modules', self.module_pages)
        nav.append('    - Compatibility: compatibility.md')
        group('Families', folder_pages('families'))
        group('The two appliances', folder_pages('hardware'))
        # the numbers: by appliance, how they are taken, and what they found
        meas = sorted((sp for sp in docs if sp.startswith('docs/measurements/') and not sp.endswith('/methods.md')),
                      key=lambda sp: (not sp.endswith('README.md'), sp))
        placed.update(meas)
        nav.append('  - Measurements:')
        group('By appliance', meas)
        if 'docs/measurements/methods.md' in self.sources:
            nav.append('    - How they are taken: docs/measurements/methods.md')
            placed.add('docs/measurements/methods.md')
        group('Findings', folder_pages('findings'))
        # the documentation: how it works, the drivers, the reference
        nav.append('  - Documentation:')
        nav.append('    - docs/README.md')
        placed.add('docs/README.md')
        if 'docs/guide/glossary.md' in self.sources:
            nav.append('    - Glossary: docs/guide/glossary.md')
            placed.add('docs/guide/glossary.md')
        guide = sorted((sp for sp in docs if sp.startswith('docs/guide/') and sp not in placed),
                       key=lambda sp: (GUIDE_ORDER.index(posixpath.basename(sp)[:-3])
                                       if posixpath.basename(sp)[:-3] in GUIDE_ORDER else 99, sp))
        placed.update(guide)
        group('How it works', guide)
        for label, folder in DOC_SECTIONS:
            group(label, folder_pages(folder))
        reference = [sp for sp in ('docs/' + name for name in DOC_REFERENCE) if sp in self.sources]
        placed.update(reference)
        group('Reference', reference)
        group('Other pages', sorted(set(docs) - placed))
        # the story, a tab of its own - the wiki itself when no checkout of it was given, so the tabs
        # keep their places and their icons
        if self.wiki:
            nav.append('  - The story:')
            nav.append('    - story/index.md')
            nav += ['    - ' + story_path(n) for n in STORY if story_path(n) in self.sources]
        else:
            nav.append('  - The story: ' + WIKI_PREFIX)
        # the project: what it is, its news, and how it is run
        nav.append('  - Project:')
        nav.append('    - project/index.md')
        nav.append('    - Where it stands: status.md')
        nav.append('    - Releases: releases.md')
        nav.append('    - News:')
        nav.append('      - news/index.md')
        nav += ['    - ' + sp for sp in PROJECT_PAGES.values() if sp in self.sources]
        # and where to write, last and on its own, so nobody has to look for it
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


def titles(site_dir):
    """titles.json: each page's title by its address, for /stats/, which the counts give as addresses
    only. Read from the search indexes MkDocs has just written, one a language; the home pages are
    not in them."""
    out = {'/': 'Home'}
    out.update(('/%s/' % lang, 'Home, in %s' % LANGUAGE_NAMES[lang]) for lang in languages())
    for prefix, index in [('/', site_dir / 'search' / 'search_index.json')] + [
            ('/%s/' % lang, site_dir / lang / 'search' / 'search_index.json') for lang in languages()]:
        try:
            docs = json.loads(index.read_text(encoding='utf-8'))['docs']
        except (OSError, ValueError, KeyError):
            continue
        for d in docs:
            loc = d.get('location', '')
            if not loc or '#' in loc:
                continue
            out.setdefault(prefix + loc, (d.get('title') or '').strip() or prefix + loc)
    (site_dir / 'titles.json').write_text(json.dumps(out, ensure_ascii=False, sort_keys=True), encoding='utf-8')
    print('titles: %d pages' % len(out))


def sitemap(site_dir):
    """A sitemap for each language, in place of the ones MkDocs writes: every page with the day it
    last changed in the repository (not the day of the build, which says nothing), and beside each
    page that is in more than one language, the others (hreflang, as xhtml:link). hooks.py recorded
    the pages as it wrote them, into a .seo.json beside each site; those are read and removed here.

    One for each language and not one for all, because the theme takes its own site's sitemap for
    the pages it may open without a full load: an Arabic page listed in the English one was opened
    inside the English layout, left to right. robots.txt names each, the languages' added here.

    And indexnow.txt: the pages that changed today, which the workflow offers to the search engines
    that take IndexNow once the site is deployed - at every merge, so a change is offered the day it
    is published."""
    import datetime
    import gzip
    from xml.sax.saxutils import escape, quoteattr
    pages = {}
    for f in [site_dir / '.seo.json'] + [site_dir / lang / '.seo.json' for lang in languages()]:
        if f.exists():
            pages.update(json.loads(f.read_text(encoding='utf-8')))
            f.unlink()
    if not pages:
        return
    prefixes = {lang: 'https://npuforge.org/%s/' % lang for lang in languages()}
    sites = [(site_dir, [u for u in pages if not any(u.startswith(p) for p in prefixes.values())])]
    sites += [(site_dir / lang, [u for u in pages if u.startswith(p)]) for lang, p in prefixes.items()]
    for where, urls in sites:
        lines = ['<?xml version="1.0" encoding="UTF-8"?>',
                 '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">']
        for url in sorted(urls, key=lambda u: (u.count('/'), u)):
            p = pages[url]
            lines.append('  <url>')
            lines.append('    <loc>%s</loc>' % escape(url))
            if p.get('lastmod'):
                lines.append('    <lastmod>%s</lastmod>' % p['lastmod'])
            if len(p.get('alternates') or []) > 2:
                for lang, href in p['alternates']:
                    lines.append('    <xhtml:link rel="alternate" hreflang=%s href=%s/>' % (quoteattr(lang), quoteattr(href)))
            lines.append('  </url>')
        lines.append('</urlset>')
        xml = '\n'.join(lines) + '\n'
        (where / 'sitemap.xml').write_text(xml, encoding='utf-8', newline='\n')
        (where / 'sitemap.xml.gz').write_bytes(gzip.compress(xml.encode('utf-8'), mtime=0))
    robots = site_dir / 'robots.txt'
    if robots.exists():
        text = robots.read_text(encoding='utf-8')
        text += ''.join('Sitemap: %ssitemap.xml\n' % p for p in prefixes.values() if p + 'sitemap.xml' not in text)
        robots.write_text(text, encoding='utf-8', newline='\n')
    today = datetime.date.today().isoformat()
    fresh = [u for u, p in sorted(pages.items()) if (p.get('lastmod') or '')[:10] == today]
    (BUILD / 'indexnow.txt').write_text(''.join(u + '\n' for u in fresh), encoding='utf-8', newline='\n')
    print('sitemaps: %d pages, %d in more than one language, %d changed today'
          % (len(pages), sum(1 for p in pages.values() if len(p.get('alternates') or []) > 2), len(fresh)))


def repository_facts(release):
    """The facts Material shows beside the repository's name - the release, the stars, the forks -
    read here, at the build, rather than by every reader's browser from GitHub: the pages fetch
    nothing from anyone else (the privacy page says so, and the site's CSP holds it to that).
    site.js brings them up to date from the site's own /api/project."""
    facts = {'version': release} if release else {}
    try:
        headers = {'User-Agent': 'npuforge.org build', 'Accept': 'application/vnd.github+json'}
        if os.environ.get('GITHUB_TOKEN'):
            headers['Authorization'] = 'Bearer ' + os.environ['GITHUB_TOKEN']
        req = urllib.request.Request('https://api.github.com/repos/%s' % REPO, headers=headers)
        with urllib.request.urlopen(req, timeout=20) as r:
            repo = json.loads(r.read().decode('utf-8'))
        facts.update(stars=repo['stargazers_count'], forks=repo['forks_count'])
    except (OSError, ValueError, KeyError):
        pass
    return facts


# The repositories the home page shows with their counts, and the order it shows them in.
# worker/index.js answers /api/repos for these names and no others.
SHOWN_REPOS = ['os-xgs-npu', 'xgs-image-builder']


def shown_repositories():
    """Each shown repository's newest release, stars and forks, read here at the build
    like the facts above - the reader's browser never asks GitHub; site.js brings them up to date from
    the site's own /api/repos. A repository GitHub does not answer for is left out, and its card shows
    no counts."""
    owner = REPO.split('/')[0]
    headers = {'User-Agent': 'npuforge.org build', 'Accept': 'application/vnd.github+json'}
    if os.environ.get('GITHUB_TOKEN'):
        headers['Authorization'] = 'Bearer ' + os.environ['GITHUB_TOKEN']

    def get(path):
        req = urllib.request.Request('https://api.github.com/repos/%s/%s' % (owner, path), headers=headers)
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read().decode('utf-8'))
    out = {}
    for name in SHOWN_REPOS:
        try:
            repo, releases = get(name), get(name + '/releases?per_page=1')
        except (OSError, ValueError):
            continue
        newest = releases[0] if releases else None
        out[name] = {'stars': repo.get('stargazers_count'), 'forks': repo.get('forks_count'),
                     'release': newest['tag_name'] if newest else None, 'release_url': newest['html_url'] if newest else None}
    return out


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
    langs = languages()
    for lang in langs:
        site.gather_lang(lang)
    cfg = site.config()
    cfg_langs = [site.config_lang(lang) for lang in langs]
    for w in warnings:
        print('WARNING -', w, file=sys.stderr)
    release = latest_release()
    env = dict(os.environ, NPUFORGE_RELEASE=release, NPUFORGE_WIKI=str(wiki.resolve()) if wiki else '',
               NPUFORGE_FACTS=json.dumps(repository_facts(release)), NPUFORGE_REPOS=json.dumps(shown_repositories()))
    cmd = [sys.executable, '-m', 'mkdocs', 'serve' if args.serve else 'build', '-f', str(cfg)]
    if args.check:
        cmd.append('--strict')
    if args.serve:
        cmd += ['--dev-addr', '127.0.0.1:8000', '--no-livereload']
    rc = subprocess.call(cmd, cwd=HERE, env=env)
    for cfg_lang in cfg_langs:
        if rc == 0 and not args.serve:
            # each language's site into /<lang>/ of the English one, which its build has just emptied
            cmd_lang = [sys.executable, '-m', 'mkdocs', 'build', '-f', str(cfg_lang)] + (['--strict'] if args.check else [])
            rc = subprocess.call(cmd_lang, cwd=HERE, env=env)
    if rc == 0 and not args.serve:
        # the README's status board, drawn from the family pages' tables as they stand - the README
        # shows it from here, so it follows every re-weighing without a commit of its pictures
        rc = subprocess.call([sys.executable, str(ROOT / 'tools' / 'readme-art.py'), '--out', str(BUILD / 'site' / 'readme')])
    if rc == 0 and not args.serve:
        sitemap(BUILD / 'site')
        titles(BUILD / 'site')
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
