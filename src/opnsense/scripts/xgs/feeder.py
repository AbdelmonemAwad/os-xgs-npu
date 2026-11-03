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
THE MOMENT ITS FAST PATH STOPS (#819). On 2026-10-31 the first XGS 3300's coprocessor printed the
first line of a kernel oops and nothing after it; its watchdog leaves a hung coprocessor about a
minute before resetting it, and fastpath.py, at the next minute of cron, took it up again five
seconds short of that. So the driver's gone latch is read here every POLL seconds - a number in the
driver's memory, read without its lock (the heartbeat's state takes it, and the lock is destroyed
in the driver's detach before newbus takes the device's sysctls away), nothing asked of the
coprocessor - and the moment the driver takes the coprocessor as gone:
  - its kernel is asked for its state over its console, SysRq l, w and p (npu-sysrq.sh, which
    asks nothing once the coprocessor's own watchdog may have reset it), what it prints kept in
    the console's log - but not when the gone is this feeder's own quiesce (A LINUX THAT STOPS
    below): its watchdog's reset is then due within the minute, and keys could stop its boot;
  - fastpath.py check is run at once rather than at the next minute - the owner's choice in XGS:
    Settings decides, as it does from cron.
And once a coprocessor that booted again answers, the crash records its kernel left in its pstore
(dmesg-ramoops - written when an oops runs to its end) are read and kept in the console's log, each
record once (STATE keeps their digests). Its console record is not: the console's log has that.
A LINUX THAT STOPS WITHOUT A PANIC (#768). The coprocessor's own watchdog resets it about a minute after
its Linux stops feeding it, and the XGS 3300's host does not come back from its driver's accesses across
that reset (2026-11-02: with the root port's completion timeout at 1 to 10 ms, and with the coprocessor's
bus mastering off). A panic is slowed down instead - see A PANIC, SLOWED DOWN below; a hang has to be
seen coming. So every POLL seconds one echo request goes to
the coprocessor's end of the management link (LINK), and once it has gone SILENT seconds without an
answer the console is asked alone - but only when the link had answered for STEADY seconds without a
gap longer than GAP before it went silent (the seconds a look takes between two polls counted as
neither a gap nor a silence: watch_link), no recovery is running (recover.sh's lock), the fast path's heartbeat is
alive and the driver not yet gone. A console that answers means the link alone is down, and nothing is
done; a console held by another caller is asked again at the next look. A console that is silent too
means its Linux has stopped: octep is told (QUIESCE) to stop touching the coprocessor before its
watchdog resets it, the stop above follows, and fastpath.py takes it up again. The second XGS 3300
showed why the steady link is asked for (2026-11-02): right after a recovery its link answered once
and went quiet for half a minute while OPNsense set its interfaces up again, its console - which drops
characters in the first minutes of a boot - did not answer either, and a healthy coprocessor was taken
as stopped. A watch started on a coprocessor long up - after an update of this
package, or by hand - had its own first two minutes unseen that way, and a stop in them went on to the
reset (2026-11-02, #987). So a look made during the link's present run of answers that finds the
coprocessor up LONG_UP seconds - long past its boot, and its console with it - makes the run steady
from its first answer. After a boot or a recovery no look can, and the two minutes are still asked for.
While the link is silent and that is not yet
decided, the feeder itself is not asked - a look at a silent coprocessor takes the link's ten seconds
and the console's eight, and the decision has to come well inside the watchdog's minute. Only octep
has QUIESCE: the XGS 136's driver sees its link go down.
And every look sets the coprocessor's panic timeout (PANIC_WAIT, on octep's family only), so that a
panic waits for the driver to let go instead of restarting at once.
Never fails the caller: a host with no coprocessor to ask says so and exits 0.
"""
import fcntl
import hashlib
import json
import os
import signal
import subprocess
import sys
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
SYSRQ = '/usr/local/opnsense/scripts/xgs/npu-sysrq.sh'
FASTPATH = '/usr/local/opnsense/scripts/xgs/fastpath.py'
CONSOLE_LOG = '/usr/local/opnsense/scripts/xgs/console_log.py'
STATE = '/var/db/os-xgs-npu/feeder.json'
PIDFILE = '/var/run/xgs-feeder.pid'
EVERY = 20
SETTLE = 120
POLL = 2
GONE = 'dev.octep.0.gone'
LINK = '169.254.254.2'		# the coprocessor's end of the management link (npuctl/mgmt-link.sh)
SILENT = 30
STEADY = 120
GAP = 3 * POLL + 1		# a gap no longer than this - an echo lost, with its wait - does not end a link's steady run
LONG_UP = 600			# a coprocessor up this long is past its boot (see A LINUX THAT STOPS)
UPTIME = {'at': None, 'uptime': None}	# the last look's answer: when (time.monotonic()), and the uptime
QUIESCED = {'at': None}		# when (time.monotonic()) this feeder last told octep its coprocessor is about to reset
RECOVER_LOCK = '/var/run/xgs-recover.lock'	# recover.sh's own, held while it runs (as fastpath.py reads it)
BUSY = 75			# npu-run.sh's exit when another caller holds the console
QUIESCE = 'dev.octep.0.quiesce'
HEARTBEAT = 'dev.octep.0.heartbeat.state'
# The crash records the coprocessor's kernel left, read without keeping pstore mounted.
PSTORE = ('mount -t pstore -o ro pstore /sys/fs/pstore 2>/dev/null; '
          'for f in /sys/fs/pstore/dmesg-*; do [ -f "$f" ] && { echo "=== $f"; cat "$f"; }; done; '
          'umount /sys/fs/pstore 2>/dev/null; true')
# One command, read back line by line: the coprocessor's uptime in seconds, the feeder's pid or
# "none", and its State line from /proc. busybox has no pgrep -f here, but has pidof.
ASK = ('cut -d. -f1 /proc/uptime; p=$(pidof watchdog | cut -d" " -f1); echo "pid ${p:-none}"; '
       '[ -n "$p" ] && grep -m1 "^State:" /proc/$p/status; true')
# A PANIC, SLOWED DOWN (#768). An OCTEON TX coprocessor's kernel restarts a second after a panic (its
# kernel.panic is 1), too soon for the driver: on 2026-11-02 that restart froze the host in four of
# five panics on the first XGS 3300 and in the one tried on the second. Told to wait PANIC_WAIT seconds
# instead, its fast path stops with it, the driver takes it as gone when the heartbeat dies at 30 s,
# and the restart comes after: the host stayed up in all four such runs, two on each XGS 3300 - the
# last with the value set by this look itself - the coprocessor taken up again in place. The value is
# a run-time one, gone with every boot of the coprocessor, so each look sets it again and reads it
# back - "panic <before> <after>". Only where octep is: the XGS 136's link goes down with its
# coprocessor, and its driver lets go at once.
PANIC_WAIT = 60
PANIC_ASK = ('; t=$(cat /proc/sys/kernel/panic 2>/dev/null); '
             '[ "$t" = "%d" ] || echo %d > /proc/sys/kernel/panic; '
             'echo "panic ${t:-unknown} $(cat /proc/sys/kernel/panic 2>/dev/null)"' % (PANIC_WAIT, PANIC_WAIT))
PANIC_TOLD = {'boot': None}	# the coprocessor's boot (its start, in host time) last said about


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
    """(uptime, pid, state letter, panic timeout before this look) of the feeder as the coprocessor
    reports it, or None."""
    out = npu(8, ASK + PANIC_ASK if sysctl(GONE) is not None else ASK)
    if out is None:
        return None
    uptime = pid = state = panic = None
    for line in out.replace('\r', '').splitlines():
        line = line.strip()
        if line.isdigit() and uptime is None:
            uptime = int(line)
        elif line.startswith('pid '):
            pid = line[4:]
        elif line.startswith('State:'):
            parts = line.split()
            state = parts[1] if len(parts) > 1 else None
        elif line.startswith('panic '):
            panic = line[6:]
    if uptime is None or pid is None:
        return None
    return uptime, (None if pid == 'none' else pid), state, panic


def load():
    try:
        with open(STATE, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save(state):
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(state, f)
    os.replace(STATE + '.tmp', STATE)


def keep(found, done, pid):
    state = load()
    state['last'] = {'at': int(time.time()), 'found': found, 'done': done, 'pid': pid}
    state['count'] = state.get('count', 0) + 1
    save(state)


def gone():
    """'1' while the driver has taken the coprocessor as gone, '0' otherwise, None with no such
    driver - a plain number in its memory, read without its lock."""
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', GONE], capture_output=True, text=True, timeout=5)
        value = done.stdout.strip()
        return value if value in ('0', '1') else None
    except (OSError, subprocess.SubprocessError):
        return None


def at_the_stop():
    """The driver has just taken the coprocessor as gone: the kernel's state over the console, then
    the owner's choice at once. Not the kernel's state when the gone is this feeder's own quiesce: its
    Linux answered on neither link nor console, and its watchdog resets it within the minute - SysRq's
    breaks and keys sent then could land in its boot firmware's countdown and stop the boot that
    fastpath.py waits for (npu-sysrq.sh's own guard counts from a dead heartbeat, and a quiesced one is
    still alive; a review of the release, 2026-11-03)."""
    if QUIESCED['at'] is not None and time.monotonic() - QUIESCED['at'] < 60:
        say('the coprocessor\'s kernel is not asked for its state over its console: the driver was told '
            'it is about to be reset, and keys sent now could reach its boot firmware')
    elif os.access(SYSRQ, os.R_OK):
        try:
            subprocess.run(['/bin/sh', SYSRQ, 'lwp', '5'], stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=60)
            say('the coprocessor\'s fast path stopped answering: its kernel was asked for its state over '
                'its console (SysRq l, w, p), kept in /var/log/xgs/npu-console.log')
        except (OSError, subprocess.SubprocessError):
            pass
    if os.access(FASTPATH, os.R_OK):
        subprocess.Popen([sys.executable, FASTPATH, 'check'], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)


def crash_records(uptime):
    """Once for each boot of the coprocessor: the crash records its kernel left in pstore, kept in the
    console's log - each one once."""
    state = load()
    booted = int(time.time()) - uptime
    if abs(booted - state.get('pstore_boot', 0)) < 60:
        return
    out = npu(15, PSTORE)
    if out is None:
        return
    state['pstore_boot'] = booted
    seen = state.get('pstore_seen', [])
    fresh = []
    for record in out.replace('\r', '').split('=== ')[1:]:
        digest = hashlib.sha1(record.encode()).hexdigest()
        if digest not in seen:
            fresh.append('=== ' + record)
            seen.append(digest)
    state['pstore_seen'] = seen[-20:]
    save(state)
    if fresh:
        console_keep(''.join(fresh), 'pstore, after the coprocessor booted again')
        say('the coprocessor\'s kernel had left %d crash record(s) in its pstore: kept in '
            '/var/log/xgs/npu-console.log' % len(fresh))


def console_keep(text, who):
    """Text into the console's log, as console_log.py keep writes a caller's read."""
    path = '/var/run/xgs-feeder-keep.%d' % os.getpid()
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    try:
        subprocess.run([sys.executable, CONSOLE_LOG, 'keep', path, who], capture_output=True, timeout=30)
    finally:
        os.unlink(path)


def watch_beat(beat):
    """The gone latch read once: at_the_stop when it turns to 1 - not for a feeder started while it
    already was - and the value to compare the next read with."""
    now = gone()
    if now is None:
        return beat
    if now == '1' and beat == '0':
        try:
            at_the_stop()
        except Exception as e:
            say('feeder.py: at the stop: %s' % e)
    return now


def answers_on_link():
    """One echo request to the coprocessor's end of the management link, given a second."""
    try:
        return subprocess.run(['/sbin/ping', '-c', '1', '-t', '1', '-q', LINK], capture_output=True,
                              timeout=5).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def sysctl(name):
    """A sysctl's value, None when there is no such sysctl."""
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def console_answers():
    """The coprocessor asked on its console alone, the link being what is silent: True when it
    answered, False when it did not, None when another caller held the console."""
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, '4', 'echo alive'], stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, timeout=40,
                              env=dict(os.environ, NPU_RUN_NO_LINK='1'))
    except (OSError, subprocess.SubprocessError):
        return False
    if done.returncode == BUSY:
        return None
    return done.returncode == 0 and 'alive' in done.stdout.split()


def recovering():
    """Whether recover.sh is running now: it holds its lock for as long as it runs."""
    try:
        fd = os.open(RECOVER_LOCK, os.O_RDONLY)
    except OSError:
        return False
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        return True
    finally:
        os.close(fd)
    return False


def tell_driver():
    try:
        return subprocess.run(['/sbin/sysctl', '-q', QUIESCE + '=1'], capture_output=True,
                              timeout=5).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def past_boot(link):
    """Whether a look made during the link's present run of answers found the coprocessor up LONG_UP
    seconds by the link's last answer. A look from before the run says nothing of it: the coprocessor
    may have started again in the silence before."""
    at, uptime = UPTIME['at'], UPTIME['uptime']
    if at is None or at < link.get('up', link['ok']):
        return False
    return link['ok'] - (at - uptime) >= LONG_UP


def watch_link(link, now=None):
    """The management link looked at once - see A LINUX THAT STOPS above. link is {'ok': when it last
    answered, None if it never has; 'up': when its present run of answers began; 'lent': seconds the
    looks took since it last answered; 'acted': whether this silence has been decided}; the next is
    returned.

    A look runs between two polls, and the time it takes is the feeder's, not the link's: no echo is
    sent meanwhile. So it is not counted, neither in the gap that ends a run nor in a silence. Counted,
    a look over the console - ten seconds of ssh's first, and the console's after - ended every run,
    which then never grew STEADY, and a hang went unseen (the second XGS 3300, 2026-11-03: looks failed
    for a minute after a boot, and a stop three minutes later was not acted on); and after a look of half
    a minute a single echo lost read as SILENT (the reviews of the release, 2026-11-03)."""
    now = time.monotonic() if now is None else now
    gap = None if link.get('ok') is None else now - link['ok'] - link.get('lent', 0)
    if answers_on_link():
        if link.get('said'):
            say('the coprocessor answers on its management link again')
        steady = gap is not None and gap <= GAP
        return {'ok': now, 'up': link.get('up', now) if steady else now, 'acted': False}
    if gap is None or link.get('acted') or gap < SILENT:
        return link
    # a link that had not answered steadily is one coming up - after a boot, or a recovery's own steps -
    # unless a look during its run found the coprocessor long past its boot
    if (link['ok'] - link.get('up', link['ok']) < STEADY and not past_boot(link)) or recovering():
        return dict(link, acted=True)
    if sysctl(QUIESCE) is None or sysctl(GONE) != '0' or sysctl(HEARTBEAT) != 'alive':
        return dict(link, acted=True)
    answered = console_answers()
    if answered is None:
        # the console held by another caller: asked again at the next look
        return dict(link, busy=link.get('busy', 0) + 1)
    # how long it really was, and why it took longer than SILENT if it did
    silent, busy = int(gap), link.get('busy', 0)
    held = ' (its console held by another caller at %d look(s) before)' % busy if busy else ''
    link = dict(link, acted=True, said=True)
    if answered:
        say('the coprocessor has not answered on its management link for %d s, but its console does: '
            'left as it is%s' % (silent, held))
        return link
    told = tell_driver()
    if told:
        QUIESCED['at'] = time.monotonic()
    what = 'octep told to stop touching it first' if told else 'octep could not be told'
    say('the coprocessor has answered neither on its management link for %d s nor on its console, while '
        'its fast path still does: its Linux has stopped, and its own watchdog resets it within the '
        'minute - %s%s' % (silent, what, held))
    keep('silent', 'the driver told to stop touching it' if told else 'the driver could not be told', None)
    return link


def holding(link, now=None):
    """Whether the feeder is left unasked: the link silent, and the silence not yet decided."""
    now = time.monotonic() if now is None else now
    return link.get('ok') is not None and not link.get('acted') and now - link['ok'] > 2 * POLL


def panic_wait(uptime, panic):
    """What the look found of the coprocessor's panic timeout ("<before> <after>"), said once for each
    of its boots: a look finds it otherwise only on the first look after a boot, and sets it then."""
    words = (panic or '').split()
    if not words or words[0] == str(PANIC_WAIT):
        return
    boot = int(time.time()) - uptime
    if PANIC_TOLD['boot'] is not None and abs(boot - PANIC_TOLD['boot']) <= 2 * EVERY:
        return
    PANIC_TOLD['boot'] = boot
    if len(words) > 1 and words[1] == str(PANIC_WAIT):
        say('the coprocessor would have restarted %s s after a panic, before the driver lets go of it: it '
            'waits %d s now (#768)' % (words[0], PANIC_WAIT))
    else:
        say('the coprocessor restarts %s s after a panic, and could not be told to wait %d s (#768)'
            % (words[-1], PANIC_WAIT))


def once():
    """One look, and what it put right: None when nothing needed it."""
    seen = look()
    if seen is None:
        return 'unreachable'
    uptime, pid, state, panic = seen
    # taken when the answer is in: the boot it gives is never earlier than the real one
    UPTIME.update(at=time.monotonic(), uptime=uptime)
    panic_wait(uptime, panic)
    if uptime < SETTLE:
        return None
    try:
        crash_records(uptime)
    except (OSError, subprocess.SubprocessError, ValueError) as e:
        say('feeder.py: the coprocessor\'s pstore: %s' % e)
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
    beat = None			# the gone latch at the last POLL; None until it is first read
    link = {'ok': None, 'acted': False}
    due = 0.0
    while True:
        beat = watch_beat(beat)
        try:
            link = watch_link(link)
        except Exception as e:
            say('feeder.py: the management link: %s' % e)
        if not holding(link) and time.monotonic() >= due:
            due = time.monotonic() + EVERY
            started = time.monotonic()
            try:
                result = once()
            except Exception as e:
                result = None
                say('feeder.py: %s' % e)
            # the look's own time is the feeder's: no echo went out meanwhile (watch_link)
            if link.get('ok') is not None:
                link = dict(link, lent=link.get('lent', 0) + time.monotonic() - started)
            # a coprocessor out of reach is said once, when it goes and when it comes back
            if result == 'unreachable' and not unreachable:
                unreachable = True
                say('the coprocessor does not answer: its watchdog feeder is not looked at until it does')
            elif result != 'unreachable' and unreachable:
                unreachable = False
                say('the coprocessor answers again: its watchdog feeder is looked at every %d s' % EVERY)
        time.sleep(POLL)


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
