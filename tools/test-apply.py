#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/apply.php's "Keep IPsec's software sessions off ccp" (ipsec_soft_crypto, XGS 136), with PHP
and no OPNsense: what net.inet.ipsec.crypto_support is written to now, from what it reads and from a row
of System: Settings: Tunables that names it. On holds the sessions to software (33554432). Off gives back
what a boot leaves - the row's number, or the kernel's own hardware or software (50331648) - where the
setting holds them, and nothing else; until 2026-11-06 off wrote nothing, and the page said applied.
And the rows of System: Settings: Tunables an apply removes: a sized tunable set to 0, and the flow
maker's own two, which OPNsense set at each boot over the page's setting until 2026-11-06 (the review's
G5). Reading the file through PHP also says the whole of apply.php parses.

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
if (isset($c['rows'])) {
    echo json_encode((object)xgs_rows_removed($c['rows'], $c['unset']));
} else {
    echo json_encode(xgs_soft_crypto($c['on'], $c['now'], $c['row']));
}
'''
SOFTWARE, BOTH, HARDWARE = '33554432', '50331648', '16777216'

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def php(case):
    out = subprocess.run(['php', '-r', HARNESS, '--', APPLY, json.dumps(case)], capture_output=True, text=True,
                         errors='replace')
    if out.returncode != 0:
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:200]
    return json.loads(out.stdout)


def written(on, now, row=None):
    return php({'on': on, 'now': now, 'row': row})


def removed(rows, unset=()):
    return php({'rows': rows, 'unset': list(unset)})


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

# The rows an apply removes, by uuid: made-up uuids, the rows a box set by hand and the page's own.
ROWS = {
    'a1b2c3d4-0000-4000-8000-000000000001': 'dev.npuep.0.flow.auto',
    'a1b2c3d4-0000-4000-8000-000000000002': 'hw.npuep.giu.poll_ms',
    'a1b2c3d4-0000-4000-8000-000000000003': 'hw.npuep.flow_max',
    'a1b2c3d4-0000-4000-8000-000000000004': 'dev.octep.0.dp.auto',
    'a1b2c3d4-0000-4000-8000-000000000005': 'net.inet.ipsec.crypto_support',
}
FLOW_MAKER = ' removed from the tunables: this page sets the flow maker, now and at every boot'
check('the flow maker\'s rows go, either family\'s, with a sized one set to 0',
      removed(ROWS, ['hw.npuep.flow_max', 'hw.npuep.giu.txq']),
      {'a1b2c3d4-0000-4000-8000-000000000001': 'dev.npuep.0.flow.auto' + FLOW_MAKER,
       'a1b2c3d4-0000-4000-8000-000000000003': 'hw.npuep.flow_max removed: the driver chooses',
       'a1b2c3d4-0000-4000-8000-000000000004': 'dev.octep.0.dp.auto' + FLOW_MAKER})
check('the flow maker\'s rows go when nothing is set to 0',
      sorted(removed(ROWS)), ['a1b2c3d4-0000-4000-8000-000000000001', 'a1b2c3d4-0000-4000-8000-000000000004'])
check('no row of its own: nothing removed',
      removed({'a1b2c3d4-0000-4000-8000-000000000002': 'hw.npuep.giu.poll_ms'}, ['hw.npuep.flow_max']), {})
check('a row merely like the flow maker\'s stays',
      removed({'a1b2c3d4-0000-4000-8000-000000000006': 'dev.npuep.0.flow.auto_made'}), {})

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
