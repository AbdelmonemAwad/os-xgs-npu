#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""install/verify.sh on a made-up XGS 3300, its commands stood in for and its paths moved under a
temporary directory: the summary blames the front ports only when the chain to them fails, and a
failure of anything else - the CPU temperature - is called what it is (issue 957, where a 3100 with
twelve working ports was told it had none).

    python3 tools/test-verify.py"""
import os
import stat
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFY = os.path.join(ROOT, 'install', 'verify.sh')
KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'

# The stand-ins read what the case wants from the environment.
STUBS = {
    'uname': 'case "$1" in -v) echo "${FAKE_UNAME_V}" ;; *) echo "FreeBSD 15.1-RELEASE-p6" ;; esac',
    'opnsense-version': 'echo "OPNsense 26.7.8_1 (amd64)"',
    'kenv': 'exit 0',
    'dmesg': 'exit 0',
    'pciconf': 'echo "none0@pci0:3:0:0: class=0x0b4000 rev=0x00 hdr=0x00 vendor=0x177d device=0xa300"',
    'kldstat': '[ "$3" = octep ] && [ "${FAKE_OCTEP_LOADED}" = 1 ] && exit 0; exit 1',
    'ifconfig': ('[ "$1" = -l ] || exit 1; ports=""; [ "${FAKE_PORTS}" = 1 ] && '
                 'ports="oxp0 oxp1 oxp2 oxp3 oxp4 oxp5 oxp6 oxp7 oxp8 oxp9 oxp10 oxp11"; '
                 'echo "${ports} igb0 lo0"'),
    'sysctl': ('[ "$1" = -n ] && [ "$2" = dev.cpu.0.temperature ] && [ "${FAKE_TEMP}" = 1 ] '
               '&& { echo 46.0C; exit 0; }; exit 1'),
    'php': 'echo amdtemp',
    'python3': 'case "$*" in *models.py*) echo "AMDA0215-0003 rev XGS_3300" ;; esac; exit 0',
}

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def run(tmp, **env):
    stubs, root = os.path.join(tmp, 'bin'), os.path.join(tmp, 'root')
    with open(VERIFY, encoding='utf-8') as f:
        text = f.read()
    for path, to in (('/usr/sbin/pciconf', stubs + '/pciconf'), ('/usr/local/bin/php', stubs + '/php'),
                     ('/usr/local/bin/python3', stubs + '/python3'), ('/boot/', root + '/boot/'),
                     ('/usr/local/opnsense/', root + '/usr/local/opnsense/'),
                     ('/usr/local/etc/', root + '/usr/local/etc/'), ('/var/log/', root + '/var/log/')):
        text = text.replace(path, to)
    script = os.path.join(tmp, 'verify.sh')
    with open(script, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    with open(os.path.join(root, 'boot', 'modules', 'octep.ko.kernel'), 'w', encoding='utf-8') as f:
        f.write(env.pop('stamp', KERNEL) + '\n')
    e = dict(os.environ, PATH=stubs + os.pathsep + os.environ.get('PATH', ''), FAKE_UNAME_V=KERNEL,
             FAKE_OCTEP_LOADED='1', FAKE_PORTS='1', FAKE_TEMP='1')
    e.update(env)
    out = subprocess.run(['sh', script], capture_output=True, text=True, env=e)
    return out.returncode, out.stdout


with tempfile.TemporaryDirectory() as tmp:
    os.makedirs(os.path.join(tmp, 'bin'))
    for name, body in STUBS.items():
        p = os.path.join(tmp, 'bin', name)
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\n' + body + '\n')
        os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    os.makedirs(os.path.join(tmp, 'root', 'boot', 'modules'))
    os.makedirs(os.path.join(tmp, 'root', 'var', 'log'))
    os.makedirs(os.path.join(tmp, 'root', 'usr', 'local', 'opnsense', 'scripts', 'xgs'))
    open(os.path.join(tmp, 'root', 'boot', 'modules', 'octep.ko'), 'w').close()
    sub = os.path.join(tmp, 'root', 'usr', 'local', 'opnsense', 'scripts', 'xgs', 'subnets.py')
    open(sub, 'w').close()
    os.chmod(sub, 0o755)

    PORTS_FAIL = 'This appliance does not currently have working front ports.'
    ELSEWHERE = 'The chain to the front ports is intact; what failed is above, and is not the ports.'

    rc, out = run(tmp)
    check('a healthy 3300: exit 0', rc, 0)
    check('a healthy 3300: the chain is intact', out.rstrip().endswith('The chain is intact.'), True)
    check('a healthy 3300: twelve front ports seen', 'ok    12 front-port interfaces' in out, True)

    rc, out = run(tmp, FAKE_TEMP='0')
    check('no CPU temperature: exit 1', rc, 1)
    check('no CPU temperature: the failure is named', 'FAIL  no CPU temperature' in out, True)
    check('no CPU temperature: the ports are not blamed', PORTS_FAIL in out, False)
    check('no CPU temperature: the summary says where it failed', ELSEWHERE in out, True)

    rc, out = run(tmp, stamp='FreeBSD 15.1-RELEASE-p4 an older kernel')
    check('octep built for another kernel: exit 1', rc, 1)
    check('octep built for another kernel: the ports are blamed', PORTS_FAIL in out, True)

    rc, out = run(tmp, FAKE_PORTS='0')
    check('octep loaded with no front ports: the ports are blamed', (rc, PORTS_FAIL in out), (1, True))

    rc, out = run(tmp, FAKE_TEMP='0', stamp='FreeBSD 15.1-RELEASE-p4 an older kernel')
    check('both failing: the ports come first', (PORTS_FAIL in out, ELSEWHERE in out), (True, False))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
