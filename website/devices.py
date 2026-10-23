#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The site's pages for the models: one that lists every model, and one for each - in English and,
from the Arabic catalogue, in Arabic.

build.py calls write() for each language while it gathers the pages. Everything a page says about a
model is website/data/devices.json, which tools/devices-table.py writes from scripts/xgs/models.py
(the ports, as the drivers have them) and tools/device_facts.py (the rest, each fact with its
source); every word around it is the catalogue's, under "devices". Two drawings are made here as SVG
inside the page, coloured by the site's own tokens so they follow the light and the dark theme: the
panel the ports are on, each port where it sits, and the board, from the host to every socket.
"""
import html
import json
import pathlib
import re
from urllib.parse import quote

HERE = pathlib.Path(__file__).resolve().parent
GH = 'https://github.com/AbdelmonemAwad/os-xgs-npu'
NEW_APPLIANCE = GH + '/issues/new?template=appliance.yml'
BENCH = 'docs/images/hardware/bench/three-appliances-2026-10-17.jpg'
# The Arabic block, by its code points: an escape typed here can end up as the letters themselves.
ARABIC = re.compile('[%s-%s]' % (chr(0x0600), chr(0x06ff)))


def load():
    return json.loads((HERE / 'data' / 'devices.json').read_text(encoding='utf-8'))


def translated(lang, rel, english):
    """The address of a page in the language's site where website/i18n/<lang>/ translates it, and the
    English page's where it does not: 'guide/installing' is /ar/guide/installing/ in Arabic."""
    if lang != 'en' and (HERE / 'i18n' / lang / (rel + '.md')).exists():
        return '/%s/%s/' % (lang, rel)
    return english


def esc(s):
    return html.escape(str(s), quote=True)


def site(rp):
    """A repository path as the site serves it: a page by its directory, anything else as it is."""
    if rp.endswith('/README.md'):
        return '/' + rp[:-len('README.md')]
    if rp.endswith('.md'):
        return '/' + rp[:-3] + '/'
    return '/' + rp


def b(s):
    """A value of its own direction: '8 RJ45, 2 SFP' or '11,500 Mbps' inside an Arabic sentence or table
    stays in its own order."""
    return '<bdi>%s</bdi>' % s


def short(label):
    return label[4:] if label.startswith('Port') else label


def text(x, y, s, cls='nf-hw-text', anchor='middle'):
    """SVG text; a run with Arabic in it is laid out right to left, and is always centred so the
    direction does not move it."""
    rtl = bool(ARABIC.search(str(s)))
    return '<text x="%s" y="%s" class="%s" text-anchor="%s" direction="%s">%s</text>' % (
        x, y, cls, 'middle' if rtl else anchor, 'rtl' if rtl else 'ltr', esc(s))


def port_title(p, w):
    if 'device' not in p:
        bits = [p['label'], p['socket'], p['speed'], p['phy'] or w['direct']]
        return ' · '.join(bits + (['PoE'] if p['poe'] else []) + ([w['bypass']] if p['bypass'] else []))
    bits = [p['label'], p['device'], w['tag'] + ' ' + p['tag'], p['speed']]
    if p['switch_port'] is not None:
        bits.append(w['switch_port'].format(n=p['switch_port']))
    else:
        bits.append(p['mac'] + (' - ' + p['phy'] if p['phy'] else ''))
    if p['poe']:
        bits.append('PoE')
    if p['bypass']:
        bits.append(w['bypass'])
    return ' · '.join(bits)


def port_class(p):
    return 'nf-hw-port nf-hw-port--%s%s%s' % ('switch' if p.get('switch_port') is not None else 'own',
                                              ' nf-hw-port--poe' if p['poe'] else '',
                                              ' nf-hw-port--bypass' if p['bypass'] else '')


# -- the panel ------------------------------------------------------------------------------------

ROW1, ROW2, ROWH, MID = 28, 60, 26, 57
GLYPH = {'RJ45': (30, 26), 'SFP': (38, 18), 'SFP+': (38, 18), 'QSFP': (52, 20)}
# A link in a drawing is written xlink:href, not href: the theme's instant loading rewrites the href
# of every element that has one in a page it opens, an SVG link's cannot be written, and the page
# stayed where it was. site.js follows a click on one.


def jack(p, x, y, w, flip, page_link='', lit=None):
    """A port where it sits: a socket of its kind, linked to its row in the table of its model's page."""
    gw, gh = GLYPH.get(p['socket'], (38, 18))
    gy = y + (ROWH - gh) / 2
    out = ['<a xlink:href="%s#port-%s" class="%s"><title>%s</title>' % (page_link, esc(p['label']), port_class(p), esc(port_title(p, w)))]
    out.append('<rect class="nf-hw-jack" x="%s" y="%s" width="%s" height="%s" rx="2"/>' % (x, gy, gw, gh))
    if p['socket'] == 'RJ45':
        ly = gy + 2 if flip else gy + gh - 7
        out.append('<rect class="nf-hw-latch" x="%s" y="%s" width="12" height="5"/>' % (x + gw / 2 - 6, ly))
    else:
        out.append('<rect class="nf-hw-latch" x="%s" y="%s" width="%s" height="%s"/>' % (x + 4, gy + 5, gw - 8, gh - 10))
    if p['poe']:
        out.append('<circle class="nf-hw-poe" cx="%s" cy="%s" r="3"/>' % (x + gw - 1, gy + 1))
    if lit is not None and p['socket'] == 'RJ45':
        # an RJ45 jack's link and activity lights, at the corners away from the latch; a lit port's
        # activity light blinks, each port out of step with the next. A cage's are beside it (leds()).
        ly = gy + gh - 5 if flip else gy + 2
        out.append('<rect class="nf-led%s" x="%s" y="%s" width="5" height="3" rx="1"/>' % (' nf-led--link' if lit else '', x + 3, ly))
        out.append('<rect class="nf-led%s" x="%s" y="%s" width="5" height="3" rx="1"%s/>' % (
            ' nf-led--act' if lit else '', x + gw - 8, ly,
            ' style="animation-delay: -%.1fs"' % (sum(map(ord, p['label'])) % 14 / 10) if lit else ''))
    out.append('</a>')
    return ''.join(out), gw


def panel_parts(page, w, page_link='', lcd='LCD', lit=None):
    """The panel's fixtures and ports from its layout, left to right; and how wide it came out. lcd is
    what the display shows, or several things it shows in turn; lit, the ports drawn with their lights
    on - None draws no lights at all."""
    ports = {p['label']: p for p in page['ports']}
    layout = page['panel']['layout'].split()
    x, parts = 18, []
    for tok in layout:
        if tok == '|':
            x += 16
            continue
        if tok == 'lcd':
            parts.append('<rect class="nf-hw-fixture" x="%s" y="26" width="96" height="34" rx="3"/>' % x)
            if isinstance(lcd, list):
                for i, line in enumerate(lcd):
                    parts.append(text(x + 48, 47, line, 'nf-hw-lcd nf-lcd-msg nf-lcd-msg--%d' % (i + 1)))
            else:
                parts.append(text(x + 48, 47, lcd, 'nf-hw-lcd' if lcd != 'LCD' else 'nf-hw-muted'))
            for i in range(4):
                parts.append('<circle class="nf-hw-fixture" cx="%s" cy="76" r="4"/>' % (x + 18 + i * 20))
            x += 104
            continue
        if tok in ('com', 'mgmt'):
            parts.append('<rect class="nf-hw-fixture" x="%s" y="%s" width="26" height="22" rx="2"/>' % (x, MID - 11))
            parts.append(text(x + 13, MID + 29, tok.upper(), 'nf-hw-muted nf-hw-small'))
            x += 36
            continue
        if tok == 'usb':
            parts.append('<rect class="nf-hw-fixture" x="%s" y="%s" width="22" height="9" rx="1"/>' % (x, MID - 12))
            parts.append('<rect class="nf-hw-fixture" x="%s" y="%s" width="22" height="9" rx="1"/>' % (x, MID + 3))
            parts.append(text(x + 11, MID + 29, 'USB', 'nf-hw-muted nf-hw-small'))
            x += 32
            continue
        if tok.startswith('led:'):
            # the cages' lights, beside them as on the panel, the cages named top to bottom. A 10G
            # cage has two, flat bars one above the other - its link blue below, its activity orange
            # above - so the column reads blue, orange, blue, orange from the bottom up; a gigabit
            # cage one, link and activity in one, on a row of its own.
            names = tok[4:].split('/')
            delay = lambda name: ' style="animation-delay: -%.1fs"' % (sum(map(ord, name)) % 14 / 10)
            if all(ports[name]['speed'] == '10G' for name in names):
                top = MID - 6 * len(names) + 1
                for i, name in enumerate(names):
                    on = lit is not None and name in lit
                    by = top + 12 * i
                    parts.append(text(x + 5, by + 7, name, 'nf-hw-ledlabel'))
                    parts.append('<rect class="nf-led%s" x="%s" y="%s" width="9" height="3" rx="1"%s/>' % (
                        ' nf-led--act10' if on else '', x + 13, by, delay(name) if on else ''))
                    parts.append('<rect class="nf-led%s" x="%s" y="%s" width="9" height="3" rx="1"/>' % (
                        ' nf-led--link10' if on else '', x + 13, by + 5))
                x += 13 + 9 + 6
                continue
            for i, name in enumerate(names):
                cy = MID - 8 * (len(names) - 1) + 16 * i
                on = lit is not None and name in lit
                parts.append(text(x + 5, cy + 3, name, 'nf-hw-ledlabel'))
                parts.append('<rect class="nf-led%s" x="%s" y="%s" width="4" height="4" rx="1"%s/>' % (
                    ' nf-led--al' if on else '', x + 13, cy - 2, delay(name) if on else ''))
            x += 13 + 4 + 6
            continue
        if tok == 'bay':
            name = w['bay_slot'].format(slot=page['slot']) if page['slot'] else w['bay']
            parts.append('<rect class="nf-hw-bay" x="%s" y="20" width="112" height="74" rx="4"/>' % x)
            parts.append(text(x + 56, 61, name, 'nf-hw-muted nf-hw-small'))
            x += 120
            continue
        col = [ports[l] for l in tok.split('/')]
        width = max(GLYPH.get(p['socket'], (38, 18))[0] for p in col)
        on = (lambda p: p['label'] in lit) if lit is not None else (lambda p: None)
        if len(col) == 2:
            g, _ = jack(col[0], x, ROW1, w, False, page_link, on(col[0]))
            parts.append(g + text(x + width / 2, ROW1 - 6, short(col[0]['label']), 'nf-hw-label'))
            g, _ = jack(col[1], x, ROW2, w, True, page_link, on(col[1]))
            parts.append(g + text(x + width / 2, ROW2 + ROWH + 14, short(col[1]['label']), 'nf-hw-label'))
        else:
            g, _ = jack(col[0], x, MID - ROWH / 2, w, False, page_link, on(col[0]))
            parts.append(g + text(x + width / 2, MID + ROWH / 2 + 15, short(col[0]['label']), 'nf-hw-label'))
        x += width + 6
    return parts, x + 12


def panel_svg(page, w):
    parts, width = panel_parts(page, w)
    # Drawn a third larger than its units, and never wider than the page: a small panel stays small.
    return ('<svg class="nf-hw-svg" width="%d" viewBox="0 0 %d 114" role="img" aria-label="%s" direction="ltr">'
            '<rect class="nf-hw-panel" x="1" y="1" width="%d" height="112" rx="8"/>%s</svg>') % (
        round(width * 1.35), width, esc(w['panel_aria'].format(model=page['model'])), width - 2, ''.join(parts))


# The home page's drawing moves a little, as the bench does: some ports' lights on, their activity
# blinking - for the look, as the port strip under the buttons, not any appliance's state - and the
# XGS 3300's display showing in turn what the front panel plugin shows on it. The XGS 136's RJ45
# jacks have their lights; its cages' are on its front, which the drawing does not show.
HERO_LIT = {'XGS 3300': {'F1', 'F2', 'Port2', 'Port3', 'Port5', 'Port6', 'Port8'},
            'XGS 136': {'Port1', 'Port3', 'Port4', 'Port6', 'Port9', 'Port11'}}
HERO_LCD = ['OPNsense', 'XGS 3300', 'Ports OK']


def hero_svg(data, w, base):
    """The home page's picture: the two appliances run here as they stand on the bench - the XGS 136's
    back on its feet, on top of the XGS 3300's front in its rack ears - every port linked to its row
    on its model's page."""
    pages = {p['model']: p for p in data['pages']}
    big, small = pages['XGS 3300'], pages['XGS 136']
    bp, bw = panel_parts(big, w, '%s/devices/%s/' % (base, big['slug']), lcd=HERO_LCD, lit=HERO_LIT['XGS 3300'])
    sp, sw = panel_parts(small, w, '%s/devices/%s/' % (base, small['slug']), lit=HERO_LIT['XGS 136'])
    ear = 26
    width = bw + 2 * ear + 4
    out = []
    # the desktop appliance on top, on its feet, as it stands on the bench
    sx, sy = (width - sw) / 2, 8
    out.append('<g transform="translate(%.0f %d)">' % (sx, sy))
    out.append('<rect class="nf-hw-panel" x="1" y="1" width="%d" height="112" rx="8"/>%s' % (sw - 2, ''.join(sp)))
    for fx in (28, sw - 70):
        out.append('<rect class="nf-hw-foot" x="%d" y="113" width="42" height="6" rx="2"/>' % fx)
    out.append('<a xlink:href="%s/devices/%s/">%s</a>' % (base, small['slug'], text(sw / 2, 138, small['model'] + ' · ' + w['hero_back'], 'nf-hw-model')))
    out.append('</g>')
    # the 1U appliance under it, its ears either side with their two holes
    by = sy + 154
    out.append('<g transform="translate(2 %d)">' % by)
    for ex in (0, ear + bw):
        out.append('<rect class="nf-hw-ear" x="%d" y="4" width="%d" height="106" rx="3"/>' % (ex, ear))
        for hy in (26, 88):
            out.append('<circle class="nf-hw-hole" cx="%d" cy="%d" r="4.5"/>' % (ex + ear / 2, hy))
    out.append('<g transform="translate(%d 0)"><rect class="nf-hw-panel" x="1" y="1" width="%d" height="112" rx="6"/>%s</g>' % (
        ear, bw - 2, ''.join(bp)))
    out.append('<a xlink:href="%s/devices/%s/">%s</a>' % (base, big['slug'], text(width / 2, 130, big['model'] + ' · ' + w['hero_front'], 'nf-hw-model')))
    out.append('</g>')
    height = by + 138
    return ('<svg class="nf-hw-svg nf-hero__drawing" viewBox="0 0 %d %d" role="img" aria-label="%s" direction="ltr">%s</svg>' % (
        width, height, esc(w['hero_aria']), ''.join(out)))


# -- the board ------------------------------------------------------------------------------------

def chips(ports, x, y, w, wide):
    """Each port of a branch as a chip: its label, and on a wide one the MAC behind it."""
    out, cw = [], (96 if wide else 36)
    per = max(1, int(436 // (cw + 6)))
    for i, p in enumerate(ports):
        cx, cy = x + (i % per) * (cw + 6), y + (i // per) * 26
        out.append('<a xlink:href="#port-%s" class="%s"><title>%s</title><rect class="nf-hw-chip" x="%s" y="%s" width="%s" height="20" rx="3"/>%s</a>' % (
            esc(p['label']), port_class(p), esc(port_title(p, w)), cx, cy, cw,
            text(cx + cw / 2, cy + 14, short(p['label']) + ((' · ' + p['mac']) if wide else ''), 'nf-hw-chiptext')))
    rows = (len(ports) + per - 1) // per
    return ''.join(out), rows * 26


def arch_svg(page, w):
    ports = page['ports']
    branches = []
    switched = [p for p in ports if p['switch_port'] is not None]
    if switched:
        branches.append((w['arch_switch'].format(chip=page['switch']),
                         w['arch_uplink'].format(mac=page['uplink'][0], link=page['uplink'][1]),
                         sorted(switched, key=lambda p: p['switch_port']), False))
    groups = {}
    for p in ports:
        if p['switch_port'] is None:
            groups.setdefault(p['phy'], []).append(p)
    for phy, ps in groups.items():
        ps = sorted(ps, key=lambda p: (len(p['label']), p['label']))
        title = w['arch_phy'].format(chip=phy) if phy else w['arch_direct']
        branches.append((title, w['arch_own'], ps, True))
    if page['slot']:
        branches.append((w['bay_slot'].format(slot=page['slot']), w['arch_slot'], [], False))

    out, y = [], 8
    BX, BW = 300, 452
    tops = []
    for title, sub, ps, wide in branches:
        body, h = chips(ps, BX + 12, y + 44, w, wide) if ps else ('', 0)
        hh = 46 + h + (6 if ps else 0)
        out.append('<rect class="nf-hw-box%s" x="%d" y="%d" width="%d" height="%d" rx="6"/>' % (
            '' if ps else ' nf-hw-box--bay', BX, y, BW, hh))
        out.append(text(BX + BW / 2, y + 18, title, 'nf-hw-title'))
        out.append(text(BX + BW / 2, y + 34, sub, 'nf-hw-muted nf-hw-small'))
        out.append(body)
        tops.append(y + 18)
        y += hh + 12
    right = y

    host = page['host'] or {}
    hy, hh = 8, 74
    out.append('<rect class="nf-hw-box" x="8" y="%d" width="232" height="%d" rx="6"/>' % (hy, hh))
    out.append(text(124, hy + 20, w['arch_host'], 'nf-hw-title'))
    out.append(text(124, hy + 40, host.get('cpu') or w['not_read'], 'nf-hw-text nf-hw-small' if host else 'nf-hw-muted nf-hw-small'))
    out.append(text(124, hy + 58, (host.get('memory') or '') + ((' · MGMT' if page['physical'] and page['physical'].get('mount') == '1U' else '')),
                    'nf-hw-muted nf-hw-small'))
    cy, ch = 132, 86
    out.append('<path class="nf-hw-edge nf-hw-edge--link" d="M124 %d V %d"/>' % (hy + hh, cy))
    out.append(text(132, hy + hh + 30, (host.get('link') or page['link']), 'nf-hw-muted nf-hw-small', anchor='start'))
    out.append('<rect class="nf-hw-box nf-hw-box--cop" x="8" y="%d" width="232" height="%d" rx="6"/>' % (cy, ch))
    out.append(text(124, cy + 20, w['arch_cop'], 'nf-hw-title'))
    out.append(text(124, cy + 40, page['chip'] + ((' · ' + page['chip_parts']) if page['chip_parts'] else ''), 'nf-hw-text nf-hw-small'))
    out.append(text(124, cy + 58, w['arch_platform'].format(platform=page['platform']), 'nf-hw-muted nf-hw-small'))
    for i, ty in enumerate(tops):
        sy = cy + 24 + i * (ch - 40) / max(1, len(tops) - 1) if len(tops) > 1 else cy + ch / 2
        out.append('<path class="nf-hw-edge" d="M240 %.0f H %d V %d H %d"/>' % (sy, 262 + i * 8, ty, BX))
    height = max(right, cy + ch + 8)
    return ('<svg class="nf-hw-svg" width="760" viewBox="0 0 760 %d" role="img" aria-label="%s" direction="ltr">%s</svg>' % (
        height, esc(w['arch_aria'].format(model=page['model'])), ''.join(out)))


# -- the pages ------------------------------------------------------------------------------------

def state(page, w, run):
    return '<span class="nf-dev-state nf-dev-state--%s">%s</span>' % ('run' if run else 'rnd', esc(w['run'] if run else w['rnd']))


def front_matter(title, description, lang, extra=''):
    fm = '---\ntitle: %s\ndescription: %s\nedit_url: %s/edit/main/tools/device_facts.py\n' % (
        json.dumps(title, ensure_ascii=False), json.dumps(description, ensure_ascii=False), GH)
    if lang != 'en':
        fm += 'lang: %s\n' % lang
    return fm + extra + '---\n\n'


def source_line(key, src, w):
    kind = w['source_kinds'].get(src['kind'], src['kind'])
    if src.get('url'):
        return '- %s: [%s](%s)' % (kind, src['title'], src['url'])
    if src.get('page'):
        return '- %s: [%s](%s)' % (kind, src['title'], site(src['page']))
    return '- %s: `%s`' % (kind, src['title'])


def details(page, data, w):
    rows, used = [], set()

    def row(name, value, source=None):
        if value:
            rows.append('| %s | %s | %s |' % (name, value, w['source_kinds_short'].get(data['sources'][source]['kind'], '') if source else ''))
            if source:
                used.add(source)

    host = page['host']
    hsrc = host['source'] if host else None
    row(w['row_host'], b('%s, %s' % (host['cpu'], host['memory'])) if host else w['not_read'], hsrc)
    row(w['row_link'], b(host['link'] if host else page['link']), hsrc if host else 'startup')
    row(w['row_cop'], b(page['chip'] + ((' (%s)' % page['chip_parts']) if page['chip_parts'] else '')), 'board')
    row(w['row_switch'], '%s · %s' % (b(page['switch']), w['arch_uplink'].format(mac=b(page['uplink'][0]), link=b(page['uplink'][1]))),
        'dt' if page['family_key'] == 'armada' else 'read3300')
    phys = sorted({p['phy'] for p in page['ports'] if p['phy']})
    if phys:
        row(w['row_phys'], b(', '.join(phys)), 'board')
    row(w['row_platform'], '`%s`' % page['platform'], 'platform')
    v = page['physical']
    if v:
        s = v['source']
        row(w['row_storage'], b(v['storage']), s)
        row(w['row_console'], b(', '.join(v['console'])), s)
        row(w['row_io'], b(', '.join(v['io'])), s)
        slots = b(v['slots'])
        if page['slot']:
            slots += ' · ' + w['bay_slot'].format(slot=page['slot'])
        row(w['row_slots'], slots, s)
        if v.get('wifi'):
            row(w['row_wifi'], b('%s (%s)' % (v['wifi'], ', '.join(x['model'] for x in page['variants'] if x['model'].endswith('w')))), s)
        if v.get('poe'):
            row(w['row_poe'], b(v['poe']), s)
        if v.get('display'):
            row(w['row_display'], b(v['display']), s)
        if v.get('bypass'):
            row(w['row_bypass'], w['bypass_pair'].format(a=b('Port1'), b=b('Port2')), s)
        row(w['row_form'], '%s · %s · %s' % (w['mount'][v['mount']], b(v['size']), b(v['weight'])), s)
        where, watts = v['psu']
        row(w['row_psu'], w['psu'][where] + ((' · ' + b(watts)) if watts else ''), s)
        power = []
        for variant, (idle, most) in zip(page['variants'], v['power']):
            power.append('%s: %s' % (b(variant['model']), w['power'].format(idle=b(idle), most=b(most))))
        row(w['row_power'], '<br>'.join(power), s)
    head = ['| %s |' % ' | '.join(w['details_head']), '|---|---|---|']
    return head + rows, used


def ports_table(page, w):
    out = ['| %s |' % ' | '.join(w['ports_head']), '|' + '---|' * len(w['ports_head'])]
    for p in page['ports']:
        if p['switch_port'] is not None:
            where = w['switch_port'].format(n=p['switch_port'])
        elif p['phy']:
            where = w['own_phy'].format(mac=b(p['mac']), phy=b(p['phy']))
        else:
            where = w['own_direct'].format(mac=b(p['mac']))
        notes = [n for n, on in (('PoE', p['poe']), (w['bypass'], p['bypass'])) if on]
        out.append('| <span id="port-%s"></span>**%s** | %s | %s | `%s` | `%s` | %s | %s |' % (
            p['label'], p['label'], b(p['socket']), b(p['speed']), p['device'], p['tag'], where, ', '.join(notes)))
    return out


def vendor_table(page, data, w):
    if not page['vendor']:
        return []
    eds = [data['sources'][v['source']] for v in page['vendor']]
    out = ['| %s | %s |' % (w['vendor_metric'], ' | '.join(w['vendor_edition'].format(edition=e['edition']) for e in eds)),
           '|---|' + '--:|' * len(eds)]
    for m in data['metrics']:
        vals = [v['figures'].get(m, '-') for v in page['vendor']]
        if all(x == '-' for x in vals):
            continue
        out.append('| %s | %s |' % (w['metrics'][m], ' | '.join(b(x) for x in vals)))
    return out


def page_md(page, data, t, lang, base, reports=None):
    w = t['devices']
    names = ', '.join(v['model'] for v in page['variants'])
    desc = w['description'].format(model=names, family=page['family'], chip=page['chip'], count=page['count'],
                                   state=w['run'] if page['run'] else w['rnd'])
    # The discussion under a page is the English site's: its privacy page, which it links, is there only.
    # its own card where the link is shared (cards.py), in every language
    extra = 'image: https://npuforge.org/assets/og/%s.png\n' % page['slug']
    md = [front_matter(page['model'], desc, lang, extra + ('comments: true\n' if lang == 'en' else '')), '# %s\n' % page['model']]
    md.append('<p class="nf-dev-lede">%s · %s · %s · %s</p>\n' % (
        b(esc(page['family'])), b(esc(page['chip'])), w['ports_count'].format(count=page['count'], sockets=b(esc(page['sockets']))),
        state(page, w, page['run'])))
    twins = [v['model'] for v in page['variants'] if not v['run']]
    if page['run']:
        md.append('!!! success "%s"\n\n    %s\n' % (w['run_title'], w['run_text'].format(
            model=page['model'], measured=site(page['photo']['measured']), hardware=site(page['photo']['page']))))
        if twins:
            md.append('    %s\n' % w['run_twins'].format(twins=b(', '.join(twins))))
    else:
        md.append('!!! warning "%s"\n\n    %s\n' % (w['rnd_title'], w['rnd_text'].format(model=page['model'])))

    # at a glance
    md.append('## %s\n' % w['glance'])
    md.append('| %s |' % ' | '.join(w['variants_head']))
    md.append('|' + '---|' * len(w['variants_head']))
    for v in page['variants']:
        md.append('| %s | `%s` | %s |' % (b(v['model']), v['assembly'], state(page, w, v['run'])))
    md.append('')
    first = page['ports'][0]['device']
    last = page['ports'][-1]['device']
    md.append(w['glance_text'].format(family=page['family'], driver=page['driver'], first=first, last=last,
                                      wan=b(page['wan']), count=page['count'], sockets=b(page['sockets']),
                                      family_link=site('docs/families/%s.md' % ('armada' if page['family_key'] == 'armada' else 'octeon-tx'))) + '\n')

    # what appliances of this model have reported
    md += reports_section(page, reports, w)

    # the panel
    md.append('## %s\n' % w['panel_title'][page['panel']['side']])
    md.append('<figure class="nf-hw" markdown="0"><div class="nf-hw-scroll">%s</div><figcaption>%s</figcaption></figure>\n' % (
        panel_svg(page, w), esc(w['panel_hint'])))
    keys = [('switch', w['key_switch'].format(chip=b(page['switch']))), ('own', w['key_own'])]
    if any(p['poe'] for p in page['ports']):
        keys.append(('poe', 'PoE'))
    if any(p['bypass'] for p in page['ports']):
        keys.append(('bypass', w['bypass_pair'].format(a=b('Port1'), b=b('Port2'))))
    md.append('<p class="nf-hw-legend">%s</p>\n' % ''.join(
        '<span><span class="nf-hw-key nf-hw-key--%s"></span>%s</span>' % k for k in keys))
    if page['panel']['modelled_on']:
        md.append(w['panel_approx'].format(other=page['panel']['modelled_on'],
                                           other_link='%s/devices/%s/' % (base, page['panel']['modelled_on_slug'])) + '\n')
    else:
        md.append(w['panel_drawn'] + '\n')

    # every port
    md.append('## %s\n' % w['ports_title'])
    md.append(w['ports_text'].format(prefix=page['ports'][0]['device'].rstrip('0123456789')) + '\n')
    md += ports_table(page, w) + ['']

    # the board
    md.append('## %s\n' % w['arch_title'])
    md.append('<figure class="nf-hw nf-hw--arch" markdown="0"><div class="nf-hw-scroll">%s</div></figure>\n' % arch_svg(page, w))
    md.append((w['arch_run'] if page['run'] else w['arch_rnd']) + '\n')

    # technical details
    md.append('## %s\n' % w['details_title'])
    rows, used = details(page, data, w)
    md += rows + ['']

    # its module bay
    md += bay_section(page, data, t, base)

    # the vendor's figures
    vt = vendor_table(page, data, w)
    if vt:
        md.append('## %s\n' % w['vendor_title'])
        md.append(w['vendor_text'] + (' ' + w['vendor_ours'].format(measured=site(page['photo']['measured'])) if page['run'] else '') + '\n')
        md += vt + ['']
        used.update(v['source'] for v in page['vendor'])

    # photographed
    if page['photo']:
        md.append('## %s\n' % w['photo_title'])
        md.append('![%s](%s)\n' % (w['photo_alt'].format(model=page['model']), site(page['photo']['image'])))
        md.append(w['photo_text'].format(hardware=site(page['photo']['page']), drawing=site(page['photo']['drawing'])) + '\n')

    # an owner
    if not page['run'] or any(not v['run'] for v in page['variants']):
        md.append('## %s\n' % w['owner_title'])
        md.append(w['owner_text'].format(
            install=translated(lang, 'guide/installing', '/docs/guide/installing/'),
            portcheck=GH + '/blob/main/src/opnsense/scripts/xgs/portcheck.py', report=NEW_APPLIANCE) + '\n')

    # sources
    used.update({'board', 'platform'})
    if page['family_key'] == 'armada':
        used.add('dt')
    md.append('## %s\n' % w['sources_title'])
    for key in data['sources']:
        if key in used:
            md.append(source_line(key, data['sources'][key], w))
    md.append('')
    return '\n'.join(md)


# -- the modules ----------------------------------------------------------------------------------

def module_columns(module):
    """A module's ports in columns, left to right: up to six in a row, more in two rows - the even
    number above the odd, as on the panel of the 1U appliances they fit."""
    ports = module['ports']
    if len(ports) <= 6:
        return [[p] for p in ports]
    by = {int(p['label'][1:]): p for p in ports}
    return [[by[n + 1], by[n]] for n in range(1, len(ports) + 1, 2)]


def module_svg(module, w):
    """The module's face: its ports where the template's numbering puts them, between its two
    thumbscrews."""
    parts, x = [], 40
    for col in module_columns(module):
        width = max(GLYPH.get(p['socket'], (38, 18))[0] for p in col)
        if len(col) == 2:
            g, _ = jack(col[0], x, ROW1, w, False)
            parts.append(g + text(x + width / 2, ROW1 - 6, col[0]['label'], 'nf-hw-label'))
            g, _ = jack(col[1], x, ROW2, w, True)
            parts.append(g + text(x + width / 2, ROW2 + ROWH + 14, col[1]['label'], 'nf-hw-label'))
        else:
            g, _ = jack(col[0], x, MID - ROWH / 2, w, False)
            parts.append(g + text(x + width / 2, MID + ROWH / 2 + 15, col[0]['label'], 'nf-hw-label'))
        x += width + 6
    width = x + 34
    for sx in (16, width - 16):
        parts.append('<circle class="nf-hw-hole" cx="%d" cy="57" r="7"/><path class="nf-hw-screw" d="M%d 52 v10"/>' % (sx, sx))
    return ('<svg class="nf-hw-svg" width="%d" viewBox="0 0 %d 114" role="img" aria-label="%s" direction="ltr">'
            '<rect class="nf-hw-panel" x="1" y="1" width="%d" height="112" rx="6"/>%s</svg>') % (
        round(width * 1.35), width, esc(w['face_aria'].format(name=module['name'])), width - 2, ''.join(parts))


def module_arch_svg(module, w):
    """From the coprocessor's bay to the module's sockets, by what is between them."""
    groups = {}
    for p in module['ports']:
        groups.setdefault(p['phy'], []).append(p)
    out, y, BX, BW = [], 8, 300, 452
    tops = []
    for phy, ps in groups.items():
        body, h = chips(ps, BX + 12, y + 44, w, False)
        hh = 46 + h + 6
        out.append('<rect class="nf-hw-box" x="%d" y="%d" width="%d" height="%d" rx="6"/>' % (BX, y, BW, hh))
        out.append(text(BX + BW / 2, y + 18, w['arch_phy'].format(chip=phy) if phy else w['mod_cages'], 'nf-hw-title'))
        out.append(text(BX + BW / 2, y + 34, ' · '.join(sorted({'%s %s' % (p['speed'], p['socket']) for p in ps})), 'nf-hw-muted nf-hw-small'))
        out.append(body)
        tops.append(y + 18)
        y += hh + 12
    ch = 86
    cy = max(8, (y - 12 - ch) / 2)
    out.append('<rect class="nf-hw-box nf-hw-box--cop" x="8" y="%d" width="232" height="%d" rx="6"/>' % (cy, ch))
    out.append(text(124, cy + 20, w['arch_cop'], 'nf-hw-title'))
    out.append(text(124, cy + 40, w['mod_bay'], 'nf-hw-text nf-hw-small'))
    out.append(text(124, cy + 58, w['mod_cpld'], 'nf-hw-muted nf-hw-small'))
    for i, ty in enumerate(tops):
        sy = cy + 24 + i * (ch - 40) / max(1, len(tops) - 1) if len(tops) > 1 else cy + ch / 2
        out.append('<path class="nf-hw-edge" d="M240 %.0f H %d V %d H %d"/>' % (sy, 262 + i * 8, ty, BX))
    height = max(y, cy + ch + 8)
    return ('<svg class="nf-hw-svg" width="760" viewBox="0 0 760 %d" role="img" aria-label="%s" direction="ltr">%s</svg>' % (
        height, esc(w['mod_arch_aria'].format(name=module['name'])), ''.join(out)))


def module_state(module, w, on=None):
    """How far this project has gone with a module, or with it in one appliance."""
    if on is not None:
        kind = 'run' if module['run_on'] == on['model'] else ('rnd' if on['slug'] else 'none')
    else:
        kind = module['state']
    return '<span class="nf-dev-state nf-dev-state--%s">%s</span>' % (kind, esc(w['mod_states'][kind]))


def module_md(module, data, t, lang, base):
    w, m = t['devices'], t['modules']
    fits = [f['model'] for f in module['fits']]
    desc = m['description'].format(name=module['name'], assembly=module['assembly'], count=module['count'],
                                   fits=', '.join(fits) or m['fits_none'])
    md = [front_matter(module['name'], desc, lang), '# %s\n' % b(module['name'])]
    md.append('<p class="nf-dev-lede">%s · %s · %s</p>\n' % (
        b('<code>%s</code>' % module['assembly']), m['ports_count'].format(count=module['count'], sockets=b(module['sockets'])),
        module_state(module, w)))
    driven = [f['model'] for f in module['fits'] if f['slug']]
    if module['state'] == 'run':
        md.append('!!! success "%s"\n\n    %s\n' % (w['mod_states']['run'], m['run_text'].format(
            model=module['run_on'], page=site('docs/octep/expansion-modules.md'))))
    elif module['state'] == 'rnd':
        md.append('!!! warning "%s"\n\n    %s\n' % (w['mod_states']['rnd'], m['rnd_text'].format(models=b(', '.join(driven)))))
    else:
        md.append('!!! note "%s"\n\n    %s\n' % (w['mod_states']['none'],
                                                  m['none_text'].format(models=b(', '.join(fits))) if fits else m['unlisted_text']))

    md.append('## %s\n' % m['face_title'])
    md.append('<figure class="nf-hw" markdown="0"><div class="nf-hw-scroll">%s</div><figcaption>%s</figcaption></figure>\n' % (
        module_svg(module, w), esc(m['face_hint'])))
    keys = [('own', w['key_own'])]
    if any(p['poe'] for p in module['ports']):
        keys.append(('poe', 'PoE'))
    if any(p['bypass'] for p in module['ports']):
        keys.append(('bypass', m['bypass_key']))
    md.append('<p class="nf-hw-legend">%s</p>\n' % ''.join(
        '<span><span class="nf-hw-key nf-hw-key--%s"></span>%s</span>' % k for k in keys))
    md.append(m['face_approx'] + '\n')

    md.append('## %s\n' % w['ports_title'])
    md.append(m['ports_text'] + '\n')
    md.append('| %s |' % ' | '.join(m['ports_head']))
    md.append('|' + '---|' * len(m['ports_head']))
    for p in module['ports']:
        between = w['own_phy'].format(mac=b(m['bay_mac']), phy=b(p['phy'])) if p['phy'] else w['own_direct'].format(mac=b(m['bay_mac']))
        notes = [n for n, on in (('PoE', p['poe']), (m['bypass_key'], p['bypass'])) if on]
        md.append('| <span id="port-%s"></span>**%s** | %s | %s | %s | %s |' % (
            p['label'], p['label'], b(p['socket']), b(p['speed']), between, ', '.join(notes)))
    md.append('')

    md.append('## %s\n' % m['arch_title'])
    md.append('<figure class="nf-hw nf-hw--arch" markdown="0"><div class="nf-hw-scroll">%s</div></figure>\n' % module_arch_svg(module, w))
    md.append(m['arch_text'] + '\n')

    md.append('## %s\n' % m['fits_title'])
    if module['fits']:
        md.append(m['fits_text'] + '\n')
        md.append('| %s |' % ' | '.join(m['fits_head']))
        md.append('|' + '---|' * len(m['fits_head']))
        for f in module['fits']:
            name = ('[%s](%s/devices/%s/)' % (f['model'], base, f['slug'])) if f['slug'] else b(f['model'])
            md.append('| %s | %s | %s |' % (name, b(f['bays']), module_state(module, w, f)))
        md.append('')
    else:
        md.append(m['unlisted_text'] + '\n')
    if module['vendor_name']:
        md.append(m['vendor_name'].format(name=b(module['vendor_name'])) + '\n')

    md.append('## %s\n' % w['sources_title'])
    md.append('- %s: `xgs-bsp/data/%sR00.txt` - %s' % (w['source_kinds']['board'], module['assembly'], m['template']))
    if module['fits']:
        md.append(source_line('br2026', data['sources']['br2026'], w))
    md.append('- %s: [%s](%s)' % (w['source_kinds']['read'], m['doc_link'], site('docs/octep/expansion-modules.md')))
    md.append('')
    return '\n'.join(md)


def modules_index_md(data, t, lang, base):
    w, m = t['devices'], t['modules']
    md = [front_matter(m['title'], m['index_description'], lang), '# %s\n' % m['title'], m['lede'] + '\n']
    md.append('<div class="nf-devices" markdown>\n')
    md.append('| %s |' % ' | '.join(m['head']))
    md.append('|' + '---|' * len(m['head']))
    for mod in data['modules']:
        fits = ', '.join(f['model'].split(' ')[1] for f in mod['fits']) or '-'
        md.append('| [%s](%s/) | `%s` | %s | %s | %s |' % (
            b(mod['name']), mod['slug'], mod['assembly'], b(w['ports_cell'].format(count=mod['count'], sockets=mod['sockets'])),
            b('XGS ' + fits) if mod['fits'] else '-', module_state(mod, w)))
    md.append('\n</div>\n')
    md.append('!!! note "%s"\n\n    %s\n' % (m['desk_title'], m['desk_text'].format(link='%s/devices/' % base)))
    md.append(m['index_more'].format(link=site('docs/octep/expansion-modules.md')) + '\n')
    return '\n'.join(md)


def bay_section(page, data, t, base):
    """A model's module bay on its page: the coprocessor's bay and the modules listed for it, or the
    host's bay and what the vendor sells for it, or none."""
    w, m = t['devices'], t['modules']
    md = ['## %s\n' % m['title']]
    if page['module_bay']:
        md.append(m['dev_bay_text'].format(bays=b(page['module_bay']['bays']), link='%s/modules/' % base) + '\n')
        md.append('| %s |' % ' | '.join(m['dev_head']))
        md.append('|' + '---|' * len(m['dev_head']))
        mods = {x['slug']: x for x in data['modules']}
        here = {'model': page['model'], 'slug': page['slug']}
        for s in page['module_bay']['modules']:
            mod = mods[s]
            md.append('| [%s](%s/modules/%s/) | %s | %s |' % (
                b(mod['name']), base, mod['slug'], b(w['ports_cell'].format(count=mod['count'], sockets=mod['sockets'])),
                module_state(mod, w, here)))
        md.append('')
    elif page['host_bay']:
        md.append(m['dev_host_text'].format(items=b(', '.join(page['host_bay']['items']))) + '\n')
    else:
        md.append(m['dev_none'] + '\n')
    return md


def write_modules(out_dir, t, lang):
    """The module pages into out_dir/modules/; their paths there, for the navigation."""
    data = load()
    base = '' if lang == 'en' else '/' + lang
    d = pathlib.Path(out_dir) / 'modules'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'index.md').write_text(modules_index_md(data, t, lang, base), encoding='utf-8', newline='\n')
    paths = ['modules/index.md']
    for mod in data['modules']:
        (d / ('%s.md' % mod['slug'])).write_text(module_md(mod, data, t, lang, base), encoding='utf-8', newline='\n')
        paths.append('modules/%s.md' % mod['slug'])
    return paths


def index_md(data, t, lang, base):
    w = t['devices']
    # Shared, the list shows the site's card, the two run models drawn (cards.site) - not the
    # photograph below, which hooks.py would otherwise take as the page's first picture.
    md = [front_matter(w['title'], w['index_description'], lang, 'image: https://npuforge.org/assets/og/site.png\n'),
          '# %s\n' % w['title'], w['lede'] + '\n']
    # The bench the two run models stand on, photographed - the home page draws them instead.
    hero = t['hero']
    md.append('<figure class="nf-bench"><img src="%s" alt="%s" width="1599" height="608" decoding="async">'
              '<figcaption>%s</figcaption></figure>\n' % (site(BENCH), esc(hero['photo_alt']), esc(hero['photo_caption'])))
    for family, doc in (('ARMADA', 'docs/families/armada.md'), ('OCTEON TX', 'docs/families/octeon-tx.md')):
        md.append('## %s\n' % family)
        md.append(w['family_text'].format(family=family, link=site(doc)) + '\n')
        md.append('<div class="nf-devices" markdown>\n')
        md.append('| %s |' % ' | '.join(w['head']))
        md.append('|' + '---|' * len(w['head']))
        for p in data['pages']:
            if p['family'] != family:
                continue
            names = ', '.join([p['variants'][0]['model']] + [v['model'].split(' ')[1] for v in p['variants'][1:]])
            boards = ', '.join([p['variants'][0]['assembly']] + [v['assembly'].split('-')[1] for v in p['variants'][1:]])
            md.append('| [%s](%s/) | `%s` | %s | %s | %s | %s |' % (
                names, p['slug'], boards, b(p['chip']), b(w['ports_cell'].format(count=p['count'], sockets=p['sockets'])),
                p['slot'] or '-', state(p, w, p['run'])))
        md.append('\n</div>\n')
    md.append('!!! note "%s"\n\n    %s\n' % (w['note_title'], w['note']))
    md.append('## %s { #others }\n' % w['others_title'])
    md.append(w['others'] + '\n')
    md.append('<div class="nf-devices" markdown>\n')
    md.append('| %s |' % ' | '.join(w['others_head']))
    md.append('|' + '---|' * len(w['others_head']))
    for r in data['others']:
        md.append('| %s | %s | `%s` | %s | %s | %s |' % (
            b(r['family']) if r['chip'] != '-' else w['none'], b(r['model']), r['assembly'], b(r['chip']), b(r['ports']), b(r['bays'])))
    md.append('\n</div>\n')
    md.append(w['index_more'].format(link=site('docs/families/front-ports-by-model.md')) + '\n')
    return '\n'.join(md)


def reports_section(page, reports, w):
    """What appliances of this model have reported: the problem reporter's issues from them, any port
    check among those, and owners' compatibility reports - read by build.py from GitHub at the build,
    so a page shows the newest the day the site is built."""
    r = (reports or {}).get(page['slug'], {})
    md = ['## %s\n' % w['reports_title']]
    p = r.get('problems')
    if p:
        label = max(p['labels'], key=p['labels'].get)
        link = GH + '/issues?q=' + quote('is:issue label:"%s"' % label)
        md.append(w['reports_problems'].format(count=p['count'], first=b(p['first']), last=b(p['last']), link=link) + '\n')
        md.append(w['reports_kinds'].format(kinds=', '.join('`%s` %d' % kv for kv in sorted(p['kinds'].items()))) + '\n')
        for c in p['portchecks']:
            md.append(w['reports_check'].format(date=b(c['date']), verdict=w['reports_verdict'][c['verdict']], url=c['url']) + '\n')
    compat = r.get('compat')
    if compat:
        md.append(w['reports_compat'] + '\n')
        md.append('| %s |' % ' | '.join(w['reports_compat_head']))
        md.append('|' + '---|' * len(w['reports_compat_head']))
        for c in compat:
            md.append('| %s | %s | %s | %s | [#%d](%s) |' % (b(c['date']), b(c['version'] or '-'), b(', '.join(c['works']) or '-'),
                                                         b(c['installed'] or '-'), c['number'], c['url']))
        md.append('')
    if not p and not compat:
        md.append(w['reports_none'].format(report=NEW_APPLIANCE) + '\n')
    return md


def write(out_dir, t, lang, reports=None):
    """The pages into out_dir/devices/; their paths there, for the navigation."""
    data = load()
    base = '' if lang == 'en' else '/' + lang
    d = pathlib.Path(out_dir) / 'devices'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'index.md').write_text(index_md(data, t, lang, base), encoding='utf-8', newline='\n')
    paths = ['devices/index.md']
    for page in data['pages']:
        (d / ('%s.md' % page['slug'])).write_text(page_md(page, data, t, lang, base, reports), encoding='utf-8', newline='\n')
        paths.append('devices/%s.md' % page['slug'])
    return paths


def slugs():
    try:
        return [p['slug'] for p in load()['pages']]
    except (OSError, ValueError, KeyError):
        return []
