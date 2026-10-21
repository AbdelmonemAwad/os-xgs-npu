#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The picture a link to a model's page shows where it is shared - a card of 1200 by 630, the size
the sites that show one ask for: the model's name, its family, coprocessor and ports, its state,
and its panel drawn as its page draws it, in the site's dark colours and its own type.

build.py calls write() while it gathers the pages, into assets/og/; devices.py names the card in each
model page's front matter, and main.html gives it to the page's head. A shared link does not run a
page's SVG, so the card is a PNG, drawn here with Pillow from website/data/devices.json.

And the site's icon for what does not read its SVG one - favicon.ico, and the icon a phone puts on
its home screen: icons(), at the site's root.
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

HERE = pathlib.Path(__file__).resolve().parent
FONTS = HERE / 'pages' / 'assets' / 'fonts'
W, H = 1200, 630

BG, PANEL, LINE, TEXT, MUTED = '#101317', '#1a1f25', '#343c47', '#e6e9ed', '#98a2af'
TEAL, COPPER, GREEN, AMBER = '#39c2c6', '#e8834a', '#3ccb86', '#e9b04c'


def font(name, size):
    """The site's own type; Pillow's when FreeType cannot read it."""
    try:
        return ImageFont.truetype(str(FONTS / name), size)
    except OSError:
        return ImageFont.load_default(size=size)


def panel(draw, page, x0, y0, width):
    """The model's panel from its layout, as devices.py lays it out, scaled to width."""
    ports = {p['label']: p for p in page['ports']}
    glyph = {'RJ45': (30, 26), 'SFP': (38, 18), 'SFP+': (38, 18)}
    items, x = [], 18
    for tok in page['panel']['layout'].split():
        if tok == '|':
            x += 16
        elif tok == 'lcd':
            items.append(('box', x, 26, 96, 34))
            x += 104
        elif tok in ('com', 'mgmt'):
            items.append(('box', x, 46, 26, 22))
            x += 36
        elif tok == 'usb':
            items += [('box', x, 45, 22, 9), ('box', x, 60, 22, 9)]
            x += 32
        elif tok == 'bay':
            items.append(('bay', x, 20, 112, 74))
            x += 120
        elif tok.startswith('led:'):
            x += 13 + 9 + 6
        else:
            col = [ports[l] for l in tok.split('/')]
            cw = max(glyph[p['socket']][0] for p in col)
            rows = (28, 60) if len(col) == 2 else (44,)
            for p, top in zip(col, rows):
                gw, gh = glyph[p['socket']]
                items.append(('port', x, top + (26 - gh) / 2, gw, gh, p))
            x += cw + 6
    total = x + 12
    k = width / total
    draw.rounded_rectangle([x0, y0, x0 + total * k, y0 + 114 * k], radius=8 * k, fill=PANEL, outline=LINE, width=2)
    for it in items:
        kind, ix, iy, iw, ih = it[:5]
        box = [x0 + ix * k, y0 + iy * k, x0 + (ix + iw) * k, y0 + (iy + ih) * k]
        if kind == 'port':
            own = it[5]['switch_port'] is None
            draw.rounded_rectangle(box, radius=2 * k, fill=BG, outline=TEAL if own else MUTED, width=3 if own else 2)
        elif kind == 'bay':
            draw.rounded_rectangle(box, radius=4 * k, outline=MUTED, width=2)
        else:
            draw.rounded_rectangle(box, radius=2 * k, outline=LINE, width=2)
    return y0 + 114 * k


def card(page):
    img = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(img)
    brand = font('IBMPlexSans-Bold.woff2', 30)
    d.text((64, 52), 'npu', font=brand, fill=TEAL)
    d.text((64 + d.textlength('npu', font=brand), 52), 'forge', font=brand, fill=COPPER)
    d.text((64, 104), page['model'], font=font('IBMPlexSans-Bold.woff2', 92), fill=TEXT)
    sub = '%s  ·  %s  ·  %d front ports: %s' % (page['family'], page['chip'], page['count'], page['sockets'])
    d.text((68, 222), sub, font=font('IBMPlexSans-Regular.woff2', 30), fill=MUTED)
    state = 'run here' if page['run'] else 'added, untested'
    sf = font('IBMPlexSans-SemiBold.woff2', 26)
    sw = d.textlength(state, font=sf)
    color = GREEN if page['run'] else AMBER
    d.rounded_rectangle([W - 64 - sw - 44, 62, W - 64, 112], radius=25, outline=color, width=3)
    d.text((W - 64 - sw - 22, 70), state, font=sf, fill=color)
    bottom = panel(d, page, 64, 300, W - 128)
    d.text((64, max(bottom + 28, 560)), 'npuforge.org/devices/%s' % page['slug'], font=font('IBMPlexMono-Medium.woff2', 24), fill=MUTED)
    return img


def write(out_dir, data):
    """Each model's card into out_dir/assets/og/<slug>.png."""
    d = pathlib.Path(out_dir) / 'assets' / 'og'
    d.mkdir(parents=True, exist_ok=True)
    for page in data['pages']:
        card(page).save(d / ('%s.png' % page['slug']), optimize=True)


def icon(size, rounded=True):
    """pages/assets/favicon.svg drawn at size: a chip, its pins on four sides, on the dark ground - for
    what does not take an SVG icon. Drawn four times larger and scaled down, for clean edges."""
    big = size * 4
    k = big / 48
    img = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if rounded:
        d.rounded_rectangle((0, 0, big - 1, big - 1), radius=11 * k, fill='#16191D')
    else:
        d.rectangle((0, 0, big - 1, big - 1), fill='#16191D')
    half = 1.2 * k
    for c in (19, 24, 29):
        for a, b in ((9, 14), (34, 39)):
            d.rectangle((c * k - half, a * k - half, c * k + half, b * k + half), fill='#E6E9ED')
            d.rectangle((a * k - half, c * k - half, b * k + half, c * k + half), fill='#E6E9ED')
    d.rounded_rectangle((14 * k, 14 * k, 34 * k, 34 * k), radius=4 * k, outline='#E8834A', width=round(3.2 * k))
    return img.resize((size, size), Image.LANCZOS)


def icons(out_dir):
    """favicon.ico and apple-touch-icon.png at the site's root, where the browsers and the sites that
    do not read favicon.svg look for one; the touch icon square, as the phone rounds it itself."""
    out = pathlib.Path(out_dir)
    icon(48).save(out / 'favicon.ico', sizes=[(16, 16), (32, 32), (48, 48)])
    icon(180, rounded=False).convert('RGB').save(out / 'apple-touch-icon.png', optimize=True)
