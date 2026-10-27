#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The coprocessor's own watchdog feeder, looked after from the host (#768).
    feeder.py start     start the watcher, detached
    feeder.py stop      stop it
    feeder.py status    one line: watching, and the last thing it put right
    feeder.py run       the watcher itself, what start runs under daemon(8)
WHY. The coprocessor's Linux arms its hardware watchdog at boot and feeds it every five seconds from
busybox's `watchdog -t 5 /dev/watchdog`, which /etc/init.d/S15watchdog starts - the same on vendor
versions 21, 22 and 23. If that one process stops while the rest of the coprocessor runs, the watchdog
resets the coprocessor about a minute later. An XGS 3300's host does not come back from that reset: a
read of the device while it boots never completes, and the host's own watchdog restarts the appliance
some four minutes on (#768). An XGS 136 takes its coprocessor up again, without the restart but not
without the minutes with nothing passing. A feeder that stopped alone costs either one a reset it did
not need.
HOW. Every EVERY seconds the coprocessor is asked, through scripts/xgs/npu-run.sh - the management
link, or the console when that does not answer - for its uptime and its feeder's state:
  running            nothing to do;
  stopped (T, t)     a SIGSTOP: it is sent SIGCONT;
  none, or a zombie  started again by the coprocessor's own init script, as at its boot.
A correction is said in the system log (xgs-feeder) and kept in STATE for the support bundle.
Not touched: a coprocessor up for less than SETTLE seconds, whose init may not have started its
feeder yet, and one that does not answer at all - its own watchdog is right to reset that one.
A lab run that stops the feeder on purpose stops this first: feeder.py stop.
Never fails the caller: a host with no coprocessor to ask says so and exits 0.
"""
import json
import os
import signal
import subprocess
import sys
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
STATE = '/var/db/os-xgs-npu/feeder.json'
PIDFILE = '/var/run/xgs-feeder.pid'
EVERY = 20
SETTLE = 120
# One command, read back line by line: the coprocessor's uptime in seconds, the feeder's pid or
# "none", and its State line from /proc. busybox has no pgrep -f here, but has pidof.
ASK = ('cut -d. -f1 /proc/uptime; p=$(pidof watchdog | cut -d" " -f1); echo "pid ${p:-none}"; '
       '[ -n "$p" ] && grep -m1 "^State:" /proc/$p/status; true')


def say(text):
    subprocess.run(['/usr/bin/logger', '-t', 'xgs-feeder', text], capture_output=True)


def npu(seconds, command):
    """What the coprocessor printed, or None when it could not be asked."""
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, str(seconds), command], stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, timeout=seconds + 60)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout if done.returncode == 0 else None


def look():
    """(uptime, pid, state letter) of the feeder as the coprocessor reports it, or None."""
    out = npu(8, ASK)
    if out is None:
        return None
    uptime = pid = state = None
    for line in out.replace('\r', '').splitlines():
        line = line.strip()
        if line.isdigit() and uptime is None:
            uptime = int(line)
        elif line.startswith('pid '):
            pid = line[4:]
        elif line.startswith('State:'):
            parts = line.split()
            state = parts[1] if len(parts) > 1 else None
    if uptime is None or pid is None:
        return None
    return uptime, (None if pid == 'none' else pid), state


def keep(found, done, pid):
    try:
        with open(STATE, encoding='utf-8') as f:
            state = json.load(f)
    except (OSError, ValueError):
        state = {}
    state['last'] = {'at': int(time.time()), 'found': found, 'done': done, 'pid': pid}
    state['count'] = state.get('count', 0) + 1
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(state, f)
    os.replace(STATE + '.tmp', STATE)


def once():
    """One look, and what it put right: None when nothing needed it."""
    seen = look()
    if seen is None:
        return 'unreachable'
    uptime, pid, state = seen
    if uptime < SETTLE:
        return None
    if pid is not None and state in ('T', 't') and pid.isdigit():
        answer = npu(5, 'kill -CONT %s && echo continued' % pid)
        done = 'continued' if answer and 'continued' in answer else 'could not continue it'
        say('the coprocessor\'s watchdog feeder (pid %s) was stopped: %s' % (pid, done))
        keep('stopped', done, pid)
        return done
    if pid is None or state == 'Z':
        answer = npu(8, '/etc/init.d/S15watchdog start; sleep 1; pidof watchdog')
        new = (answer or '').replace('\r', '').split()
        done = 'started again (pid %s)' % new[-1] if new and new[-1].isdigit() else 'could not start it'
        say('the coprocessor\'s watchdog feeder was %s: %s' % ('a zombie' if pid else 'not running', done))
        keep('zombie' if pid else 'gone', done, pid)
        return done
    return None


def run():
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    unreachable = False
    while True:
        try:
            result = once()
        except Exception as e:
            result = None
            say('feeder.py: %s' % e)
        # a coprocessor out of reach is said once, when it goes and when it comes back
        if result == 'unreachable' and not unreachable:
            unreachable = True
            say('the coprocessor does not answer: its watchdog feeder is not looked at until it does')
        elif result != 'unreachable' and unreachable:
            unreachable = False
            say('the coprocessor answers again: its watchdog feeder is looked at every %d s' % EVERY)
        time.sleep(EVERY)


def running():
    try:
        with open(PIDFILE) as f:
            os.kill(int(f.read().strip()), 0)
        return True
    except (OSError, ValueError):
        return False


def main(what):
    if what == 'run':
        run()
    elif what == 'start':
        if not os.path.exists(NPU_RUN):
            print('no %s: no coprocessor to ask' % NPU_RUN)
        elif running():
            print('watching already')
        else:
            subprocess.run(['/usr/sbin/daemon', '-f', '-p', PIDFILE, sys.executable,
                            os.path.abspath(__file__), 'run'])
            print('watching the coprocessor\'s watchdog feeder every %d s' % EVERY)
    elif what == 'stop':
        if running():
            with open(PIDFILE) as f:
                os.kill(int(f.read().strip()), signal.SIGTERM)
    elif what == 'status':
        try:
            with open(STATE, encoding='utf-8') as f:
                last = json.load(f).get('last')
        except (OSError, ValueError):
            last = None
        line = 'watching the coprocessor\'s watchdog feeder' if running() else 'not watching'
        if last:
            line += '; last put right %s UTC: %s, %s' % (
                time.strftime('%Y-%m-%d %H:%M', time.gmtime(last['at'])), last['found'], last['done'])
        print(line)
    else:
        print('usage: feeder.py start|stop|status|run', file=sys.stderr)
        return 64
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'status'))
    except KeyboardInterrupt:
        sys.exit(0)
