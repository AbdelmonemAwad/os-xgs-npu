#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/temperatures.sh on a made-up XGS 3300: the boot its flags and its memory sensor's answer
are kept for. A boot's first reading says a flag that is not as a healthy board has it, and a module
without a memory sensor is asked once a boot; a clock stepped within the boot changes neither (the
review of 2026-11-06, D11). Until then the boot was kern.boottime, which a step moves, and the next
run said the flag again and asked the module again.

    python3 tools/test-temperatures.py

POSIX sh. sysctl(8), the coprocessor's sensors, the management link and the system log are stood in
for, in a copy of the script whose paths point at them; /var/run is a directory of the test's own.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
XGS = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs')

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


run = tempfile.mkdtemp(prefix='temperatures-test-')
try:
    def stub(name, body):
        path = os.path.join(run, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    clock = os.path.join(run, 'boottime')
    logged = os.path.join(run, 'logged')
    asked = os.path.join(run, 'asked')
    # the die and the memory have their nodes, the port chips do not: those are another test's
    sysctl = stub('sysctl', 'case "$1" in\n'
                            '-N) case "$2" in dev.octep.0.temperature.npu|dev.octep.0.temperature.memory) exit 0 ;; esac\n'
                            '    exit 1 ;;\n'
                            '-n) [ "$2" = kern.boottime ] && echo "{ sec = $(cat %s), usec = 0 } Fri Nov  6 05:00:00 2026"\n'
                            '    exit 0 ;;\n'
                            'esac\nexit 0' % clock)
    # fan 0 under speed, everything else as a healthy board has it
    sensors = stub('xgs3300-sensors', 'echo \'{"npu_c": 60.5, "inlet_c": 30.5, "fan0_under": 1, "fan1_under": 0, '
                                      '"psu_internal_good": 1, "psu_external_good": 1, "reset_flag0": 0, '
                                      '"reset_flag1": 0}\'')
    # a module without a sensor: its read fails
    link = stub('mgmt-link', 'echo "$*" >> %s\necho "Error: Read failed"' % asked)
    logger = stub('logger', 'for m in "$@"; do :; done\necho "$m" >> %s' % logged)
    with open(os.path.join(XGS, 'temperatures.sh'), encoding='utf-8') as f:
        text = f.read()
    for real, fake in (('/usr/local/opnsense/scripts/npuctl/mgmt-link.sh', link),
                       ('/usr/local/opnsense/scripts/xgs/board.sh', os.path.join(XGS, 'board.sh')),
                       ('/usr/local/sbin/xgs3300-sensors', sensors), ('/usr/bin/logger', logger),
                       ('/sbin/sysctl', sysctl), ('/var/run/xgs-sensors-boot', os.path.join(run, 'run-boot')),
                       ('/var/db/os-xgs-npu', os.path.join(run, 'db'))):
        text = text.replace(real, fake)
    script = os.path.join(run, 'temperatures.sh')
    with open(script, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)

    def once(boottime):
        """One minute's run with kern.boottime at boottime: what the log has, and how often the
        memory sensor was asked, since the first run."""
        with open(clock, 'w', encoding='utf-8') as f:
            f.write('%d\n' % boottime)
        done = subprocess.run(['sh', script], capture_output=True, text=True, errors='replace', timeout=60,
                              env=dict(os.environ, XGS_PLANAR_SERIAL='XYZAMDA0202-000412345678'))
        if done.returncode != 0:
            print(done.stderr)
        lines = []
        for path in (logged, asked):
            try:
                with open(path, encoding='utf-8') as f:
                    lines.append(f.read().splitlines())
            except OSError:
                lines.append([])
        return lines[0], len(lines[1])

    check("a boot's first reading: the fan said, the memory sensor asked",
          once(1793941200), (['fan 0 under speed'], 1))
    check('the clock stepped a second within the boot: nothing said again, nothing asked again',
          once(1793941201), (['fan 0 under speed'], 1))
    check('and stepped an hour back: the same', once(1793937601), (['fan 0 under speed'], 1))
    try:
        os.remove(os.path.join(run, 'run-boot'))     # a reboot: /var/run is emptied
    except FileNotFoundError:
        pass
    check('the next boot: the fan said again, the sensor asked again',
          once(1793952000), (['fan 0 under speed', 'fan 0 under speed'], 2))
finally:
    shutil.rmtree(run, ignore_errors=True)

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
