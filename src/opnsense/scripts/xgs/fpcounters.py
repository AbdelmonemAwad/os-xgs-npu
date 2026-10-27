#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The coprocessor's fast-path counters, named, for the Datapath counters tab of XGS: Overview.

    fpcounters.py show     every counter that is not zero, with its name, as JSON
    fpcounters.py clear    zero them on the coprocessor first, then the same

The counters are the fast path's own - what it forwarded, dropped and why - kept on the coprocessor
and read with one control-message command (44, LO_WORKER_SYS_CNT_READ), not the host driver's
counters the tab's grid shows. The tools that read them, and the lists that name them for each vendor
version, are the ones an owner is asked to run when something looks wrong: tools/npuep-fpcnt.sh on
the ARMADA family, tools/octep-fpcnt.sh on the OCTEON TX family, in the source tree the package
keeps. Clearing them is discovery 13: both drivers send the clear (counters_clear), the workers take
it off their ring within a second. Nothing else is changed by it.
"""
import json
import re
import subprocess
import sys
import time

BOARD = '/usr/local/opnsense/scripts/xgs/board.sh'
TOOLS = '/usr/local/share/os-xgs-npu/source/tools'
TOOL = {'armada': 'npuep-fpcnt.sh', 'octeontx': 'octep-fpcnt.sh'}
GEN = {'armada': 'dev.npuep.0.rpc_channel.sfos', 'octeontx': 'dev.octep.0.rpc.sfos'}
LINE = re.compile(r'^\s*(\d+)\s+(?:(\S+)\s+)?(\d+)\s*$')


def run(argv, timeout=60):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return None, ''
    return out.returncode, out.stdout


def family():
    _, out = run(['/bin/sh', BOARD], timeout=10)
    words = out.split()
    return words[1] if len(words) > 1 else ''


def read(clear=False):
    fam = family()
    answer = {'at': int(time.time()), 'family': fam, 'cleared': bool(clear), 'counters': []}
    if fam not in TOOL:
        answer['error'] = 'not an XGS appliance this project knows'
        return answer
    _, gen = run(['/sbin/sysctl', '-n', GEN[fam]], timeout=10)
    answer['generation'] = gen.strip() or None
    rc, out = run(['/bin/sh', '%s/%s' % (TOOLS, TOOL[fam])] + (['-z'] if clear else []), timeout=90)
    if rc is None or rc != 0:
        answer['error'] = 'the coprocessor did not answer'
        return answer
    for line in out.splitlines():
        m = LINE.match(line)
        if m:
            answer['counters'].append({'index': int(m.group(1)), 'name': m.group(2) or '',
                                       'value': int(m.group(3))})
    return answer


def main(argv):
    what = argv[1] if len(argv) > 1 else 'show'
    if what not in ('show', 'clear'):
        print(json.dumps({'error': 'usage: fpcounters.py show|clear'}))
        return 1
    print(json.dumps(read(clear=(what == 'clear'))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
