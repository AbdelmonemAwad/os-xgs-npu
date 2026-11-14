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

# Every association the kernel has, and why one stays there (2026-11-14): a transport-mode one, one
# with an HMAC the engine does not know, and the two above, which the rules allow.
SETKEY_MORE = SETKEY + """198.51.100.2 203.0.113.1
\tesp mode=transport spi=286331153(0x11111111) reqid=3(0x00000003)
\tE: aes-gcm-16  00000000 00000000 00000000 00000000 00000000
\tseq=0x00000000 replay=0 flags=0x00000000 state=mature
\tsadb_seq=2 pid=4242 refcnt=1
203.0.113.1 198.51.100.2
\tesp mode=tunnel spi=572662306(0x22222222) reqid=4(0x00000004)
\tE: rijndael-cbc  00000000 00000000 00000000 00000000
\tA: hmac-md5  00000000 00000000 00000000 00000000
\tseq=0x00000000 replay=0 flags=0x00000000 state=mature
\tsadb_seq=3 pid=4242 refcnt=1
203.0.113.1 198.51.100.2
\tesp mode=tunnel spi=858993459(0x33333333) reqid=5(0x00000005)
\tE: rijndael-cbc  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
\tA: hmac-sha2-256  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
\tseq=0x00000000 replay=0 flags=0x00000000 state=mature
\tsadb_seq=4 pid=4242 refcnt=1
"""
ip.run = lambda argv: SETKEY_MORE if argv[:2] == ['/sbin/setkey', '-D'] else \
    ('\tinet 203.0.113.1 netmask 0xffffff00\n' if argv[0] == '/sbin/ifconfig' else None)
detail = ip.kernel_detail()
check('the kernel view: a GCM key with its salt', (detail['c0bbb22b']['enc'], detail['c0bbb22b']['keylen']),
      ('aes-gcm-16', 20))
check('the kernel view: CBC with its HMAC', (detail['33333333']['enc'], detail['33333333']['auth']),
      ('rijndael-cbc', 'hmac-sha2-256'))
check('the rules allow tunnel-mode GCM', ip.rule_reason(detail['c0bbb22b']), None)
check('the rules allow tunnel-mode CBC with SHA-256', ip.rule_reason(detail['33333333']), None)
check('the rules refuse transport mode', ip.rule_reason(detail['11111111']), ('mode', 'transport'))
check('the rules refuse HMAC MD5', ip.rule_reason(detail['22222222']), ('auth', 'hmac-md5'))

# the XGS 3300: the engine holds one; the others are kernel rows, each with its reason and direction
held = [{'direction': 'decrypt', 'spi': '0xc0bbb22b', 'tunnel': '', 'port': 'oxp1', 'window': '64',
         'handle': '1', 'engine': True, 'note': ''}]
rows = {r['spi']: r for r in ip.kernel_rows(held, detail, True, 'octeontx')}
check('a row for every association the kernel has', sorted(rows),
      ['0x11111111', '0x12345678', '0x22222222', '0x33333333', '0xc0bbb22b'])
check('the held one: in hardware, with its cipher', (rows['0xc0bbb22b']['engine'], rows['0xc0bbb22b']['cipher']),
      (True, 'aes-gcm-16'))
check('transport mode: kernel, and why', (rows['0x11111111']['engine'], rows['0x11111111']['reason']), (False, 'mode'))
check('allowed by the rules and not held: "other"', rows['0x33333333']['reason'], 'other')
check('a kernel row from our address: encrypt', rows['0x33333333']['direction'], 'encrypt')
rows = {r['spi']: r for r in ip.kernel_rows([], detail, False, 'octeontx')}
check('the engine off: said so where the rules allow it', rows['0x33333333']['reason'], 'engine_off')
check('the engine off: a rule still named where one applies', rows['0x22222222']['reason'], 'auth')
rows = {r['spi']: r for r in ip.kernel_rows([], detail, True, 'armada')}
check('the XGS 136: not offered to a front port', rows['0x33333333']['reason'], 'not_offered')

# The XGS 3300's ipsec.table (2026-11-14): an IPv6 association's ends are text, an IPv4 one's two hex
# words, and a port that is gone reads "no interface"; until then only the hex form was read, and an
# IPv6 association the engine held showed as one in the kernel.
TABLE = """  1 handle 2 rev 1  decrypt  spi 0xcdba8404  fd99:136::1 -> fd99:23::1  lif 0x7000  oxp9  drv_spi 12  win 32  after seq 0
  2 handle 3 rev 1  encrypt  spi 0xc8660138  0xc6336402 -> 0xcb007101  lif 0x7000  oxp9  drv_spi 28  win 0  after seq 1048576
      1 connection(s) in the flow table; by flows since install 1500 bytes 1 packets, told to the kernel
  3 handle 4 rev 2  encrypt  spi 0x0000beef  0xc6336402 -> 0xcb007101  lif 0x7000  no interface  drv_spi 29  win 0  after seq 0  (not ready)
"""
rows = ip.table_rows(TABLE)
check('the table: one row per association, the flow line skipped', [r['spi'] for r in rows],
      ['0xcdba8404', '0xc8660138', '0x0000beef'])
check('the table: an IPv6 association read whole', (rows[0]['direction'], rows[0]['tunnel'], rows[0]['port'],
      rows[0]['window'], rows[0]['engine']), ('decrypt', 'fd99:136::1 -> fd99:23::1', 'oxp9', '32', True))
check('the table: an IPv4 association, its hex ends as addresses', rows[1]['tunnel'], '198.51.100.2 -> 203.0.113.1')
check('the table: a port that is gone', rows[2]['port'], 'no interface')

# setkey -D wraps a long key: an AES-256-GCM key and its salt are 36 bytes, eight groups on the first
# line and the ninth on its own; and "hw offl if" names the port whose driver took it. Until
# 2026-11-14 the ninth group was not counted, and the page said "a key of 224 bits".
SETKEY_WRAP = """fd99:23::1 fd99:136::1
\tesp mode=tunnel spi=3362128184(0xc8660138) reqid=1(0x00000001)
\tE: aes-gcm-16  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
\t00000000
\tseq=0x00000000 replay=0 flags=0x00000000 state=mature
\tcurrent: 0(bytes)\thard: 0(bytes)\tsoft: 0(bytes)
\tallocated: 0\thard: 0\tsoft: 0
\thw offl if: oxp9
\tsadb_seq=1 pid=4242 refcnt=1
fd99:136::1 fd99:23::1
\tesp mode=tunnel spi=305419896(0x12345678) reqid=2(0x00000002)
\tE: aes-gcm-16  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
\t00000000
\tseq=0x00000000 replay=0 flags=0x00000000 state=mature
\tsadb_seq=0 pid=4242 refcnt=1
"""
# A link-local end carries its scope (seen on the XGS 3300 after a rekey, 2026-11-14): the block is read
# like any other, the scope left out of the address.
SETKEY_SCOPED = """fd99:23::1 fe80::7e5a:1cff:feb4:ef97%oxp9
\tesp mode=tunnel spi=3487353563(0xcfdccadb) reqid=1(0x00000001)
\tE: aes-gcm-16  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
\t00000000
\tseq=0x00000000 replay=0 flags=0x00000000 state=mature
\tsadb_seq=1 pid=4242 refcnt=1
fe80::7e5a:1cff:feb4:ef97%oxp9[4500] fd99:23::1[4500]
\tesp-udp mode=tunnel spi=3387257334(0xc9e571f6) reqid=1(0x00000001)
\tE: aes-gcm-16  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
\t00000000
\tseq=0x00000000 replay=4 flags=0x00000000 state=mature
\thw offl if: oxp9
\tsadb_seq=0 pid=4242 refcnt=1
"""
ip.run = lambda argv: SETKEY_SCOPED if argv[:2] == ['/sbin/setkey', '-D'] else None
scoped = ip.kernel_detail()
check('a scoped end: both blocks read', sorted(scoped), ['c9e571f6', 'cfdccadb'])
check('a scoped end: the address without its scope', (scoped['cfdccadb']['dst'], scoped['c9e571f6']['src']),
      ('fe80::7e5a:1cff:feb4:ef97', 'fe80::7e5a:1cff:feb4:ef97'))
check('a scoped end with a port: read, and the port taken', scoped['c9e571f6']['hw_if'], 'oxp9')
check('a scoped end: the kernel view of its SPIs', sorted(ip.kernel_sas()), ['c9e571f6', 'cfdccadb'])

ip.run = lambda argv: SETKEY_WRAP if argv[:2] == ['/sbin/setkey', '-D'] else \
    ('\tinet6 fd99:23::1 prefixlen 64\n' if argv[0] == '/sbin/ifconfig' else None)
wrap = ip.kernel_detail()
check('a wrapped key: all 36 bytes counted', wrap['c8660138']['keylen'], 36)
check('a wrapped AES-256-GCM key: the rules allow it', ip.rule_reason(wrap['c8660138']), None)
check('"hw offl if": the port read', wrap['c8660138']['hw_if'], 'oxp9')
check('no "hw offl if": no port', wrap['12345678']['hw_if'], '')
rows = {r['spi']: r for r in ip.kernel_rows([], wrap, True, 'octeontx')}
check('a port took it and the engine list lacks it: said so, with the port',
      (rows['0xc8660138']['reason'], rows['0xc8660138']['reason_word'], rows['0xc8660138']['port']),
      ('port_took', 'oxp9', 'oxp9'))
check('no port named: "other"', rows['0x12345678']['reason'], 'other')

# Both drivers take only an association whose local address is on the port itself
# (octep_ipsec_direction, npuipsec_direction): one whose address is on a loopback is never theirs,
# whatever route its packets take. The XGS 136 in the lab, 2026-11-14: fd99:136::1 on lo0, the route
# by npup8, and the page said "not offered to a front port: the tunnel does not use one".
IFCONFIG = """lo0: flags=1008049<UP,LOOPBACK,RUNNING,MULTICAST,LOWER_UP> metric 0 mtu 16384
\tinet6 fd99:136::1 prefixlen 128
npup8: flags=1008843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST,LOWER_UP> metric 0 mtu 1500
\tinet6 fe80::1%npup8 prefixlen 64 scopeid 0xd
\tinet6 fd99:23::2 prefixlen 64
"""
ip.run =lambda argv: SETKEY_WRAP if argv[:2] == ['/sbin/setkey', '-D'] else \
    (IFCONFIG if argv[0] == '/sbin/ifconfig' else None)
owners = ip.address_owners()
check('ifconfig: each address with its interface, the scope left out',
      (owners.get('fd99:136::1'), owners.get('fd99:23::2'), owners.get('fe80::1')), ('lo0', 'npup8', 'npup8'))
plain = {k: dict(v, hw_if='') for k, v in ip.kernel_detail().items()}
rows = {r['spi']: r for r in ip.kernel_rows([], plain, True, 'armada')}
check('the XGS 136: the local address on a loopback, said so with its interface',
      (rows['0xc8660138']['reason'], rows['0xc8660138']['reason_word'], rows['0xc8660138']['direction']),
      ('not_on_port', 'lo0', 'decrypt'))
IFCONFIG_PORT = IFCONFIG.replace('lo0:', 'npup7:')
ip.run = lambda argv: SETKEY_WRAP if argv[:2] == ['/sbin/setkey', '-D'] else \
    (IFCONFIG_PORT if argv[0] == '/sbin/ifconfig' else None)
rows = {r['spi']: r for r in ip.kernel_rows([], plain, True, 'armada')}
check('the XGS 136: the local address on a front port and not in the list', rows['0xc8660138']['reason'],
      'not_offered')
rows = {r['spi']: r for r in ip.kernel_rows([], plain, False, 'armada')}
check('the engine off comes before the interface', rows['0xc8660138']['reason'], 'engine_off')
check('the counts are not taken for a key line', ip.kernel_counts().get('c8660138'), {'bytes': 0, 'packets': 0})

# the encryption profile, from the swanctl.conf OPNsense writes
check('GCM-16 alone: hardware', ip.proposal_verdict('aes256gcm16-modp2048')[0], 'all')
check('GCM-16 named gcm128: hardware', ip.proposal_verdict('aes128gcm128-ecp256')[0], 'all')
check('CBC with SHA-256 and PFS: hardware', ip.proposal_verdict('aes256-sha256-modp2048')[0], 'all')
check('CBC with MD5: kernel', ip.proposal_verdict('aes128-md5')[0], 'none')
check('ChaCha20: kernel', ip.proposal_verdict('chacha20poly1305-x25519')[0], 'none')
check('GCM-16 or ChaCha20: the peer decides', ip.proposal_verdict('aes256gcm16-chacha20poly1305')[0], 'some')
check('CBC with SHA-256 or MD5: the peer decides', ip.proposal_verdict('aes256-sha256-md5')[0], 'some')
check("strongSwan's default: the peer decides", ip.proposal_verdict('default')[0], 'some')

SWAN = """# generated
connections {
    c1 {
        proposals = aes256-sha256-modp2048
        dpd_delay = 10s
        children {
            k1 {
                esp_proposals = aes256gcm16-modp2048,aes128-md5
                mode = tunnel
            }
            k2 {
                esp_proposals = aes256gcm16
                mode = transport
            }
        }
    }
}
"""
conf = ip.swanctl_blocks(SWAN)
check('swanctl.conf: a child read', conf['connections']['c1']['children']['k1']['mode'], 'tunnel')
import tempfile
with tempfile.NamedTemporaryFile('w', suffix='.conf', delete=False) as f:
    f.write(SWAN)
ip.SWANCTL = f.name
ip.CONFIG = os.path.join(tempfile.gettempdir(), 'no-such-config.xml')
prof = ip.profile()
os.unlink(f.name)
check('the profile: one row per child', [(p['connection'], p['child']) for p in prof], [('c1', 'k1'), ('c1', 'k2')])
check('the profile: each proposal judged', [(e['proposal'], e['verdict']) for e in prof[0]['esp']],
      [('aes256gcm16-modp2048', 'all'), ('aes128-md5', 'none')])
check('the profile: a child of both, the peer decides', prof[0]['verdict'], 'some')
check('the profile: transport mode, kernel whatever its cipher', prof[1]['verdict'], 'none')
check('the profile: IKE proposals and DPD', (prof[0]['ike'], prof[0]['dpd']), ('aes256-sha256-modp2048', '10s'))

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
