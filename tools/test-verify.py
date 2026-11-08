#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""install/verify.sh on a made-up XGS 3300, its commands stood in for and its paths moved under a
temporary directory: the summary blames the front ports only when the chain to them fails, and a
failure of anything else - the CPU temperature - is called what it is (issue 957, where a 3100 with
twelve working ports was told it had none). And what it says of this project's installation - the
release installed, the last update, the installer's files against their digests - which until
2026-11-06 it did not say at all (issue 911; the review's U13). And whether the coprocessor's fast path
answers its heartbeat, or the driver has taken the coprocessor as gone - a failure of the chain, which
until 2026-11-08 it did not look at.

    python3 tools/test-verify.py"""
import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFY = os.path.join(ROOT, 'install', 'verify.sh')
KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'

# The stand-ins read what the case wants from the environment.
STUBS = {
    'uname': 'case "$1" in -v) echo "${FAKE_UNAME_V}" ;; *) echo "FreeBSD 15.1-RELEASE-p6" ;; esac',
    'opnsense-version': 'echo "OPNsense 26.7.8_1 (amd64)"',
    'kenv': 'exit 0',
    'dmesg': 'exit 0',
    'pciconf': ('[ "${FAKE_ARMADA}" = 1 ] && { echo "none0@pci0:2:0:0: class=0x058000 rev=0x00 hdr=0x00 '
                'vendor=0x11ab device=0x7080"; exit 0; }; '
                'echo "none0@pci0:3:0:0: class=0x0b4000 rev=0x00 hdr=0x00 vendor=0x177d device=0xa300"'),
    # as the kernel answers: the driver's module is pci/octep (DRIVER_MODULE), asked for with -m
    'kldstat': '[ "$2" = -m ] && [ "$3" = pci/octep ] && [ "${FAKE_OCTEP_LOADED}" = 1 ] && exit 0; exit 1',
    'ifconfig': ('[ "$1" = -l ] || exit 1; ports=""; [ "${FAKE_PORTS}" = 1 ] && '
                 'ports="oxp0 oxp1 oxp2 oxp3 oxp4 oxp5 oxp6 oxp7 oxp8 oxp9 oxp10 oxp11"; '
                 'echo "${ports} igb0 lo0"'),
    'sysctl': ('[ "$1" = -n ] && [ "$2" = kern.boottime ] && [ -n "${FAKE_BOOTED}" ] '
               '&& { echo "{ sec = ${FAKE_BOOTED}, usec = 0 } Fri Nov  6 08:00:00 2026"; exit 0; }; '
               '[ "$1" = -n ] && [ "$2" = dev.cpu.0.temperature ] && [ "${FAKE_TEMP}" = 1 ] '
               '&& { echo 46.0C; exit 0; }; '
               # the heartbeat and the gone flag, either driver's, as the case sets them; unset is a node
               # the driver does not have
               '[ "$1" = -n ] || exit 1; v=""; case "$2" in dev.octep.0.*|dev.npuep.0.*) case "${2#dev.*.0.}" in '
               'heartbeat.state) v="${FAKE_HB_STATE}" ;; heartbeat.deaths) v="${FAKE_HB_DEATHS}" ;; '
               'heartbeat.dead_ms) v="${FAKE_HB_DEAD_MS}" ;; heartbeat.since_answer_ms) v="${FAKE_HB_SINCE}" ;; '
               'gone) v="${FAKE_GONE}" ;; gone_at) v="${FAKE_GONE_AT}" ;; esac ;; esac; '
               '[ -n "$v" ] && { echo "$v"; exit 0; }; exit 1'),
    'php': 'echo amdtemp',
    # the kernel in /boot read as kernel-follow.sh reads it; the test's stand-in kernel is a text file
    'strings': 'cat "$2" 2>/dev/null',
    # models.py answers for the model; anything else - subnets.py, the installation's records - is run
    'python3': ('case "$*" in *models.py*) echo "AMDA0215-0003 rev XGS_3300"; exit 0 ;; esac; '
                'exec "${REAL_PYTHON}" "$@"'),
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
                     ('/usr/local/etc/', root + '/usr/local/etc/'), ('/var/log/', root + '/var/log/'),
                     ('/usr/local/share/', root + '/usr/local/share/'), ('/var/db/', root + '/var/db/')):
        text = text.replace(path, to)
    script = os.path.join(tmp, 'verify.sh')
    with open(script, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    with open(os.path.join(root, 'boot', 'modules', 'octep.ko.kernel'), 'w', encoding='utf-8') as f:
        f.write(env.pop('stamp', KERNEL) + '\n')
    e = dict(os.environ, PATH=stubs + os.pathsep + os.environ.get('PATH', ''), FAKE_UNAME_V=KERNEL,
             FAKE_OCTEP_LOADED='1', FAKE_PORTS='1', FAKE_TEMP='1', REAL_PYTHON=sys.executable)
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

    # The heartbeat with the fast path, and the driver's gone (2026-11-08: an XGS 3300 whose fast path had
    # stopped, its twelve ports there and carrying nothing, was told "9 passed, 0 failed").
    HB = dict(FAKE_HB_DEAD_MS='30000', FAKE_HB_DEATHS='0', FAKE_HB_SINCE='1200', FAKE_GONE='0', FAKE_GONE_AT='0')
    rc, out = run(tmp, FAKE_HB_STATE='alive', **HB)
    check('the fast path answering: said, and the chain intact',
          ("ok    the coprocessor's fast path answers its heartbeat" in out, rc,
           out.rstrip().endswith('The chain is intact.')), (True, 0, True))

    dead = dict(HB, FAKE_HB_STATE='dead', FAKE_HB_DEATHS='1', FAKE_HB_SINCE='412000')
    rc, out = run(tmp, **dead)
    check('the fast path dead: a failure of the chain', (rc, PORTS_FAIL in out), (1, True))
    check('... named, with since when',
          ("FAIL  the coprocessor's fast path stopped answering its heartbeat (dev.octep.0.heartbeat.state: dead)"
           in out, '        the heartbeat: no answer for 412 s, taken as stopped at 30 s; 1 stop(s) since the '
           'driver loaded' in out), (True, True))
    check('... and what to do, the remedy the project gives',
          ('XGS: Settings, "When the fast path stops"' in out, 'scripts/xgs/recover.sh' in out), (True, True))

    rc, out = run(tmp, **dict(dead, FAKE_GONE='1', FAKE_GONE_AT='5321'))
    check('the coprocessor taken as gone: a failure of the chain', (rc, PORTS_FAIL in out), (1, True))
    check('... named once, with when, the heartbeat beside it',
          ("FAIL  the driver has taken the coprocessor as gone 5321 s after the host's boot (dev.octep.0.gone)"
           in out, out.count('  FAIL  '), '        the heartbeat: no answer for 412 s' in out), (True, 1, True))
    check('... and what to do', 'scripts/xgs/recover.sh' in out, True)

    rc, out = run(tmp, **dict(HB, FAKE_HB_STATE='alive', FAKE_GONE='1'))
    check('gone with the heartbeat not dead: still a failure', (rc, 'FAIL  the driver has taken the coprocessor '
          'as gone (dev.octep.0.gone)' in out), (1, True))

    rc, out = run(tmp, **dict(HB, FAKE_HB_STATE='late', FAKE_HB_SINCE='17000'))
    check('the fast path late: worth a look, not a failure',
          ("note  the coprocessor's fast path is late answering its heartbeat - no answer for 17 s" in out, rc),
          (True, 0))

    rc, out = run(tmp)
    check('no heartbeat node (an older driver): worth a look, not a failure',
          ('note  no dev.octep.0.heartbeat.state - a driver from before the heartbeat' in out, rc,
           out.rstrip().endswith('The chain is intact.')), (True, 0, True))

    rc, out = run(tmp, FAKE_ARMADA='1', **dead)
    check('the ARMADA family: the same check, its own nodes',
          "FAIL  the coprocessor's fast path stopped answering its heartbeat (dev.npuep.0.heartbeat.state: dead)"
          in out, True)
    rc, out = run(tmp, FAKE_ARMADA='1', **dict(HB, FAKE_HB_STATE='alive'))
    check('the ARMADA family answering: said', "ok    the coprocessor's fast path answers its heartbeat" in out, True)

    # After an update the kernel follower built octep.ko for the kernel now in /boot: nothing is broken,
    # the reboot loads it (until 2026-11-06 this said there were no working front ports).
    NEXT = 'FreeBSD 15.1-RELEASE-p7 stable/26.7-n284200-0123456789ab SMP'
    os.makedirs(os.path.join(tmp, 'root', 'boot', 'kernel'), exist_ok=True)
    with open(os.path.join(tmp, 'root', 'boot', 'kernel', 'kernel'), 'w', encoding='utf-8') as f:
        f.write('@(#)something else\n' + NEXT + '\nmore\n')
    rc, out = run(tmp, stamp=NEXT)
    check('octep built for the next boot\'s kernel: exit 0', rc, 0)
    check('octep built for the next boot\'s kernel: said so',
          'ok    octep.ko is built for the kernel the next boot will use' in out, True)
    check('octep built for the next boot\'s kernel: the ports are not blamed', PORTS_FAIL in out, False)
    rc, out = run(tmp, stamp='FreeBSD 15.1-RELEASE-p4 an older kernel')
    check('octep built for neither kernel: still a failure of the chain', (rc, PORTS_FAIL in out), (1, True))
    with open(os.path.join(tmp, 'root', 'boot', 'kernel', 'kernel'), 'w', encoding='utf-8') as f:
        f.write(KERNEL + '\n')
    rc, out = run(tmp)
    check('the next boot\'s kernel the running one: a healthy 3300 as before',
          (rc, 'ok    octep matches this kernel' in out), (0, True))

    # This project's installation (the review's U13): nothing recorded at all, then a record of each -
    # the release installed, the last update, the digests of what the installer wrote.
    rc, out = run(tmp)
    check('no records: each said to be missing',
          ('note  no record of the release installed' in out,
           '  last update from XGS: Updates: none recorded' in out,
           'note  no list of the files the installer wrote' in out), (True, True, True))
    check('no records: worth a look, not a failure', (rc, out.rstrip().endswith('The chain is intact.')),
          (0, True))

    share = os.path.join(tmp, 'root', 'usr', 'local', 'share', 'os-xgs-npu')
    update = os.path.join(tmp, 'root', 'var', 'db', 'os-xgs-npu', 'update')
    os.makedirs(share)
    os.makedirs(update)
    COMMIT = '1eca2ab3c4d5e6f708192a3b4c5d6e7f80912a3b'
    BOOTED = int(time.time()) - 3600

    def record(name, obj):
        with open(os.path.join(update if name == 'apply.json' else share, name), 'w', encoding='utf-8') as f:
            json.dump(obj, f)

    def manifest(*paths):
        with open(os.path.join(share, 'manifest.sha256'), 'w', encoding='utf-8') as f:
            for p in paths:
                try:
                    with open(p, 'rb') as g:
                        digest = hashlib.sha256(g.read()).hexdigest()
                except OSError:
                    digest = '0' * 64
                f.write('%s %s\n' % (digest, p))

    def at(t):
        return time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime(t))

    record('installed.json', {'commit': COMMIT, 'date': '2026-11-06T09:00:00Z', 'source': 'update',
                              'release': 'v0.1.0-alpha.14', 'installed_at': BOOTED - 600})
    record('apply.json', {'status': 'done', 'sha': COMMIT, 'started_at': BOOTED - 900,
                          'finished_at': BOOTED - 600, 'driver_changed': True, 'hooks_changed': [], 'reboot': True})
    octep = os.path.join(tmp, 'root', 'boot', 'modules', 'octep.ko')
    manifest(sub, octep)
    rc, out = run(tmp, FAKE_BOOTED=str(BOOTED))
    check('the release installed: its tag, its commit, how and when',
          ('  installed: v0.1.0-alpha.14 - commit 1eca2ab3c4d5, dated 2026-11-06T09:00:00Z, installed by '
           'XGS: Updates on %s' % at(BOOTED - 600)) in out, True)
    check('the last update: its commit and when', '  last update: 1eca2ab3c4d5, installed on %s'
          % at(BOOTED - 600) in out, True)
    check('... loaded by the boot since: nothing said to wait', 'waits for a reboot' in out, False)
    check('the installer\'s files as it wrote them', 'ok    the 2 files the installer wrote are as it wrote them'
          in out, True)
    check('... and the chain intact', (rc, out.rstrip().endswith('The chain is intact.')), (0, True))

    record('apply.json', {'status': 'done', 'sha': COMMIT, 'started_at': BOOTED + 60,
                          'finished_at': BOOTED + 300, 'driver_changed': True, 'hooks_changed': [], 'reboot': True})
    rc, out = run(tmp, FAKE_BOOTED=str(BOOTED))
    check('an update finished since the boot, asking for one: waits for a reboot',
          'note  the last update waits for a reboot: what it changed takes effect at the next boot' in out, True)

    record('apply.json', {'status': 'failed', 'sha': COMMIT, 'started_at': BOOTED + 60, 'finished_at': BOOTED + 300,
                          'message': 'the download failed: <urlopen error [Errno 51] Network is unreachable> '
                                     '(tried 3 times), by way of 192.0.2.7'})
    rc, out = run(tmp, FAKE_BOOTED=str(BOOTED))
    check('a failed update: said, with its reason',
          'note  the last update, to 1eca2ab3c4d5, failed on %s: the download failed: <urlopen error [Errno 51] '
          'Network is unreachable> (tried 3 times)' % at(BOOTED + 300) in out, True)
    check('... an address in it masked', ('192.0.2.7' in out, 'by way of x.x.x.x' in out), (False, True))
    check('... worth a look, and the ports not blamed', (rc, out.rstrip().endswith('The chain is intact.')),
          (0, True))

    record('apply.json', {'status': 'running', 'pid': 99999999, 'sha': COMMIT, 'started_at': BOOTED - 60})
    rc, out = run(tmp, FAKE_BOOTED=str(BOOTED))
    check('an update running when the appliance restarted: said not to have finished',
          'note  the last update, to 1eca2ab3c4d5, was running when the appliance restarted, and did not finish'
          in out, True)
    record('apply.json', {'status': 'running', 'pid': 99999999, 'sha': COMMIT, 'started_at': BOOTED + 60})
    rc, out = run(tmp, FAKE_BOOTED=str(BOOTED))
    check('an update of this boot: what its record says, and when it started',
          'note  the record of the last update, to 1eca2ab3c4d5, says it is running: started on %s'
          % at(BOOTED + 60) in out, True)

    gone = os.path.join(tmp, 'root', 'usr', 'local', 'opnsense', 'scripts', 'xgs', 'gone.py')
    manifest(sub, octep, gone)
    with open(sub, 'w') as f:
        f.write('# changed by hand\n')
    rc, out = run(tmp, FAKE_BOOTED=str(BOOTED))
    check('files changed since the installer wrote them: how many',
          'note  2 of the 3 files the installer wrote are not as it wrote them' in out, True)
    check('... each named, the missing one so', ('          ' + sub in out, '          ' + gone + ' (missing)' in out),
          (True, True))
    check('... and the one as it was not named', '          ' + octep + '\n' in out, False)
    check('... worth a look, not a failure', (rc, out.rstrip().endswith('The chain is intact.')), (0, True))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
