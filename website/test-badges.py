#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The site's badges (website/build.py, copy_badge): a fresh copy when the badge's host answers, the
last good copy with a note when it does not, and a warning for a badge never copied - which the
build's --check makes fatal. Since 2026-11-21 (#1147): a slow img.shields.io failed whole builds.

    python3 website/test-badges.py        (with website/requirements.txt installed)
"""
import importlib.util
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'website'))
spec = importlib.util.spec_from_file_location('site_build', ROOT / 'website' / 'build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

URL = 'https://img.shields.io/badge/a-b-green'
SVG = b'<svg xmlns="http://www.w3.org/2000/svg"></svg>'
failures = 0
checks = 0


def check(name, got, want):
    global failures, checks
    checks += 1
    if got != want:
        failures += 1
        print('FAIL %s\n  got:  %r\n  want: %r' % (name, got, want))
    else:
        print('ok   %s' % name)


def down(url):
    raise OSError('timed out')


def answers(url):
    return SVG


with tempfile.TemporaryDirectory() as tmp:
    cache = pathlib.Path(tmp)
    sleeps = []
    path, note, warning = build.copy_badge(URL, cache, fetch=down, sleep=sleeps.append)
    check('never copied, its host down: no copy, no note, a warning',
          (path, note, 'could not be copied' in (warning or '')), (None, None, True))
    check('three tries, 3 s and then 6 s apart', sleeps, [3, 6])

    path, note, warning = build.copy_badge(URL, cache, fetch=answers, sleep=sleeps.append)
    check('its host answers: the copy written, no note, no warning',
          (path.read_bytes() == SVG, note, warning), (True, None, None))

    path, note, warning = build.copy_badge(URL, cache, fetch=down, sleep=lambda s: None)
    check('its host down, a copy kept: that copy, a note giving its date, no warning',
          (path.read_bytes() == SVG, 'could not be refreshed' in (note or '') and 'UTC' in note, warning),
          (True, True, None))

    calls = []

    def not_svg(url):
        calls.append(url)
        return b'<html>rate limited</html>'

    path, note, warning = build.copy_badge(URL, cache, fetch=not_svg, sleep=lambda s: None)
    check('an answer that is not an SVG: asked once, the good copy kept and used with a note',
          (len(calls), path.read_bytes() == SVG, 'not an SVG' in (note or ''), warning), (1, True, True, None))

print('%d of %d checks failed' % (failures, checks) if failures else '%d checks passed' % checks)
sys.exit(1 if failures else 0)
