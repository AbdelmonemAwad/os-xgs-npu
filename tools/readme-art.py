#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Draw the README's banner and its status board, each in a dark and a light version.

    python3 tools/readme-art.py

Writes docs/images/readme/{banner,status}-{dark,light}.svg. The README shows the version that
matches the reader's GitHub theme through <picture>. The drawing is the one the author's profile
uses, so the two read as one family.

Every number below is copied from a page in docs/ that carries the measurement and how it was
taken; the comment beside each one says which. Each card's weighted completion is not copied: it is
computed here from its family page's table (WEIGHED), so re-weighing a row is all it takes.

    python3 tools/readme-art.py               write the SVGs into docs/images/readme
    python3 tools/readme-art.py --out DIR     write them into DIR (website/build.py, for npuforge.org)
    python3 tools/readme-art.py --check       say whether every page that states a completion states
                                              the one its table weighs, and fail when one does not

The README shows the status board from npuforge.org, which draws it at every build of the site, so
the picture follows the tables without a commit.
"""
import pathlib
import re
import sys

OUT = pathlib.Path(__file__).resolve().parent.parent / 'docs' / 'images' / 'readme'
DOCS = pathlib.Path(__file__).resolve().parent.parent / 'docs'

# The page whose table weighs each card's completion.
WEIGHED = {'XGS 3300': 'families/octeon-tx-reference.md', 'XGS 136': 'families/armada.md'}


def weighed(page):
    """A family page's completion: its last table - the one above its last 'Weights sum to N' -
    each row's done (the value left when the struck ones are taken out) times its weight, over N.
    The weights must add up to the N the page says, or the table is not what the page claims."""
    text = (DOCS / page).read_text(encoding='utf-8').replace('\r\n', '\n')
    end = text.rindex('\nWeights sum to ')
    total = int(re.match(r'\nWeights sum to (\d+)', text[end:]).group(1))
    done = weights = 0
    for line in reversed(text[:end].rstrip('\n').split('\n')):
        if not line.startswith('|'):
            break
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if not cells[0].isdigit():
            continue
        pct = int(re.search(r'(\d+)%', re.sub(r'~~.*?~~', '', cells[2])).group(1))
        done += pct * int(cells[3])
        weights += int(cells[3])
    if weights != total:
        sys.exit('%s: the weights add up to %d, and the page says %d' % (page, weights, total))
    return done / total


def stated(text):
    """The percentage a sentence states once its struck values are taken out."""
    m = re.search(r'(\d+)%', re.sub(r'~~.*?~~', '', text))
    return int(m.group(1)) if m else None


def check():
    """Every place a completion is written down, against the table it comes from."""
    bad = []
    for name, page in WEIGHED.items():
        want = int(weighed(page) + 0.5)
        text = (DOCS / page).read_text(encoding='utf-8').replace('\r\n', '\n')
        line = text[text.rindex('\nWeights sum to '):].split('\n')[1]
        # what is left of the line once every struck value is out, from its "Weighted:" on
        live = re.sub(r'~~.*?~~', '', line)
        places = [('its "Weighted"', stated(live.split('Weighted:', 1)[-1].split('(')[0]))]
        m = re.search(r'^ +state .*\n +(.*) by the table below', text, re.M)
        if m:
            places.append(('its header', stated(m.group(1))))
        parity = (DOCS / 'families' / 'parity.md').read_text(encoding='utf-8')
        m = re.search(r'((?:~~\*\*\d+%\*\*~~ )*\*\*\d+%\*\*) for the ' + re.escape(name), parity)
        if m:
            places.append(('parity.md', stated(m.group(1))))
        for where, got in places:
            if got != want:
                bad.append('%s: %s says %s%%, its table weighs %d%%' % (page if where != 'parity.md' else name, where, got, want))
    for b in bad:
        print(b)
    print('%d place(s) out of step with their tables' % len(bad) if bad else
          'every stated completion is the one its table weighs: ' +
          ', '.join('%s %d%%' % (n, int(weighed(p) + 0.5)) for n, p in WEIGHED.items()))
    return not bad

# ---------------------------------------------------------------- data ----
STAGES = [  # (label, state)  state: done | now | next
    ('Discovery', 'done'),
    ('Front ports', 'done'),
    ('Offload', 'done'),
    ('Integration', 'done'),             # the work-in-progress notice lifted on 2026-10-18 (#461)
    ('Completing, as updates', 'now'),
]
STAGE_NOTE = '&#8220;The main part is done; what is left arrives as updates.&#8221;'

# ------------------------------------------------------------ features ----
# The FEATURES rows are not written here. Each is read from docs/families/parity.md at every drawing:
# its circle from the marks of the parity rows it stands for, and its words from those rows' cells -
# so a row in that table changed, or a mark moved from ⬜ to ✅, is the board changed, as the weighted
# completion follows the family pages. Written by hand until 2026-10-20, the XGS 136's IPsec row still
# said "no IPv6" the day IPv6 was built and measured (#572).
#
#   (label, parity row)                    one row: the circle from its mark, the words from its cell
#   (label, [(parity row, short), ...])    a group: full when every row is built and run, half when some
#                                          are, empty when none; the words name what is built, what is
#                                          partly, what is not. A row the hardware lacks (➖) is left out.
FEATURES = [
    ('Front ports', 'every front port an interface'),
    ('Receive', 'receive'),
    ('Transmit', 'transmit'),
    ('Flow table', 'a table the board\'s size'),
    ('Flow writes', 'flow writes posted without a wait'),
    ('IPv6 flows', 'IPv6'),
    ('Handed back', 'a connection handed back, revived in place'),
    ('IPsec', [('AES-GCM, both ways', 'GCM'), ('AES-CBC with an HMAC', 'CBC'), ('NAT traversal', 'NAT-T'),
               ('IPv6, outside and inside', 'IPv6'), ('extended sequence numbers', 'ESN')]),
    ('Tunnel flows', 'tunnel connections in the flow table'),
    ('Appliance', [('temperatures in OPNsense', 'sensors'), ('front panel', 'panel'),
                   ('hardware watchdog', 'watchdog'), ('PoE', 'PoE')]),
]
CARD_W = 400       # a card's width, here because the rows are fitted to it
MARKS = {'✅': 'ok', '🟡': 'wip', '⬜': 'no', '❌': 'no', '➖': 'none'}
# What a cell with no words of its own says, by its mark.
SAYS = {'ok': 'built and run', 'wip': 'partly', 'no': 'not built yet', 'none': 'none on this model'}


def parity_table():
    """{feature: [cell for the XGS 3300, cell for the XGS 136]} from parity.md's table, each cell with
    its struck values taken out and its links reduced to their words."""
    rows = {}
    for line in (DOCS / 'families' / 'parity.md').read_text(encoding='utf-8').split('\n'):
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if len(cells) != 4 or not line.startswith('|'):
            continue
        live = []
        for c in cells[2:]:
            c = re.sub(r'~~.*?~~', '', c)
            live.append(re.sub(r'\s+', ' ', c).strip())
        rows[cells[1]] = live
    return rows


def cell_mark(cell):
    """The mark a cell opens with, once its struck values are out, as ok | wip | no | none."""
    for m, state in MARKS.items():
        if cell.startswith(m):
            return state, cell[len(m):].strip(' -:')
    raise SystemExit('parity.md: a cell without a mark: %r' % cell[:60])


def fits(label, size=12.5):
    """How many characters of the value's monospace fit beside a label on a card."""
    room = (CARD_W - 22) - (48 + len(label) * 7.6 + 14)
    return int(room / (size * 0.6))


def phrase(words, limit):
    """The first thing a cell says, short enough for the card: up to its first link, bracket, dash or
    colon, then cut back clause by clause - at a comma, then before 'from', 'when', 'with', 'since' -
    until it fits. None when nothing does."""
    words = re.split(r'\[| \(|;| - |: ', words)[0].strip(' ,.')
    while words and len(words) > limit:
        if ', ' in words:
            words = words[:words.rindex(', ')]
            continue
        m = re.search(r' (?:from|when|with|since|by|after|until|as) ', words[8:])
        if not m:
            break
        words = words[:8 + m.start()]
    return words if words and len(words) <= limit else None


def feature_rows(column):
    """The FEATURES rows of one card - column 0 the XGS 3300's, 1 the XGS 136's - as the drawing wants
    them: (state, label, value), value in the SVG's entities."""
    table = parity_table()
    out = []
    for label, what in FEATURES:
        limit = fits(label)
        if isinstance(what, str):
            if what not in table:
                raise SystemExit('parity.md has no row %r for the board\'s %s' % (what, label))
            state, words = cell_mark(table[what][column])
            value = phrase(words, limit) or SAYS[state]
            out.append(('no' if state == 'none' else state, label, value))
            continue
        done, part, none = [], [], []
        for row, short in what:
            if row not in table:
                raise SystemExit('parity.md has no row %r for the board\'s %s' % (row, label))
            state, words = cell_mark(table[row][column])
            if state == 'ok':
                done.append(short)
            elif state == 'wip':
                part.append((short, phrase(words.split(',')[0], limit)))   # "read", "outbound only"
            elif state == 'no':
                none.append(short)
        state = 'ok' if not part and not none else ('wip' if done or part else 'no')
        tries = []
        for detailed in (True, False):
            v = ' &#183; '.join(done)
            if part:
                p = ', '.join('%s %s' % (s, w) if detailed and w else '%s partly' % s for s, w in part)
                v = '%s, %s' % (v, p) if v else p
            if none:
                v = '%s, no %s' % (v, ' &#183; '.join(none)) if v else 'not built yet'
            tries.append(v)
        value = next((v for v in tries if len(v.replace('&#183;', '.')) <= limit), tries[-1])
        out.append((state, label, value))
    return [(s, l, v.replace('\'', '&#8217;')) for s, l, v in out]


CARDS = [
    {
        'name': 'XGS 3300',
        'chip': 'Cavium OCTEON TX &#183; CN83XX',
        'rows': [  # ('section', title) or (state, label, value); state: ok | wip | no | m
            # The same rows, in the same order, on both cards - docs/families/parity.md - so a gap
            # on one shows as a half or an empty circle beside the other's full one.
            ('section', 'FEATURES'),
            ('features', 0),
            ('section', 'MEASURED'),
            ('m', 'IPv6, flows in hardware', '9.24 &#183; 9.27 Gbit/s'),     # measurements/2026-10-18
            ('m', 'IPsec, MTU 9000', '7.41 Gbit/s, one stream'),           # measurements/mtu-2026-10-17
            ('m', '1,024 connections at once', '9,837 Mbit/s, CPU 7.8 %'),  # measurements/xgs3300-many-2026-10-19
        ],
    },
    {
        'name': 'XGS 136',
        'chip': 'Marvell CN9131 &#183; ARMADA',
        'rows': [
            ('section', 'FEATURES'),
            ('features', 1),
            ('section', 'MEASURED'),
            ('m', 'Flows in hardware', '941 &#183; 914 Mbit/s, CPU 0.4 %'),  # measurements/xgs136-hardware-2026-10-18
            ('m', 'IPsec, MTU 9000', '982 Mbit/s, link-bound'),           # measurements/mtu-2026-10-17
            ('m', 'Two hours of TLS', '795 GiB, 0 bytes changed'),       # measurements/xgs136-soak-2026-10-19
        ],
    },
]

# Each card's completion, weighed by its family page's table, and its feature rows, read from
# parity.md - neither written here by hand.
for _card in CARDS:
    _card['done'] = int(weighed(WEIGHED[_card['name']]) + 0.5)
    _rows = []
    for _r in _card['rows']:
        _rows.extend(feature_rows(_r[1]) if _r[0] == 'features' else [_r])
    _card['rows'] = _rows

FAMILIES = [
    ('OCTEON TX2', 'described from vendor binaries, no hardware'),
    ('OCTEON TX2 98XX', 'described from vendor binaries, no hardware'),
    ('TOPAZ &#183; GR', 'no coprocessor, nothing to drive'),
]

STATS = [
    ('3,186 GiB', 'over TLS on both, not a byte changed'),  # measurements/xgs3300 2,391 + xgs136-soak 795
    ('6,100', 'give-backs revived, none refused'),        # measurements/xgs3300
    ('10 &#183; 19', 'claims withdrawn &#183; dead ends recorded'),  # the-road
]

# --------------------------------------------------------------- theme ----
THEMES = {
    'dark': dict(bg='#0D1117', card='#161B22', line='#30363D', fg='#E6EDF3', sub='#8B949E',
                 kk='#6E7681', acc='#E3B341', ok='#3FB950', wip='#58A6FF', no='#6E7681',
                 track='#21262D', grad='#161B22', dot='#FFFFFF'),
    'light': dict(bg='#FFFFFF', card='#F6F8FA', line='#D0D7DE', fg='#1F2328', sub='#59636E',
                  kk='#6E7781', acc='#9A6700', ok='#1A7F37', wip='#0969DA', no='#8C959F',
                  track='#EAEEF2', grad='#F6F8FA', dot='#1F2328'),
}
SANS = 'ui-sans-serif,-apple-system,Segoe UI,Inter,Helvetica,Arial,sans-serif'
MONO = 'ui-monospace,SFMono-Regular,Menlo,Consolas,monospace'

W = 900
ROW, SECTION = 28, 36
CARD_Y = 214
CARD_H = 92 + max(sum(SECTION if r[0] == 'section' else ROW for r in c['rows']) for c in CARDS) + 100
NOTE_Y = CARD_Y + CARD_H + 26
FAM_Y = NOTE_Y + 37
STAT_Y = FAM_Y + 74
H = STAT_Y + 74


def text(x, y, s, size, fill, weight=400, anchor='start', mono=False, extra=''):
    fam = MONO if mono else SANS
    return (f'<text x="{x}" y="{y}" font-family="{fam}" font-size="{size}" font-weight="{weight}" '
            f'fill="{fill}" text-anchor="{anchor}"{extra}>{s}</text>')


def state_icon(cx, cy, state, t):
    if state == 'ok':
        return (f'<circle cx="{cx}" cy="{cy}" r="8" fill="{t["ok"]}"/>'
                f'<path d="M{cx-3.6} {cy+0.2} l2.4 2.5 l4.8 -5" fill="none" stroke="{t["card"]}" '
                f'stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"/>')
    if state == 'wip':
        return (f'<circle cx="{cx}" cy="{cy}" r="7.2" fill="none" stroke="{t["wip"]}" stroke-width="1.6"/>'
                f'<path d="M{cx} {cy-7.2} A7.2 7.2 0 0 1 {cx} {cy+7.2} Z" fill="{t["wip"]}"/>')
    if state == 'm':
        return ''.join(f'<rect x="{cx-6+i*4.6:.1f}" y="{cy+6-h}" width="3" height="{h}" rx="1" fill="{t["acc"]}"/>'
                       for i, h in enumerate((5, 8.5, 12)))
    return f'<circle cx="{cx}" cy="{cy}" r="7.2" fill="none" stroke="{t["no"]}" stroke-width="1.6"/>'


def chip_tile(x, y, t):
    """A coprocessor glyph in a rounded tile: the package, its die, three pins a side and a pin-one mark."""
    size, body, die = 56, 28, 12
    cx, cy = x + size / 2, y + size / 2
    bx, by = cx - body / 2, cy - body / 2
    out = [f'<rect x="{x}" y="{y}" width="{size}" height="{size}" rx="12" fill="{t["bg"]}" stroke="{t["line"]}"/>']
    for k in (-8, 0, 8):
        out.append(f'<path d="M{cx+k} {by-2} v-5 M{cx+k} {by+body+2} v5 M{bx-2} {cy+k} h-5 M{bx+body+2} {cy+k} h5" '
                   f'stroke="{t["sub"]}" stroke-width="1.6" stroke-linecap="round" fill="none"/>')
    out.append(f'<rect x="{bx}" y="{by}" width="{body}" height="{body}" rx="5" fill="{t["acc"]}" fill-opacity="0.10" '
               f'stroke="{t["acc"]}" stroke-width="1.8"/>')
    out.append(f'<rect x="{cx-die/2}" y="{cy-die/2}" width="{die}" height="{die}" rx="2.5" fill="{t["acc"]}"/>')
    out.append(f'<circle cx="{bx+5}" cy="{by+5}" r="1.6" fill="{t["acc"]}"/>')
    return ''.join(out)


def card(x, c, t):
    y = CARD_Y
    out = [f'<rect x="{x}" y="{y}" width="{CARD_W}" height="{CARD_H}" rx="12" fill="{t["card"]}" stroke="{t["line"]}"/>',
           chip_tile(x + 22, y + 14, t),
           text(x + 92, y + 39, c['name'], 20, t['fg'], 700),
           text(x + 92, y + 59, c['chip'], 12.5, t['sub']),
           f'<line x1="{x+22}" y1="{y+80}" x2="{x+CARD_W-22}" y2="{y+80}" stroke="{t["line"]}"/>']
    ry = y + 92
    for row in c['rows']:
        if row[0] == 'section':
            ry += SECTION
            out.append(text(x + 22, ry - 9, row[1], 10.5, t['kk'], 700, mono=True, extra=' letter-spacing="1.6"'))
            continue
        state, label, value = row
        ry += ROW
        out.append(state_icon(x + 30, ry - 4, state, t))
        out.append(text(x + 48, ry, label, 13.5, t['fg'], 600))
        out.append(text(x + CARD_W - 22, ry, value, 12.5, t['sub'], anchor='end', mono=True))
    by = y + CARD_H - 52
    out.append(f'<line x1="{x+22}" y1="{by-18}" x2="{x+CARD_W-22}" y2="{by-18}" stroke="{t["line"]}"/>')
    out.append(text(x + 22, by, 'Weighted completion', 12.5, t['sub']))
    bw = CARD_W - 44
    out.append(text(x + CARD_W - 22, by + 1, f'{c["done"]} %', 17, t['fg'], 700, anchor='end'))
    out.append(f'<rect x="{x+22}" y="{by+12}" width="{bw}" height="9" rx="4.5" fill="{t["track"]}"/>')
    out.append(f'<rect x="{x+22}" y="{by+12}" width="{bw*c["done"]/100:.1f}" height="9" rx="4.5" fill="{t["ok"]}"/>')
    return ''.join(out)


def stage_track(t):
    xs = [100, 275, 450, 625, 800]
    y = 132
    out = [f'<line x1="{xs[0]}" y1="{y}" x2="{xs[-1]}" y2="{y}" stroke="{t["line"]}" stroke-width="2"/>']
    last = max(i for i, (_, s) in enumerate(STAGES) if s in ('done', 'now'))
    out.append(f'<line x1="{xs[0]}" y1="{y}" x2="{xs[last]}" y2="{y}" stroke="{t["ok"]}" stroke-width="2"/>')
    for x, (label, state) in zip(xs, STAGES):
        if state == 'done':
            out.append(state_icon(x, y, 'ok', dict(t, card=t['bg'])))
            colour, weight = t['fg'], 500
        elif state == 'now':
            out.append(f'<circle cx="{x}" cy="{y}" r="12" fill="{t["bg"]}" stroke="{t["acc"]}" stroke-width="2"/>'
                       f'<circle cx="{x}" cy="{y}" r="5" fill="{t["acc"]}"/>')
            colour, weight = t['acc'], 700
        else:
            out.append(f'<circle cx="{x}" cy="{y}" r="8" fill="{t["bg"]}" stroke="{t["no"]}" stroke-width="1.6"/>')
            colour, weight = t['sub'], 500
        out.append(text(x, y + 30, label, 12.5, colour, weight, anchor='middle'))
    out.append(text(W / 2, y + 58, STAGE_NOTE, 12.5, t['sub'], anchor='middle', extra=' font-style="italic"'))
    return ''.join(out)


def alt_text():
    parts = ['os-xgs-npu status. Stage: discovery, front ports, offload and integration done, the '
             'work-in-progress notice lifted; what is left being completed as updates.']
    for c in CARDS:
        rows = '; '.join(f'{r[1]}: {r[2]}' for r in c['rows'] if r[0] != 'section')
        parts.append(f'{c["name"]}, {c["chip"]}: {rows}; weighted completion {c["done"]} per cent.')
    parts.append('OCTEON TX2 and TX2 98XX described only; TOPAZ and GR have no coprocessor.')
    parts.append('; '.join(f'{a} {b}' for a, b in STATS) + '.')
    s = ' '.join(parts)
    for a, b in (('&#183;', 'and'), ('&#8217;', "'"), ('&amp;', 'and'), ('&#8220;', ''), ('&#8221;', '')):
        s = s.replace(a, b)
    return s.replace('"', '')


def status(theme):
    t = THEMES[theme]
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
           f'role="img" aria-label="{alt_text()}">',
           f'<rect width="{W}" height="{H}" rx="14" fill="{t["bg"]}"/>',
           f'<rect x="40" y="34" width="3" height="20" rx="1.5" fill="{t["acc"]}"/>',
           text(56, 51, 'os-xgs-npu &#8212; where it stands', 20, t['fg'], 700),
           text(56, 74, 'WHAT RUNS ON EACH APPLIANCE, AND HOW FAR ALONG IT IS', 11.5, t['kk'],
                mono=True, extra=' letter-spacing="1.4"'),
           f'<rect x="672" y="33" width="188" height="28" rx="14" fill="none" stroke="{t["wip"]}" stroke-width="1.4"/>',
           text(766, 52, 'BEING COMPLETED', 11.5, t['wip'], 700, anchor='middle', mono=True, extra=' letter-spacing="1.6"'),
           stage_track(t),
           card(40, CARDS[0], t),
           card(460, CARDS[1], t)]
    out.append(text(40, NOTE_Y, 'The same features on both cards: a full circle is run on that appliance, a half is '
                    'built or partial, an empty one is not built yet.', 11.5, t['sub']))
    out.append(text(40, NOTE_Y + 17, 'Every row is in docs/families/parity.md; every figure on a page in '
                    'docs/measurements, with a photograph of the run.', 11.5, t['sub']))
    for i, (name, desc) in enumerate(FAMILIES):
        fx = 40 + i * 280
        out.append(f'<rect x="{fx}" y="{FAM_Y}" width="260" height="52" rx="10" fill="none" stroke="{t["line"]}" stroke-dasharray="4 4"/>')
        out.append(state_icon(fx + 22, FAM_Y + 20, 'no', t))
        out.append(text(fx + 40, FAM_Y + 24, name, 13, t['fg'], 600))
        out.append(text(fx + 40, FAM_Y + 42, desc, 11.5, t['sub']))
    for i, (big, small) in enumerate(STATS):
        sx = 40 + i * 280
        out.append(text(sx, STAT_Y + 26, big, 22, t['acc'], 700))
        out.append(text(sx, STAT_Y + 48, small, 12, t['sub']))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def banner(theme):
    t = THEMES[theme]
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 300" width="1200" height="300" role="img" aria-label="os-xgs-npu - Sophos XGS hardware, back as OPNsense firewalls">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="{t['bg']}"/>
      <stop offset="100%" stop-color="{t['grad']}"/>
    </linearGradient>
    <pattern id="dots" width="22" height="22" patternUnits="userSpaceOnUse">
      <circle cx="1.5" cy="1.5" r="1.1" fill="{t['dot']}" opacity="0.05"/>
    </pattern>
  </defs>
  <rect width="1200" height="300" rx="14" fill="url(#bg)"/>
  <rect width="1200" height="300" rx="14" fill="url(#dots)"/>
  <g transform="translate(904 46) scale(2)">{chip_tile(0, 0, dict(t, bg=t['card'])).replace('rx="12"', 'rx="16"')}</g>
  <g fill="none" stroke="{t['acc']}" stroke-width="1.5">
    <path d="M960 196 L1060 226 L960 256 L860 226 Z" opacity="0.55"/>
    <path d="M960 214 L1060 244 L960 274 L860 244 Z" opacity="0.25"/>
  </g>
  <rect x="80" y="96" width="4" height="108" rx="2" fill="{t['acc']}"/>
  <text x="108" y="140" font-family="{SANS}" font-size="54" font-weight="700" fill="{t['fg']}" letter-spacing="-1">os-xgs-npu</text>
  <text x="110" y="182" font-family="{SANS}" font-size="22" font-weight="500" fill="{t['sub']}">Sophos XGS hardware, back as OPNsense firewalls</text>
  <text x="110" y="216" font-family="{MONO}" font-size="15" fill="{t['kk']}">FreeBSD drivers &#183; OCTEON TX &amp; ARMADA &#183; flow &amp; IPsec offload &#183; Arabic &amp; RTL</text>
</svg>
'''


if __name__ == '__main__':
    if '--check' in sys.argv:
        sys.exit(0 if check() else 1)
    if '--out' in sys.argv:
        OUT = pathlib.Path(sys.argv[sys.argv.index('--out') + 1])
    OUT.mkdir(parents=True, exist_ok=True)
    for theme in THEMES:
        (OUT / f'status-{theme}.svg').write_text(status(theme), encoding='utf-8', newline='\n')
        (OUT / f'banner-{theme}.svg').write_text(banner(theme), encoding='utf-8', newline='\n')
    print(f'wrote {OUT}/{{banner,status}}-{{dark,light}}.svg (status {W}x{H}), '
          + ', '.join(f"{c['name']} {c['done']}%" for c in CARDS))
