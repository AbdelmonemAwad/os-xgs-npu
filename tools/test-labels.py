#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/labels.py against made-up issue events and a stand-in GitHub: the model a form names becomes
the issue's label, a changed model moves it, and nothing else is touched.

    python3 tools/test-labels.py"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import labels  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


class GitHub:
    def __init__(self):
        self.written = []

    def __call__(self, method, path, body=None):
        self.written.append((method, path.split('/issues/')[1], body))


FORM = '### Appliance\n\n%s\n\n### Board\n\nAMDA0200\n\n### Version\n\nv0.1.0-alpha.10.6'


def go(action, body, have=(), pull=False):
    gh = GitHub()
    issue = {'number': 5, 'body': body, 'labels': [{'name': n} for n in have]}
    if pull:
        issue['pull_request'] = {}
    labels.run({'action': action, 'issue': issue}, gh, say=lambda s: None)
    return gh.written


check('the model from the form', labels.model(FORM % 'XGS 107'), 'XGS 107')
check('another model is no model', labels.model(FORM % 'Another model'), None)
check('no answer is no model', labels.model(FORM % '_No response_'), None)
check('no form is no model', labels.model('It does not boot.'), None)
check('a new issue gets its model', go('opened', FORM % 'XGS 3300'),
      [('POST', '5/labels', {'labels': ['XGS 3300']})])
check('a label it has already is not put on again', go('edited', FORM % 'XGS 3300', ['XGS 3300']), [])
check('a model changed in an edit moves the label', go('edited', FORM % 'XGS 136', ['XGS 3300', 'bug']),
      [('DELETE', '5/labels/XGS%203300', None), ('POST', '5/labels', {'labels': ['XGS 136']})])
check("the reporter's own labels are left alone", go('edited', FORM % 'XGS 3300', ['Sophos XGS 3300r1']),
      [('POST', '5/labels', {'labels': ['XGS 3300']})])
check('a model answered away takes the label off', go('edited', FORM % 'Another model', ['XGS 87']),
      [('DELETE', '5/labels/XGS%2087', None)])
check('a pull request is left alone', go('opened', FORM % 'XGS 126', pull=True), [])
check('other actions are left alone', go('labeled', FORM % 'XGS 126'), [])

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
