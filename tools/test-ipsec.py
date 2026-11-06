#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/ipsec.py - the XGS 136's rows on the IPsec page, from the driver's ipsec.sas and the
kernel's associations as `setkey -D` prints them. An association in UDP (NAT-T) has its ports in
brackets after its two addresses, and is a row like any other (the review of 2026-11-06, D9): until
then the brackets were not allowed for, and its row was left out. The text is made up; nothing here
comes from an appliance.

    python3 tools/test-ipsec.py
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'ipsec', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'ipsec.py'))
ip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ip)

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


# setkey -D as FreeBSD's libipsec prints it (pfkey_sadump): a plain association, and one in UDP whose
# addresses each carry the port it is sent from and to.
SETKEY = """198.51.100.2 203.0.113.1
\tesp mode=tunnel spi=3233584171(0xc0bbb22b) reqid=1(0x00000001)
\tE: aes-gcm-16  00000000 00000000 00000000 00000000 00000000
\tseq=0x00000000 replay=64 flags=0x00000000 state=mature
\tcreated: Nov  6 05:30:01 2026\tcurrent: Nov  6 08:30:01 2026
\tdiff: 10800(s)\thard: 28800(s)\tsoft: 25920(s)
\tlast: Nov  6 08:30:00 2026\thard: 0(s)\tsoft: 0(s)
\tcurrent: 9000(bytes)\thard: 0(bytes)\tsoft: 0(bytes)
\tallocated: 90\thard: 0\tsoft: 0
\tsadb_seq=1 pid=4242 refcnt=1
203.0.113.1[4500] 192.0.2.7[4500]
\tesp-udp mode=tunnel spi=305419896(0x12345678) reqid=2(0x00000002)
\tE: aes-gcm-16  00000000 00000000 00000000 00000000 00000000
\tseq=0x00000032 replay=0 flags=0x00000000 state=mature
\tcreated: Nov  6 05:30:02 2026\tcurrent: Nov  6 08:30:01 2026
\tdiff: 10799(s)\thard: 28800(s)\tsoft: 25920(s)
\tlast: Nov  6 08:30:00 2026\thard: 0(s)\tsoft: 0(s)
\tcurrent: 5000(bytes)\thard: 0(bytes)\tsoft: 0(bytes)
\tallocated: 50\thard: 0\tsoft: 0
\tsadb_seq=0 pid=4242 refcnt=1
"""

# ipsec.sas as the XGS 136's driver prints it: its three summary lines, then each association offered
# to a front port; a third, removed from the kernel since, is not a row.
SAS = """offered 3 (encrypt 2 decrypt 1) | probe on (ok 3 fail 0) | accept_decrypt on (accepted 1 fail 0 removed 0)
receive: done 90 (IPv6 inside 0) nosa 0 nokey 0 bad 0 noenc 0 blocked 0 queuefail 0 | last word 0x00000000
transmit: accept_encrypt on (accepted 2 fail 0 removed 1) | forwarded 50 generated 0 orig 0 bypass 0
oxp1   spi 0xc0bbb22b decrypt inet alg 20 win 64 12s ago | SA_ADD accepted by the engine
oxp1   spi 0x12345678 encrypt inet alg 20 9s ago | SA_ADD accepted by the engine
oxp1   spi 0x0badcafe encrypt inet alg 20 900s ago | SA_ADD accepted by the engine
"""

ip.run = lambda argv: SETKEY if argv[:2] == ['/sbin/setkey', '-D'] else None

kernel = ip.kernel_sas()
check('a plain association: its two addresses', kernel.get('c0bbb22b'), '198.51.100.2 -> 203.0.113.1')
check('an association in UDP: its two addresses, the ports left out', kernel.get('12345678'),
      '203.0.113.1 -> 192.0.2.7')
check('every association the kernel has, and nothing else', sorted(kernel), ['12345678', 'c0bbb22b'])

rows = ip.sas_rows(SAS, kernel)
check('a row for each association the kernel still has', [r['spi'] for r in rows], ['0xc0bbb22b', '0x12345678'])
check("the one in UDP: its direction, tunnel and the engine's answer",
      [(r['direction'], r['tunnel'], r['engine']) for r in rows if r['spi'] == '0x12345678'],
      [('encrypt', '203.0.113.1 -> 192.0.2.7', True)])

counts = ip.kernel_counts()
check("the counts the kernel keeps for the one in UDP", counts.get('12345678'), {'bytes': 5000, 'packets': 50})

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
