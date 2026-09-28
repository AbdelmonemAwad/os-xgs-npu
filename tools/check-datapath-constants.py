#!/usr/bin/env python3
"""Check that the datapath constants in the header and the documents still agree.

WHY THIS EXISTS. The OCTEON TX transmit path carries four numbers that are not independent:
the private header length, the instruction front size, the PKI skip length and the checksum
offset. Two of them are derived from the other two.

They were wrong for weeks because TOTAL_TAG_LEN had been assumed to be 0 when it is 66, and
nothing compared the header against the pages that quote it. Being wrong that way is silent:
frames post, counters rise, and the far side discards everything.

This does not know the right answer - no runner has an appliance, and the vendor source is not
in this repository. What it knows is that the header must be internally consistent and that the
documents must quote what the header computes. Drift between them is the failure mode it exists
to catch.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / 'contrib' / 'octep' / 'octep.h'
DOCS = [
    ROOT / 'docs' / 'families' / 'octeon-tx-reference.md',
    ROOT / 'docs' / 'families' / 'octeon-tx.md',
    ROOT / 'docs' / 'netagent.md',
]

# name -> the #define to read it from
SIMPLE = {
    'PPORT_HLEN': r'#define\s+OCTEP_PPORT_HLEN\s+(\d+)',
    'CUSTOM_META_LEN': r'#define\s+OCTEP_CUSTOM_META_LEN\s+(\d+)',
    'INSTR_FSZ': r'#define\s+OCTEP_INSTR_FSZ\s+(\d+)',
}


def fail(msg):
    print('FAIL: ' + msg)
    return 1


def main():
    if not HEADER.exists():
        print('skip: %s is not present' % HEADER.relative_to(ROOT))
        return 0

    text = HEADER.read_text(encoding='utf-8', errors='replace')
    errors = 0
    v = {}

    for name, pattern in SIMPLE.items():
        m = re.search(pattern, text)
        if not m:
            errors += fail('%s is not defined in octep.h' % name)
        else:
            v[name] = int(m.group(1))

    if len(v) != len(SIMPLE):
        return 1

    # The two derived ones must be written as expressions, not as numbers. A magic number here
    # is exactly how the old values survived a change to the thing they depend on.
    for name, must_contain in (
        ('OCTEP_TOTAL_TAG_LEN', 'OCTEP_PPORT_HLEN'),
        ('OCTEP_INSTR_SL', 'OCTEP_TOTAL_TAG_LEN'),
        ('OCTEP_IRH_CKSUM_OFF', 'OCTEP_TOTAL_TAG_LEN'),
    ):
        m = re.search(r'#define\s+' + name + r'\s+(.+)', text)
        if not m:
            errors += fail('%s is not defined in octep.h' % name)
        elif must_contain not in m.group(1):
            errors += fail('%s is written as a literal. It is derived from %s and has to say so, '
                           'or it will not follow when that changes.' % (name, must_contain))

    total = v['PPORT_HLEN'] + v['CUSTOM_META_LEN']
    expected = {
        'the private header length': total,
        'the PKI skip length': v['INSTR_FSZ'] + total,
        'the checksum offset': total + 14 + 1,
    }

    # Every document that quotes one of these has to quote the computed value.
    quoted = {
        'the PKI skip length': re.compile(r'`?(?:pki_ih3\.)?sl`?\s*(?:is|=|\|)\s*\*{0,2}(\d+)'),
        'the checksum offset': re.compile(r'checksum offset[^0-9\n]{0,40}\*{0,2}(\d+)'),
    }

    for doc in DOCS:
        if not doc.exists():
            continue
        body = doc.read_text(encoding='utf-8', errors='replace')
        for lineno, line in enumerate(body.splitlines(), 1):
            for label, pattern in quoted.items():
                want = expected[label]
                # A line that also carries the right value is a correction - 'was 28, is 94' -
                # and those are worth keeping in the log, so they are not failures.
                if str(want) in line:
                    continue
                for found in {int(x) for x in pattern.findall(line)}:
                    if found != want:
                        errors += fail('%s:%d quotes %s as %d; the header computes %d'
                                       % (doc.relative_to(ROOT), lineno, label, found, want))

    if errors:
        print('\n%d problem(s). The header is the source of truth; fix whichever side is stale.'
              % errors)
        return 1

    print('datapath constants agree: tag %d, fsz %d, sl %d, checksum offset %d'
          % (total, v['INSTR_FSZ'], expected['the PKI skip length'], expected['the checksum offset']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
