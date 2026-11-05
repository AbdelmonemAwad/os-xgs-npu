#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Two answers of an owner's report (.github/ISSUE_TEMPLATE/appliance.yml), cleaned for the
compatibility page. Only what matches reaches the page; the answer itself stays on GitHub, behind the
report's link (#1041)."""
import re

BOARD = re.compile(r'\bAMDA\d{4}(?:-\d{4})?\b')
VERSION = re.compile(r'[A-Za-z0-9 .,+_-]{1,100}')


def board(answer):
    """The assembly number in the answer, with its revision when the owner wrote one, or ''. The form
    asks for the Board row of XGS: Overview, which reads "XGS 3100, AMDA0202 (OCTEON TX)"."""
    m = BOARD.search(answer or '')
    return m.group(0) if m else ''


def version(answer):
    """The release and OPNsense's version as the owner wrote them, spaces and tabs folded to one space,
    or '' when the answer holds anything else. OPNsense's own version can carry a package revision,
    as in 26.7.8_2."""
    v = ' '.join((answer or '').split())
    return v if VERSION.fullmatch(v) else ''
