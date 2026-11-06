#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The first boot's question, end to end without an appliance: scripts/xgs/countdown.py on a
pseudo-terminal - 0 when the time runs out, 3 when a key is pressed, 0 on a console that is not a
terminal - and rc.syshook.d/early/30-xgs-assign's answer to each, with stand-ins for PHP and the
countdown: assigned, marked as by hand, marked as failed, or a countdown that failed and is asked
again at the next boot (until 2026-11-06 that last was marked as a key, for good).

    python3 tools/test-countdown.py"""
import os
import pty
import stat
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COUNTDOWN = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'countdown.py')
HOOK = os.path.join(ROOT, 'src', 'etc', 'rc.syshook.d', 'early', '30-xgs-assign')

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


def countdown(console, key=False):
    env = dict(os.environ, XGS_ASSIGN_CONSOLE=console, XGS_ASSIGN_SECONDS='2')
    proc = subprocess.Popen([sys.executable, COUNTDOWN], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    if key:
        time.sleep(0.6)
        os.write(master, b'x')
    return proc.wait(timeout=15)


master, slave = pty.openpty()
check('the time runs out: 0', countdown(os.ttyname(slave)), 0)
check('a key is pressed: 3', countdown(os.ttyname(slave), key=True), 3)
check('a console that is not a terminal: taken as run out, 0', countdown(os.devnull), 0)
check('no console to open: 0', countdown('/nonexistent/console'), 0)


def stub(path, body):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('#!/bin/sh\n' + body + '\n')
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def hook(countdown_rc, apply_rc):
    """30-xgs-assign with its paths moved into a scratch directory, PHP and the countdown stood in
    for: what it writes as its mark, or None."""
    with tempfile.TemporaryDirectory() as tmp:
        xgs, mark = os.path.join(tmp, 'xgs'), os.path.join(tmp, 'mark')
        os.makedirs(xgs)
        open(os.path.join(xgs, 'assign.php'), 'w').close()
        stub(os.path.join(xgs, 'board.sh'), 'echo "201 armada"')
        # --check: something to do; --plan: nothing to say; --apply: as the case asks
        stub(os.path.join(tmp, 'php'), 'case "$2" in --apply) exit %d ;; esac; exit 0' % apply_rc)
        stub(os.path.join(tmp, 'python3'), 'exit %d' % countdown_rc)
        stub(os.path.join(tmp, 'logger'), 'exit 0')
        with open(HOOK, encoding='utf-8') as f:
            text = f.read()
        for old, new in (('DIR=/usr/local/opnsense/scripts/xgs', 'DIR=' + xgs),
                         ('MARK=/conf/os-xgs-npu-assign', 'MARK=' + mark),
                         ('LOG=/var/log/xgs-assign.log', 'LOG=' + os.path.join(tmp, 'log')),
                         ('/usr/local/bin/php', os.path.join(tmp, 'php')),
                         ('/usr/local/bin/python3', os.path.join(tmp, 'python3')),
                         ('/usr/bin/logger', os.path.join(tmp, 'logger'))):
            assert old in text, old
            text = text.replace(old, new)
        script = os.path.join(tmp, 'hook.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        rc = subprocess.run(['sh', script], capture_output=True).returncode
        if not os.path.exists(mark):
            return rc, None
        with open(mark, encoding='utf-8') as f:
            return rc, f.read().split(':')[0]


check('the time ran out, assigned: marked', hook(0, 0), (0, 'assigned by the names on the chassis'))
rc, said = hook(0, 1)
check('the time ran out, the assignment failed: marked as failed, not asked every boot',
      (rc, bool(said) and said.startswith('the assignment failed')), (0, True))
check('a key: marked as by hand', hook(3, 0), (0, 'stopped by a key, assigned by hand'))
check('the countdown failed: nothing marked, asked again at the next boot', hook(1, 0), (0, None))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
