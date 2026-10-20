#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The ports' own counters - errors and drops - for XGS: Ports: the switch's for the panel ports behind
it, and on the XGS 3300 the coprocessor's own MACs for the ports that are not.

    portstats.py collect    ask the coprocessor now, keep the answer with the one before, print it
    portstats.py show       print what was asked last, without asking

WHY THE SWITCH. Port1 to Port8 and two of the cages hang off the 88E6193X on both families, and a
frame the switch takes in with a bad checksum, too short, too long or dropped for want of room is
never handed on: neither the host's interface counters nor the coprocessor's see it. The switch's
RMON counters do, read on the coprocessor by contrib/mvsw ("mvsw 0 2 stats"), which reads every
counter twice because the network agent shares the MDIO bus.

WHICH PORT IS WHICH, from the vendor's platform data (docs/families/armada.md, octeon-tx.md): the
switch's ports 1 to 8 are Port1 to Port8 on both; its 9 and 10 are F1 and F2 on the XGS 136 and F3
and F4 on the XGS 3300. The XGS 136's labels 9 to 12 are not on the switch and have no row here.

THE XGS 3300'S OWN MACS. Its F1 and F2 and the expansion module's ports hang off the coprocessor's
BGX blocks, not the switch, and each LMAC counts its own frames in registers the coprocessor reads
with busy-box devmem - not cleared by a read, 48 bits, as Cavium's public register descriptions give
them (the BDK's bdk-csrs-bgx.h, BGX(a)_CMR(b)_RX_STAT0 to 8 and TX_STAT0 to 17, at
0x87E0E0000000 + a * 0x1000000 + b * 0x100000 on the CN83XX). F1 and F2 are BGX 2's LMACs 0 and 1
(the coprocessor's own enumeration, docs/families/octeon-tx.md); the module's ports are BGX 0's
LMACs in the order of their tags in ports.json, then BGX 1's - measured 2026-10-20 on the second
XGS 3300's 4x10G module (AMDA0207): A4, A3, A2 and A1 are LMACs 0 to 3, each one's frames equal to
what its interface counted, and A3, the one cage with no light, at zero.

NOT ON THE XGS 136 (#639). The switch is in multi-chip mode - every read a command written to one
register and the answer read from another - and the coprocessor's network agent follows the ports'
links on the same bus the same way. When the two interleave, the agent takes our answer for its own
and reports a linked port down; on the XGS 136 it follows links by interrupt, so the wrong state
stays until the link changes again. Measured 2026-10-20: every carrier change the 136's driver
logged between 17:30 and 18:52, apart from a reload and a boot, came in the first two seconds of
this script's cron minutes, and four ports read "no carrier" for forty minutes while their traffic
crossed them. The agent's own counters are no substitute there: its all-ports answer (0x45) gives
the switch ports one identical, meaningless block. So on that family nothing is read and the page
says why. The XGS 3300's link changes over the same hour do not line up with its reads.

WHY KEPT, as health.py: the XGS 3300's coprocessor is reached over its console, one command at a
time for every plugin, so the cron asks every five minutes and the page shows the last answer, with
what changed since the one before it.
"""

import json
import os
import subprocess
import sys
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
BOARD = '/usr/local/opnsense/scripts/xgs/board.sh'
STATE = '/var/db/os-xgs-npu/portstats.json'
COMMAND = '/persistent/os-xgs-npu/mvsw 0 2 stats 1 10'
PORTS_JSON = '/var/db/os-xgs-npu/ports.json'
BGX_BASE = 0x87E0E0000000
# name on the page, register offset (Cavium BDK, bgx#_cmr#_rx_stat# and _tx_stat#)
BGX_COUNTERS = (
    ('in_frames', 0x70),        # RX_STAT0: good frames - not PAUSE, not filtered, not dropped, no error
    ('in_good_octets', 0x78),   # RX_STAT1: their octets
    ('in_pause', 0x80),         # RX_STAT2: PAUSE frames
    ('in_dmac_drop', 0x90),     # RX_STAT4: dropped by the destination-address filter
    ('in_fifo_drop', 0xa0),     # RX_STAT6: dropped for a full receive FIFO
    ('in_errors', 0xb0),        # RX_STAT8: received with an error and not dropped otherwise
    ('out_octets', 0x620),      # TX_STAT4: octets sent, PAUSE frames not counted
    ('out_frames', 0x628),      # TX_STAT5: frames sent, PAUSE frames not counted
    ('out_underflow', 0x680),   # TX_STAT16: frames sent short of data (underflow)
    ('out_pause', 0x688),       # TX_STAT17: PAUSE frames sent
)
CAGES = {'armada': {9: 'F1', 10: 'F2'}, 'octeontx': {9: 'F3', 10: 'F4'}}
# The counters that say something went wrong, as the page lists them first.
ERRORS = ('in_fcs_err', 'in_rx_err', 'in_undersize', 'in_fragments', 'in_oversize', 'in_jabber',
          'in_bad_octets', 'in_discards', 'out_fcs_err', 'collisions', 'late', 'excessive',
          'in_errors', 'in_fifo_drop', 'out_underflow')


def family():
    try:
        out = subprocess.run(['/bin/sh', BOARD], stdin=subprocess.DEVNULL, capture_output=True,
                             text=True, timeout=10).stdout.split()
    except (OSError, subprocess.SubprocessError):
        return None
    return out[1] if len(out) > 1 else None


def label(port, fam):
    if port <= 8:
        return 'Port%d' % port
    return CAGES.get(fam, {}).get(port)


def parse(text, fam):
    """Lines of "port=N name=value ..." -> {label: {name: int or None}}; "?" was never agreed on."""
    ports = {}
    for line in text.replace('\r', '').split('\n'):
        fields = line.split()
        if not fields or not fields[0].startswith('port='):
            continue
        counters = {}
        for field in fields[1:]:
            name, _, value = field.partition('=')
            counters[name] = int(value) if value.isdigit() else None
        try:
            name = label(int(fields[0][5:]), fam)
        except ValueError:
            continue
        if name:
            ports[name] = counters
    return ports


def bgx_ports():
    """[(label, bgx, lmac)] for the XGS 3300's ports on its own MACs: F1, F2, the module's."""
    ports = [('F1', 2, 0), ('F2', 2, 1)]
    try:
        with open(PORTS_JSON, encoding='utf-8') as handle:
            extra = (json.load(handle) or {}).get('extra') or []
    except (OSError, ValueError):
        extra = []
    extra = sorted((e for e in extra if isinstance(e.get('tag'), int) and e.get('name')),
                   key=lambda e: e['tag'])
    for i, e in enumerate(extra[:8]):
        if all(ch.isalnum() for ch in e['name']):
            ports.append((e['name'], i // 4, i % 4))
    return ports


def bgx_command(ports):
    """One shell line for the coprocessor: "bgx=<label> name=value ..." for each port."""
    regs = ' '.join('%s:%d' % (name, offset) for name, offset in BGX_COUNTERS)
    each = ' '.join('%s:%d' % (label, BGX_BASE + bgx * 0x1000000 + lmac * 0x100000)
                    for label, bgx, lmac in ports)
    return ('for p in %s; do b=${p#*:}; printf "bgx=%%s" ${p%%%%:*}; for r in %s; do '
            'printf " %%s=%%d" ${r%%%%:*} $(( $(devmem $(printf 0x%%x $((b + ${r#*:}))) 64) & 0xFFFFFFFFFFFF )); '
            'done; echo; done' % (each, regs))


def parse_bgx(text):
    ports = {}
    for line in text.replace('\r', '').split('\n'):
        fields = line.split()
        if not fields or not fields[0].startswith('bgx='):
            continue
        counters = {}
        for field in fields[1:]:
            name, _, value = field.partition('=')
            counters[name] = int(value) if value.isdigit() else None
        ports[fields[0][4:]] = counters
    return ports


def since(now, before):
    """What each counter moved by between two readings; a counter that went back was cleared."""
    out = {}
    for name, counters in now.items():
        old = before.get(name) or {}
        moved = {}
        for key, value in counters.items():
            if value is None or old.get(key) is None:
                continue
            moved[key] = value - old[key] if value >= old[key] else value
        out[name] = moved
    return out


def show():
    try:
        with open(STATE, encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def collect():
    fam = family()
    answer = {'at': int(time.time()), 'family': fam, 'errors': list(ERRORS)}
    if fam == 'armada':
        # Not one read of the switch from here (#639): what is kept is this, and nothing older.
        answer['error'] = ("not read on the XGS 136: reading the switch over the bus its coprocessor's "
                           "network agent uses makes the agent report linked ports down (#639)")
        answer['read_at'] = answer['at']
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        with open(STATE + '.tmp', 'w', encoding='utf-8') as handle:
            json.dump(answer, handle)
        os.replace(STATE + '.tmp', STATE)
        return answer
    command = COMMAND
    if fam == 'octeontx':
        # The coprocessor's own MACs in the same visit to its console as the switch.
        command += '; ' + bgx_command(bgx_ports())
    try:
        for tries in range(3):
            done = subprocess.run(['/bin/sh', NPU_RUN, '20', command], stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=120)
            # 75: another plugin's command holds the XGS 3300's console; it is one at a time.
            if done.returncode != 75:
                break
            time.sleep(10)
        ports = parse(done.stdout, fam)
        if fam == 'octeontx':
            ports.update(parse_bgx(done.stdout))
        if ports:
            answer['ports'] = ports
        elif 'usage' in done.stdout:
            # The tool on the coprocessor predates "stats"; switch-jumbo.sh replaces it at the next
            # boot with the one this host carries.
            answer['error'] = ('the switch tool on the coprocessor is older than this release; '
                               'it is replaced at the next boot')
        else:
            answer['error'] = (done.stderr.strip() or done.stdout.strip() or
                               'exit %d' % done.returncode)[:200]
    except (OSError, subprocess.SubprocessError) as failure:
        answer['error'] = str(failure)[:200]
    before = show()
    if answer.get('ports') and before.get('ports'):
        answer['before_at'] = before.get('read_at', before.get('at'))
        answer['since'] = since(answer['ports'], before['ports'])
    if 'ports' in answer or 'error' in answer:
        if 'ports' not in answer and before.get('ports'):
            # Keep the last good reading when this one failed; the error says why it is old.
            for key in ('ports', 'since', 'before_at'):
                if key in before:
                    answer[key] = before[key]
            answer['read_at'] = before.get('read_at', before.get('at'))
        else:
            answer['read_at'] = answer['at']
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        with open(STATE + '.tmp', 'w', encoding='utf-8') as handle:
            json.dump(answer, handle)
        os.replace(STATE + '.tmp', STATE)
    return answer


def main(argv):
    command = argv[1] if len(argv) > 1 else 'show'
    if command == 'collect':
        print(json.dumps(collect()))
    elif command == 'show':
        print(json.dumps(show()))
    else:
        print(json.dumps({'error': 'usage: portstats.py collect|show'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
