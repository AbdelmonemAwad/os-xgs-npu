#!/usr/bin/env python3
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
# SPDX-License-Identifier: BSD-2-Clause
"""Mark the photographs and screenshots with their owner and their licence.

Every PNG and JPEG under the folders given (docs/images by default) gets a strip added below it -
under the picture, never over it, so a measurement photographed is still exactly what the screen
showed and an appliance's labels and lights are not covered - reading

    © 2026 Abdelmonem Awad  ·  github.com/AbdelmonemAwad  ·  CC BY 4.0

and the same notice inside the file (a PNG text chunk, the JPEG's EXIF Copyright and Artist). A
JPEG's other EXIF - the camera's, where it was taken - is not kept. An image already marked is left
alone, so this can run after every new photograph, and the check finds one that was not.

    python3 tools/mark-images.py [dir|file ...]          mark what is not marked yet (needs Pillow)
    python3 tools/mark-images.py --check [dir|file ...]  name every image not marked; exit 1 if any
                                                         (reads the files' bytes only, no Pillow)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT = ['docs/images']
EXT = ('.png', '.jpg', '.jpeg')
OWNER = 'Abdelmonem Awad'
VISIBLE = '\u00a9 2026 ' + OWNER + '  \u00b7  github.com/AbdelmonemAwad  \u00b7  CC BY 4.0'
# ASCII, as EXIF wants it; also what the check looks for in either kind of file
NOTICE = 'Copyright 2026 ' + OWNER + '. Licensed CC BY 4.0: https://creativecommons.org/licenses/by/4.0/'
BAND = (21, 23, 28)
INK = (196, 200, 208)
FONTS = ['C:/Windows/Fonts/segoeui.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
         '/usr/local/share/fonts/dejavu/DejaVuSans.ttf']


def images(dirs):
    for d in dirs:
        d = os.path.join(ROOT, d)
        if os.path.isfile(d):
            if d.lower().endswith(EXT):
                yield d
            continue
        for base, _, files in os.walk(d):
            for f in sorted(files):
                if f.lower().endswith(EXT):
                    yield os.path.join(base, f)


def marked(path):
    with open(path, 'rb') as fh:
        return NOTICE.encode('ascii') in fh.read()


def font(size):
    from PIL import ImageFont
    for f in FONTS:
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default(size)


def mark(path):
    from PIL import Image, ImageDraw, ImageOps
    from PIL.PngImagePlugin import PngInfo

    src = Image.open(path)
    jpeg = src.format == 'JPEG'
    im = ImageOps.exif_transpose(src) if jpeg else src.copy()
    w, h = im.size
    band = max(22, round(h * 0.03))
    size = round(band * 0.58)
    f = font(size)
    pad = round(band * 0.5)
    while size > 9 and f.getlength(VISIBLE) > w - 2 * pad:
        size -= 1
        f = font(size)
    mode = 'RGBA' if im.mode in ('RGBA', 'LA', 'P') else 'RGB'
    out = Image.new(mode, (w, h + band), BAND + ((255,) if mode == 'RGBA' else ()))
    out.paste(im.convert(mode), (0, 0))
    d = ImageDraw.Draw(out)
    top, bottom = f.getbbox(VISIBLE)[1], f.getbbox(VISIBLE)[3]
    d.text((pad, h + (band - (bottom - top)) / 2 - top), VISIBLE, font=f, fill=INK)
    if jpeg:
        exif = Image.Exif()
        exif[0x8298] = NOTICE       # Copyright
        exif[0x013B] = OWNER        # Artist
        out.save(path, 'JPEG', quality=92, exif=exif.tobytes())
    else:
        info = PngInfo()
        info.add_text('Copyright', NOTICE)
        info.add_text('Author', OWNER)
        out.save(path, 'PNG', pnginfo=info, optimize=True)


def main(argv):
    check = '--check' in argv
    dirs = [a for a in argv if not a.startswith('--')] or DEFAULT
    todo = [p for p in images(dirs) if not marked(p)]
    if check:
        for p in todo:
            print('not marked:', os.path.relpath(p, ROOT).replace(os.sep, '/'))
        print(f'{len(todo)} image(s) not marked' if todo else 'every image is marked')
        return 1 if todo else 0
    for p in todo:
        mark(p)
        print('marked', os.path.relpath(p, ROOT).replace(os.sep, '/'))
    print(f'{len(todo)} image(s) marked')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
