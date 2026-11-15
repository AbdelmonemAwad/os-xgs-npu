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
G5). And the inspector's depth a save sets while "Inspect, then hardware" is on: a quarter over
Suricata's stream.reassembly.depth, so Suricata reaches its own depth and bypasses a connection before
the driver puts it in hardware (issue 1148). And "Inspection threads per port" (ids_threads): the netmap
rings it gives each port - Suricata takes a thread a ring - and the drivers' flow id, on with more than one
ring, from 1 to 8, nothing for one or empty. Reading the file through PHP also says the whole of
apply.php parses.

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
if (array_key_exists('ids_threads', $c)) {
    echo json_encode(xgs_ids_rings($c['ids_threads']));
} elseif (array_key_exists('depth', $c)) {
    echo json_encode(xgs_ips_depth($c['now'], $c['depth']));
} elseif (isset($c['rows'])) {
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


def raised(now, depth):
    return php({'depth': depth, 'now': now})


def rings(field):
    return php({'ids_threads': field})


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

check("the rings' row goes when the setting is empty, as one netmap ring a port",
      removed({'a1b2c3d4-0000-4000-8000-000000000007': 'dev.netmap.generic_rings'}, ['dev.netmap.generic_rings']),
      {'a1b2c3d4-0000-4000-8000-000000000007': "dev.netmap.generic_rings removed: one netmap ring a port, FreeBSD's own"})
check("the rings' row stays while the setting names more than one",
      removed({'a1b2c3d4-0000-4000-8000-000000000007': 'dev.netmap.generic_rings'}, ['hw.octep.flow_max']), {})

# Inspection threads per port: the rings and the drivers' flow id.
ONE = {'rings': None, 'flowid': '0'}
check('empty: no row, one ring, no flow id', rings(''), ONE)
check('1: the same as empty', rings('1'), ONE)
check('0, which the page refuses: the same as empty', rings('0'), ONE)
check('2: two rings, the flow id on', rings('2'), {'rings': '2', 'flowid': '1'})
check('4 with spaces around it', rings(' 4 '), {'rings': '4', 'flowid': '1'})
check('8, the most', rings('8'), {'rings': '8', 'flowid': '1'})
check('12, more than the page allows: 8', rings('12'), {'rings': '8', 'flowid': '1'})
check('not a number: the same as empty', rings('four'), ONE)
check('a negative number: the same as empty', rings('-3'), ONE)

# The inspector's depth against Suricata's, as --dump-config prints it.
check("OPNsense's 1mb, the driver's 1 MiB: raised a quarter", raised('1048576', '1mb'), '1310720')
check('already a quarter over: left', raised('1310720', '1mb'), None)
check("the owner's larger depth: left", raised('2097152', '1mb'), None)
check('0, inspected for good: left', raised('0', '1mb'), None)
check('Suricata without a depth: left, it never bypasses', raised('1048576', '0'), None)
check('a depth in kb', raised('100000', '512kb'), '655360')
check('a depth in bytes, rounded up', raised('1', '1000001'), '1250002')
check('a depth in capitals with a space', raised('1048576', '2 MB'), '2621440')
check('a depth this cannot read: left', raised('1048576', 'lots'), None)
check('no depth read (no suricata): left', raised('1048576', None), None)
check('no driver: left', raised(None, '1mb'), None)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
