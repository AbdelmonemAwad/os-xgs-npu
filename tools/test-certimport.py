#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""XGS: Certificate Import's reading of a certificate file (scripts/xgs/certimport.inc), with PHP and the
openssl command and no OPNsense: an authority and a certificate it signed, made here, written as DER, as
PKCS#7 in DER and in PEM, as PKCS#12 with a password - in today's encryption and in the old one OpenSSL 3
reads only with its legacy provider - and as PEM with the key. Each is read into PEM: the certificate the
key belongs to first, with the key, then the authority; a wrong password, an empty file and one that is no
certificate are named, not guessed at.

    python3 tools/test-certimport.py"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'certimport.inc')
HARNESS = r'''
include $argv[1];
$r = xgs_cert_convert(file_get_contents($argv[2]), $argv[3]);
if (isset($r['certs'])) {
    foreach ($r['certs'] as &$c) { unset($c['pem']); }
    $r['key'] = $r['key'] !== null;
}
echo json_encode($r);
'''

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def openssl(*args):
    subprocess.run(['openssl'] + list(args), check=True, capture_output=True)


def read(path, password=''):
    out = subprocess.run(['php', '-r', HARNESS, '--', LIB, path, password], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.startswith('{'):
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:300]
    return json.loads(out.stdout)


def shape(r):
    if not isinstance(r, dict) or r.get('status') != 'ok':
        return r
    return [r['kind'], [(c['subject'], c['ca'], c['key']) for c in r['certs']], r['key']]


for tool in ('php', 'openssl'):
    if shutil.which(tool) is None:
        print('FAIL %s is not installed, so the certificate import cannot be checked' % tool)
        sys.exit(1)

with tempfile.TemporaryDirectory() as d:
    p = lambda name: os.path.join(d, name)
    openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', p('ca.key'), '-out', p('ca.pem'),
            '-days', '30', '-subj', '/CN=Lab CA', '-addext', 'basicConstraints=critical,CA:TRUE')
    openssl('req', '-newkey', 'rsa:2048', '-nodes', '-keyout', p('leaf.key'), '-out', p('leaf.csr'),
            '-subj', '/CN=lab.example.net')
    with open(p('ext'), 'w') as f:
        f.write('basicConstraints=CA:FALSE\n')
    openssl('x509', '-req', '-in', p('leaf.csr'), '-CA', p('ca.pem'), '-CAkey', p('ca.key'), '-CAcreateserial',
            '-out', p('leaf.pem'), '-days', '30', '-extfile', p('ext'))
    openssl('x509', '-in', p('leaf.pem'), '-outform', 'DER', '-out', p('leaf.der'))
    openssl('crl2pkcs7', '-nocrl', '-certfile', p('leaf.pem'), '-certfile', p('ca.pem'), '-outform', 'DER',
            '-out', p('both.p7b'))
    openssl('crl2pkcs7', '-nocrl', '-certfile', p('leaf.pem'), '-certfile', p('ca.pem'), '-out', p('both.p7b.pem'))
    openssl('pkcs12', '-export', '-inkey', p('leaf.key'), '-in', p('leaf.pem'), '-certfile', p('ca.pem'),
            '-passout', 'pass:test-pass', '-out', p('leaf.pfx'))
    with open(p('bundle.pem'), 'w') as f:
        f.write(open(p('ca.pem')).read() + open(p('leaf.pem')).read() + open(p('leaf.key')).read())
    with open(p('empty'), 'w') as f:
        f.write('')
    with open(p('junk'), 'wb') as f:
        f.write(b'\x30\x03\x02\x01\x07 not a certificate at all')

    LEAF, CA = ('lab.example.net', False, False), ('Lab CA', True, False)
    LEAF_KEY = ('lab.example.net', False, True)
    check('DER: the one certificate', shape(read(p('leaf.der'))), ['der', [LEAF], False])
    check('PKCS#7 in DER: the certificate, then the authority', shape(read(p('both.p7b'))), ['p7b', [LEAF, CA], False])
    check('PKCS#7 in PEM: the same', shape(read(p('both.p7b.pem'))), ['p7b', [LEAF, CA], False])
    check('PKCS#12: the certificate with its key first, then the authority', shape(read(p('leaf.pfx'), 'test-pass')),
          ['pfx', [LEAF_KEY, CA], True])
    got = read(p('leaf.pfx'), 'wrong')
    check('PKCS#12 with a wrong password: named', [got.get('status'), 'password' in got.get('message', '')]
          if isinstance(got, dict) else got, ['error', True])
    check('PEM with the authority, the certificate and its key: the certificate first',
          shape(read(p('bundle.pem'))), ['pem', [LEAF_KEY, CA], True])
    check('an empty file: named', read(p('empty')), {'status': 'error', 'message': 'the file is empty'})
    check('a file that is no certificate: named', read(p('junk')),
          {'status': 'error', 'message': 'no certificate was found in the file'})
    # The old encryption (RC2 and 3DES), as older systems export: PHP's openssl does not open it under
    # OpenSSL 3, and the openssl command with -legacy does - where this machine's openssl has that provider.
    legacy = subprocess.run(['openssl', 'pkcs12', '-export', '-legacy', '-inkey', p('leaf.key'), '-in', p('leaf.pem'),
                             '-certfile', p('ca.pem'), '-passout', 'pass:test-pass', '-out', p('old.pfx')],
                            capture_output=True)
    if legacy.returncode == 0:
        check('PKCS#12 in the old encryption: read through the openssl command',
              shape(read(p('old.pfx'), 'test-pass')), ['pfx', [LEAF_KEY, CA], True])
    else:
        print('skip PKCS#12 in the old encryption: this openssl cannot write it')

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
