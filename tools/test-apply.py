#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/apply.php's "Keep IPsec's software sessions off ccp" (ipsec_soft_crypto, XGS 136), with PHP
and no OPNsense: what net.inet.ipsec.crypto_support is written to now, from what it reads and from a row
of System: Settings: Tunables that names it. On holds the sessions to software (33554432). Off gives back
what a boot leaves - the row's number, or the kernel's own hardware or software (50331648) - where the
setting holds them, and nothing else; until 2026-11-06 off wrote nothing, and the page said applied.
Reading the file through PHP also says the whole of apply.php parses.

    python3 tools/test-apply.py"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPLY = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'apply.php')
HARNESS = r'''
putenv('XGS_APPLY_FUNCTIONS_ONLY=1');
include $argv[1];
$c = json_decode($argv[2], true);
echo json_encode(xgs_soft_crypto($c['on'], $c['now'], $c['row']));
'''
SOFTWARE, BOTH, HARDWARE = '33554432', '50331648', '16777216'

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def written(on, now, row=None):
    case = json.dumps({'on': on, 'now': now, 'row': row})
    out = subprocess.run(['php', '-r', HARNESS, '--', APPLY, case], capture_output=True, text=True,
                         errors='replace')
    if out.returncode != 0:
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:200]
    return json.loads(out.stdout)


if shutil.which('php') is None:
    print('FAIL php is not installed, so apply.php cannot be checked')
    sys.exit(1)

check('on, from the kernel\'s own: held to software', written(True, BOTH), SOFTWARE)
check('on, held already: nothing written', written(True, SOFTWARE), None)
check('on, no IPsec module yet: nothing now, 07-npuep at boot', written(True, None), None)
check('off, held by the setting: the kernel\'s own again', written(False, SOFTWARE), BOTH)
check('off, a row of Tunables names a number: the row\'s', written(False, SOFTWARE, HARDWARE), HARDWARE)
check('off, the row holds it to software itself: nothing written', written(False, SOFTWARE, SOFTWARE), None)
check('off, a row whose value is no number: the kernel\'s own', written(False, SOFTWARE, 'default'), BOTH)
check('off, a value the setting did not write: left as it is', written(False, HARDWARE), None)
check('off, no IPsec module: nothing', written(False, None), None)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
