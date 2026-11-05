#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""website/compat_fields.py on the owners' answers as GitHub wrote them into their reports - the four
reports of issue 1041 - and on answers that must not reach the compatibility page.

    python3 tools/test-compat-fields.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'website'))
import compat_fields                                                     # noqa: E402


class Board(unittest.TestCase):

    def test_the_overview_row_copied_as_the_form_asks(self):         # report 929
        self.assertEqual(compat_fields.board('XGS 3100, AMDA0202 (OCTEON TX)'), 'AMDA0202')

    def test_the_number_alone(self):                                 # report 952
        self.assertEqual(compat_fields.board('AMDA0200'), 'AMDA0200')

    def test_a_revision_is_kept(self):
        self.assertEqual(compat_fields.board('XGS 116, AMDA0208-0001 (ARMADA)'), 'AMDA0208-0001')

    def test_nothing_that_is_not_one(self):                          # reports 729 and 736 gave none
        for answer in ('', None, 'AMDA02', 'XAMDA0202', 'board 0202'):
            self.assertEqual(compat_fields.board(answer), '', answer)


class Version(unittest.TestCase):

    def test_a_pasted_tab(self):                                     # report 929
        self.assertEqual(compat_fields.version('v0.1.0-alpha.10.4,  \t26.7.8'), 'v0.1.0-alpha.10.4, 26.7.8')

    def test_a_package_revision(self):                               # report 952
        self.assertEqual(compat_fields.version('v0.1.0-alpha.10.6, 26.7.8_2'), 'v0.1.0-alpha.10.6, 26.7.8_2')

    def test_the_image_name_as_before(self):                         # reports 729 and 736
        for answer in ('xgs-image-based-on-opnsense-26.7-serial-115200, v0.1.0-alpha.5.6 on opnsense-26.7.7',
                       'xgs-image-based-on-opnsense-26.7-serial-115200.img.xz'):
            self.assertEqual(compat_fields.version(answer), answer)

    def test_nothing_else_reaches_the_page(self):
        for answer in ('<script>x</script>', 'v1 | v2', '[link](https://example.com)', 'x' * 101, '', None):
            self.assertEqual(compat_fields.version(answer), '', answer)


if __name__ == '__main__':
    unittest.main()
