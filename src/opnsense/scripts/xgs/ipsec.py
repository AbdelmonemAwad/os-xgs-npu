#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""
Report IPsec in the coprocessor's engine as JSON, for the IPsec page: whether the driver was
started with it (the loader tunable), what it does now, the associations it holds and every
counter of its IPsec subtree - dev.octep.0.ipsec on the XGS 3300, dev.npuep.0.ipsec on the 136 -
and, on the 136, where IPsec's software sessions go.

Read-only: sysctl reads and kenv.
"""

import json
import subprocess
import sys


def run(argv):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout if out.returncode == 0 else None


def main():
    parts = (run(['/bin/sh', '/usr/local/opnsense/scripts/xgs/board.sh']) or '').split()
    fam = parts[1] if len(parts) > 1 else None
    base = 'dev.octep.0.ipsec' if fam == 'octeontx' else 'dev.npuep.0.ipsec'
    tunable = 'hw.octep.ipsec_on' if fam == 'octeontx' else 'hw.npuep.ipsec_on'
    counters = {}
    texts = {}
    tree = run(['/sbin/sysctl', '-e', base]) or ''
    for line in tree.splitlines():
        if '=' not in line:
            # a multi-line text value continues on the next lines; it is read whole below
            continue
        name, _, value = line.partition('=')
        leaf = name[len(base) + 1:]
        if value.strip().lstrip('-').isdigit():
            counters[leaf] = int(value.strip())
    for leaf in ('table', 'sas'):
        v = run(['/sbin/sysctl', '-n', base + '.' + leaf])
        if v:
            texts[leaf] = v.rstrip()
    out = {
        'family': fam,
        'present': bool(tree),
        'tunable': {'name': tunable, 'value': (run(['/bin/kenv', '-q', tunable]) or '').strip() or None},
        'counters': counters,
        'texts': texts,
    }
    if fam == 'octeontx':
        out['flows_tunable'] = (run(['/bin/kenv', '-q', 'hw.octep.ipsec_flows']) or '').strip() or None
    else:
        out['crypto_support'] = (run(['/sbin/sysctl', '-n', 'net.inet.ipsec.crypto_support']) or '').strip() or None
        out['ccp_loaded'] = bool(run(['/sbin/kldstat', '-q', '-n', 'ccp']) is not None)
    print(json.dumps(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
