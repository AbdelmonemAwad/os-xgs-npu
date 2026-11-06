#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/coprocessor.py's state when a look cannot read the coprocessor: the page's file says
so, and the reading before it is kept - for the next look's carried-over contents and for a problem
report's vendor build. Until 2026-11-06 the failed look replaced it.

    python3 tools/test-coprocessor-state.py"""
import importlib.util
import json
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'coprocessor.py')

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def load(tmp):
    spec = importlib.util.spec_from_file_location('coprocessor_under_test', TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.STATE = os.path.join(tmp, 'coprocessor.json')
    module.LAST_GOOD = os.path.join(tmp, 'coprocessor.last-good.json')
    module.run = lambda argv, timeout=60: ''       # the console answered nothing
    module.models = None
    return module


def read(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


GOOD = {'at': 100, 'software': {'sfversion': '21.0.0.169'}, 'emmc': []}

with tempfile.TemporaryDirectory() as tmp:
    m = load(tmp)
    # a good reading written by a version before LAST_GOOD existed
    with open(m.STATE, 'w', encoding='utf-8') as f:
        json.dump(GOOD, f)
    m.collect()
    check('a look that could not read: the page is told', read(m.STATE).get('error'), 'unread')
    check('the reading before it is kept', read(m.LAST_GOOD).get('software'), GOOD['software'])
    check('and it is what last_good() gives', m.last_good().get('at'), 100)
    m.collect()
    check('a second look that could not read keeps it still', read(m.LAST_GOOD).get('at'), 100)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
