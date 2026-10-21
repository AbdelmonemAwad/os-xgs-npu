#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Every string the coprocessor's pages show has its Arabic in opnsense-arabic's catalogue.

    tools/check-gui-strings.py [repository]

The strings are the ones OPNsense passes through gettext: lang._() in the views, the labels and
help of the settings form, the menu's and the privileges' names, and gettext() in PHP. A string
added to a page without its Arabic shows up in English on an otherwise Arabic page and nothing
says so - this does, and fails.

The Arabic is in opnsense-arabic's ar_SA.po since 2026-10-22 (its issue 5), the one Arabic
catalogue of the whole GUI, read by tools/arabic_catalogue.py: from a checkout of it when ARABIC_PO
names its ar_SA.po, else from its main branch on GitHub. A string added here needs its Arabic there
first.
"""

import os
import pathlib
import re
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arabic_catalogue                                                  # noqa: E402


def strings():
    found = {}
    for f in sorted((ROOT / 'src').rglob('*')):
        if not f.is_file():
            continue
        rel = str(f.relative_to(ROOT))
        if f.suffix == '.volt':
            t = f.read_text(encoding='utf-8')
            for s in (re.findall(r"lang\._\('((?:[^'\\]|\\.)*)'\)", t) +
                      re.findall(r'lang\._\("((?:[^"\\]|\\.)*)"\)', t)):
                found.setdefault(s.replace("\\'", "'"), rel)
        elif f.suffix == '.xml' and ('forms' in f.parts or f.name in ('Menu.xml', 'ACL.xml')):
            t = f.read_text(encoding='utf-8')
            for s in re.findall(r'<(?:label|help|name|description)>(.*?)</(?:label|help|name|description)>', t, re.S):
                found.setdefault(' '.join(s.split()), rel)
            for s in re.findall(r'VisibleName="([^"]+)"', t):
                found.setdefault(s, rel)
        elif f.suffix == '.xml' and 'widgets' in f.parts:
            # A dashboard widget's strings: the dashboard controller passes every child of
            # <translations> through gettext. Comments first out: the file's own comment names
            # the element, and would otherwise open a match.
            t = re.sub(r'<!--.*?-->', '', f.read_text(encoding='utf-8'), flags=re.S)
            for block in re.findall(r'<translations>(.*?)</translations>', t, re.S):
                for s in re.findall(r'<(\w+)>([^<]+)</\1>', block):
                    found.setdefault(' '.join(s[1].split()), rel)
        elif f.suffix == '.xml' and 'models' in f.parts:
            # A model's choices: OptionField passes the text of each one through gettext, so the
            # problem reports' modes are shown in the language of the page like any label.
            t = f.read_text(encoding='utf-8')
            for block in re.findall(r'<OptionValues>(.*?)</OptionValues>', t, re.S):
                for s in re.findall(r'<(\w+)>([^<]+)</\1>', block):
                    found.setdefault(' '.join(s[1].split()), rel)
        elif f.suffix in ('.inc', '.php'):
            t = f.read_text(encoding='utf-8', errors='ignore')
            for s in re.findall(r"gettext\('((?:[^'\\]|\\.)*)'\)", t):
                found.setdefault(s, rel)
    return found


def main():
    used = strings()
    try:
        table = arabic_catalogue.catalogue()
    except (OSError, ValueError) as exc:
        print("opnsense-arabic's catalogue cannot be read: %s" % exc)
        return 1
    missing = [(s, f) for s, f in used.items() if not table.get(s)]
    for s, f in missing:
        print('NEEDS ARABIC (%s): %s' % (f, s[:100]))
    if missing:
        print("%d string(s) without Arabic. Add each to opnsense-arabic's ar_SA.po first." % len(missing))
        return 1
    print('%d string(s) on the pages, every one with its Arabic.' % len(used))
    return 0


if __name__ == '__main__':
    sys.exit(main())
