#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Every API action of this package that saves config.xml itself - Config::getInstance()->save(), not
through OPNsense's own model controllers, which check for themselves - refuses a user who holds
user-config-readonly first, with ApiControllerBase::throwReadOnly(), as those controllers do. Until
2026-11-06 ReportsController and PoeController did not (the review of 2026-11-06, G11).

Read from the source, nothing run:
    python3 tools/test-readonly-saves.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTROLLERS = os.path.join(ROOT, 'src', 'opnsense', 'mvc', 'app', 'controllers')
ACTION = re.compile(r'function\s+(\w+Action)\s*\([^)]*\)\s*\{')
SAVE = 'Config::getInstance()->save()'
CHECK = '$this->throwReadOnly()'
# comments and quoted strings blanked, so that a brace in either does not count
NOISE = re.compile(r"/\*.*?\*/|//[^\n]*|#[^\n]*|'(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\"", re.S)


def bodies(text):
    """(name, body) of each action, its body to the brace that closes it - with its comments and
    strings blanked, so that a comment naming the check is not taken for the check."""
    plain = NOISE.sub(lambda m: ' ' * len(m.group(0)), text)
    for m in ACTION.finditer(plain):
        depth, i = 1, m.end()
        while depth and i < len(plain):
            depth += {'{': 1, '}': -1}.get(plain[i], 0)
            i += 1
        yield m.group(1), plain[m.end():i]


def main():
    saving, failures = 0, []
    for top, _dirs, files in os.walk(CONTROLLERS):
        for name in sorted(files):
            if not name.endswith('.php'):
                continue
            path = os.path.join(top, name)
            with open(path, encoding='utf-8') as f:
                text = f.read()
            for action, body in bodies(text):
                if SAVE not in body:
                    continue
                saving += 1
                where = '%s %s' % (os.path.relpath(path, ROOT).replace(os.sep, '/'), action)
                if CHECK not in body or body.index(CHECK) > body.index(SAVE):
                    failures.append(where)
                    print('FAIL %s saves config.xml without throwReadOnly() first' % where)
                else:
                    print('ok   %s' % where)
    if saving == 0:
        print('FAIL no action saves config.xml directly: the test reads nothing')
        return 1
    print('%d action(s) save config.xml directly, %d without the check' % (saving, len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
