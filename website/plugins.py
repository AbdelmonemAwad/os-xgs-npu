#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The site's pages for the plugins of the os-xgs-npu family: one that lists them by what they are
for, and one for each - in every language the site has.

build.py calls write() for each language while it gathers the pages, as it does devices.write().
Everything a page says about a plugin is its file, website/data/plugins/<repo>.json; every word
around it is the catalogue's, under "plugins". A plugin with no file has no page, and a link to its
page anywhere on the site - a card on the project page, on the home page - goes to its repository
instead (fallback(), which hooks.py runs over every page and the home page's cards).

A file, checked here before anything is written (DataError names the file and the key):

    repo           the repository's name under AbdelmonemAwad, and the file's own name
    title          its name, as its page in the menu has it; no leading "The"
    icon           a Material Design Icons name without "material-", one of the theme's own
    group          appliance, system, protection, reports, language or look - the list's sections
    order          its place in the XGS menu (50-58), 90 and up for one with no page of its own
    menu           {"xgs": where its page is with os-xgs-npu, "own": where without}, or null
    screenshot     {"en": a path under docs/images/, and the other languages' own where there is
                   one}, or null; a picture not in the repository yet is left out, with a warning
    first_release  the day of its first release, YYYY-MM-DD
    text           for each of en, ar, de, es, fr, ja, ru, tr: summary (a sentence), body (two to
                   four paragraphs), features ([title, text] pairs), install (a paragraph) and
                   not_tested (a list, empty when nothing is left); each language as many of each
                   as English has
"""
import datetime
import html
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
DATA = HERE / 'data' / 'plugins'
GH = 'https://github.com/AbdelmonemAwad/os-xgs-npu'
OWNER = 'https://github.com/AbdelmonemAwad/'
LANGS = ('en', 'ar', 'de', 'es', 'fr', 'ja', 'ru', 'tr')
# The list's sections, in its order.
GROUPS = ('appliance', 'system', 'protection', 'reports', 'language', 'look')
KEYS = ('repo', 'title', 'icon', 'group', 'order', 'menu', 'screenshot', 'first_release', 'text')
TEXT = ('summary', 'body', 'features', 'install', 'not_tested')
PICTURES = ('.png', '.jpg', '.jpeg', '.webp')
# A link to a plugin's page, in any language's site: /plugins/os-fanctl/, /ar/plugins/os-fanctl/ -
# on its own (a catalogue's card), or in a page, between the brackets or the quotes around it.
PAGE = r'/(?:[a-z]{2}/)?plugins/([a-z0-9][a-z0-9-]*)/'
PAGE_LINK = re.compile(r'(?<=[("\'])%s(?=[)"\'#])' % PAGE)
# What the pages never carry (website/data/plugins is published as it is): an address of anyone's
# outside the lab's 10.99 ranges, a MAC address, the name of the server the lab once had.
MAC = re.compile(r'\b[0-9a-f]{2}(?::[0-9a-f]{2}){5}\b', re.I)
IPV4 = re.compile(r'\b(?:\d{1,3}\.){3}\d{1,3}\b')
NAMES = re.compile(r'synology|\bdsm\b', re.I)

_warned = set()


class DataError(ValueError):
    pass


def icons():
    """The theme's own Material Design Icons, or None where the theme is not installed."""
    try:
        import material
    except ImportError:
        return None
    d = pathlib.Path(material.__file__).resolve().parent / 'templates' / '.icons' / 'material'
    return d if d.is_dir() else None


def strings(x, key):
    if isinstance(x, str):
        yield key, x
    elif isinstance(x, list):
        for i, v in enumerate(x):
            yield from strings(v, '%s[%d]' % (key, i))
    elif isinstance(x, dict):
        for k, v in x.items():
            yield from strings(v, '%s.%s' % (key, k) if key else k)


def validate(path, d):
    name = path.relative_to(ROOT).as_posix() if ROOT in path.parents else str(path)

    def fail(key, what):
        raise DataError('%s: %s: %s' % (name, key, what))

    def text_of(key, v):
        if not isinstance(v, str) or not v.strip():
            fail(key, 'a sentence or a paragraph, not %r' % (v,))

    if not isinstance(d, dict):
        fail('(the file)', 'an object')
    for k in KEYS:
        if k not in d:
            fail(k, 'missing')
    for k in d:
        if k not in KEYS:
            fail(k, 'not a key of a plugin\'s file (%s)' % ', '.join(KEYS))
    if d['repo'] != path.stem or not re.fullmatch(r'[a-z0-9][a-z0-9-]*', str(d['repo'])):
        fail('repo', '%r, where the file is %s.json' % (d['repo'], path.stem))
    text_of('title', d['title'])
    if d['title'].startswith('The '):
        fail('title', 'no leading "The"')
    if not isinstance(d['icon'], str) or not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', d['icon']) or d['icon'].startswith('material-'):
        fail('icon', 'a Material Design Icons name without "material-", not %r' % (d['icon'],))
    have = icons()
    if have is not None and not (have / (d['icon'] + '.svg')).exists():
        fail('icon', '%r is not one of the theme\'s icons' % d['icon'])
    if d['group'] not in GROUPS:
        fail('group', 'one of %s, not %r' % (', '.join(GROUPS), d['group']))
    if not isinstance(d['order'], int) or isinstance(d['order'], bool) or d['order'] < 0:
        fail('order', 'a whole number, not %r' % (d['order'],))
    menu = d['menu']
    if menu is not None:
        if not isinstance(menu, dict) or not menu or any(k not in ('xgs', 'own') for k in menu):
            fail('menu', 'null, or {"xgs": ..., "own": ...}')
        for k, v in menu.items():
            text_of('menu.' + k, v)
    shot = d['screenshot']
    if shot is not None:
        if not isinstance(shot, dict) or 'en' not in shot:
            fail('screenshot', 'null, or {"en": ..., and the other languages\' own}')
        for k, v in shot.items():
            if k not in LANGS:
                fail('screenshot.' + k, 'not a language of the site (%s)' % ', '.join(LANGS))
            if (not isinstance(v, str) or not v.startswith('docs/images/') or '..' in v or '\\' in v
                    or not v.lower().endswith(PICTURES)):
                fail('screenshot.' + k, 'a picture under docs/images/, not %r' % (v,))
    try:
        datetime.date.fromisoformat(d['first_release'])
        if not re.fullmatch(r'\d{4}-\d{2}-\d{2}', d['first_release']):
            raise ValueError
    except (TypeError, ValueError):
        fail('first_release', 'a day, YYYY-MM-DD, not %r' % (d['first_release'],))
    text = d['text']
    if not isinstance(text, dict):
        fail('text', 'an object, a key for each language')
    for lang in LANGS:
        if lang not in text:
            fail('text.' + lang, 'missing: every language of the site is there')
    for lang in text:
        if lang not in LANGS:
            fail('text.' + lang, 'not a language of the site (%s)' % ', '.join(LANGS))
    for lang in LANGS:
        t, at = text[lang], 'text.' + lang
        if not isinstance(t, dict):
            fail(at, 'an object')
        for k in TEXT:
            if k not in t:
                fail('%s.%s' % (at, k), 'missing')
        for k in t:
            if k not in TEXT:
                fail('%s.%s' % (at, k), 'not a key of a language\'s text (%s)' % ', '.join(TEXT))
        text_of(at + '.summary', t['summary'])
        text_of(at + '.install', t['install'])
        if not isinstance(t['body'], list) or not 2 <= len(t['body']) <= 4:
            fail(at + '.body', 'two to four paragraphs')
        for i, v in enumerate(t['body']):
            text_of('%s.body[%d]' % (at, i), v)
        if not isinstance(t['features'], list) or not t['features']:
            fail(at + '.features', 'a list of [title, text] pairs')
        for i, v in enumerate(t['features']):
            if not isinstance(v, list) or len(v) != 2:
                fail('%s.features[%d]' % (at, i), 'a pair, [title, text]')
            text_of('%s.features[%d][0]' % (at, i), v[0])
            text_of('%s.features[%d][1]' % (at, i), v[1])
        if not isinstance(t['not_tested'], list):
            fail(at + '.not_tested', 'a list, empty when nothing is left')
        for i, v in enumerate(t['not_tested']):
            text_of('%s.not_tested[%d]' % (at, i), v)
        if lang != 'en':
            for k in ('body', 'features', 'not_tested'):
                if len(t[k]) != len(text['en'][k]):
                    fail('%s.%s' % (at, k), '%d, where English has %d: each language says the same'
                         % (len(t[k]), len(text['en'][k])))
    for key, s in strings(d, ''):
        if MAC.search(s):
            fail(key, 'a MAC address')
        for ip in IPV4.findall(s):
            if not ip.startswith('10.99.'):
                fail(key, 'an address (%s): only the lab\'s 10.99 ranges' % ip)
        if NAMES.search(s):
            fail(key, 'the server\'s name: "a server"')


def load():
    """Every plugin with a file, checked, in the list's order: by section, then by its place."""
    out = []
    for f in sorted(DATA.glob('*.json')) if DATA.is_dir() else []:
        try:
            d = json.loads(f.read_text(encoding='utf-8'))
        except ValueError as e:
            raise DataError('%s: not JSON (%s)' % (f.relative_to(ROOT).as_posix(), e))
        validate(f, d)
        out.append(d)
    return sorted(out, key=lambda d: (GROUPS.index(d['group']), d['order'], d['repo']))


def repos():
    """The plugins that have a page, for hooks.py: none when the files cannot be read."""
    try:
        return [d['repo'] for d in load()]
    except (OSError, ValueError):
        return []


def fallback(text, have):
    """Every link in a page to the page of a plugin that has none - no file in website/data/plugins -
    made a link to its repository: the card keeps working until the plugin's page is there."""
    return PAGE_LINK.sub(lambda m: m.group(0) if m.group(1) in have else OWNER + m.group(1), text)


def fallback_link(url, have):
    """The same for a link on its own, a catalogue's: the plugin's page, or its repository."""
    m = re.fullmatch(PAGE, url or '')
    return OWNER + m.group(1) if m and m.group(1) not in have else url


# -- the pages ------------------------------------------------------------------------------------

def esc(s):
    return html.escape(str(s), quote=True)


def b(s):
    """A run of its own direction - a menu path, a repository's name - inside an Arabic sentence."""
    return '<bdi>%s</bdi>' % s


def picture_size(path):
    """(width, height) of a PNG from its header, or None - the page does not jump as it loads."""
    try:
        with open(path, 'rb') as f:
            head = f.read(24)
    except OSError:
        return None
    if head[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    return int.from_bytes(head[16:20], 'big'), int.from_bytes(head[20:24], 'big')


def screenshot(p, w, lang):
    """The plugin's page in the GUI - or the GUI with it, for one with no page of its own - the
    language's own where there is one, else the English; None when there is none, or the picture is
    not in the repository yet. The caption says "in English" for a picture of the English GUI shown
    on another language's page (screens/en/; the Arabic translation's picture is the Arabic GUI's in
    every language)."""
    shot = p['screenshot']
    if not shot:
        return None
    rp = shot.get(lang) or shot['en']
    english = lang != 'en' and lang not in shot and '/en/' in rp
    what = w['shot_page'] if p['menu'] else w['shot_gui']
    if not (ROOT / rp).is_file():
        if rp not in _warned:
            _warned.add(rp)
            print('plugins: %s, the screenshot of %s, is not in the repository yet - its page is written without it'
                  % (rp, p['repo']), file=sys.stderr)
        return None
    size = picture_size(ROOT / rp)
    return ('<figure class="nf-shot"><img src="/%s" alt="%s"%s>'
            '<figcaption>%s</figcaption></figure>\n') % (
        rp, esc(w['shot_alt'].format(title=p['title'])),
        ' width="%d" height="%d"' % size if size else '', esc(w['shot_english' if english else 'shot_caption'].format(what=what)))


def front_matter(title, description, lang, edit, extra=''):
    fm = '---\ntitle: %s\ndescription: %s\nedit_url: %s/edit/main/%s\n' % (
        json.dumps(title, ensure_ascii=False), json.dumps(description, ensure_ascii=False), GH, edit)
    if lang != 'en':
        fm += 'lang: %s\n' % lang
    return fm + extra + '---\n\n'


def page_md(p, t, lang):
    w = t['plugins']
    x = p['text'][lang]
    repo = OWNER + p['repo']
    # The discussion under a page is the English site's, as the models' (devices.py).
    md = [front_matter(p['title'], x['summary'], lang, 'website/data/plugins/%s.json' % p['repo'],
                       'comments: true\n' if lang == 'en' else ''),
          '# :material-%s: %s { .nf-plugin__title }\n' % (p['icon'], p['title'])]
    md.append('<p class="nf-dev-lede">%s · %s</p>\n' % (b('<code>%s</code>' % esc(p['repo'])), esc(w['groups'][p['group']])))
    md.append(x['summary'] + '\n')
    if p['menu']:
        md.append('!!! info "%s"\n' % w['menu'])
        for k in ('xgs', 'own'):
            if p['menu'].get(k):
                md.append('    - %s' % w['menu_' + k].format(place='<strong>%s</strong>' % b(esc(p['menu'][k]))))
        md.append('')
    shot = screenshot(p, w, lang)
    if shot:
        md.append(shot)
    md += [para + '\n' for para in x['body']]

    md.append('## %s\n' % w['features'])
    md.append('<div class="grid cards" markdown>\n')
    for title, text in x['features']:
        md.append('-   **%s**\n\n    %s\n' % (title, text))
    md.append('</div>\n')

    md.append('## %s\n' % w['install'])
    md.append(x['install'] + '\n')

    if x['not_tested']:
        md.append('## %s\n' % w['not_tested'])
        md += ['- ' + item for item in x['not_tested']]
        md.append('')

    md.append('## %s\n' % w['repository'])
    md.append('%s: %s\n' % (w['first_release'], b(p['first_release'])))
    md.append('[:fontawesome-brands-github: %s](%s){ .md-button .md-button--primary } [%s](%s/releases){ .md-button }\n' % (
        p['repo'], repo, w['releases'], repo))
    return '\n'.join(md)


def index_md(plugins, t, lang):
    w = t['plugins']
    project = '/%s/project/' % lang if lang != 'en' and (HERE / 'i18n' / lang / 'project' / 'index.md').exists() else '/project/'
    md = [front_matter(w['title'], w['description'], lang, 'website/plugins.py'), '# %s\n' % w['title'],
          w['lede'].format(project=project) + '\n']
    for group in GROUPS:
        here = [p for p in plugins if p['group'] == group]
        if not here:
            continue
        md.append('## %s\n' % w['groups'][group])
        md.append('<div class="grid cards" markdown>\n')
        for p in here:
            md.append('-   :material-%s: **[%s](%s/)** · `%s`\n\n    ---\n\n    %s\n' % (
                p['icon'], p['title'], p['repo'], p['repo'], p['text'][lang]['summary']))
        md.append('</div>\n')
    return '\n'.join(md)


def write(out_dir, t, lang):
    """The pages into out_dir/plugins/; their paths there, for the navigation - none when no plugin
    has a file yet."""
    plugins = load()
    if not plugins:
        return []
    d = pathlib.Path(out_dir) / 'plugins'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'index.md').write_text(index_md(plugins, t, lang), encoding='utf-8', newline='\n')
    paths = ['plugins/index.md']
    for p in plugins:
        (d / ('%s.md' % p['repo'])).write_text(page_md(p, t, lang), encoding='utf-8', newline='\n')
        paths.append('plugins/%s.md' % p['repo'])
    return paths
