#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Every command the plugin's scripts read as text is decoded with errors='replace'.

A strict decode raises UnicodeDecodeError on one byte that is not UTF-8 - a coprocessor's console
gives them, and so can dmesg - and that error is neither OSError nor SubprocessError, so the
scripts' own handlers let it through and the script stopped (the review of 2026-11-06, D6). This
reads each script's syntax tree, so a call written over several lines is seen as one.

    python3 tools/test-decodes.py"""
import ast
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(ROOT, 'src', 'opnsense', 'scripts')


def strict_calls(path):
    """The lines of the calls in path that ask for text and do not say errors='replace'."""
    with open(path, encoding='utf-8') as f:
        tree = ast.parse(f.read(), path)
    found = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        kws = {k.arg: k.value for k in node.keywords if k.arg}
        text = kws.get('text') or kws.get('universal_newlines')
        if isinstance(text, ast.Constant) and text.value is True:
            errors = kws.get('errors')
            if not (isinstance(errors, ast.Constant) and errors.value == 'replace'):
                found.append(node.lineno)
    return found


failures = 0
checked = 0
for top, _, names in os.walk(SCRIPTS):
    for name in sorted(names):
        if name.endswith('.py'):
            path = os.path.join(top, name)
            checked += 1
            for line in strict_calls(path):
                failures += 1
                print('FAIL %s:%d decodes strictly' % (os.path.relpath(path, ROOT), line))

print('%d script(s) read, %d strict decode(s)' % (checked, failures))
sys.exit(1 if failures else 0)
