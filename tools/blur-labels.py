#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Pixelate the parts of a photograph that identify one machine - a serial number, a barcode, a
# QR code - and bring it to the size the pages use. Part numbers, chip markings and reference
# designators are left: they describe the design and not the unit.
#
#   tools/blur-labels.py IN OUT [--max 1600] [--box x0,y0,x1,y1 ...] [--pct]
#
# Boxes are in the source image's pixels, or in percent of its width and height with --pct. Each
# box is reduced to a few blocks and scaled back, which cannot be undone by sharpening. The output
# is a JPEG with its EXIF dropped, so no camera serial, position or time travels with it either.
#
# The private-data checker skips images, so this is the one place the rule for them is enforced:
# a photograph goes into docs/images only through here, and the boxes it was given are written
# into the commit that adds it.
import sys
from PIL import Image, ImageOps


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    src, dst = argv[0], argv[1]
    boxes = []
    maxside = 1600
    pct = "--pct" in argv
    i = 2
    while i < len(argv):
        if argv[i] == "--max":
            maxside = int(argv[i + 1]); i += 2
        elif argv[i] == "--box":
            boxes.append(tuple(float(v) for v in argv[i + 1].split(","))); i += 2
        else:
            i += 1
    im = Image.open(src)
    im = ImageOps.exif_transpose(im)   # apply the camera's rotation, then drop the EXIF with it
    im = im.convert("RGB")
    w, h = im.size
    for b in boxes:
        if pct:
            b = (b[0] * w / 100, b[1] * h / 100, b[2] * w / 100, b[3] * h / 100)
        x0, y0, x1, y1 = (int(round(v)) for v in b)
        x0, x1 = max(0, min(x0, x1)), min(w, max(x0, x1))
        y0, y1 = max(0, min(y0, y1)), min(h, max(y0, y1))
        if x1 - x0 < 2 or y1 - y0 < 2:
            continue
        region = im.crop((x0, y0, x1, y1))
        blocks = max(4, min(12, (x1 - x0) // 40))
        small = region.resize((blocks, max(2, blocks * (y1 - y0) // max(1, x1 - x0))), Image.BILINEAR)
        im.paste(small.resize(region.size, Image.NEAREST), (x0, y0))
    if max(w, h) > maxside:
        s = maxside / max(w, h)
        im = im.resize((int(w * s), int(h * s)), Image.LANCZOS)
    im.save(dst, "JPEG", quality=86, optimize=True)
    print("%s -> %s %dx%d, %d box(es) pixelated" % (src, dst, im.size[0], im.size[1], len(boxes)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
