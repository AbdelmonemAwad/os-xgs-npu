#!/usr/bin/env python3
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
# SPDX-License-Identifier: BSD-2-Clause
"""Every relative link and image in the Markdown pages must point at a file that exists.

A page that is moved takes its links with it, and a link that was right from the old place
is wrong from the new one; this is the check that says so before a merge does. Absolute
links (http, https, mailto) are not followed. A fragment after '#' is ignored. A link to a
directory is accepted when the directory exists.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK = re.compile(r'(?<!\\)\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
SKIP = ('http://', 'https://', 'mailto:', '#')


def pages():
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in ('.git', 'kernel-sources')]
        for f in files:
            if f.endswith('.md'):
                yield os.path.join(base, f)


def main():
    bad = 0
    for page in pages():
        text = open(page, encoding='utf-8').read()
        here = os.path.dirname(page)
        for n, line in enumerate(text.splitlines(), 1):
            for target in LINK.findall(line):
                if target.startswith(SKIP):
                    continue
                path = target.split('#', 1)[0]
                if not path:
                    continue
                full = os.path.normpath(os.path.join(here, path))
                if not os.path.exists(full):
                    rel = os.path.relpath(page, ROOT).replace(os.sep, '/')
                    print(f'{rel}:{n}: {target} does not exist')
                    bad += 1
    if bad:
        print(f'{bad} link(s) point at nothing')
        return 1
    print('every relative link in the pages points at a file that exists')
    return 0


if __name__ == '__main__':
    sys.exit(main())
