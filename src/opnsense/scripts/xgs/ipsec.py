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

Each association the kernel has and the engine does not is a row too, with the reason it stays in
the kernel (2026-11-14): the engine off, a protocol, mode, cipher, HMAC or key length the drivers'
rules refuse (octep_ipsec_sa_newkey, npuipsec_alg_of: tunnel-mode ESP with AES-GCM-16, or AES-CBC
with HMAC SHA-1/256/384/512, keys of 128, 192 or 256 bits), a port that took it by the kernel's word
("hw offl if") though the engine's list lacks it, its local address on an interface that is not a
front port (a loopback, a VLAN: both drivers take only an association whose address is on the port),
on the XGS 136 not in the driver's list of offers, or "other" - what the drivers count without
naming per association (a replay window, inbound extended sequence numbers, the coprocessor not
ready).

    profile   the encryption profile as OPNsense configured it, from the swanctl.conf it writes:
              each child's ESP proposals and whether the engine takes them, its mode, the IKE
              proposals and dead peer detection. Shown here; it is set under VPN: IPsec.

Read-only: sysctl reads, kenv, setkey -D, ifconfig, swanctl.conf and config.xml.
"""

import json
import re
import subprocess
import sys

# The XGS 3300's ipsec.table prints an IPv4 association's ends as two hex words and an IPv6 one's as
# text, and "no interface" where the port is gone (octep_ipsec.c); until 2026-11-14 only the hex form
# was read, and an IPv6 association the engine held showed on the page as one in the kernel.
TABLE_LINE = re.compile(r'^\s*\d+\s+handle\s+(\d+)\s+rev\s+\d+\s+(encrypt|decrypt)\s+spi\s+(0x[0-9a-f]+)\s+'
                        r'(0x[0-9a-f]{8}|[0-9a-f:.]+)\s+->\s+(0x[0-9a-f]{8}|[0-9a-f:.]+)\s+lif\s+\S+\s+'
                        r'(.+?)\s+drv_spi\s+\d+\s+win\s+(\d+)', re.I)
SAS_LINE = re.compile(r'^(\S+)\s+spi\s+(0x[0-9a-f]+)\s+(encrypt|decrypt)\s+(inet6?)\s+alg\s+\d+'
                      r'(?:\s+win\s+(\d+))?(\s+esn)?\s+-?\d+s ago\s*(?:\|\s*(.*))?$', re.I)
KERNEL_SPI = re.compile(r'\bspi=\d+\(0x([0-9a-f]+)\)')
# A block's first line, its two addresses; an association in UDP (NAT-T) has each one's port after it
# in brackets, "192.0.2.1[4500] 198.51.100.2[4500]", as setkey prints it (pfkey_sadump). Until
# 2026-11-06 the brackets were not allowed for, and on the XGS 136 such an association had no row.
KERNEL_ENDS = re.compile(r'^([0-9a-fA-F.:]+)(?:\[\d+\])? ([0-9a-fA-F.:]+)(?:\[\d+\])?$')
# an association's counts in setkey -D: what its lifetime runs on, and the part the driver reported
KERNEL_BYTES = re.compile(r'^\s*current: (\d+)\(bytes\)')
KERNEL_ALLOC = re.compile(r'^\s*allocated: (\d+)')
KERNEL_HW = re.compile(r'hw offl allocated: (\d+)\s+hw offl: (\d+)\(bytes\)')
# setkey wraps a long key: the hex groups after the first line come on lines of their own (an
# AES-256-GCM key and its 4-byte salt are 36 bytes - eight groups, then one).
KERNEL_KEYMORE = re.compile(r'^\s+([0-9a-f]+(?:\s+[0-9a-f]+)*)\s*$', re.I)
# The interface the kernel handed the association to for inline offload, when it did.
KERNEL_HWIF = re.compile(r'hw offl if:\s*(\S+)')


def run(argv):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, errors='replace', timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout if out.returncode == 0 else None


def ipv4(word):
    n = int(word, 16)
    return '.'.join(str((n >> s) & 0xff) for s in (24, 16, 8, 0))


def end(word):
    """One end of an association in ipsec.table: an IPv4 address as a hex word, an IPv6 one as text."""
    return ipv4(word) if word.lower().startswith('0x') else word


def table_rows(text):
    """The XGS 3300's ipsec.table: what the engine holds now, one line each."""
    rows = []
    for line in text.splitlines():
        m = TABLE_LINE.match(line)
        if m:
            rows.append({'direction': m.group(2).lower(), 'spi': m.group(3).lower(),
                         'tunnel': end(m.group(4)) + ' -> ' + end(m.group(5)), 'port': m.group(6),
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


KERNEL_HEAD = re.compile(r'^\s*(esp|esp-udp|ah|ipcomp|tcp)\s+mode=(\S+)\s+spi=\d+\(0x([0-9a-f]+)\)', re.I)
KERNEL_ALG = re.compile(r'^\s*([EA]):\s+(\S+)((?:\s+[0-9a-f]+)*)\s*$', re.I)
# setkey's names (libipsec's pfkey_dump) for what both drivers take: AES-GCM with a 16-byte ICV, and
# AES-CBC - printed "rijndael-cbc" for SADB_X_EALG_AES, "aes-cbc" by some builds - with an HMAC of
# SHA-1, SHA-256, SHA-384 or SHA-512 (octep_ipsec_sa_newkey, npuipsec_alg_of).
GCM16 = ('aes-gcm-16',)
CBC = ('rijndael-cbc', 'aes-cbc')
HMACS = ('hmac-sha1', 'hmac-sha2-256', 'hmac-sha2-384', 'hmac-sha2-512', 'hmac-sha256', 'hmac-sha384',
         'hmac-sha512')


def kernel_detail():
    """Every association the kernel has, {spi: {...}} from setkey -D: its two ends, its protocol and
    mode, its cipher and key length - a long key's hex runs on to the next line - its HMAC, and the
    port whose driver took it ("hw offl if"). What the rules below need, and nothing of the keys."""
    found, ends, spi, key = {}, None, None, None
    for line in (run(['/sbin/setkey', '-D']) or '').splitlines():
        m = KERNEL_ENDS.match(line)
        if m:
            ends = (m.group(1), m.group(2))
            spi, key = None, None
            continue
        m = KERNEL_HEAD.match(line)
        if m and ends:
            spi, key = m.group(3).rjust(8, '0'), None
            found[spi] = {'src': ends[0], 'dst': ends[1], 'proto': m.group(1).lower(),
                          'mode': m.group(2).lower(), 'enc': '', 'keylen': 0, 'auth': '', 'hw_if': ''}
            continue
        m = KERNEL_ALG.match(line)
        if m and spi:
            key = m.group(1)
            if key == 'E':
                found[spi]['enc'] = m.group(2).lower()
                found[spi]['keylen'] = len(''.join(m.group(3).split())) // 2
            else:
                found[spi]['auth'] = m.group(2).lower()
            continue
        m = KERNEL_KEYMORE.match(line)
        if m and spi and key:
            if key == 'E':
                found[spi]['keylen'] += len(''.join(m.group(1).split())) // 2
            continue
        key = None
        m = KERNEL_HWIF.search(line)
        if m and spi:
            found[spi]['hw_if'] = m.group(1)
    return found


def rule_reason(sa):
    """Why the engine cannot take this association by the drivers' own rules, as a code and the
    word it is about, or None when the rules allow it - then whatever kept it in the kernel is
    something the drivers count without naming it per association."""
    if sa['proto'] not in ('esp', 'esp-udp'):
        return 'protocol', sa['proto']
    if sa['mode'] != 'tunnel':
        return 'mode', sa['mode']
    if sa['enc'] in GCM16:
        keylen = sa['keylen'] - 4               # the 4-byte salt rides at the end of the key
    elif sa['enc'] in CBC:
        if sa['auth'] not in HMACS:
            return 'auth', sa['auth'] or 'none'
        keylen = sa['keylen']
    else:
        return 'cipher', sa['enc'] or 'none'
    if sa['keylen'] and keylen not in (16, 24, 32):
        return 'keylen', str(keylen * 8)
    return None


def address_owners():
    """{address: interface} for every address this host owns, from ifconfig -a: to tell a
    kernel-only association's direction, and on which interface its local end is."""
    found, ifname = {}, None
    for line in (run(['/sbin/ifconfig', '-a']) or '').splitlines():
        if line and not line[0].isspace():
            ifname = line.split(':', 1)[0]
            continue
        parts = line.split()
        if len(parts) > 1 and parts[0] in ('inet', 'inet6'):
            found[parts[1].split('%')[0]] = ifname or ''
    return found


# the front ports, whose driver takes an association: oxp* on the XGS 3300, npup* on the 136
FRONT = {'octeontx': 'oxp', 'armada': 'npup'}


def kernel_rows(rows, detail, engine_on, family):
    """Add a row for every association the kernel has and the engine does not, each with the reason
    it stays there: the drivers' rules, a port that took it by the kernel's word, the engine off,
    its local address on an interface that is not a front port - both drivers take only an
    association whose address is on the port itself (octep_ipsec_direction, npuipsec_direction) -
    on the XGS 136 not in the driver's list of offers; and give every row its algorithms."""
    ours = address_owners()
    seen = set()
    for r in rows:
        key = r['spi'].lower()[2:].rjust(8, '0')
        seen.add(key)
        sa = detail.get(key)
        if sa:
            r['cipher'] = sa['enc'] + (' / ' + sa['auth'] if sa['auth'] else '')
            r['mode'] = sa['mode']
        if not r['engine'] and not r.get('reason'):
            why = rule_reason(sa) if sa else None
            r['reason'], r['reason_word'] = why if why else ('engine_off' if not engine_on else 'other', '')
    for key, sa in sorted(detail.items()):
        if key in seen:
            continue
        local = sa['src'] if sa['src'] in ours else (sa['dst'] if sa['dst'] in ours else None)
        on = ours.get(local, '') if local else ''
        why = rule_reason(sa)
        if why is None and sa.get('hw_if'):
            # setkey names the port whose driver took it ("hw offl if"), yet the engine's own list,
            # the record of what it holds, does not show it: say both rather than guess
            why = ('port_took', sa['hw_if'])
        elif why is None and not engine_on:
            why = ('engine_off', '')
        elif why is None and on and not on.startswith(FRONT.get(family, '-')):
            why = ('not_on_port', on)
        elif why is None:
            why = ('not_offered', '') if family != 'octeontx' else ('other', '')
        direction = 'encrypt' if sa['src'] in ours else ('decrypt' if sa['dst'] in ours else '')
        rows.append({'direction': direction, 'spi': '0x' + key, 'tunnel': sa['src'] + ' -> ' + sa['dst'],
                     'port': sa.get('hw_if', ''), 'window': '', 'handle': '', 'engine': False, 'note': '',
                     'cipher': sa['enc'] + (' / ' + sa['auth'] if sa['auth'] else ''), 'mode': sa['mode'],
                     'reason': why[0], 'reason_word': why[1]})
    return rows


SWANCTL = '/usr/local/etc/swanctl/swanctl.conf'
CONFIG = '/conf/config.xml'
ENC_GCM16 = re.compile(r'^aes(128|192|256)gcm(16|128)$')
ENC_CBC = re.compile(r'^aes(128|192|256)(cbc)?$')
INTEG_OK = ('sha1', 'sha256', 'sha384', 'sha512', 'sha2_256', 'sha2_384', 'sha2_512')
INTEG_ANY = re.compile(r'^(md5|sha|aesxcbc|aescmac|aes\d*gmac)')
ENC_ANY = re.compile(r'^(aes|3des|des|cast|blowfish|camellia|chacha|null|serpent|twofish)')


def proposal_verdict(text):
    """What the engine makes of one ESP proposal as strongSwan writes it (aes256gcm16-modp2048,
    aes128-sha256-ecp256): 'all' when every cipher it allows is one the engine takes, 'some' when
    the peer's choice decides, 'none' when none is - and the parts it judged by. The Diffie-Hellman
    group (PFS) and esn/noesn do not change it."""
    if text == 'default':
        return 'some', [], []
    encs, integs = [], []
    for word in text.lower().split('-'):
        if ENC_ANY.match(word) and not INTEG_ANY.match(word):
            encs.append(word)
        elif INTEG_ANY.match(word):
            integs.append(word)
    ok = []
    for enc in encs or ['?']:
        if ENC_GCM16.match(enc):
            ok.append(True)
        elif ENC_CBC.match(enc):
            ok.extend(i in INTEG_OK for i in (integs or ['none']))
        else:
            ok.append(False)
    verdict = 'all' if all(ok) else ('some' if any(ok) else 'none')
    return verdict, encs, integs


def swanctl_blocks(text):
    """swanctl.conf as nested dicts: `name { ... }` a section, `key = value` a setting."""
    root, stack = {}, []
    cur = root
    for raw in text.splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if line.endswith('{'):
            node = {}
            cur[line[:-1].strip()] = node
            stack.append(cur)
            cur = node
        elif line == '}':
            cur = stack.pop() if stack else root
        elif '=' in line:
            key, _, value = line.partition('=')
            cur[key.strip()] = value.strip()
    return root


def descriptions():
    """The descriptions OPNsense keeps for its IPsec connections and children, {uuid: text}."""
    found = {}
    try:
        import xml.etree.ElementTree as ET
        tree = ET.parse(CONFIG)
    except Exception:
        return found
    for node in tree.iter():
        if node.tag.lower() != 'swanctl':
            continue
        for item in node.iter():
            uuid = item.get('uuid')
            if uuid:
                d = item.find('description')
                if d is not None and (d.text or '').strip():
                    found[uuid] = d.text.strip()
    return found


def profile():
    """The encryption profile as OPNsense configured it (VPN: IPsec), read from the swanctl.conf it
    writes: each child's ESP proposals with what the engine makes of them, its mode, and the
    connection's IKE proposals and dead peer detection. Shown, never written."""
    try:
        with open(SWANCTL, encoding='utf-8', errors='replace') as f:
            conf = swanctl_blocks(f.read())
    except OSError:
        return None
    names = descriptions()
    out = []
    for cname, conn in sorted((conf.get('connections') or {}).items()):
        if not isinstance(conn, dict):
            continue
        for chname, child in sorted((conn.get('children') or {}).items()):
            if not isinstance(child, dict):
                continue
            mode = child.get('mode', 'tunnel')
            props = []
            for p in [x.strip() for x in child.get('esp_proposals', 'default').split(',') if x.strip()]:
                verdict, _, _ = proposal_verdict(p)
                if mode != 'tunnel':
                    verdict = 'none'
                props.append({'proposal': p, 'verdict': verdict})
            verdicts = {p['verdict'] for p in props}
            out.append({
                'connection': names.get(cname, cname),
                'child': names.get(chname, chname),
                'mode': mode,
                'esp': props,
                'verdict': verdicts.pop() if len(verdicts) == 1 else 'some',
                'ike': conn.get('proposals', 'default'),
                'dpd': conn.get('dpd_delay', ''),
                'rekey': child.get('rekey_time', ''),
            })
    return out


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
        # what the driver says of this one association, when it says why the engine did not take it
        if not rows[-1]['engine']:
            if 'extended sequence numbers inbound' in note:
                rows[-1]['reason'], rows[-1]['reason_word'] = 'esn', ''
            elif 'window refused' in note:
                rows[-1]['reason'], rows[-1]['reason_word'] = 'window', rows[-1]['window']
            elif 'SA_ADD refused' in note:
                rows[-1]['reason'], rows[-1]['reason_word'] = 'engine_refused', ''
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
    rows = kernel_rows(rows, kernel_detail(), out['tunable']['value'] == '1', fam)
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
    out['profile'] = profile()
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
