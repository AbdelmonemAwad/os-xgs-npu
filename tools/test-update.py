#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""What scripts/xgs/update.py offers and what apply() does with it, without GitHub (issue 677).

    python3 tools/test-update.py

On 2026-10-22 a check that could not reach GitHub was followed by an update that installed the
target of an earlier check, and that target - a release - was older than the build it replaced. So:
apply() checks again itself and stops with the check's error; and a build that has diverged from the
newest release is not taken back to it when it is dated after it. GitHub's answers are stood in for
here, and the state directory is a temporary one.
"""
import importlib.util
import os
import sys
import tempfile
import urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'update', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'update.py'))
up = importlib.util.module_from_spec(spec)
spec.loader.exec_module(up)

STATE = tempfile.mkdtemp(prefix='test-update-')
up.STATE = STATE
up.CHECK = os.path.join(STATE, 'check.json')
up.APPLY = os.path.join(STATE, 'apply.json')
up.LOG = os.path.join(STATE, 'apply.log')
up.BRANCH = ''
up.out = lambda obj: obj          # apply() and check() hand back what they would print

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


RELEASE = {'tag_name': 'v0.1.0-alpha.5.3', 'name': 'v0.1.0-alpha.5.3', 'draft': False,
           'published_at': '2026-10-21T18:00:00Z', 'body': '- a change', 'html_url': ''}
REL_SHA = 'a' * 40
BUILD_SHA = 'b' * 40


def github(compare_status):
    """GitHub's three answers to a check: the releases, the newest one's commit, the comparison."""
    def get(url, direct=False):
        if '/releases' in url:
            return [RELEASE]
        if '/compare/' in url:
            return {'status': compare_status, 'ahead_by': 3, 'behind_by': 5}
        if '/commits/' in url:
            return {'sha': REL_SHA}
        raise AssertionError('unexpected request ' + url)
    return get


def unreachable(url, direct=False):
    raise urllib.error.URLError('Name could not be resolved')


def build(date):
    return lambda: {'commit': BUILD_SHA, 'date': date, 'source': 'update', 'release': ''}


# The dates.
check('a git date read', up.when('2026-10-22T01:00:00+00:00') > 0, True)
check('a GitHub date read', up.when('2026-10-21T18:00:00Z'), up.when('2026-10-21T18:00:00+00:00'))
check('no date', up.when(''), 0)
check('a date that is not one', up.when('yesterday'), 0)
check('unknown either way is not newer', up.newer_than('', '2026-10-21T18:00:00Z'), False)

# A diverged build dated after the release: not offered, and said to be newer (issue 677).
up.get = github('diverged')
up.installed = build('2026-10-22T01:00:00+00:00')
r = up.run_check()
check('diverged, built after the release: not offered', r.get('update_available'), False)
check('diverged, built after the release: said to be newer', r.get('newer_than_target'), True)

# A diverged build dated before it - a test build left behind: offered, as since 2026-10-19.
up.installed = build('2026-10-19T10:00:00+00:00')
r = up.run_check()
check('diverged, built before the release: offered', r.get('update_available'), True)
check('diverged, built before the release: not said to be newer', r.get('newer_than_target'), None)

# Ahead and behind as they were.
up.get = github('ahead')
check('a release ahead: offered', up.run_check().get('update_available'), True)
up.get = github('behind')
check('a release behind: not offered', up.run_check().get('update_available'), False)

# apply() after a check that failed: an earlier answer offered an update, the check made now fails,
# and nothing is downloaded.
up.get = github('ahead')
up.installed = build('2026-10-19T10:00:00+00:00')
check('the earlier check offered it', up.run_check().get('update_available'), True)
up.get = unreachable


def no_download(*args, **kwargs):
    raise AssertionError('apply() downloaded after a failed check')


up.urllib.request.urlopen = no_download
up.tag_commit = no_download
r = up.apply()
check('apply() after a failed check: an error', r.get('status'), 'error')
check('apply() after a failed check: the check\'s own reason in it',
      'Name could not be resolved' in r.get('message', ''), True)
check('apply() after a failed check: no update started', os.path.exists(up.APPLY), False)

# apply() when the check made now finds nothing newer.
up.get = github('behind')
r = up.apply()
check('apply() with nothing newer: an error', r.get('status'), 'error')
check('apply() with nothing newer: no update started', os.path.exists(up.APPLY), False)

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
