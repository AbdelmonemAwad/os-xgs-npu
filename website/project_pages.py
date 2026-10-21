#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Three of the site's pages that are written at the build, in each language from its catalogue
("project"), out of what the repository and GitHub already say:

  * status.md - where the project stands: each appliance's completion, weighed from its family page's
    table as tools/readme-art.py weighs it, the models added untested and what their appliances have
    reported, and the issues open on the day of the build;
  * releases.md - every release, from CHANGELOG.md, each with its news post and its files on GitHub;
  * compare.md - the vendor's figures beside ours, for the two appliances run here. It is not in the
    navigation, not in the search, not in the sitemap and asks not to be indexed: it is kept hidden
    until it is decided to show it.
"""
import importlib.util
import json
import pathlib
import re

from devices import translated

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
GH = 'https://github.com/AbdelmonemAwad/os-xgs-npu'

_spec = importlib.util.spec_from_file_location('readme_art', ROOT / 'tools' / 'readme-art.py')
readme_art = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(readme_art)


def b(s):
    return '<bdi>%s</bdi>' % s


def html_links(s):
    """A catalogue sentence's Markdown links as HTML, for a sentence that goes inside HTML."""
    return re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', s)


def front_matter(title, description, lang, extra=''):
    fm = '---\ntitle: %s\ndescription: %s\nedit_url: %s/edit/main/website/project_pages.py\n' % (
        json.dumps(title, ensure_ascii=False), json.dumps(description, ensure_ascii=False), GH)
    if lang != 'en':
        fm += 'lang: %s\n' % lang
    return fm + extra + '---\n\n'


def remainder(page):
    """The rows of a family page's weighed table that are not done, and the weight they leave."""
    text = (ROOT / 'docs' / page).read_text(encoding='utf-8').replace('\r\n', '\n')
    end = text.rindex('\nWeights sum to ')
    total = int(re.match(r'\nWeights sum to (\d+)', text[end:]).group(1))
    rows, left = 0, 0.0
    for line in reversed(text[:end].rstrip('\n').split('\n')):
        if not line.startswith('|'):
            break
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if not cells[0].isdigit():
            continue
        pct = int(re.search(r'(\d+)%', re.sub(r'~~.*?~~', '', cells[2])).group(1))
        if pct < 100:
            rows += 1
            left += (100 - pct) * int(cells[3]) / 100
    return rows, left, total


# -- where the project stands -----------------------------------------------------------------------

def status_md(t, lang, base, reports, issues, releases, devices):
    w = t['project']
    md = [front_matter(w['status_title'], w['status_description'], lang), '# %s\n' % w['status_title'], w['status_lede'] + '\n']
    md.append('## %s\n' % w['status_run'])
    md.append('<div class="nf-status">\n')
    for name, page in readme_art.WEIGHED.items():
        pct = int(readme_art.weighed(page) + 0.5)
        rows, left, total = remainder(page)
        slug = name.lower().replace(' ', '-')
        md.append('<div class="nf-status__card"><p class="nf-status__name"><a href="%s/devices/%s/">%s</a></p>'
                  '<p class="nf-status__pct">%d%%</p><div class="nf-status__bar"><span style="width: %d%%"></span></div>'
                  '<p class="nf-status__left">%s</p></div>\n' % (
                      base, slug, name, pct, pct, html_links(w['status_left'].format(rows=rows, left=b('%.2f' % left), total=total,
                                                                                    link='/docs/%s/' % page[:-3]))))
    md.append('</div>\n')
    md.append(w['status_how'].format(link=translated(lang, 'measurements/methods', '/docs/measurements/methods/')) + '\n')

    md.append('## %s\n' % w['status_untested'])
    untested = [p for p in devices['pages'] if not p['run']]
    seen = [p['model'] for p in untested if (reports or {}).get(p['slug'], {}).get('problems')]
    md.append(w['status_untested_text'].format(count=len(untested), link='%s/devices/' % base) + '\n')
    if seen:
        md.append(w['status_seen'].format(models=b(', '.join(seen))) + '\n')

    md.append('## %s\n' % w['status_issues'])
    if issues is None:
        md.append(w['status_unread'] + '\n')
    else:
        open_issues = [i for i in issues if not any(l['name'] == 'problem report' for l in i.get('labels', []))]
        problems = len(issues) - len(open_issues)
        md.append(w['status_issues_text'].format(count=len(open_issues), problems=problems,
                                                 link=GH + '/issues', problems_link=GH + '/issues?q=' +
                                                 'is%3Aissue%20is%3Aopen%20label%3A%22problem%20report%22') + '\n')
        if open_issues:
            md.append('| %s |' % ' | '.join(w['status_issues_head']))
            md.append('|---|---|---|')
            for i in sorted(open_issues, key=lambda i: i['updated_at'], reverse=True)[:25]:
                title = i['title'].replace('|', '/').replace('<', '&lt;')
                md.append('| [#%d](%s) | %s | %s |' % (i['number'], i['html_url'], b(title), b(i['updated_at'][:10])))
            md.append('')

    if releases:
        v, date, _ = releases[0]
        md.append('## %s\n' % w['status_release'])
        md.append(w['status_release_text'].format(version=b(v), date=b(date), link='%s/releases/' % base) + '\n')
    return '\n'.join(md)


# -- releases ---------------------------------------------------------------------------------------

def changelog():
    text = (ROOT / 'CHANGELOG.md').read_text(encoding='utf-8').replace('\r\n', '\n')
    out = []
    for m in re.finditer(r'^## (\d+\.\d+\.\d+\S*) - (\d{4}-\d{2}-\d{2})\n(.*?)(?=^## |\Z)', text, re.M | re.S):
        body = m.group(3).strip()
        lead = body.split('\n\n', 1)[0] if not body.startswith('#') else ''
        lead = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', ' '.join(lead.split()))
        out.append((m.group(1), m.group(2), lead))
    return out


def releases_md(t, lang, base, releases):
    w = t['project']
    md = [front_matter(w['releases_title'], w['releases_description'], lang), '# %s\n' % w['releases_title'],
          w['releases_lede'].format(builder='https://build.npuforge.org/',
                                    updating=translated(lang, 'guide/updating', '/docs/guide/updating/')) + '\n']
    for v, date, lead in releases:
        slug = 'release-' + re.sub(r'[^a-z0-9]+', '-', v.lower()).strip('-')
        md.append('## %s\n' % b(v))
        md.append('<p class="nf-dev-lede">%s</p>\n' % b(date))
        if lead:
            md.append(lead + '\n')
        md.append(w['releases_links'].format(news='/news/%s/' % slug, github='%s/releases/tag/v%s' % (GH, v)) + '\n')
    return '\n'.join(md)


# -- the vendor's figures beside ours: hidden for now ------------------------------------------------

# For each appliance run here: a figure the vendor's datasheet gives, the one measured here that comes
# nearest, the page it is on, and what bounds ours. Ours are bounded by the links of the bench, not by
# the appliance, and the vendor's are taken under ideal conditions its own way: they sit side by side,
# they are not the same kind of number.
COMPARE = {
    'XGS 3300': [
        ('fw', '58 Gbps', '9,837 Mbit/s', 'docs/measurements/xgs3300-many-2026-10-19.md', 'link10'),
        ('ipsec', '31.1 Gbps', '9,019 Mbit/s', 'docs/measurements/many-tunnels-3300-2026-10-20.md', 'link10'),
        ('ipsec_tunnels', '6,500', '16', 'docs/measurements/many-tunnels-3300-2026-10-20.md', 'count'),
        ('conns', '13,700,000', '1,032', 'docs/measurements/xgs3300-many-2026-10-19.md', 'count'),
    ],
    'XGS 136': [
        ('fw', '11,500 Mbps', '945 Mbit/s', 'docs/measurements/xgs136-soak-2026-10-19.md', 'link1'),
        ('ipsec', '6,350 Mbps', '915 Mbit/s', 'docs/measurements/many-tunnels-136-2026-10-20.md', 'link1'),
        ('ipsec_tunnels', '2,500', '6', 'docs/measurements/many-tunnels-136-2026-10-20.md', 'count'),
    ],
}


def compare_md(t, lang, base):
    w, m = t['project'], t['devices']['metrics']
    extra = 'hidden: true\nsearch:\n  exclude: true\n'
    md = [front_matter(w['compare_title'], w['compare_description'], lang, extra), '# %s\n' % w['compare_title'],
          w['compare_lede'] + '\n']
    for model, rows in COMPARE.items():
        md.append('## %s\n' % model)
        md.append('| %s |' % ' | '.join(w['compare_head']))
        md.append('|---|--:|--:|---|')
        for metric, theirs, ours, page, bound in rows:
            md.append('| %s | %s | %s | %s |' % (m[metric], b(theirs), '[%s](/%s/)' % (b(ours), page[:-3]), w['compare_bound'][bound]))
        md.append('')
    return '\n'.join(md)


def write(out_dir, t, lang, reports=None, issues=None, devices=None):
    """The three pages into out_dir; the paths of the two that are in the navigation."""
    base = '' if lang == 'en' else '/' + lang
    out = pathlib.Path(out_dir)
    releases = changelog()
    (out / 'status.md').write_text(status_md(t, lang, base, reports, issues, releases, devices), encoding='utf-8', newline='\n')
    (out / 'releases.md').write_text(releases_md(t, lang, base, releases), encoding='utf-8', newline='\n')
    (out / 'compare.md').write_text(compare_md(t, lang, base), encoding='utf-8', newline='\n')
    return ['status.md', 'releases.md']
