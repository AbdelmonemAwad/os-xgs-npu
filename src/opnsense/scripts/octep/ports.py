#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The coprocessor's ports beyond the panel's twelve - an expansion module's - and how to bind each.

    ports.py --count            how many ports there are beyond the panel's twelve, and nothing else -
                                so the boot reads the console only when there is something to name
    ports.py [platform file]    one line per extra port, for bringup.sh:
                                    <tag> <lifport> <name> <mac> <place>
                                and /var/db/os-xgs-npu/ports.json with everything known about each

THREE SOURCES, JOINED BY ADDRESS. NetAgent's discover (the driver's nwa.ports) lists every port the
coprocessor has, with the tag the host must use. NetAgent's MAC attribute gives each tag's address.
The coprocessor's platform database (npu-platform.sh) gives, for each of its ethN, the address, the
logical-interface number (lifport), the speed, and the name printed on the metal. The address is the
one thing both sides carry, so it is the join: measured on an XGS 3300 with a 4x10G module, tags 3
to 6 are A4 to A1, at lifports 12 to 15.

Nothing here knows a module by its part number, so a module nobody has seen yet binds the same way.

WITHOUT THE PLATFORM DATABASE (the console did not answer), a port still binds: its lifport follows
the board's two cages - the direct ports in tag order take the numbers after the switch's ten, as
they do on the XGS 3300 - and its name is "Ext" and the tag, which says plainly that it is not the
name on the metal.

THE PLACE is the port's among every port reported beyond the panel's, in tag order, bound or not:
bringup.sh names its interface with the unit after the panel's at that place, so a port that does
not bind leaves its unit empty and the ports after it keep theirs. Until 2026-11-06 the interfaces
took units in the order they were made, and one port skipped here moved every port after it to the
name of the one before it - and so, since OPNsense assigns an interface by its name, to that port's
firewall rules and addresses (the review's O3). A port NetAgent does not report at all has no place
to keep; when the platform database names more ports in the bays than NetAgent reported, that is said.
"""
import json
import os
import re
import subprocess
import sys

S = 'dev.octep.0'
COUNT = len(sys.argv) > 1 and sys.argv[1] == '--count'
PLATFORM = sys.argv[1] if len(sys.argv) > 1 and not COUNT else '/var/db/os-xgs-npu/npu-platform.txt'
JSON_OUT = '/var/db/os-xgs-npu/ports.json'
# The panel's twelve, which bringup.sh binds by itself: the two cages and the ten behind the switch.
KNOWN = {1, 2} | {0x8000 | (p << 8) for p in range(1, 11)}
SWITCH_PORTS = 10
# What is taken from the platform database: a name as the vendor's board files write every one -
# letters and digits, "F2", "A4" - no longer than dp.if_label holds, and a lifport that is a number.
# Each goes into a line bringup.sh splits at its spaces, and the database is text read back from the
# coprocessor - over its console when its ssh does not answer - and taken here as it came. Until
# 2026-11-06 a value was taken whole: a name with a space put its second word where bringup.sh reads
# the port's address, which the driver refused, so the port's logical interface was installed with
# the address the port before it had left staged; and a lifport that was not a number ended this
# script, every port beyond the panel's with it.
PLAIN_NAME = re.compile(r'[A-Za-z0-9]{1,15}')
NUMBER = re.compile(r'[0-9]{1,4}')


def sysctl(*args):
    return subprocess.run(['/sbin/sysctl'] + list(args), capture_output=True, text=True, errors='replace').stdout


def discover():
    """(tag, flags) for every port the coprocessor reports."""
    sysctl('%s.nwa.discover=1' % S)
    ports = []
    for line in sysctl('-n', '%s.nwa.ports' % S).splitlines():
        m = re.match(r'\s*\d+\s+0x([0-9a-f]{4})\s+\S+\s+0x([0-9a-f]{4})\s', line)
        if m:
            ports.append((int(m.group(1), 16), int(m.group(2), 16)))
    return ports


def mac_of(tag):
    """The port's own address, from NetAgent; None when it would not say."""
    for _ in range(3):
        sysctl('%s.nwa.op=4' % S, '%s.nwa.sub=3' % S, '%s.nwa.port=%d' % (S, tag),
               '%s.nwa.param=0' % S, '%s.nwa.param2=0' % S)
        sysctl('%s.nwa.request=1' % S)
        last = sysctl('-n', '%s.nwa.last' % S)
        words = re.findall(r'\[\s*\d+\]\s+0x([0-9a-f]{8})', last)
        if 'status 0x00000000' in last and len(words) >= 2:
            b = int(words[0], 16).to_bytes(4, 'little') + int(words[1], 16).to_bytes(4, 'little')[:2]
            return ':'.join('%02x' % x for x in b)
    return None


def platform():
    """address -> what the coprocessor's board file says about that port."""
    try:
        text = open(PLATFORM, encoding='utf-8', errors='replace').read()
    except OSError:
        return {}, {}
    keys = {k: v.strip() for k, v in re.findall(r'^(npu0\.[^=\s]+)=(.*)$', text, re.M)}
    # phyN names the socket and points at its ethN; ethN carries the address and the lifport.
    label_of_eth = {}
    for key, value in keys.items():
        m = re.match(r'npu0\.phy(\d+)\.eth$', key)
        if m and keys.get('npu0.phy%s.label' % m.group(1)):
            label_of_eth[value] = keys['npu0.phy%s.label' % m.group(1)]
    by_mac = {}
    for key, value in keys.items():
        m = re.match(r'npu0\.eth(\d+)\.mac$', key)
        if m:
            eth = m.group(1)
            by_mac[value.lower()] = {
                'eth': 'eth' + eth,
                'lifport': keys.get('npu0.eth%s.lifport' % eth),
                'speed': keys.get('npu0.eth%s.init_speed' % eth),
                'label': label_of_eth.get(eth),
            }
    slots = {}
    for key, value in keys.items():
        m = re.match(r'npu0\.slot([A-Z])\.(present|assembly\.partno|assembly\.revision|eth\.macs)$', key)
        if m:
            slots.setdefault(m.group(1), {})[m.group(2)] = value
    return by_mac, slots


def main():
    reported = discover()
    if COUNT:
        print(len([t for t, f in reported if f & 1 and t != 0 and t not in KNOWN]))
        return 0
    by_mac, slots = platform()
    direct = sorted(t for t, f in reported if f & 1 and t != 0 and t < 0x8000)
    extra = sorted({t for t, f in reported if f & 1 and t != 0 and t not in KNOWN})
    # Each one's place, counted over every port reported, bound or not: THE PLACE above.
    place = {tag: n for n, tag in enumerate(extra)}
    # The ports the database names in a bay: a letter that is not the panel's F or P, as for 'slot' below.
    bays = [e for e in by_mac.values()
            if e.get('label') and e['label'][0].isalpha() and e['label'][0] not in 'FP']
    if len(bays) > len(extra):
        print("ports: the coprocessor's platform database names %d ports in its bays and NetAgent reported "
              "%d beyond the panel's; one it did not report has no place to keep, and the ports after it "
              "in tag order each take the unit the one before them would have had"
              % (len(bays), len(extra)), file=sys.stderr)
    lines, record = [], []
    for tag in extra:
        mac = mac_of(tag)
        if mac is None:
            print('ports: tag %d gave no address; not bound, and its place, %d, left empty so the ports '
                  'after it keep their units' % (tag, place[tag]), file=sys.stderr)
            continue
        info = by_mac.get(mac, {})
        lifport = info.get('lifport')
        name = info.get('label')
        if name is not None and not PLAIN_NAME.fullmatch(name):
            print('ports: tag %d: the platform database names it %r, which is not a name as the board '
                  'files write them' % (tag, name), file=sys.stderr)
            name = None
        if lifport is not None and not NUMBER.fullmatch(lifport):
            print('ports: tag %d: the platform database gives it the logical interface %r, which is not '
                  'a number' % (tag, lifport), file=sys.stderr)
            lifport = None
        source = 'platform'
        if lifport is None or name is None:
            # The rule seen on the XGS 3300: direct ports in tag order, after the switch's ten.
            lifport = str(SWITCH_PORTS + direct.index(tag)) if tag in direct else None
            name = 'Ext%d' % tag
            source = 'derived'
        if lifport is None:
            print('ports: tag %d has no logical interface to bind; not bound, and its place, %d, left '
                  'empty so the ports after it keep their units' % (tag, place[tag]), file=sys.stderr)
            continue
        slot = slots.get(name[0], {}) if name and name[0].isalpha() and name[0] not in 'FP' else {}
        record.append({'tag': tag, 'name': name, 'lifport': int(lifport), 'mac': mac, 'place': place[tag],
                       'speed': info.get('speed'), 'eth': info.get('eth'), 'source': source,
                       'slot': name[0] if slot else None,
                       'module': slot.get('assembly.partno'), 'module_rev': slot.get('assembly.revision')})
        lines.append('%d %s %s %s %d' % (tag, lifport, name, mac, place[tag]))
    os.makedirs(os.path.dirname(JSON_OUT), exist_ok=True)
    with open(JSON_OUT + '.new', 'w', encoding='utf-8') as handle:
        json.dump({'reported': len(reported), 'extra': record,
                   'slots': {k: v for k, v in slots.items() if v.get('present') == '1'}}, handle, indent=1)
    os.replace(JSON_OUT + '.new', JSON_OUT)
    for line in lines:
        print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
