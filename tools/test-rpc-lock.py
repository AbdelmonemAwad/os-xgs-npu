#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/offload.sh and tools/octep-fpcnt.sh against one made-up XGS 3300 driver: the offload
gate and a counter read go through the same rpc sysctls - rpc.cmd and the rest staged, rpc.post, the
answer read after - and each runs its staging under one lock (the review of 2026-11-06, D14). Until
then a read staged between the gate's rpc.cmd and its rpc.post had command 44 posted in the gate's
place: the gate was not written, and the read's rc was taken for its answer.

    python3 tools/test-rpc-lock.py

POSIX sh and util-linux flock(1), which stands in for FreeBSD's lockf(1). The driver is a sysctl(8)
that keeps its values in files and writes down every command posted, with what was staged for it.
"""
import fcntl
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
OFFLOAD = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'offload.sh')
FPCNT = os.path.join(HERE, 'octep-fpcnt.sh')

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


def wait_for(test, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if test():
            return True
        time.sleep(0.05)
    return test()


run = tempfile.mkdtemp(prefix='rpc-lock-test-')
try:
    bin_dir = os.path.join(run, 'bin')
    drv = os.path.join(run, 'driver')
    os.makedirs(bin_dir)
    os.makedirs(drv)

    def stub(name, body):
        path = os.path.join(bin_dir, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    # The driver's rpc sysctls. Staging rpc.cmd=2 stops until the test says go, so that a reader has
    # the moment between the gate's rpc.cmd and its rpc.post that a busy appliance can give it.
    sysctl = stub('sysctl', r'''D=%s
if [ "$1" = -n ]; then
	case $2 in
	*.rpc.last) printf '\ncmd %%s\nrc 0x0000 (ok)  descriptor_done 1  magic_seed 0x0000  payload 0 bytes\n' "$(cat "$D/last")" ;;
	*.rpc.buf) printf '\nbuffer at pa 0x0000000000001000, first 128 bytes\n' ;;
	*) [ -f "$D/$2" ] || exit 1; cat "$D/$2" ;;
	esac
	exit 0
fi
for a; do
	oid=${a%%%%=*} v=${a#*=}
	case $oid in
	*.rpc.post)
		echo "$(cat "$D/dev.octep.0.rpc.cmd") $(cat "$D/dev.octep.0.rpc.fw_cfg")" >> "$D/posts"
		cat "$D/dev.octep.0.rpc.cmd" > "$D/last" ;;
	*)
		echo "$v" > "$D/$oid"
		if [ "$oid" = dev.octep.0.rpc.cmd ] && [ "$v" = 2 ]; then
			: > "$D/staged"
			i=0
			while [ ! -e "$D/go" ] && [ $i -lt 200 ]; do sleep 0.05; i=$((i + 1)); done
		fi ;;
	esac
done
exit 0''' % drv)
    # the counters' page as hexdump(1) would print it from /dev/mem: the reply's two header words,
    # then each counter's low and high word
    stub('dd', 'cat > /dev/null 2>&1; exit 0')
    stub('hexdump', 'awk \'BEGIN { print 0; print 0; for (i = 0; i < 256; i++) { print i; print 0 } }\'')
    logger = stub('logger', 'for m in "$@"; do :; done\necho "$m" >> %s' % os.path.join(run, 'logged'))
    # lockf [-k] [-t seconds] file command..., as the scripts use it, over flock(1): 75 when it waited
    # its seconds out, as lockf answers
    lockf = stub('lockf', 'while :; do\n\tcase $1 in\n\t-k|-s) shift ;;\n\t-t) t=$2; shift 2 ;;\n\t*) break ;;\n'
                          '\tesac\ndone\nf=$1; shift\nexec flock -E 75 ${t:+-w "$t"} "$f" "$@"')

    def copy(src, name, extra=()):
        with open(src, encoding='utf-8') as f:
            text = f.read()
        for real, fake in (('/sbin/sysctl', sysctl), ('/usr/bin/logger', logger), ('/usr/bin/lockf', lockf)) + extra:
            text = text.replace(real, fake)
        path = os.path.join(run, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        return path

    offload = copy(OFFLOAD, 'offload.sh')
    fpcnt = copy(FPCNT, 'octep-fpcnt.sh')
    lock = os.path.join(run, 'rpc.lock')
    env = dict(os.environ, PATH=bin_dir + os.pathsep + os.environ.get('PATH', ''), RPC_LOCK=lock,
               XGS_CUT_HW='1', XGS_COPIES='0', CONFIG_READER='/nonexistent',
               NAMES=os.path.join(HERE, 'fpcntr-names-sfos22.txt'))

    def driver():
        for f in os.listdir(drv):
            os.remove(os.path.join(drv, f))
        for oid, value in (('rpc.ready', 1), ('rpc.fw_cfg', 2), ('rpc.allow_write', 0), ('rpc.sfos', 22),
                           ('rpc.cmd', 0)):
            with open(os.path.join(drv, 'dev.octep.0.' + oid), 'w', encoding='utf-8') as f:
                f.write('%d\n' % value)

    def posts():
        try:
            with open(os.path.join(drv, 'posts'), encoding='utf-8') as f:
                return f.read().splitlines()
        except OSError:
            return []

    # The gate written while the Datapath counters tab reads: the read waits for the gate's command
    # to be posted, then has its own.
    driver()
    gate = subprocess.Popen(['sh', offload, 'on'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    staged = wait_for(lambda: os.path.exists(os.path.join(drv, 'staged')), 10)
    check('the gate staged its command', staged, True)
    reader = subprocess.Popen(['sh', fpcnt], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    wait_for(lambda: reader.poll() is not None, 3)
    open(os.path.join(drv, 'go'), 'w').close()
    out, _ = gate.communicate(timeout=60)
    reader.communicate(timeout=60)
    check('posted: the gate, fw_cfg 0x7, and then the read', posts(), ['2 7', '44 7'])
    check('both done', (gate.returncode, reader.returncode), (0, 0))
    answer = json.loads(out.decode('utf-8', 'replace') or '{}')
    check("the gate's answer", (answer.get('applied'), answer.get('message')),
          (True, 'offloading is on: the coprocessor accepted fw_cfg 0x7'))
    with open(os.path.join(drv, 'dev.octep.0.rpc.allow_write'), encoding='utf-8') as f:
        check('the write gate shut again', f.read().strip(), '0')

    # A reader that holds the lock and does not let go: after its wait - a second here, thirty on an
    # appliance - the gate is written all the same, and the log says so.
    driver()
    open(os.path.join(drv, 'go'), 'w').close()
    if os.path.exists(os.path.join(run, 'logged')):
        os.remove(os.path.join(run, 'logged'))
    quick =copy(OFFLOAD, 'offload-quick.sh', (('-t 30', '-t 1'),))
    with open(lock, 'w') as held:
        fcntl.flock(held, fcntl.LOCK_EX)
        done = subprocess.run(['sh', quick, 'on'], capture_output=True, text=True, errors='replace', timeout=60,
                              env=env)
    check('a lock held past its wait: the gate written all the same', (done.returncode, posts()), (0, ['2 7']))
    try:
        with open(os.path.join(run, 'logged'), encoding='utf-8') as f:
            logged = f.read().splitlines()
    except OSError:
        logged = []
    check('and said, before what was written', logged,
          ["the coprocessor's rpc lock was not had (lockf 75); the gate is written without it",
           'offload gate opened, fw_cfg 0x7'])

    # A lock that cannot be made at all - its directory not there: the gate is written all the same.
    driver()
    open(os.path.join(drv, 'go'), 'w').close()
    if os.path.exists(os.path.join(run, 'logged')):
        os.remove(os.path.join(run, 'logged'))
    done =subprocess.run(['sh', offload, 'on'], capture_output=True, text=True, errors='replace', timeout=60,
                          env=dict(env, RPC_LOCK=os.path.join(run, 'not-there', 'rpc.lock')))
    with open(os.path.join(run, 'logged'), encoding='utf-8') as f:
        logged = f.read().splitlines()
    check('a lock that cannot be made: the gate written all the same, and said',
          (done.returncode, posts(), [line.split(' (lockf')[0] for line in logged]),
          (0, ['2 7'], ["the coprocessor's rpc lock was not had", 'offload gate opened, fw_cfg 0x7']))
finally:
    shutil.rmtree(run, ignore_errors=True)

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
