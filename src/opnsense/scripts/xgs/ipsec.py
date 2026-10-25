#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""
Report IPsec in the coprocessor's engine as JSON, for the IPsec page and the Overview: whether the
driver was started with it (the loader tunable), what it does now, the associations it holds and
every counter of its IPsec subtree - dev.octep.0.ipsec on the XGS 3300, dev.npuep.0.ipsec on the 136 -
and, on the 136, where IPsec's software sessions go.

ONE SUMMARY FOR BOTH FAMILIES (2026-10-20). The two drivers print different things: the XGS 3300's
`ipsec.table` lists the associations the engine holds now, the XGS 136's `ipsec.sas` lists every
association the kernel offered to a front port since the driver loaded, the ones it has since
removed included; and their counters have different names. The page read the 3300's for both, so
on the 136 it counted every association since boot as held (14 where the kernel had 2) and showed
0 installed and 0 declined. So the rows and the counts are made here, once, for either family:

    rows      every association as the page's grid shows it; on the 136 only those the kernel
              still has (its SPI in `setkey -D`), each saying whether the engine took it - of the
              last sixteen offers, which is all the 136's driver keeps
    summary   live (encrypt, decrypt): what the engine holds now - on the 136 what it took less
              what the kernel removed, which does not depend on the list's sixteen
              installed: what the engine took since the driver loaded
              declined: what was offered and not taken, and what that count includes
              encrypted, decrypted: frames through the engine either way since the driver loaded

Read-only: sysctl reads, kenv and setkey -D.
"""

import json
import re
import subprocess
import sys

TABLE_LINE = re.compile(r'^\s*\d+\s+handle\s+(\d+)\s+rev\s+\d+\s+(encrypt|decrypt)\s+spi\s+(0x[0-9a-f]+)\s+'
                        r'(0x[0-9a-f]+)\s+->\s+(0x[0-9a-f]+)\s+lif\s+\S+\s+(\S+)\s+drv_spi\s+\d+\s+win\s+(\d+)', re.I)
SAS_LINE = re.compile(r'^(\S+)\s+spi\s+(0x[0-9a-f]+)\s+(encrypt|decrypt)\s+(inet6?)\s+alg\s+\d+'
                      r'(?:\s+win\s+(\d+))?(\s+esn)?\s+-?\d+s ago\s*(?:\|\s*(.*))?$', re.I)
KERNEL_SPI = re.compile(r'\bspi=\d+\(0x([0-9a-f]+)\)')
KERNEL_ENDS = re.compile(r'^([0-9a-fA-F.:]+) ([0-9a-fA-F.:]+)$')
# an association's counts in setkey -D: what its lifetime runs on, and the part the driver reported
KERNEL_BYTES = re.compile(r'^\s*current: (\d+)\(bytes\)')
KERNEL_ALLOC = re.compile(r'^\s*allocated: (\d+)')
KERNEL_HW = re.compile(r'hw offl allocated: (\d+)\s+hw offl: (\d+)\(bytes\)')


def run(argv):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout if out.returncode == 0 else None


def ipv4(word):
    n = int(word, 16)
    return '.'.join(str((n >> s) & 0xff) for s in (24, 16, 8, 0))


def table_rows(text):
    """The XGS 3300's ipsec.table: what the engine holds now, one line each."""
    rows = []
    for line in text.splitlines():
        m = TABLE_LINE.match(line)
        if m:
            rows.append({'direction': m.group(2).lower(), 'spi': m.group(3).lower(),
                         'tunnel': ipv4(m.group(4)) + ' -> ' + ipv4(m.group(5)), 'port': m.group(6),
                         'window': m.group(7), 'handle': m.group(1), 'engine': True, 'note': ''})
    return rows


def kernel_sas():
    """The kernel's associations, {spi: 'source -> destination'}, from setkey -D: each block starts
    with its two addresses and its next line carries the SPI. Nothing else of it is kept."""
    found, ends = {}, None
    for line in (run(['/sbin/setkey', '-D']) or '').splitlines():
        m = KERNEL_ENDS.match(line)
        if m:
            ends = m.group(1) + ' -> ' + m.group(2)
            continue
        m = KERNEL_SPI.search(line)
        if m and ends:
            found[m.group(1).rjust(8, '0')] = ends
            ends = None
    return found


def kernel_counts():
    """Each association's counts as the kernel keeps them, {spi: {...}} from setkey -D: bytes and
    packets - what its byte and packet lifetimes run on - and the part of them the driver reported for
    what the coprocessor carried ("hw offl", #761, #774)."""
    found, spi = {}, None
    for line in (run(['/sbin/setkey', '-D']) or '').splitlines():
        m = KERNEL_SPI.search(line)
        if m:
            spi = m.group(1).rjust(8, '0')
            found[spi] = {}
            continue
        if spi is None:
            continue
        m = KERNEL_BYTES.match(line)
        if m:
            found[spi]['bytes'] = int(m.group(1))
        m = KERNEL_ALLOC.match(line)
        if m:
            found[spi]['packets'] = int(m.group(1))
        m = KERNEL_HW.search(line)
        if m:
            found[spi]['hw_packets'] = int(m.group(1))
            found[spi]['hw_bytes'] = int(m.group(2))
    return found


def sas_rows(text, kernel):
    """The XGS 136's ipsec.sas: every association offered since the driver loaded; only the ones
    the kernel still has are kept, each saying whether the engine took it."""
    rows = []
    for line in text.splitlines():
        m = SAS_LINE.match(line.strip())
        if not m or m.group(2).lower()[2:].rjust(8, '0') not in kernel:
            continue
        note = (m.group(7) or '').strip()
        rows.append({'direction': m.group(3).lower(), 'spi': m.group(2).lower(),
                     'tunnel': kernel[m.group(2).lower()[2:].rjust(8, '0')],
                     'port': m.group(1), 'window': m.group(5) or '', 'handle': '',
                     'engine': 'accepted by the engine' in note,
                     'note': note + (' | extended sequence numbers' if m.group(6) else '')})
    return rows


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
    flows = 'hw.octep.ipsec_flows' if fam == 'octeontx' else 'hw.npuep.ipsec_flows'
    out['flows_tunable'] = (run(['/bin/kenv', '-q', flows]) or '').strip() or None
    # what the driver does now, which a sysctl can have changed since the boot read the tunable
    live = run(['/sbin/sysctl', '-n', 'dev.octep.0.ipsec.flows' if fam == 'octeontx' else
                'dev.npuep.0.flow.ipsec_flows'])
    out['flows_now'] = live.strip() if live else None
    if fam == 'octeontx':
        rows = table_rows(texts.get('table', ''))
        installed = counters.get('sa_installed')
        declined = counters.get('sa_refused')
        # the kernel offers every association to every port that can take one; the ones made to
        # a port the tunnel does not use are refused too, and counted here with the rest
        declined_note = 'other ports included'
    else:
        rows = sas_rows(texts.get('sas', ''), kernel_sas())
        installed = None
        if 'accepted' in counters or 'accepted_encrypt' in counters:
            installed = counters.get('accepted', 0) + counters.get('accepted_encrypt', 0)
        declined = None
        if 'offered' in counters and installed is not None:
            declined = max(0, counters['offered'] - installed)
        declined_note = 'front ports only'
    counts = kernel_counts()
    for r in rows:
        r.update(counts.get(r['spi'].lower()[2:].rjust(8, '0'), {}))
    held = [r for r in rows if r['engine']]
    live_enc = sum(1 for r in held if r['direction'] == 'encrypt')
    live_dec = sum(1 for r in held if r['direction'] == 'decrypt')
    if fam != 'octeontx' and 'accepted' in counters and 'removed' in counters:
        # the 136's list keeps the last sixteen offers, so with many tunnels a live association
        # can have left it; what the engine took and the kernel has not removed is the count
        live_enc = max(0, counters.get('accepted_encrypt', 0) - counters.get('removed_encrypt', 0))
        live_dec = max(0, counters['accepted'] - counters['removed'])
    out['rows'] = rows
    out['summary'] = {
        'live_encrypt': live_enc,
        'live_decrypt': live_dec,
        'installed': installed,
        'declined': declined,
        'declined_note': declined_note,
        # frames handed to the engine to encrypt, and frames it decrypted, by either driver's names
        'encrypted': counters.get('tx_encrypt') if fam == 'octeontx' else
        (counters.get('tx_taken', 0) + counters.get('tx_accel', 0) if 'tx_taken' in counters else None),
        'decrypted': counters.get('rx_done'),
    }
    if fam != 'octeontx':
        out['crypto_support'] = (run(['/sbin/sysctl', '-n', 'net.inet.ipsec.crypto_support']) or '').strip() or None
        out['ccp_loaded'] = bool(run(['/sbin/kldstat', '-q', '-n', 'ccp']) is not None)
    print(json.dumps(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
