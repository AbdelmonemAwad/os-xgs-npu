#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/status.py's fastpath_share: the fast path's share of the frames that arrived on the
front ports, from each driver's lif_counted ("in" only) against the frames it handed the host, and the
reason counters each family's driver has - read from a made-up sysctl tree, nothing else.

    python3 tools/test-fastpath-share.py"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs'))
import status  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def tree(values):
    status.sysctl = lambda name: values.get(name)
    status.sysctl_int = lambda name: int(values[name]) if name in values else None


tree({
    'dev.octep.0.dp.lif_counted': 'oxp0  in 300 packets 450000 bytes  out 0 packets 0 bytes\n'
                                  'oxp1  in 0 packets 0 bytes  out 300 packets 450000 bytes\n',
    'dev.octep.0.dp.rx_done': '100',
    'dev.octep.0.dp.netmap_refused': '4',
    'dev.octep.0.dp.flow_excluded': '9',
})
fp = status.fastpath_share('octeontx')
check('XGS 3300: forwarded frames, in only', fp['forwarded_frames'], 300)
check('XGS 3300: the share against the host\'s', fp['share'], 0.75)
check('XGS 3300: only the counters the driver has',
      [(r['key'], r['count'], r['unit']) for r in fp['reasons']],
      [('excluded', 9, 'attempts'), ('netmap', 4, 'connections')])

tree({
    'dev.npuep.0.flow.lif_counted': 'npup1 (Port1)  in 0 packets 0 bytes  out 0 packets 0 bytes\n',
    'dev.npuep.0.giu.rx_packets': '0',
    'dev.npuep.0.flow.ipsec_refused': '2',
})
fp = status.fastpath_share('armada')
check('XGS 136: nothing counted, no share', fp['share'], None)
check('XGS 136: its own counter names', [r['counter'] for r in fp['reasons']], ['flow.ipsec_refused'])

tree({})
fp = status.fastpath_share('armada')
check('no driver: nothing at all', (fp['forwarded_frames'], fp['host_frames'], fp['share'], fp['reasons']),
      (None, None, None, []))

sys.exit(1 if failures else 0)
