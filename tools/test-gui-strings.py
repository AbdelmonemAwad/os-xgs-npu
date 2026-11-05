#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""tools/check-gui-strings.py on a made-up repository: a page with two strings, and a catalogue with
the Arabic of one of them. Without ARABIC_LATER the other fails the check, as on a pull request from a
branch of this repository; with ARABIC_LATER=1 - a contributor's pull request, or main - it is a
warning GitHub shows and a line of the job's summary, and the check passes.

    python3 tools/test-gui-strings.py
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = os.path.join(HERE, 'check-gui-strings.py')
PAGE = "<h1>{{ lang._('Board') }}</h1>\n<p>{{ lang._('A string nobody has translated yet') }}</p>\n"
CATALOGUE = ('msgid ""\nmsgstr ""\n"Content-Type: text/plain; charset=UTF-8\\n"\n\n'
             'msgid "Board"\nmsgstr "Board, translated"\n')


class ArabicLater(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        views = os.path.join(self.root, 'src', 'opnsense', 'mvc', 'app', 'views', 'OPNsense', 'XGS')
        os.makedirs(views)
        with open(os.path.join(views, 'page.volt'), 'w', encoding='utf-8') as f:
            f.write(PAGE)
        self.po = os.path.join(self.root, 'ar_SA.po')
        with open(self.po, 'w', encoding='utf-8') as f:
            f.write(CATALOGUE)
        self.summary = os.path.join(self.root, 'summary.md')

    def tearDown(self):
        self.tmp.cleanup()

    def check(self, later):
        env = {k: v for k, v in os.environ.items() if k not in ('ARABIC_LATER', 'GITHUB_STEP_SUMMARY')}
        env['ARABIC_PO'] = self.po
        if later:
            env.update(ARABIC_LATER='1', GITHUB_STEP_SUMMARY=self.summary)
        return subprocess.run([sys.executable, CHECK, self.root], env=env, capture_output=True, text=True)

    def test_without_arabic_later_the_missing_string_fails(self):
        done = self.check(False)
        self.assertEqual(done.returncode, 1)
        self.assertIn('NEEDS ARABIC', done.stdout)
        self.assertIn('A string nobody has translated yet', done.stdout)
        self.assertNotIn('::warning', done.stdout)

    def test_with_arabic_later_it_is_a_warning_and_passes(self):
        done = self.check(True)
        self.assertEqual(done.returncode, 0)
        self.assertIn('::warning title=Arabic to follow::A string nobody has translated yet', done.stdout)
        self.assertNotIn('::warning title=Arabic to follow::Board', done.stdout)
        with open(self.summary, encoding='utf-8') as f:
            self.assertIn('- A string nobody has translated yet\n', f.read())

    def test_with_every_string_translated_both_pass(self):
        with open(self.po, 'a', encoding='utf-8') as f:
            f.write('\nmsgid "A string nobody has translated yet"\nmsgstr "A string, translated"\n')
        for later in (False, True):
            done = self.check(later)
            self.assertEqual(done.returncode, 0)
            self.assertNotIn('::warning', done.stdout)


if __name__ == '__main__':
    unittest.main()
